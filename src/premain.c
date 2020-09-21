#include <stdio.h>
#include <stdint.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <linux/auxvec.h>  /* For AT_xxx definitions */
#include "donald.h"

extern int _DYNAMIC;        // defined for us by the linker
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
		case R_X86_64_JUMP_SLOT:
		case R_X86_64_GLOB_DAT:
			*reloc_addr = (Elf64_Addr)(at_base + SYMADDR(p_rela->r_info));
			break;
		default: 
			/* We can't report an error in any useful way here. */
			break;
	}
#undef SYMADDR
}

static inline void __attribute__((always_inline)) bootstrap_relocate(unsigned char *at_base)
{
	/* We scan _DYNAMIC to get our own symbol table. HACK: we manually relocate &_DYNAMIC
	 * by our load address to get its actual address. */
	ElfW(Dyn) *p_dyn = (void*)(at_base + (uintptr_t) &_DYNAMIC);
	ElfW(Sym) *dynsym_start = NULL;
	unsigned long dynsym_nsyms = 0;
	ElfW(Rela) *rela_dyn_start = NULL;
	ElfW(Rela) *rela_plt_start = NULL;
	unsigned long rela_dyn_sz = 0;
	unsigned long rela_dyn_entsz = 0;
	unsigned long rela_dyn_nents = 0;
	unsigned long rela_plt_sz = 0;
	while (p_dyn->d_tag != DT_NULL)
	{
		if (p_dyn->d_tag == DT_SYMTAB) dynsym_start = (void*)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_SYMENT) dynsym_nsyms = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELA) rela_dyn_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_RELASZ) rela_dyn_sz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELAENT) rela_dyn_entsz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_JMPREL) rela_plt_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_PLTRELSZ) rela_plt_sz = p_dyn->d_un.d_val;
		++p_dyn;
	}
	if (rela_dyn_entsz > 0) rela_dyn_nents = rela_dyn_sz / rela_dyn_entsz;
	
	/* We loop over the relocs table and relocate what needs relocating. 
	 * uClibc claims that we should *only* relocate things that are not 
	 * subject to interposition. IS THIS TRUE? We're the dynamic loader, so
	 * we shouldn't have an interposable stuff (either references or definitions)
	 * internally, should we? What would it mean to interpose on a symbol defined
	 * by the dynamic linker? What would it mean to interpose on a reference made
	 * from the dynamic linker? HMM. */
	ElfW(Rela) *p_rela = rela_dyn_start;
	for (int i = 0; i < rela_dyn_nents; ++i)
	{
		do_one_rela(rela_dyn_start + i, at_base, dynsym_start);
	}
	p_rela = rela_plt_start;
	/* HACK: we assume PLT contains rela, not rel, for now. */
	for (int i = 0; i < (rela_plt_sz / sizeof (Elf64_Rela)); ++i)
	{
		do_one_rela(rela_plt_start + i, at_base, dynsym_start);
	}
	/* Also do .rela.plt */
}

/* The function prologue pushes rbp on entry, decrementing the stack
 * pointer by 8. Then it saves rsp into rbp. So by the time we see rbp, 
 * it holds the entry stack pointer *minus 8 bytes*. */
#define BP_TO_SP_FIXUP 0x8

void *rsp_on_entry HIDDEN;
/* This isn't the usual "main"; it's the raw entry point of the application. 
 * We link with -nostartfiles. We then define our own main. */
int _start(void)
{
	/* gcc doesn't let us disable prologue/epilogue, so we have to fudge it.
	 * We assume rsp is saved into rbp in the prologue. */
	register unsigned char *bp_after_main_prologue;
	__asm__ ("movq %%rbp, %0\n" : "=r"(bp_after_main_prologue));
	
	int argc;
	char **argv;
	rsp_on_entry = bp_after_main_prologue + BP_TO_SP_FIXUP;
	preinit(rsp_on_entry, &argc, &argv); // get us a sane environment
	
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
