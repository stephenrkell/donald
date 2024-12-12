#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
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

#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif

__attribute__((visibility("hidden")))
struct loadee_info
load_file(const char *loadee_path, uintptr_t loadee_base_addr_hint,
	ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs)
{
	struct loadee_info loadee = { 
		.dynamic_vaddr = (uintptr_t) -1
	};
	// in load_file() we have a special way to die
#define die(s, ...) do { snprintf(loadee.errmsg, sizeof loadee.errmsg, \
    DONALD_NAME ": " s , ##__VA_ARGS__); loadee.dynamic_vaddr = 0; return loadee; } while(0)

	int loadee_fd = open(loadee_path, O_RDONLY);
	if (loadee_fd == -1) { die("could not open %s\n", loadee_path); }
	struct stat loadee_stat;
	int ret = fstat(loadee_fd, &loadee_stat);
	if (ret != 0) { die("could not open %s\n", loadee_path); }
	
	// read the ELF header
	ssize_t nread;
	nread = read(loadee_fd, &loadee.ehdr, sizeof (ElfW(Ehdr)));
	if (nread != sizeof (ElfW(Ehdr))) die("could not read ELF header of %s\n", loadee_path);

	_Bool is_elf = 0, class_matches = 0, is_lsb = 0, is_current = 0, is_sysv_or_gnu = 0,
		is_exec_or_solib = 0;
	// check it's a file we can grok
	if (loadee.ehdr.e_ident[EI_MAG0] != 0x7f
			|| loadee.ehdr.e_ident[EI_MAG1] != 'E'
			|| loadee.ehdr.e_ident[EI_MAG2] != 'L'
			|| loadee.ehdr.e_ident[EI_MAG3] != 'F'
			|| (is_elf = 1, loadee.ehdr.e_ident[EI_CLASS] != DONALD_ELFCLASS)
			|| (class_matches = 1, loadee.ehdr.e_ident[EI_DATA] != ELFDATA2LSB)
			|| (is_lsb = 1, loadee.ehdr.e_ident[EI_VERSION] != EV_CURRENT)
			|| (is_current = 1, loadee.ehdr.e_ident[EI_OSABI] != ELFOSABI_SYSV && loadee.ehdr.e_ident[EI_OSABI] != ELFOSABI_GNU)
			// || ehdr. e_ident[EI_ABIVERSION] != /* what? */
			|| (is_sysv_or_gnu = 1, loadee.ehdr.e_type != ET_EXEC && loadee.ehdr.e_type != ET_DYN)
			|| (is_exec_or_solib = 1, loadee.ehdr.e_machine != DONALD_ELFMACHINE)
			)
	{
		die("unsupported file (%s): %s\n",
			!is_elf ? "not an ELF file"
			: !class_matches ? "not of expected ELF class"
			: !is_lsb ? "not ELFDATA2LSB"
			: !is_current ? "not EV_CURRENT"
			: !is_sysv_or_gnu ? "not System V or GNU ABI"
			: !is_exec_or_solib ? "not an executable"
			: "unexpected machine",
			loadee_path);
	}
	
	// process the PT_LOADs
	off_t newloc = lseek(loadee_fd, loadee.ehdr.e_phoff, SEEK_SET);
	ElfW(Phdr) phdrs[loadee.ehdr.e_phnum];
	for (unsigned i = 0; i < loadee.ehdr.e_phnum; ++i)
	{
		off_t off = loadee.ehdr.e_phoff + i * loadee.ehdr.e_phentsize;
		newloc = lseek(loadee_fd, off, SEEK_SET);
		if (newloc != off) die("could not seek to program header %d in %s\n", i, loadee_path);
		size_t ntoread = MIN(sizeof phdrs[0], loadee.ehdr.e_phentsize);
		nread = read(loadee_fd, &phdrs[i], ntoread);
		if (nread != ntoread) die("could not read program header %d in %s\n", i, loadee_path);
	}
	// also snarf the shdrs (FIXME: we don't seem to use these anywhere?)
	newloc = lseek(loadee_fd, loadee.ehdr.e_shoff, SEEK_SET);
	ElfW(Shdr) shdrs[loadee.ehdr.e_shnum];
	for (unsigned i = 0; i < loadee.ehdr.e_shnum; ++i)
	{
		off_t off = loadee.ehdr.e_shoff + i * loadee.ehdr.e_shentsize;
		newloc = lseek(loadee_fd, off, SEEK_SET);
		if (newloc != off) die("could not seek to section header %d in %s\n", i, loadee_path);
		size_t ntoread = MIN(sizeof shdrs[0], loadee.ehdr.e_shentsize);
		nread = read(loadee_fd, &shdrs[i], ntoread);
		if (nread != ntoread) die("could not read section header %d in %s\n", i, loadee_path);
	}
	/* Now we've snarfed the phdrs. But remember that we want to map them
	 * without holes. To do this, calculate the maximum vaddr we need,
	 * then map a whole chunk of memory PROT_NONE in that space. We will
	 * use the ldso fd, so that it appears as a mapping of that file
	 * (this helps liballocs). */
	ElfW(Addr) max_vaddr = 0;
	for (unsigned i = 0; i < loadee.ehdr.e_phnum; ++i)
	{
		if (phdrs[i].p_type == PT_DYNAMIC) loadee.dynamic_vaddr = phdrs[i].p_vaddr;
		ElfW(Addr) max_vaddr_this_obj = phdrs[i].p_vaddr + phdrs[i].p_memsz;
		if (max_vaddr_this_obj > max_vaddr) max_vaddr = max_vaddr_this_obj;
	}
	/* We don't use MAP_FIXED because in the 'requested' case, the hint address
	 * might already be used by the executable itself (if it's PIE) thanks to the
	 * kernel having mapped it. FIXME: holey executables are a problem here too.
	 * We really should detect this eagerly and re-exec ourselves as 'invoked'
	 * if we see a holey executable. */
	void *base = mmap((void*) loadee_base_addr_hint, max_vaddr, PROT_NONE, MAP_PRIVATE,
		loadee_fd, 0);
	if (base == MAP_FAILED) die("could not map %s with PROT_NONE\n", loadee_path);
	loadee.base_addr = (uintptr_t) base;
	loadee.phdrs_addr = 0;
	loadee.dynamic = NULL;
	loadee.dynamic_size = 0;
	if (out_phdrs)
	{
		assert(p_n_out_phdrs);
		memcpy(out_phdrs, phdrs, MIN(*p_n_out_phdrs, loadee.ehdr.e_phnum) * sizeof (ElfW(Phdr)));
		*p_n_out_phdrs = loadee.ehdr.e_phnum;
	}
	for (unsigned i = 0; i < loadee.ehdr.e_phnum; ++i)
	{
		if (phdrs[i].p_type == PT_LOAD)
		{
			_Bool read = (phdrs[i].p_flags & PF_R);
			_Bool write = (phdrs[i].p_flags & PF_W);
			_Bool exec = (phdrs[i].p_flags & PF_X);

			if (phdrs[i].p_offset < loadee.ehdr.e_phoff
					&& phdrs[i].p_filesz >= loadee.ehdr.e_phoff + (loadee.ehdr.e_phnum + loadee.ehdr.e_phentsize))
			{
				loadee.phdrs_addr = loadee.base_addr + phdrs[i].p_vaddr + (loadee.ehdr.e_phoff - phdrs[i].p_offset);
			}
			ret = load_one_phdr(loadee.base_addr, loadee_fd, phdrs[i].p_vaddr,
				phdrs[i].p_offset, phdrs[i].p_memsz, phdrs[i].p_filesz, read, write, exec);
			switch (ret)
			{
				case 2: die("file %s has bad PT_LOAD filesz/memsz (phdr index %d)\n", 
						loadee_path, i);
				case 1: die("could not create mapping for PT_LOAD phdr index %d\n", i);
				case 0: break;
				default:
					die("BUG: mysterious error in load_one_phdr() for PT_LOAD phdr index %d\n", i);
					break;
			}
		}
		else if (phdrs[i].p_type == PT_DYNAMIC)
		{
			loadee.dynamic = (ElfW(Dyn)*)(loadee.base_addr + phdrs[i].p_vaddr);
			loadee.dynamic_size = phdrs[i].p_memsz;
		}
	}
	close(loadee_fd);
	return loadee;
#undef die
}

