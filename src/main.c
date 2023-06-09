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

#ifdef CHAIN_LOADER_COVER_TRACKS_DECLS
CHAIN_LOADER_COVER_TRACKS_DECLS
#endif

#define die(s, ...) do { fprintf(stderr, DONALD_NAME ": " s , ##__VA_ARGS__); return -1; } while(0)
// #define die(s, ...) do { fwrite(DONALD_NAME ": " s , sizeof DONALD_NAME ": " s, 1, stderr); return -1; } while(0)

extern int _start(void);

#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif

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
	fprintf(stderr, "We think we are%sthe program\n", we_are_the_program ? " " : " not ");
	if (entry == (uintptr_t) &_start)
	{
		// we were invoked as an executable
		argv_program_ind = 1;
	} else argv_program_ind = 0; // we were invoked as an interp
	
	if (argc <= argv_program_ind) { die("no program specified\n"); }

	int inferior_fd;
	struct stat inferior_stat;
	const char *inferior_path;
#ifdef CHAIN_LOADER
	/* We always chain-load the ld.so and let it load the program. Let's read it. */
	inferior_path = SYSTEM_LDSO_PATH;
#else
	/* We have a program to run. Let's read it. */
	inferior_path = argv[argv_program_ind];
#endif
	inferior_fd = open(inferior_path, O_RDONLY);
	if (inferior_fd == -1) { die("could not open %s\n", inferior_path); }
	int ret = fstat(inferior_fd, &inferior_stat);
	if (ret != 0) { die("could not open %s\n", inferior_path); }
	
	// read the ELF header
	ssize_t nread;
	ElfW(Ehdr) ehdr; nread = read(inferior_fd, &ehdr, sizeof (ElfW(Ehdr)));
	if (nread != sizeof (ElfW(Ehdr))) die("could not read ELF header of %s\n", inferior_path);

	_Bool is_elf = 0, class_matches = 0, is_lsb = 0, is_current = 0, is_sysv_or_gnu = 0,
		is_exec_or_solib = 0;
	// check it's a file we can grok
	if (ehdr.e_ident[EI_MAG0] != 0x7f
			|| ehdr.e_ident[EI_MAG1] != 'E'
			|| ehdr.e_ident[EI_MAG2] != 'L'
			|| ehdr.e_ident[EI_MAG3] != 'F'
			|| (is_elf = 1, ehdr.e_ident[EI_CLASS] != DONALD_ELFCLASS)
			|| (class_matches = 1, ehdr.e_ident[EI_DATA] != ELFDATA2LSB)
			|| (is_lsb = 1, ehdr.e_ident[EI_VERSION] != EV_CURRENT)
			|| (is_current = 1, ehdr.e_ident[EI_OSABI] != ELFOSABI_SYSV && ehdr.e_ident[EI_OSABI] != ELFOSABI_GNU)
			// || ehdr. e_ident[EI_ABIVERSION] != /* what? */
			|| (is_sysv_or_gnu = 1, ehdr.e_type != ET_EXEC && ehdr.e_type != ET_DYN)
			|| (is_exec_or_solib = 1, ehdr.e_machine != DONALD_ELFMACHINE)
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
			argv[argv_program_ind]);
	}
	
	// process the PT_LOADs
	off_t newloc = lseek(inferior_fd, ehdr.e_phoff, SEEK_SET);
	ElfW(Phdr) phdrs[ehdr.e_phnum];
	for (unsigned i = 0; i < ehdr.e_phnum; ++i)
	{
		off_t off = ehdr.e_phoff + i * ehdr.e_phentsize;
		newloc = lseek(inferior_fd, off, SEEK_SET);
		if (newloc != off) die("could not seek to program header %d in %s\n", i, inferior_path);
		size_t ntoread = MIN(sizeof phdrs[0], ehdr.e_phentsize);
		nread = read(inferior_fd, &phdrs[i], ntoread);
		if (nread != ntoread) die("could not read program header %d in %s\n", i, inferior_path);
	}
	// also snarf the shdrs
	newloc = lseek(inferior_fd, ehdr.e_shoff, SEEK_SET);
	ElfW(Shdr) shdrs[ehdr.e_shnum];
	for (unsigned i = 0; i < ehdr.e_shnum; ++i)
	{
		off_t off = ehdr.e_shoff + i * ehdr.e_shentsize;
		newloc = lseek(inferior_fd, off, SEEK_SET);
		if (newloc != off) die("could not seek to section header %d in %s\n", i, inferior_path);
		size_t ntoread = MIN(sizeof shdrs[0], ehdr.e_shentsize);
		nread = read(inferior_fd, &shdrs[i], ntoread);
		if (nread != ntoread) die("could not read section header %d in %s\n", i, inferior_path);
	}
	/* Now we've snarfed the phdrs. But remember that we want to map them
	 * without holes. To do this, calculate the maximum vaddr we need,
	 * then map a whole chunk of memory PROT_NONE in that space. We will
	 * use the ldso fd, so that it appears as a mapping of that file
	 * (this helps liballocs). */
	ElfW(Addr) max_vaddr = 0;
	uintptr_t inferior_dynamic_vaddr = (uintptr_t) -1;
	for (unsigned i = 0; i < ehdr.e_phnum; ++i)
	{
		if (phdrs[i].p_type == PT_DYNAMIC) inferior_dynamic_vaddr = phdrs[i].p_vaddr;
		ElfW(Addr) max_vaddr_this_obj = phdrs[i].p_vaddr + phdrs[i].p_memsz;
		if (max_vaddr_this_obj > max_vaddr) max_vaddr = max_vaddr_this_obj;
	}
