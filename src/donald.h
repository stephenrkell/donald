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

/* donald's main.c will fill in this structure as it goes. Keeping these together in a
 * struct makes it easy to give out our entire context to helper functions. */
#define MAX_LDSO_PHDR 16
struct load_ctxt
{
	int argc;
	char **argv;
	int argv_program_ind;         // used to get loadee_path, but our callee might want it
	uintptr_t entry;              // from AT_ENTRY / does NOT duplicate inferior_entry_point: this is &_start by default, overridden by AT_ENTRY, in either invoked or requested cases; if invoked, AT_ENTRY matches &_start and this is how we detect it
	_Bool we_are_the_program;
	uintptr_t our_load_address;
	ElfW(auxv_t) *phdr_ent;       // XXX: whose phdrs are these? if from AT_PHDR, "program headers for program" -- what about in invoked vs requested cases?
	ElfW(auxv_t) *phnum_ent;
	ElfW(Phdr) phdrs[MAX_LDSO_PHDR];
	unsigned n_phdrs;
	const char *loadee_path;
	uintptr_t loadee_base_addr_hint;
	struct loadee_info *loadee;
	uintptr_t loadee_entry_point;
};

const char *get_loadee_path(struct load_ctxt *ctxt) HIDDEN;
uintptr_t get_loadee_base_addr_hint(struct load_ctxt *ctxt) HIDDEN;

struct loadee_info
load_file(const char *loadee_path, uintptr_t loadee_base_addr_hint,
	ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs) HIDDEN;
struct loadee_info
load_from_fd(int fd, const char *loadee_path /* for diagnostic only*/,
	uintptr_t loadee_base_addr_hint, ElfW(Phdr) *out_phdrs, unsigned *p_n_out_phdrs) HIDDEN;

int main(int argc, char **argv) HIDDEN;
int load_one_phdr(unsigned long base_addr, int fd, unsigned long vaddr, unsigned long offset,
	unsigned long memsz, unsigned long filesz, _Bool read, _Bool write, _Bool exec) HIDDEN;
void enter(struct load_ctxt *ctxt) __attribute__((noreturn)) HIDDEN;
uintptr_t __get_from_tls_reg_offset(unsigned off);
#ifdef CHAIN_LOADER
ElfW(Dyn) *find_or_create_dt_debug(uintptr_t inferior_load_addr, uintptr_t inferior_dynamic_vaddr,
	size_t our_dynamic_size, uintptr_t inferior_r_debug_vaddr) HIDDEN;
void point_dt_debug_to_inferior_r_debug(ElfW(Dyn) *d, uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr) HIDDEN;
void install_fake_singleton_inferior_link_map_in_r_debug(uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr) HIDDEN;
#endif

#endif
