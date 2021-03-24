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
	// we need an argument
	if (argc < 2) { die("no program specified\n"); }
	
	/* We use musl as our libc. That means there are (up to) two libc instances
	 * in the process! Before we do any libc calls, make sure musl's state
	 * is initialized. */
	__init_libc((char**) environ, *argv);

	struct stat proc_exe;
	struct stat argv0;
	int ret = stat("/proc/self/exe", &proc_exe);
	if (ret != 0) { die("could not stat /proc/self/exe\n"); }
	ret = stat(argv[0], &argv0);
	if (ret != 0) { die("could not stat %s\n", argv[0]); }
	
	// were we invoked by name, or as a .interp?
	int argv_program_ind;
	if (proc_exe.st_dev == argv0.st_dev
			&& proc_exe.st_ino == argv0.st_ino)
	{
		// we were invoked as an executable
		argv_program_ind = 1;
	} else argv_program_ind = 0;
	
	if (argc <= argv_program_ind) { die("no program specified\n"); }

	/* We have a program to run. Let's read it. */
	int exe_fd = open(argv[argv_program_ind], O_RDONLY);
	if (exe_fd == -1) { die("could not open %s\n", argv[argv_program_ind]); }
	ret = fstat(exe_fd, &argv0);
	if (ret != 0) { die("could not open %s\n", argv[argv_program_ind]); }
	
	// mmap it all
	unsigned long mapped_size = argv0.st_size;
	char *mapping = mmap(NULL, mapped_size, PROT_READ, MAP_PRIVATE, exe_fd, 0);
	if (mapping == MAP_FAILED) { die("could not mmap %s\n", argv[argv_program_ind]); }
	
	// read the elf header
	ElfW(Ehdr) *p_hdr = (void*) mapping;
	// check it's a file we can grok
	if (p_hdr->e_ident[EI_MAG0] != 0x7f
			|| p_hdr->e_ident[EI_MAG1] != 'E'
			|| p_hdr->e_ident[EI_MAG2] != 'L'
			|| p_hdr->e_ident[EI_MAG3] != 'F'
			|| p_hdr->e_ident[EI_CLASS] != DONALD_ELFCLASS
			|| p_hdr->e_ident[EI_DATA] != ELFDATA2LSB
			|| p_hdr->e_ident[EI_VERSION] != EV_CURRENT
			|| (p_hdr->e_ident[EI_OSABI] != ELFOSABI_SYSV && p_hdr->e_ident[EI_OSABI] != ELFOSABI_GNU)
			// || phdr->e_ident[EI_ABIVERSION] != /* what? */
			|| p_hdr->e_type != ET_EXEC
			|| p_hdr->e_machine != DONALD_ELFMACHINE
			)
	{
		die("unsupported file: %s\n", argv[argv_program_ind]);
	}
	
	// process the PT_LOADs
	ElfW(Phdr) *p_phdr = (void*) (mapping + p_hdr->e_phoff);
	uintptr_t base_addr = 0;
	for (unsigned i = 0; i < p_hdr->e_phnum; ++i)
	{
		if (p_phdr[i].p_type == PT_LOAD)
		{	
			_Bool read = (p_phdr[i].p_flags & PF_R);
			_Bool write = (p_phdr[i].p_flags & PF_W);
			_Bool exec = (p_phdr[i].p_flags & PF_X);

			ret = load_one_phdr(base_addr, exe_fd, p_phdr[i].p_vaddr,
				p_phdr[i].p_offset, p_phdr[i].p_memsz, p_phdr[i].p_filesz, read, write, exec);
			switch (ret)
			{
				case 2: die("file %s has bad PT_LOAD filesz/memsz (phdr index %d)\n", 
						argv[argv_program_ind], i);
				case 1: die("could not create mapping for PT_LOAD phdr index %d\n", i);
				default:
					break;
			}
		}
	}
	
	// do relocations!

	// grab the entry point
	register unsigned long entry_point = p_hdr->e_entry;
	
	// now we're finished with the file
	munmap(mapping, mapped_size);
	close(exe_fd);
	
	// jump to the entry point
	enter((void*) entry_point);
}