#if defined(__x86_64__)
	uintptr_t base_addr_hint = 0x555555556000;
#elif defined (__i386__)
	uintptr_t base_addr_hint = 0x55556000;
#else
#error "Unrecognised architecture."
#endif
	void *base = mmap((void*) base_addr_hint, max_vaddr, PROT_NONE, MAP_PRIVATE,
		inferior_fd, 0);
	if (base == MAP_FAILED) die("could not map %s with PROT_NONE\n", inferior_path);
	uintptr_t base_addr = (uintptr_t) base;
	uintptr_t inferior_phdrs_addr = 0;
	ElfW(Dyn)* inferior_dynamic = NULL;
	size_t inferior_dynamic_size = 0;
	for (unsigned i = 0; i < ehdr.e_phnum; ++i)
	{
		if (phdrs[i].p_type == PT_LOAD)
		{
			_Bool read = (phdrs[i].p_flags & PF_R);
			_Bool write = (phdrs[i].p_flags & PF_W);
			_Bool exec = (phdrs[i].p_flags & PF_X);

			if (phdrs[i].p_offset < ehdr.e_phoff
					&& phdrs[i].p_filesz >= ehdr.e_phoff + (ehdr.e_phnum + ehdr.e_phentsize))
			{
				inferior_phdrs_addr = base_addr + phdrs[i].p_vaddr + (ehdr.e_phoff - phdrs[i].p_offset);
			}
			ret = load_one_phdr(base_addr, inferior_fd, phdrs[i].p_vaddr,
				phdrs[i].p_offset, phdrs[i].p_memsz, phdrs[i].p_filesz, read, write, exec);
			switch (ret)
			{
				case 2: die("file %s has bad PT_LOAD filesz/memsz (phdr index %d)\n", 
						inferior_path, i);
				case 1: die("could not create mapping for PT_LOAD phdr index %d\n", i);
				case 0: break;
				default:
					die("BUG: mysterious error in load_one_phdr() for PT_LOAD phdr index %d\n", i);
					break;
			}
		}
		else if (phdrs[i].p_type == PT_DYNAMIC)
		{
			inferior_dynamic = (ElfW(Dyn)*)(base_addr + phdrs[i].p_vaddr);
			inferior_dynamic_size = phdrs[i].p_memsz;
		}
	}

	// do relocations!

	// grab the entry point
	register unsigned long entry_point = base_addr + ehdr.e_entry;

