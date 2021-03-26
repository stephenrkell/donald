#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <err.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#ifdef __x86_64__
#include <asm/prctl.h> /* for ARCH_SET_FS */
#endif
#ifdef __i386__
#include <linux/unistd.h>
#include <asm/ldt.h>
#endif
#include <assert.h>
#include "donald.h"

#define die(s, ...) do { fprintf(stderr, "donald: " s , ##__VA_ARGS__); return -1; } while(0)
// #define die(s, ...) do { fwrite("donald: " s , sizeof "donald: " s, 1, stderr); return -1; } while(0)

extern int _start(void);

#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif

static char fake_tls[4096]; // FIXME: better way to size this
void __init_tls(size_t *auxv)
{
#if defined(__x86_64__)
	syscall(SYS_arch_prctl, ARCH_SET_FS, (unsigned long) fake_tls);
#elif defined (__i386__)
	/* What's the 386 equivalent? musl seems to do set_thread_area...
	 * but not in its ld.so. FIXME: this is probably wrong. */
	static struct user_desc u;
	u.entry_number = -1;
	syscall(SYS_set_thread_area, &u);
#else
#error "Unrecognised architecture."
#endif
	*(void**)fake_tls = &fake_tls[0];
}

void __init_libc(char **envp, char *pn); // musl-internal API
int __init_tp(void *p);

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
#if defined(__x86_64__)
	const char ldso_path[] = "/lib64/ld-linux-x86-64.so.2";
#elif defined(__i386__)
	const char ldso_path[] = "/lib/ld-linux.so.2";
#else
#error "Unrecognised architecture."
#endif
	inferior_path = ldso_path;
#else
	/* We have a program to run. Let's read it. */
	inferior_path = argv[argv_program_ind];
#endif
	inferior_fd = open(argv[argv_program_ind], O_RDONLY);
	if (inferior_fd == -1) { die("could not open %s\n", inferior_path); }
	int ret = fstat(inferior_fd, &inferior_stat);
	if (ret != 0) { die("could not open %s\n", inferior_path); }
	
	// read the ELF header
	ssize_t nread;
	ElfW(Ehdr) ehdr; nread = read(inferior_fd, &ehdr, sizeof (ElfW(Ehdr)));
	if (nread != sizeof (ElfW(Ehdr))) die("could not read ELF header of %s\n", inferior_path);

	// check it's a file we can grok
	if (ehdr.e_ident[EI_MAG0] != 0x7f
			|| ehdr.e_ident[EI_MAG1] != 'E'
			|| ehdr.e_ident[EI_MAG2] != 'L'
			|| ehdr.e_ident[EI_MAG3] != 'F'
			|| ehdr.e_ident[EI_CLASS] != DONALD_ELFCLASS
			|| ehdr.e_ident[EI_DATA] != ELFDATA2LSB
			|| ehdr.e_ident[EI_VERSION] != EV_CURRENT
			|| (ehdr.e_ident[EI_OSABI] != ELFOSABI_SYSV && ehdr.e_ident[EI_OSABI] != ELFOSABI_GNU)
			// || ehdr. e_ident[EI_ABIVERSION] != /* what? */
			|| ehdr.e_type != ET_EXEC
			|| ehdr.e_machine != DONALD_ELFMACHINE
			)
	{
		die("unsupported file: %s\n", argv[argv_program_ind]);
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
	/* Now we've snarfed the phdrs. But remember that we want to map them
	 * without holes. To do this, calculate the maximum vaddr we need,
	 * then map a whole chunk of memory PROT_NONE in that space. We will
	 * use the ldso fd, so that it appears as a mapping of that file
	 * (this helps liballocs). */
	ElfW(Addr) max_vaddr = 0;
	for (unsigned i = 0; i < ehdr.e_phnum; ++i)
	{
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
	uintptr_t phdrs_addr = 0;
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
				phdrs_addr = base_addr + phdrs[i].p_vaddr + (ehdr.e_phoff - phdrs[i].p_offset);
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
	}

	// do relocations!

	// grab the entry point
	register unsigned long entry_point = base_addr + ehdr.e_entry;

	// now we're finished with the file
	close(inferior_fd);

#if defined(CHAIN_LOADER) && defined(CHAIN_LOADER_COVER_TRACKS) /* FIXME: reinstate */
	CHAIN_LOADER_COVER_TRACKS
#endif

	// jump to the entry point
	enter((void*) entry_point);
}
