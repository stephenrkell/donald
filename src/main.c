#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <err.h>
#include "donald.h"

// #define die(s, ...) do { fprintf(stderr, "donald: " s , ##__VA_ARGS__); return -1; } while(0)
#define die(s, ...) do { fwrite("donald: " s , sizeof "donald: " s, 1, stderr); return -1; } while(0)

#define PAGE_SIZE 4096
#define PAGE_ADJUST(n) (((uintptr_t)(n)) % PAGE_SIZE)

int main(void)
{
	int argc = *p_argc;
	
	// we need an argument
	if (argc < 2) { die("no program specified\n"); }
	
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
			|| p_hdr->e_ident[EI_CLASS] != ELFCLASS64
			|| p_hdr->e_ident[EI_DATA] != ELFDATA2LSB
			|| p_hdr->e_ident[EI_VERSION] != EV_CURRENT
			|| (p_hdr->e_ident[EI_OSABI] != ELFOSABI_SYSV && p_hdr->e_ident[EI_OSABI] != ELFOSABI_GNU)
			// || phdr->e_ident[EI_ABIVERSION] != /* what? */
			|| p_hdr->e_type != ET_EXEC
			|| p_hdr->e_machine != EM_X86_64
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
			// mmap it
			int prot = 0;
			if (p_phdr[i].p_flags & PF_R) prot |= PROT_READ;
			if (p_phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
			if (p_phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
			
			// we either have filesz == 0 or filesz == memsz 
			// ... or memsz > filesz and extends beyond the end of the file
			// ... or offset + filesz hits a page boundary and memsz is bigger
			void *ret;
			if (p_phdr[i].p_filesz == 0)
			{
				char *addr = (char*) base_addr + p_phdr[i].p_vaddr;
				ret = mmap(addr - PAGE_ADJUST(addr), p_phdr[i].p_memsz + PAGE_ADJUST(addr),
					prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			} 
			else if (p_phdr[i].p_filesz == p_phdr[i].p_memsz)
			{
				char *addr = (char*) base_addr + p_phdr[i].p_vaddr;
				ret = mmap(addr - PAGE_ADJUST(addr), p_phdr[i].p_memsz + PAGE_ADJUST(addr),
					prot, MAP_FIXED | MAP_PRIVATE, exe_fd, p_phdr[i].p_offset - PAGE_ADJUST(addr));
			} 
			else if (p_phdr[i].p_memsz > p_phdr[i].p_filesz
				 && p_phdr[i].p_offset + p_phdr[i].p_filesz == mapped_size)
			{
				// we can map more than filesz if we're beyond the end of the file
				char *addr = (char*) base_addr + p_phdr[i].p_vaddr;
				ret = mmap(addr - PAGE_ADJUST(addr), p_phdr[i].p_memsz + PAGE_ADJUST(addr),
					prot, MAP_FIXED | MAP_PRIVATE, exe_fd, p_phdr[i].p_offset - PAGE_ADJUST(addr));
			} 
			else if (p_phdr[i].p_memsz > p_phdr[i].p_filesz
				 && (p_phdr[i].p_offset + p_phdr[i].p_filesz) % PAGE_SIZE == 0)
			{
				// two mappings: one as usual for the filesz...
				char *addr = (char*) base_addr + p_phdr[i].p_vaddr;
				ret = mmap(addr - PAGE_ADJUST(addr), p_phdr[i].p_filesz + PAGE_ADJUST(addr),
					prot, MAP_FIXED | MAP_PRIVATE, exe_fd, p_phdr[i].p_offset - PAGE_ADJUST(addr));
				if (ret != MAP_FAILED)
				{
					// one anonymous for the remainder of the memsz
					addr = (char*) base_addr + p_phdr[i].p_vaddr + p_phdr[i].p_filesz;
					// assert(PAGE_ADJUST(addr) == 0);
					ret = mmap(addr, p_phdr[i].p_memsz - p_phdr[i].p_filesz,
						prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				}
			}
			else
			{
				die("file %s has bad PT_LOAD filesz/memsz (phdr index %d)\n", 
							argv[argv_program_ind], i);
			}
			
			if (ret == MAP_FAILED) die("could not create mapping for PT_LOAD phdr index %d\n", i);
		}
	}
	
	// do relocations!

	// grab the entry point
	register unsigned long entry_point = p_hdr->e_entry;
	
	// now we're finished with the file
	munmap(mapping, mapped_size);
	close(exe_fd);
	
	// jump to the entry point
	__asm__("jmpq *%0\n" : : "r"(entry_point));
			
	// we shouldn't have to return, but...
	return 0;
}
