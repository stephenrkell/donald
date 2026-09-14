#define _GNU_SOURCE  /* so that strings.h declares bzero() */
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <strings.h> // for bzero

#include "donald.h"
#include "chain.h"

/* If we're a chain loader, the inferior is the relevant ld.so for the
 * binary named on the command line -- not the binary itself. That's
 * assuming the named binary is dynamically linked. We assume this,
 * for now. */
const char *__wrap_get_loadee_path(struct load_ctxt *ctxt)
{
	/* We always chain-load the ld.so and let it load the program. Let's read it. */
	/* TODO: allow for binaries that tell us which ld.so to chain to, even when
	 * they request us. In allocsld we support this by allowing the next loader to
	 * be appended within the .interp section, after the NUL terminator. In the
	 * requested case, check for this. */
	const char *loadee_path;
	if (ctxt->we_are_the_program)
	{
		/* The inferior is the dynamic linker that the argument program wants
		 * us to run, assuming it is a dynamically linked program. Otherwise it's
		 * just the program itself.
		 *
		 * XXX: what if the inferior is us? Do we chain ourselves? This should
		 * work, so yes, let's keep it simple and just chain onto whatever
		 * we find, even if it's us. */
		loadee_path = SYSTEM_LDSO_PATH; // FIXME: check for a different PT_INTERP
	}
	else
	{
		/* We are being requested. So when we check the .interp section we
		 * should find ourselves. If the .interp section is *bigger* than that,
		 * interpret the string after the NUL as the path to chain to. The
		 * executable should be mapped already. */
		assert(ctxt->phdr_ent);
		assert(ctxt->phnum_ent);
		uintptr_t executable_load_address = (uintptr_t) -1;
		int i = 0;
		/* Using a PHDR we can figure out the executable's load address. */
		for (i = 0; i < ctxt->phnum_ent->a_un.a_val; ++i)
		{
			ElfW(Phdr) *ph = ((ElfW(Phdr) *) ctxt->phdr_ent->a_un.a_val) + i;
			if (ph->p_type == PT_PHDR)
			{
				executable_load_address =
					/* actual addr of phdrs minus their vaddr */
					(uintptr_t) ctxt->phdr_ent->a_un.a_val - ph->p_vaddr;
				break;
			}
		}
		if (executable_load_address == (uintptr_t) -1)
		{
			/* didn't get it! */
			debug_printf(0, "No PT_PHDR so could not infer executable vaddr. "
				"Skipping .interp processing.");
			loadee_path = SYSTEM_LDSO_PATH;
		}
		else
		{
#if 0 /* srk: see Experiments/writable-interp */
			for (i = 0; i < phnum_ent->a_un.a_val; ++i)
			{
				ElfW(Phdr) *ph = ((ElfW(Phdr) *) phdr_ent->a_un.a_val) + i;
				// is this an interp?
				if (ph->p_type == PT_INTERP)
				{
					char *interp = (char*)(executable_load_address + ph->p_vaddr);
					char *second_interp = NULL;
					size_t interp_len = strnlen(interp, ph->p_memsz);
					if (interp_len + 1 < ph->p_memsz)
					{
						second_interp = interp + interp_len + 1;
					}
					/* Check the first interp against our soname. It should match! */
					char *our_soname = NULL;
					ElfW(Dyn) *d = &_DYNAMIC[0];
					ElfW(Dyn) *saw_dynstr = NULL;
					ElfW(Dyn) *saw_soname = NULL;
					for (; d->d_tag; ++d)
					{
						if      (d->d_tag == DT_SONAME) saw_soname = d;
						else if (d->d_tag == DT_STRTAB) saw_dynstr = d;
						if (saw_soname && saw_dynstr) break;
					}
					if (saw_soname && saw_dynstr)
					{
						char *dynstr = (char*)(
							our_load_address + (uintptr_t) saw_dynstr->d_un.d_ptr
						);
						assert(0 == strcmp(
							basename(interp),
							dynstr + saw_soname->d_un.d_val
						));
					}
					else
					{
						// warn that we did not find a soname to check against
						debug_printf(0, "Running as requested interpreter but no soname "
							"(requested: `%s', after the NUL: `%s')\n",
							interp, second_interp);
					}
					/* if we got here then we have at least interp */
					if (second_interp && *second_interp) loadee_path = second_interp;
					else loadee_path = SYSTEM_LDSO_PATH;
					break; // exit the loop early
				}
				assert(i != phnum_ent->a_un.a_val &&
					"'requested' as interpreter, but did not find a PT_INTERP??");
			} // end for each phdr
#endif
			loadee_path = SYSTEM_LDSO_PATH;
		} // end else 
		// if we got here, we definitely saw a PT_INTERP *or* failed the PHDR bit, so...
		assert(loadee_path);
	}
	return loadee_path;
}

