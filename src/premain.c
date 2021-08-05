#define _GNU_SOURCE /* for syscall() */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <asm/unistd.h>
#ifdef __x86_64__
#include <asm/prctl.h> /* for ARCH_SET_FS */
#endif
#ifdef __i386__
#include <linux/unistd.h>
#include <asm/ldt.h>
#endif
#include <assert.h>
#include "donald.h"
#define RELF_DEFINE_STRUCTURES
#include "relf.h"

extern int _begin HIDDEN;   // defined by our hacked linker script (in Makefile)

/* FIXME: instead of copying these guys out of auxv, can we *define symbols* 
 * located at the top of the stack, so that clients can just *link* to them? 
 * One way to do this would be to include this in the bootstrap relocation
 * phase: we update our own symbol definitions to point to the addresses
 * of these things on the stack. Note that this isn't quite "relocation" per
 * se. HMM. Another way would be to treat them as *undefined* symbols in ld.so, 
 * which are then defined by the initial process image. But which link map
 * would then include them? We'd need a special "process" link map entry.
 * So extending the ld.so's symbol table seems sensible. */
char **environ;
ElfW(Dyn) *p_dyn;
ElfW(auxv_t) *p_auxv;
unsigned long page_size;

static inline void __attribute__((always_inline)) bootstrap_relocate(unsigned char *at_base);

static inline void __attribute__((always_inline)) preinit(unsigned char *sp_on_entry,
		int *p_argc, char ***p_argv)
{
	/* At start-of-day, we have
	 * 
	 * - auxv on the stack above us -- in ascending address order:
	 *     - argc; argv ptr vec, NULL-term'd; env ptr vec, NULL-term'd; auxv vec, AT_NULL-term'd
	 *          ... then padding, asciiz sections for arg and env, end marker
	 * 
	 * - no access to globals! we have to bootstrap-relocate first
	 * 
	 * To bootstrap-relocate, we need to know our own base address, which
	 * our hacked linker script provides.
	 */
	uintptr_t *p = (uintptr_t *)sp_on_entry;
	*p_argc = *(int *)(p++);
	char **address_of_argv0 = (char**)p++;
	*p_argv = address_of_argv0;
	while (*p++);
	char **address_of_envp0 = (char**)p++;
	environ = address_of_envp0;
	while (*p++);
	void **address_of_auxv0 = (void**)p;
	p_auxv = (void*) address_of_auxv0;
	// grab the page size out of the auxv
	ElfW(auxv_t) *p_pagesize = p_auxv;
	while (p_pagesize->a_type != AT_NULL && p_pagesize->a_type != AT_PAGESZ)
	{ ++p_pagesize; }
	if (p_pagesize->a_type == AT_PAGESZ) page_size = p_pagesize->a_un.a_val;

	unsigned char *base_addr = (unsigned char *) &_begin;
	bootstrap_relocate(base_addr);
}

static inline void __attribute__((always_inline)) 
do_one_rela(ElfW(Rela) *p_rela, unsigned char *at_base, ElfW(Sym) *p_dynsym)
{
#if defined(__x86_64__)
#define SYMADDR(r_info) (p_dynsym[ELF64_R_SYM((r_info))].st_value)
	Elf64_Addr *reloc_addr = (Elf64_Addr *)(at_base + p_rela->r_offset);
	switch (ELF64_R_TYPE(p_rela->r_info))
	{
		case R_X86_64_RELATIVE: // no symbol addr, because we're RELATIVE
			*reloc_addr = (Elf64_Addr)(at_base + p_rela->r_addend); 
			break;
		case R_X86_64_64: 
			*reloc_addr = (Elf64_Addr)(at_base + SYMADDR(p_rela->r_info) + p_rela->r_addend);
			break;
		/* Beware weak symbols having GOT entries. They should remain zero. */
		case R_X86_64_JUMP_SLOT:
		case R_X86_64_GLOB_DAT:
			*reloc_addr = (Elf64_Addr)(at_base + (SYMADDR(p_rela->r_info) ?: -(uintptr_t)at_base));
			break;
		default: 
			/* We can't report an error in any useful way here. */
			break;
	}
#undef SYMADDR
#elif defined(__i386__)
#define SYMADDR(r_info) (p_dynsym[ELF32_R_SYM((r_info))].st_value)
	Elf32_Addr *reloc_addr = (Elf32_Addr *)(at_base + p_rela->r_offset);
	switch (ELF32_R_TYPE(p_rela->r_info))
	{
		case R_386_RELATIVE: // no symbol addr, because we're RELATIVE
			*reloc_addr = (Elf32_Addr)(at_base + p_rela->r_addend); 
			break;
		case R_386_32:
			*reloc_addr = (Elf32_Addr)(at_base + SYMADDR(p_rela->r_info) + p_rela->r_addend);
			break;
		/* Beware weak symbols having GOT entries. They should remain zero. */
		case R_386_JMP_SLOT:
		case R_386_GLOB_DAT:
			*reloc_addr = (Elf32_Addr)(at_base + (SYMADDR(p_rela->r_info) ?: -(uintptr_t)at_base));
			break;
		default:
			/* We can't report an error in any useful way here. */
			break;
	}
#undef SYMADDR

#else
#error "Unknown architecture."
#endif
}

