#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <err.h>
#include <assert.h>
#include <string.h> /* for strcmp() */
#include <linux/auxvec.h>  /* For AT_xxx definitions */
#include "donald.h"

#ifdef CHAIN_LOADER_COVER_TRACKS_H
#include CHAIN_LOADER_COVER_TRACKS_H
#endif

extern int _start(void);

// in main() we have another special way to die
#define die(s, ...) do { debug_printf(0, s , ##__VA_ARGS__); return -1; } while(0)
// #define die(s, ...) do { fwrite(DONALD_NAME ": " s , sizeof DONALD_NAME ": " s, 1, stderr); return -1; } while(0)

int main(int argc, char **argv)
{
	// were we invoked by name, or as a .interp?
	// use AT_ENTRY to find out: it's _start if we were invoked as a program,
	// otherwise it's the program's _start
	int argv_program_ind;
	uintptr_t entry = (uintptr_t) &_start;
	_Bool we_are_the_program = 1;
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_ENTRY:
				if (p->a_un.a_val != (uintptr_t) &_start) we_are_the_program = 0;
				entry = p->a_un.a_val;
				break;
			default:
				break;
		}
	}
	uintptr_t our_load_address = 0;
	ElfW(auxv_t) *phdr_ent = NULL;
	ElfW(auxv_t) *phnum_ent = NULL;
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_BASE:
				if (p->a_un.a_val == 0)
				{
					assert(we_are_the_program);
					our_load_address = (uintptr_t) &_begin & ~(page_size-1);
				}
				else
				{
					our_load_address = p->a_un.a_val;
				}
				break;
			case AT_PHDR:
				phdr_ent = p;
				break;
			case AT_PHNUM:
				phnum_ent = p;
				break;
			default:
				break;
		}
	}

	debug_printf(1, "We think we are%sthe program\n", we_are_the_program ? " " : " not ");
	if (entry == (uintptr_t) &_start)
	{
		// we were invoked as an executable
		argv_program_ind = 1;
	} else argv_program_ind = 0; // we were invoked as an interp
	
	if (argc <= argv_program_ind) { die("no program specified\n"); }

	const char *inferior_path;
#ifdef CHAIN_LOADER
	/* We always chain-load the ld.so and let it load the program. Let's read it. */
	/* TODO: allow for binaries that tell us which ld.so to chain to, even when
	 * they request us. In allocsld we support this by allowing the next loader to
	 * be appended within the .interp section, after the NUL terminator. In the
	 * requested case, check for this. */
	if (we_are_the_program)
	{
		/* The inferior is the dynamic linker that the argument program wants
		 * us to run, assuming it is a dynamically linked program. Otherwise it's
		 * just the program itself.
		 *
		 * XXX: what if the inferior is us? Do we chain ourselves? This should
		 * work, so yes, let's keep it simple and just chain onto whatever
		 * we find, even if it's us. */
		inferior_path = SYSTEM_LDSO_PATH; // FIXME: check for a different PT_INTERP
	}
	else
	{
		/* We are being requested. So when we check the .interp section we
		 * should find ourselves. If the .interp section is *bigger* than that,
		 * interpret the string after the NUL as the path to chain to. The
		 * executable should be mapped already. */
		assert(phdr_ent);
		assert(phnum_ent);
		int i = 0;
		for (; i < phnum_ent->a_un.a_val; ++i)
		{
			ElfW(Phdr) *ph = ((ElfW(Phdr) *) phdr_ent->a_un.a_val) + i;
			// is this an interp?
			if (ph->p_type == PT_INTERP)
			{
				char *interp = our_load_address + ph->p_vaddr;
				char *second_interp = NULL;
				size_t interp_len = strnlen(interp, ph->p_memsz);
				if (interp_len + 1 < ph->p_memsz)
				{
					second_interp = interp + interp_len + 1;
				}
				/* Check the first interp against our soname. It should match! */
				char *our_soname = NULL;
				ElfW(Dyn) *d = &_DYNAMIC[0];
				for (; d->d_tag; ++d)
				{
					if (d->d_tag == DT_SONAME)
					{
						assert(0 == strcmp(basename(interp),
							(char*)(our_load_address + (uintptr_t) d->d_un.d_ptr)
								));
						break;
					}
				}
				if (!d->d_tag)
				{
					// warn that we did not find a soname to check against
					debug_printf(1, "Running as requested interpreter but no soname "
						"(requested: `%s', after the NUL: `%s')\n",
						interp, second_interp);
				}
			}
			break;
		}
		assert(i == phnum_ent->a_un.a_val &&
			"'requested' as interpreter, but did not find a PT_INTERP??");
	}