uintptr_t __wrap_get_loadee_base_addr_hint(struct load_ctxt *ctxt)
{
	/* The default ...55556000 this doesn't work for allocsld, which wants to install
	 * trampolines that bridge from the ld.so to its own DSO. To enable
	 * a PC32 (or similarly width-constrained) jump, we could map the real
	 * ld.so immediately before ourselves. That requires us to figure
	 * out the maximum vaddr before we infer the base address hint. That's
	 * annoying so let's approximate: pick an address 32MB before us in
	 * the address space. (We used to use 256MB but Valgrind wants to load
	 * PIEs at 64MB, so allowing for an unmapped zero region, ....) */
	debug_printf(0, "Loader's load address is %p\n", (void*) ctxt->our_load_address);
	assert(ctxt->our_load_address >= 32 * 1024 * 1024);
	return ctxt->our_load_address -  32 * 1024 * 1024;
	// FIXME: move this logic to allocsld
}

void __hook_pre_enter(struct chain_load_ctxt *ctxt) __attribute__((weak));

void __real_enter(struct load_ctxt *ctxt);
void __wrap_enter(struct load_ctxt *ctxt)
{
	struct chain_load_ctxt cc;
	bzero(&cc, sizeof cc);
	cc.load = ctxt;
	/* Fix up the auxv so that the ld.so thinks it's just been invoked.
	 * If 'we are the program' it means make the phdrs look like the
	 * ld.so was run as the executable, i.e. modify the auxv in place
	 * to directly reference the inferior stuff. Otherwise the only
	 * one we need to modify is AT_BASE, which points to the *interpreter*
	 * so needs to be pointed at the inferior ld.so; the others don't change.
	 *
	 * As we scan the auxv, pull out the program headers we find there.
	 * These may be ours (if we're invoked) or the executable's (if we're
	 * requested). That's the executable, not the inferior loader.
	 * If we were 'requested', we never get hold of our own phdrs. But we
	 * create a separate variable just so we're statically clear which we want. */
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_ENTRY:
				if (ctxt->we_are_the_program) p->a_un.a_val = ctxt->loadee_entry_point;
				debug_printf(1, "AT_ENTRY is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHDR:
				if (ctxt->we_are_the_program) {
					cc.our_phdrs = (ElfW(Phdr) *) p->a_un.a_val;
					p->a_un.a_val = ctxt->loadee->phdrs_addr;
				} else cc.program_phdrs = (void*) p->a_un.a_val;
				debug_printf(1, "AT_PHDR is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHENT:
				if (ctxt->we_are_the_program) {
					cc.our_phentsize = p->a_un.a_val;
					p->a_un.a_val = ctxt->loadee->ehdr.e_phentsize;
				} else cc.program_phentsize = p->a_un.a_val;
				debug_printf(1, "AT_PHENT is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHNUM:
				if (ctxt->we_are_the_program) {
					cc.our_phnum = p->a_un.a_val;
					p->a_un.a_val = ctxt->loadee->ehdr.e_phnum;
				} else cc.program_phnum = p->a_un.a_val;
				debug_printf(1, "AT_PHNUM is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_BASE:
				if (ctxt->we_are_the_program) p->a_un.a_val = 0;
				else p->a_un.a_val = ctxt->loadee->base_addr;
				debug_printf(1, "AT_BASE is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_EXECFN:
				if (ctxt->we_are_the_program) p->a_un.a_val = (uintptr_t) ctxt->argv[0];
				debug_printf(1, "AT_EXECFN is %p (%s)\n", (void*) p->a_un.a_val, (char*) p->a_un.a_val);
				break;
		}
	}
	/* In the 'invoked' case, to give debugging a chance of working,
	 * we try to create a DT_DEBUG entry in our own _DYNAMIC section,
	 * because this is where a debugger will look. We then point it
	 * at the _r_debug structure in the *inferior* ld.so.
	 *
	 * In the 'requested' case, the debugger will look in the executable's
	 * DT_DEBUG, and we also make this point to the inferior ld.so's
	 * _r_debug.
	 *
	 * Although optional, we try hard to make introspection work early on,
	 * by populating the DT_DEBUG. If we skip this, the inferior ld.so will
	 * get around to populating its own _r_debug and debugging will start
	 * working when that happens.
	 */

	/* To find the _r_debug symbol, we use a simple but slow linear search
	 * rather than the hash table, to save ourselves depending on. However,
	 * we do need to look up the size of the dynamic symbol table. This is
	 * most easily found via the System V hash table. */
	unsigned nsyms = -1;
	for (ElfW(Dyn) *dyn = ctxt->loadee->dynamic; dyn->d_tag != DT_NULL; ++dyn)
	{
		switch (dyn->d_tag)
		{
			case DT_HASH:
				ElfW(Word) *hash = (ElfW(Word) *)(ctxt->loadee->base_addr + dyn->d_un.d_ptr);
				nsyms = hash[1];
				break;
			case DT_SYMTAB:
				cc.inferior_symtab = (ElfW(Sym) *)(ctxt->loadee->base_addr + dyn->d_un.d_ptr);
				break;
			default: break;
		}
	}
	for (ElfW(Dyn) *dyn = ctxt->loadee->dynamic; dyn->d_tag != DT_NULL; ++dyn)
	{
		switch (dyn->d_tag)
		{
			case DT_STRTAB:
				cc.inferior_strtab = (const unsigned char *)(ctxt->loadee->base_addr + dyn->d_un.d_ptr);
				/* if no SysV hash, fall back on this hack for sizing the symtab,
				 * but it's liable to break (e.g. strcmp crash when searching for a symbol) */
				assert(nsyms != -1);
				cc.inferior_symtab_end = /*(nsyms == -1) ? (ElfW(Sym) *) cc.inferior_strtab
					 : */ (cc.inferior_symtab + nsyms);
				break;
			default: break;
		}
	}
	for (ElfW(Sym) *p_sym = &cc.inferior_symtab[0]; p_sym && p_sym <= cc.inferior_symtab_end; ++p_sym)
	{
		if (0 == strcmp((const char*) &cc.inferior_strtab[p_sym->st_name], "_r_debug"))
		{
			/* match */
			cc.inferior_r_debug_sym = p_sym;
			break;
		}
	}
	/* In the 'invoked' case, to give debugging a chance of working,
	 * try to create a DT_DEBUG entry in our _DYNAMIC section. */
	if (cc.inferior_r_debug_sym && ctxt->we_are_the_program)
	{
		assert(cc.our_phdrs);
		size_t our_dynamic_size = 0;
		for (ElfW(Phdr) *phdr = cc.our_phdrs; phdr != cc.our_phdrs + cc.our_phnum; ++phdr)
		{
			if (phdr->p_type == PT_DYNAMIC) { our_dynamic_size = phdr->p_memsz; break; }
		}
		cc.our_dt_debug = find_or_create_dt_debug(ctxt->loadee->base_addr, ctxt->loadee->dynamic_vaddr,
			our_dynamic_size, cc.inferior_r_debug_sym->st_value);
	}
	else if (cc.inferior_r_debug_sym) // !we_are_the_program
	{
		/* We are just the requested dynamic linker, but we want the DT_DEBUG
		 * in the actual executable. How do we get that? */
		assert(!ctxt->we_are_the_program);
		assert(cc.program_phdrs);
		/* How do we get the program's load address? Look for a PT_PHDR.
		 * FIXME: can we get this instead from auxv above? That would be nicer. */
		uintptr_t program_load_addr = (uintptr_t) -1;
		unsigned i = 0; 
		for (; i < cc.program_phnum; ++i)
		{
			if (cc.program_phdrs[i].p_type == PT_PHDR)
			{
				program_load_addr = (uintptr_t) &cc.program_phdrs[i]
					- cc.program_phdrs[i].p_vaddr;
				break;
			}
		}
		if (i != cc.program_phnum)
		{
			assert(program_load_addr != (uintptr_t) -1);
			for (i = 0; i < cc.program_phnum; ++i)
			{
				if (cc.program_phdrs[i].p_type == PT_DYNAMIC)
				{
					for (ElfW(Dyn) *dyn = (ElfW(Dyn) *) (cc.program_phdrs[i].p_vaddr + program_load_addr);
							dyn->d_tag != DT_NULL; ++dyn)
					{
						if (dyn->d_tag == DT_DEBUG)
						{
							cc.program_dt_debug = dyn;
							break;
						}
					}
				}
			}
		}
		else
		{
			// we didn't find it...
		}
	}
	if (cc.program_dt_debug && cc.inferior_r_debug_sym)
	{
		/* When we created our DT_DEBUG, that internally called this helper
		 * to point at the inferior r_debug.
		 *
		 * This does the same but for the *program's DT_DEBUG, if it has one. */
		point_dt_debug_to_inferior_r_debug(cc.program_dt_debug, ctxt->loadee->base_addr,
			ctxt->loadee->dynamic_vaddr, cc.inferior_r_debug_sym->st_value);
	}
	/* We've set up the DT_DEBUG, but for debugging e also need to fix up the
	 * ld.so's path in the link map. Without a fix, the ld.so will think its path
	 * in the filesystem is actually our path, which it finds in argv[0].
	 * We simply overwrite argv[0], making it look to the inferior ld.so like it was
	 * invoked on the command line directly.
	 *
	 * NOTE that this works in the 'invoked' ("we are the program") case only.
	 * For the 'requested' case, a more elaborate fix is needed that rewrites the
	 * .interp section, since that is where the ld.so gets its own name in such cases.
	 * Such a fix is implemented in allocsld but not here. Normally .interp is not
	 * writable and cannot be guaranteed large enough to hold the new interp string,
	 * (whereas we *can* guarantee both of those things in the liballocs context).
	 */
	if (ctxt->we_are_the_program) ctxt->argv[0] = (char*) SYSTEM_LDSO_PATH;

	/* A hook may be linked in for further extension. */
	if (__hook_pre_enter) __hook_pre_enter(&cc);

	__real_enter(ctxt);
}