static inline void __attribute__((always_inline))
do_one_rel(ElfW(Rel) *p_rel, unsigned char *at_base, ElfW(Sym) *p_dynsym)
{
#if defined(__x86_64__)
/* Nothing: x86_64 does not use rel */
#elif defined(__i386__)
#define SYMADDR(r_info) (p_dynsym[ELF32_R_SYM((r_info))].st_value)
	Elf32_Addr *reloc_addr = (Elf32_Addr *)(at_base + p_rel->r_offset);
	Elf32_Addr current; memcpy(&current, reloc_addr, sizeof (Elf32_Addr));
	switch (ELF32_R_TYPE(p_rel->r_info))
	{
		case R_386_RELATIVE: // no symbol addr, because we're RELATIVE
			*reloc_addr = (Elf32_Addr) at_base + current;
			break;
		case R_386_32:
			*reloc_addr = (Elf32_Addr)(at_base + SYMADDR(p_rel->r_info) + current);
			break;
		/* Beware weak symbols having GOT entries. They should remain zero. */
		case R_386_JMP_SLOT:
		case R_386_GLOB_DAT:
			*reloc_addr = (Elf32_Addr)((intptr_t)at_base + current + (SYMADDR(p_rel->r_info) ?: -(current + (uintptr_t) at_base)));
			break;
		default:
			/* We can't report an error in any useful way here. */
			break;
	}
#undef SYMADDR
#else
#error "Unknown architecture."
#endif
}

static inline void __attribute__((always_inline)) bootstrap_relocate(unsigned char *at_base)
{
	/* We scan _DYNAMIC to get our own symbol table. HACK: we manually relocate &_DYNAMIC
	 * by our load address to get its actual address. */
	ElfW(Dyn) *p_dyn = (void*)(at_base + (uintptr_t) &_DYNAMIC);
	ElfW(Sym) *dynsym_start = NULL;
	unsigned long dynsym_nsyms = 0;

	void *rela_plt_start = NULL;
	unsigned long rela_plt_sz = 0;

	ElfW(Rela) *rela_dyn_start = NULL;
	unsigned long rela_dyn_sz = 0;
	unsigned long rela_dyn_entsz = 0;
	unsigned long rela_dyn_nents = 0;

	ElfW(Rel) *rel_dyn_start = NULL;
	unsigned long rel_dyn_sz = 0;
	unsigned long rel_dyn_entsz = 0;
	unsigned long rel_dyn_nents = 0;

	unsigned long pltrel = 0;
	while (p_dyn->d_tag != DT_NULL)
	{
		if (p_dyn->d_tag == DT_SYMTAB) dynsym_start = (void*)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_SYMENT) dynsym_nsyms = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELA) rela_dyn_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_RELASZ) rela_dyn_sz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELAENT) rela_dyn_entsz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_REL) rel_dyn_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_RELSZ) rel_dyn_sz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELENT) rel_dyn_entsz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_JMPREL) rela_plt_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_PLTRELSZ) rela_plt_sz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_PLTREL) pltrel = p_dyn->d_un.d_val;
		++p_dyn;
	}
	if (rela_dyn_entsz > 0) rela_dyn_nents = rela_dyn_sz / rela_dyn_entsz;
	if (rel_dyn_entsz > 0) rel_dyn_nents = rel_dyn_sz / rel_dyn_entsz;
	if (rela_dyn_entsz > 0 && rel_dyn_entsz > 0) abort();
	unsigned long dynrel;
	if (rela_dyn_entsz > 0) dynrel = DT_RELA; else dynrel = DT_REL;
	
	/* We loop over the relocs table and relocate what needs relocating. 
	 * uClibc claims that we should *only* relocate things that are not 
	 * subject to interposition. IS THIS TRUE? We're the dynamic loader, so
	 * we shouldn't have an interposable stuff (either references or definitions)
	 * internally, should we? What would it mean to interpose on a symbol defined
	 * by the dynamic linker? What would it mean to interpose on a reference made
	 * from the dynamic linker? HMM. */
	//ElfW(Rela) *p_rela = rela_dyn_start;
	for (int i = 0; i < ((dynrel == DT_REL) ? rel_dyn_nents : rela_dyn_nents); ++i)
	{
		if (dynrel == DT_REL)
		     do_one_rel (rel_dyn_start  + i, at_base, dynsym_start);
		else do_one_rela(rela_dyn_start + i, at_base, dynsym_start);
	}
	/* Also do .rela.plt */
	/* NOTE: it's called rela_plt_start, but it could be rel or rela */
	for (int i = 0;
			i < ((pltrel == DT_REL) ? (rela_plt_sz / sizeof (ElfW(Rel)))
			                        : (rela_plt_sz / sizeof (ElfW(Rela))));
			++i)
	{
		if (pltrel == DT_REL)
		    do_one_rel(((ElfW(Rel) *) rela_plt_start) + i, at_base, dynsym_start);
		else do_one_rela(((ElfW(Rela) *) rela_plt_start) + i, at_base, dynsym_start);
	}
}

