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

int *p_argc HIDDEN;
char **argv HIDDEN;
char **envp HIDDEN;
ElfW(Dyn) *p_dyn HIDDEN;
ElfW(auxv_t) *p_auxv HIDDEN;

int main(void) HIDDEN;

#endif