#else
	/* We have a program to run, given to us on the command line.
	 * Let's read it. */
	inferior_path = argv[argv_program_ind];
#endif

	/* We used to use the following base addresses.... */
#if 0
#if defined(__x86_64__)
	uintptr_t inferior_base_addr_hint = 0x555555556000;
#elif defined (__i386__)
	uintptr_t inferior_base_addr_hint = 0x55556000;
#else
#error "Unrecognised architecture."
#endif
#endif
	/* ... but this doesn't work for allocsld, which wants to install
	 * trampolines that bridge from the ld.so to its own DSO. To enable
	 * a PC32 (or similarly width-constrained) jump, we could map the real
	 * ld.so immediately before ourselves. That requires us to figure
	 * out the maximum vaddr before we infer the base address hint. That's
	 * annoying so let's approximate: pick an address 256MB before us in
	 * the address space. */
	assert(our_load_address >= 256 * 1024 * 1024);
	uintptr_t inferior_base_addr_hint = our_load_address -  256 * 1024 * 1024;
#define MAX_LDSO_PHDR 16
	ElfW(Phdr) phdrs[MAX_LDSO_PHDR];
	unsigned n_phdrs = MAX_LDSO_PHDR;
	struct loadee_info inferior = load_file(inferior_path, inferior_base_addr_hint,
			phdrs, &n_phdrs);
	if (!inferior.dynamic_vaddr) die("%s", inferior.errmsg);

	// do relocations!

	// grab the entry point
	register unsigned long entry_point = inferior.base_addr + inferior.ehdr.e_entry;

