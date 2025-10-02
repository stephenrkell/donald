#define _GNU_SOURCE /* for MAP_ANONYMOUS */
#include "donald.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h> /* for memset */
#include <assert.h>

#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif

int load_one_phdr(unsigned long base_addr, int fd, unsigned long vaddr, unsigned long offset,
	unsigned long memsz, unsigned long filesz, _Bool read, _Bool write, _Bool exec)
{
	// mmap it
	int prot = 0;
	if (read) prot |= PROT_READ;
	if (write) prot |= PROT_WRITE;
	if (exec) prot |= PROT_EXEC;

	// we either have filesz == 0 or filesz == memsz 
	// ... or memsz > filesz and extends beyond the end of the file
	// ... or offset + filesz hits a page boundary and memsz is bigger
	void *ret;
	if (filesz == 0)
	{
		char *addr = (char*) base_addr + vaddr;
		ret = mmap(addr - PAGE_ADJUST(addr),
			ROUND_UP_TO(page_size, memsz + PAGE_ADJUST(addr)),
			prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return (ret == MAP_FAILED);
	}
	else if (memsz >= filesz)
	{
		// up to two mappings: one as usual for the filesz...
		char *addr = (char*) base_addr + vaddr;
		size_t mapping_size = ROUND_UP_TO(page_size, filesz + PAGE_ADJUST(addr));
		// clear the range from filesz upwards if necessary -- it might be bss
		ssize_t uninited_size = mapping_size - PAGE_ADJUST(addr) - filesz;
		int temporary_prot = (uninited_size <= 0) ? prot : ((prot | PROT_WRITE) & ~PROT_EXEC);
		ret = mmap(addr - PAGE_ADJUST(addr),
			mapping_size,
			temporary_prot, MAP_FIXED | MAP_PRIVATE, fd, offset - PAGE_ADJUST(addr));
		if (ret != MAP_FAILED)
		{
			if (uninited_size > 0)
			{
				memset((char*) ret + PAGE_ADJUST(addr) + filesz, 0, uninited_size);
			}
			if (temporary_prot != prot)
			{
				mprotect(ret, mapping_size, prot);
			}
			uintptr_t mapped_up_to_vaddr = vaddr - PAGE_ADJUST(vaddr) + mapping_size;
			if (mapped_up_to_vaddr < vaddr + memsz)
			{
				uintptr_t mapped_end_vaddr = ROUND_UP_TO(page_size, vaddr + memsz);
				assert(mapped_end_vaddr > mapped_up_to_vaddr);
				ret = mmap((char*) base_addr + mapped_up_to_vaddr,
					mapped_end_vaddr - mapped_up_to_vaddr,
					prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			}
		}
		return (ret == MAP_FAILED);
	}
	else return 2;
}

// in load_file() and similar we have a special way to die
#define die(s, ...) do { snprintf(loadee.errmsg, sizeof loadee.errmsg, \
    DONALD_NAME ": " s , ##__VA_ARGS__); loadee.dynamic_vaddr = 0; return loadee; } while(0)

#define LOADEE_FAILED \
{ \
	.dynamic_vaddr = (uintptr_t) -1 \
}

__attribute__((visibility("hidden")))
struct loadee_info
load_from_fd(int loadee_fd, const char *loadee_path /* for diagnostic only */,
	uintptr_t loadee_base_addr_hint,
	ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs)
{
	struct loadee_info loadee = LOADEE_FAILED;
	struct stat loadee_stat;
	int ret = fstat(loadee_fd, &loadee_stat);
	if (ret != 0) { die("could not stat %s\n", loadee_path); }

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
	return loadee;
}

__attribute__((visibility("hidden")))
struct loadee_info
load_file(const char *loadee_path, uintptr_t loadee_base_addr_hint,
	ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs)
{
	struct loadee_info loadee = LOADEE_FAILED;

	int loadee_fd = open(loadee_path, O_RDONLY);
	if (loadee_fd == -1) { die("could not open %s\n", loadee_path); }
	struct loadee_info info = load_from_fd(
		loadee_fd, loadee_path, loadee_base_addr_hint, out_phdrs, p_n_out_phdrs);
	close(loadee_fd);
	return info;
}

#undef die