// in main() we have another special way to die
#define die(s, ...) do { fprintf(stderr, DONALD_NAME ": " s , ##__VA_ARGS__); return -1; } while(0)
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
			default:
				break;
		}
	}

	fprintf(stderr, "We think we are%sthe program\n", we_are_the_program ? " " : " not ");
	if (entry == (uintptr_t) &_start)
	{
		// we were invoked as an executable
		argv_program_ind = 1;
	} else argv_program_ind = 0; // we were invoked as an interp
	
	if (argc <= argv_program_ind) { die("no program specified\n"); }

	int inferior_fd;
	const char *inferior_path;
#ifdef CHAIN_LOADER
	/* We always chain-load the ld.so and let it load the program. Let's read it. */
	inferior_path = SYSTEM_LDSO_PATH;
#else
	/* We have a program to run. Let's read it. */
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
				fprintf(stderr, "AT_ENTRY is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHDR:
				if (we_are_the_program) {
					our_phdrs = (ElfW(Phdr) *) p->a_un.a_val;
					p->a_un.a_val = inferior.phdrs_addr;
				} else program_phdrs = (void*) p->a_un.a_val;
				fprintf(stderr, "AT_PHDR is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHENT:
				if (we_are_the_program) {
					our_phentsize = p->a_un.a_val;
					p->a_un.a_val = inferior.ehdr.e_phentsize;
				} else program_phentsize = p->a_un.a_val;
				fprintf(stderr, "AT_PHENT is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHNUM:
				if (we_are_the_program) {
					our_phnum = p->a_un.a_val;
					p->a_un.a_val = inferior.ehdr.e_phnum;
				} else program_phnum = p->a_un.a_val;
				fprintf(stderr, "AT_PHNUM is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_BASE:
				if (we_are_the_program) p->a_un.a_val = 0;
				else p->a_un.a_val = inferior.base_addr;
				fprintf(stderr, "AT_BASE is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_EXECFN:
				if (we_are_the_program) p->a_un.a_val = (uintptr_t) argv[0];
				fprintf(stderr, "AT_EXECFN is %p (%s)\n", (void*) p->a_un.a_val, (char*) p->a_un.a_val);
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
		dt_debug = create_dt_debug(inferior.base_addr, inferior.dynamic_vaddr,
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
