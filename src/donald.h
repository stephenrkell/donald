#ifndef DONALD_H_
#define DONALD_H_

#include <elf.h>
#include <unistd.h>

// stolen from glibc's link.h
#define __ELF_NATIVE_CLASS 64
#define ElfW(type)      _ElfW (Elf, __ELF_NATIVE_CLASS, type)
#define _ElfW(e,w,t)    _ElfW_1 (e, w, _##t)
#define _ElfW_1(e,w,t)  e##w##t

/* object-local  */
#define HIDDEN __attribute__((visibility("hidden")))

#define PAGE_ADJUST(n) (((uintptr_t)(n)) % page_size)

#ifndef DONALD_NAME
#define DONALD_NAME "donald"
#endif

extern char **environ HIDDEN;
extern ElfW(Dyn) *p_dyn HIDDEN;
extern ElfW(auxv_t) *p_auxv HIDDEN;
extern unsigned long page_size HIDDEN;
extern void *rsp_on_entry HIDDEN;

int main(int argc, char **argv) HIDDEN;
int load_one_phdr(unsigned long base_addr, int fd, unsigned long vaddr, unsigned long offset,
	unsigned long memsz, unsigned long filesz, _Bool read, _Bool write, _Bool exec) HIDDEN;
void enter(void *entry_point) __attribute__((noreturn)) HIDDEN;
#endif