#ifdef CHAIN_LOADER
	/* Fix up the auxv so that the ld.so thinks it's just been run.
	 * If 'we are the program' it means make the phdrs look like the
	 * ld.so was run as the executable, i.e. modify the auxv in place
	 * to directly reference the inferior stuff. Otherwise the only
	 * one we need to modify is AT_BASE, which points to the *interpreter*
	 * so needs to be pointed at the inferior ld.so; the others don't change. */
	ElfW(Phdr) *program_phdrs = NULL;
	unsigned program_phentsize = 0;
	unsigned program_phnum = 0;
	/* FIXME: if "we are the progam", i.e. we were 'invoked' not 'requested',
	 * it's easy to get our phdrs. If we were 'requested', however, we never
	 * get hold of our own phdrs -- but it's easy to get hold of the program's.
	 * This really does mean the program's, not the inferior loader's. */
	ElfW(Phdr) *our_phdrs = NULL;
	unsigned our_phentsize = 0;
	unsigned our_phnum = 0;
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_ENTRY:
				if (we_are_the_program) p->a_un.a_val = entry_point;
				debug_printf(1, "AT_ENTRY is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHDR:
				if (we_are_the_program) {
					our_phdrs = (ElfW(Phdr) *) p->a_un.a_val;
					p->a_un.a_val = inferior.phdrs_addr;
				} else program_phdrs = (void*) p->a_un.a_val;
				debug_printf(1, "AT_PHDR is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHENT:
				if (we_are_the_program) {
					our_phentsize = p->a_un.a_val;
					p->a_un.a_val = inferior.ehdr.e_phentsize;
				} else program_phentsize = p->a_un.a_val;
				debug_printf(1, "AT_PHENT is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHNUM:
				if (we_are_the_program) {
					our_phnum = p->a_un.a_val;
					p->a_un.a_val = inferior.ehdr.e_phnum;
				} else program_phnum = p->a_un.a_val;
				debug_printf(1, "AT_PHNUM is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_BASE:
				if (we_are_the_program) p->a_un.a_val = 0;
				else p->a_un.a_val = inferior.base_addr;
				debug_printf(1, "AT_BASE is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_EXECFN:
				if (we_are_the_program) p->a_un.a_val = (uintptr_t) argv[0];
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
	 * rather than the hash table. */
	ElfW(Dyn) *dt_debug = NULL;
	ElfW(Sym) *symtab = NULL;
	ElfW(Sym) *symtab_end = NULL;
	const unsigned char *strtab = NULL;
	for (ElfW(Dyn) *dyn = inferior.dynamic; dyn->d_tag != DT_NULL; ++dyn)
	{
		switch (dyn->d_tag)
		{
			case DT_SYMTAB:
				symtab = (ElfW(Sym) *)(inferior.base_addr + dyn->d_un.d_ptr);
				break;
			case DT_STRTAB:
				strtab = (const unsigned char *)(inferior.base_addr + dyn->d_un.d_ptr);
				symtab_end = (ElfW(Sym) *)strtab;
				break;
			default: break;
		}
	}
	ElfW(Sym) *found_r_debug_sym = NULL;
	for (ElfW(Sym) *p_sym = &symtab[0]; p_sym && p_sym <= symtab_end; ++p_sym)
	{
		if (0 == strcmp((const char*) &strtab[p_sym->st_name], "_r_debug"))
		{
			/* match */
			found_r_debug_sym = p_sym;
			break;
		}
	}
	/* In the 'invoked' case, to give debugging a chance of working,
	 * try to create a DT_DEBUG entry in our _DYNAMIC section. */
	if (found_r_debug_sym && we_are_the_program)
	{
		assert(our_phdrs);
		size_t our_dynamic_size = 0;
		for (ElfW(Phdr) *phdr = our_phdrs; phdr != our_phdrs + our_phnum; ++phdr)
		{
			if (phdr->p_type == PT_DYNAMIC) { our_dynamic_size = phdr->p_memsz; break; }
		}
		dt_debug = find_or_create_dt_debug(inferior.base_addr, inferior.dynamic_vaddr,
			our_dynamic_size, found_r_debug_sym->st_value);
	}
	else if (found_r_debug_sym)
	{
		/* We are just the requested dynamic linker, but we want the DT_DEBUG
		 * in the actual executable. How do we get that? */
		assert(program_phdrs);
		/* How do we get the program's load address? Look for a PT_PHDR.
		 * FIXME: can we get this instead from auxv above? That would be nicer. */
		uintptr_t program_load_addr = (uintptr_t) -1;
		unsigned i = 0; 
		for (; i < program_phnum; ++i)
		{
			if (program_phdrs[i].p_type == PT_PHDR)
			{
				program_load_addr = (uintptr_t) &program_phdrs[i]
					- program_phdrs[i].p_vaddr;
				break;
			}
		}
		if (i != program_phnum)
		{
			assert(program_load_addr != (uintptr_t) -1);
			for (i = 0; i < program_phnum; ++i)
			{
				if (program_phdrs[i].p_type == PT_DYNAMIC)
				{
					for (ElfW(Dyn) *dyn = (ElfW(Dyn) *) (program_phdrs[i].p_vaddr + program_load_addr);
							dyn->d_tag != DT_NULL; ++dyn)
					{
						if (dyn->d_tag == DT_DEBUG)
						{
							dt_debug = dyn;
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
	if (dt_debug && found_r_debug_sym) populate_dt_debug(dt_debug, inferior.base_addr,
		inferior.dynamic_vaddr, found_r_debug_sym->st_value);

	/* Fix debugging: make it look to the debugger like the system ld.so was
	 * invoked on the command line directly. NOTE that this works in the 'invoked'
	 * ("we are the program") case only. For the 'requested' case, a more elaborate
	 * fix is needed that rewrites the .interp section. This is implemented in
	 * allocsld but not here. Normally .interp is not writable and cannot be
	 * guaranteed large enough to hold the new interp string, but we can guarantee
	 * both of those things in the liballocs context.
	 */
	if (we_are_the_program) argv[0] = (char*) SYSTEM_LDSO_PATH;

#ifdef CHAIN_LOADER_COVER_TRACKS_INC
	#include CHAIN_LOADER_COVER_TRACKS_INC
#endif
#endif

	// jump to the entry point
	enter((void*) entry_point);
}