#ifdef CHAIN_LOADER
	/* Fix up the auxv so that the ld.so thinks it's just been run.
	 * If 'we are the program' it means make the phdrs look like the
	 * ld.so was run as the executable, i.e. modify the auxv in place
	 * to directly reference the inferior stuff. Otherwise the only
	 * one we need to modify is AT_BASE, which points to the *interpreter*
	 * so needs to be pointed at the inferior; the others don't change. */
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
					p->a_un.a_val = inferior_phdrs_addr;
				} else program_phdrs = (void*) p->a_un.a_val;
				fprintf(stderr, "AT_PHDR is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHENT:
				if (we_are_the_program) {
					our_phentsize = p->a_un.a_val;
					p->a_un.a_val = ehdr.e_phentsize;
				} else program_phentsize = p->a_un.a_val;
				fprintf(stderr, "AT_PHENT is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_PHNUM:
				if (we_are_the_program) {
					our_phnum = p->a_un.a_val;
					p->a_un.a_val = ehdr.e_phnum;
				} else program_phnum = p->a_un.a_val;
				fprintf(stderr, "AT_PHNUM is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_BASE:
				if (we_are_the_program) p->a_un.a_val = 0;
				else p->a_un.a_val = base_addr;
				fprintf(stderr, "AT_BASE is %p\n", (void*) p->a_un.a_val);
				break;
			case AT_EXECFN:
				if (we_are_the_program) p->a_un.a_val = (uintptr_t) argv[0];
				fprintf(stderr, "AT_EXECFN is %p (%s)\n", (void*) p->a_un.a_val, (char*) p->a_un.a_val);
				break;
		}
	}
	/* In the 'invoked' case, to give debugging a chance of working,
	 * try to create a DT_DEBUG entry in our _DYNAMIC section. */
	ElfW(Dyn) *created_dt_debug = NULL;
	if (we_are_the_program)
	{
		assert(our_phdrs);
		/* The only thing we do is point it at the _r_debug structure in
		 * the *inferior* ld.so.
		 * It's up to the client code to populate this _r_debug. If it needs
		 * introspection to work early on, it might do this early using
		 * temporary values, e.g. during the 'covering tracks' phase that
		 * immediately follows this code. Otherwise the inferior ld.so will
		 * get around to populating its own _r_debug and debugging will start
		 * working when that happens.
		 * To find the _r_debug symbol, we use a simple but slow linear search
		 * rather than the hash table. */
		ElfW(Sym) *symtab = NULL;
		ElfW(Sym) *symtab_end = NULL;
		const unsigned char *strtab = NULL;
		size_t our_dynamic_size = 0;
		for (ElfW(Phdr) *phdr = our_phdrs; phdr != our_phdrs + our_phnum; ++phdr)
		{
			if (phdr->p_type == PT_DYNAMIC) { our_dynamic_size = phdr->p_memsz; break; }
		}
		for (ElfW(Dyn) *dyn = inferior_dynamic; dyn->d_tag != DT_NULL; ++dyn)
		{
			switch (dyn->d_tag)
			{
				case DT_SYMTAB:
					symtab = (ElfW(Sym) *)(base_addr + dyn->d_un.d_ptr);
					break;
				case DT_STRTAB:
					strtab = (const unsigned char *)(base_addr + dyn->d_un.d_ptr);
					symtab_end = (ElfW(Sym) *)strtab;
					break;
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
		if (found_r_debug_sym)
		{
			created_dt_debug = create_dt_debug(base_addr, inferior_dynamic_vaddr,
				our_dynamic_size, found_r_debug_sym->st_value);
		}
	}
#ifdef CHAIN_LOADER_COVER_TRACKS
	CHAIN_LOADER_COVER_TRACKS
#endif
#endif

	// now we're finished with the file
	close(inferior_fd);

	// jump to the entry point
	enter((void*) entry_point);
}