// with musl we don't need a fake_tls -- it has a static builtin_tls which we link in
/* __init_tp is called by __init_libc: __init_tp(__copy_tls((void*) builtin_tls))
 * and returns zero on success. We wrap it. */
int __set_thread_area(struct user_desc *u_info);
int __wrap___init_tp(void *tp)
{
#if defined(__x86_64__)
	// syscall(SYS_arch_prctl, ARCH_SET_FS, (unsigned long) fake_tls);
	__set_thread_area(tp); // musl-internal, does the same thing
#elif defined (__i386__)
	__set_thread_area(tp); // musl-internal, does the same thing
#else
#error "Unrecognised architecture."
#endif
	return 0;
}

//void static_init_tls(size_t *auxv); // in musl
//void __init_tls(size_t *auxv) { return static_init_tls(auxv); } /* HACK borrowed from musl's own dynlink.c */
// Actually we want to run the stock musl __init_tls because we have no __dls2b() to call it,
// and it does the __init_tp call with the builtin_tls argument we wanted.
void __init_libc(char **envp, char *pn); // musl-internal API
int __init_tp(void *p);

// for TLS debugging
uintptr_t __get_from_tls_reg_offset(unsigned off) __attribute__((visibility("hidden")));
uintptr_t __get_from_tls_reg_offset(unsigned off)
{
	uintptr_t word_read;
#if defined(__x86_64__)
	__asm__ volatile ("movq %%fs:(%1), %0" : "=r"(word_read) : "r"(off) : /* clobbers */);
#elif defined(__i386__)
	__asm__ volatile ("mov  %%gs:(%1), %0" : "=r"(word_read) : "r"(off) : /* clobbers */);
#else
#error "Unsupported architecture"
#endif
	return word_read;
}

static void tls_sanity_check(void)
{
	// assert here that our TLS state is sane, i.e. that the
	// first word on the end of our %gs points to itself,
	uintptr_t tp_as_read = __get_from_tls_reg_offset(0);
	uintptr_t tp_as_read_from_itself = *(uintptr_t*) tp_as_read;
	assert(tp_as_read == tp_as_read_from_itself);
	// and that + 16 bytes is the vdso's sysinfo (kernel_vsyscall) entry point
	// FIXME: this is i386 sysdep, though harmless on x86_64
	ElfW(auxv_t) *sysinfo_ent = auxv_lookup(p_auxv, AT_SYSINFO);
	if (sysinfo_ent)
	{
		uintptr_t sysinfo_as_read = __get_from_tls_reg_offset(16);
		assert(sysinfo_as_read == sysinfo_ent->a_un.a_val);
	}
}

/* The function prologue pushes rbp on entry, decrementing the stack
 * pointer by 8. Then it saves rsp into rbp. So by the time we see rbp, 
 * it holds the entry stack pointer *minus 8 bytes*. */
#define BP_TO_SP_FIXUP sizeof(char*)

void *sp_on_entry HIDDEN;
/* This isn't the usual "main"; it's the raw entry point of the application. 
 * We link with -nostartfiles. We then define our own main. */
int _start(void)
{
	/* gcc doesn't let us disable prologue/epilogue, so we have to fudge it.
	 * We assume rsp is saved into rbp in the prologue. */
	register unsigned char *bp_after_main_prologue;
#if defined(__x86_64__)
	__asm__ ("movq %%rbp, %0\n" : "=r"(bp_after_main_prologue));
#elif defined(__i386__)
	__asm__ ("mov %%ebp, %0\n" : "=r"(bp_after_main_prologue));
#else
#error "Unrecognised architecture."
#endif
	
	int argc;
	char **argv;
	sp_on_entry = bp_after_main_prologue + BP_TO_SP_FIXUP;
	preinit(sp_on_entry, &argc, &argv); // get us a sane environment
	// calls __init_tp... FIXME: do we really want to init musl?
	// Perhaps we should simply fake up TLS ourselves, using __set_thread_area
	// directly, and work entirely "underneath". Avoids fragility of depending
	// on musl internals, like its builtin_tls, and __init_libc call sequence.
	__init_libc(environ, argv[0]);
	tls_sanity_check();

	printf("Hello from " DONALD_NAME "!\n");
	
	int ret = main(argc, argv);
	
	/* We're executing without startfile code, so returning 0 would not make sense. 
	 * Calling exit() brings a dependency on fini_array stuff that we need to avoid
	 * since it depends on the startup files. So just do the syscall directly. */
	syscall(SYS_exit, ret);
	
	__builtin_unreachable();
}

void __GI_exit(int status)
{
	/* We provide our own exit() implementation to avoid depending on uClibc's fini_array. 
	 * We don't need a fini_array because we don't use atexit(). */
	syscall(SYS_exit, status);
}
