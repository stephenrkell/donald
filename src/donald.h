#ifndef DONALD_H_
#define DONALD_H_

#include <elf.h>
#include <unistd.h>
#include <stdint.h>

// stolen from glibc's link.h
#ifdef __x86_64__
#ifndef __ELF_NATIVE_CLASS
#define __ELF_NATIVE_CLASS 64
#endif
#define DONALD_ELFCLASS ELFCLASS64
#define DONALD_ELFMACHINE EM_X86_64
#elif defined(__i386__)
#define __ELF_NATIVE_CLASS 32
#define DONALD_ELFCLASS ELFCLASS32
#define DONALD_ELFMACHINE EM_386
#else
#error "Unrecognised architecture."
#endif

#ifndef ElfW
#define ElfW(type)      _ElfW (Elf, __ELF_NATIVE_CLASS, type)
#define _ElfW(e,w,t)    _ElfW_1 (e, w, _##t)
#define _ElfW_1(e,w,t)  e##w##t
#endif

/* object-local  */
#define HIDDEN __attribute__((visibility("hidden")))

#define PAGE_ADJUST(n) (((uintptr_t)(n)) % page_size)
#define ROUND_UP_TO(mult, v) \
    ((mult) * (((v) + ((mult)-1)) / (mult)))

#ifndef DONALD_NAME
#define DONALD_NAME "donald"
#endif

#ifndef debug_printf
#define debug_printf(lvl, fmt, ...) \
   do { fprintf(stderr, DONALD_NAME ": " fmt , ##__VA_ARGS__); } while(0)
#endif

#ifndef SYSTEM_LDSO_PATH
#if defined(__x86_64__)
#define SYSTEM_LDSO_PATH "/lib64/ld-linux-x86-64.so.2"
#elif defined(__i386__)
#define SYSTEM_LDSO_PATH "/lib/ld-linux.so.2"
#else
#error "Unrecognised architecture."
#endif
#endif

extern int _begin; // defined by linker script; if used, we need it, so non-weak
extern char **environ HIDDEN;
extern ElfW(Dyn) *p_dyn HIDDEN;
extern ElfW(auxv_t) *p_auxv HIDDEN;
extern unsigned long page_size HIDDEN;
extern void *sp_on_entry HIDDEN;
extern ElfW(Dyn) _DYNAMIC[];

/* FIXME: this is really a lightly expanded version of a standard SVr4 struct link_map.
 * Perhaps we can copy its initial fields? We lack l_name currently, but could use "". */
struct loadee_info
{
	uintptr_t dynamic_vaddr;
	uintptr_t base_addr;
	uintptr_t phdrs_addr;
	ElfW(Ehdr) ehdr;
	ElfW(Dyn)* dynamic;
	size_t dynamic_size;
	char errmsg[400];
};

struct loadee_info
load_file(const char *loadee_path, uintptr_t loadee_base_addr_hint,
	ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs) HIDDEN;
struct loadee_info
load_from_fd(int fd, const char *loadee_path /* for diagnostic only*/,
	uintptr_t loadee_base_addr_hint, ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs) HIDDEN;

int main(int argc, char **argv) HIDDEN;
int load_one_phdr(unsigned long base_addr, int fd, unsigned long vaddr, unsigned long offset,
	unsigned long memsz, unsigned long filesz, _Bool read, _Bool write, _Bool exec) HIDDEN;
void enter(void *entry_point) __attribute__((noreturn)) HIDDEN;
uintptr_t __get_from_tls_reg_offset(unsigned off);
#ifdef CHAIN_LOADER
ElfW(Dyn) *find_or_create_dt_debug(uintptr_t inferior_load_addr, uintptr_t inferior_dynamic_vaddr,
	size_t our_dynamic_size, uintptr_t inferior_r_debug_vaddr) HIDDEN;
void populate_dt_debug(ElfW(Dyn) *d, uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr) HIDDEN;
#endif

#endif
