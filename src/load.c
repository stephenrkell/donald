#include "donald.h"
#include <sys/mman.h>

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
		ret = mmap(addr - PAGE_ADJUST(addr),
			mapping_size,
			prot, MAP_FIXED | MAP_PRIVATE, fd, offset - PAGE_ADJUST(addr));
		
		if (ret != MAP_FAILED && mapping_size - PAGE_ADJUST(addr) < memsz)
		{
			// assert(PAGE_ADJUST(addr) == 0);
			ret = mmap(addr - PAGE_ADJUST(addr) + mapping_size,
				/*ROUND_UP_TO(page_size, */memsz - (mapping_size - PAGE_ADJUST(addr))/*)*/,
				prot, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		}
		return (ret == MAP_FAILED);
	}
	else return 2;
}
