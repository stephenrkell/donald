#include <stdio.h>
#include <stdint.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include "donald.h"

extern int _DYNAMIC;        // defined for us by the linker
extern int _begin HIDDEN;   // defined by our hacked linker script (in Makefile)

static inline void __attribute__((always_inline)) bootstrap_relocate(unsigned char *at_base);

static inline void __attribute__((always_inline)) preinit(unsigned char *sp_on_entry)
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
	p_argc = (int *) p;
	uintptr_t argc = *p++;
	char **address_of_argv0 = (char**)p++;
	argv = address_of_argv0; 
	while (*p++);
	char **address_of_envp0 = (char**)p++;
	envp = address_of_envp0;
	while (*p++);
	void **address_of_auxv0 = (void**)p;
	p_auxv = (void*) address_of_auxv0;

	unsigned char *base_addr = (unsigned char *) &_begin;
	bootstrap_relocate(base_addr);
}

static inline void __attribute__((always_inline)) bootstrap_relocate(unsigned char *at_base)
{
	/* We scan _DYNAMIC to get our own symbol table. HACK: we manually relocate &_DYNAMIC
	 * by our load address to get its actual address. */
	ElfW(Dyn) *p_dyn = (void*)(at_base + (uintptr_t) &_DYNAMIC);
	ElfW(Sym) *dynsym_start = NULL;
	unsigned long dynsym_nsyms = 0;
	ElfW(Rela) *rela_dyn_start = NULL;
	unsigned long rela_dyn_sz = 0;
	unsigned long rela_dyn_nents = 0;
	while (p_dyn->d_tag != DT_NULL)
	{
		if (p_dyn->d_tag == DT_SYMTAB) dynsym_start = (void*)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_SYMENT) dynsym_nsyms = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELA) rela_dyn_start = (void *)(at_base + p_dyn->d_un.d_ptr);
		else if (p_dyn->d_tag == DT_RELASZ) rela_dyn_sz = p_dyn->d_un.d_val;
		else if (p_dyn->d_tag == DT_RELAENT) rela_dyn_nents = p_dyn->d_un.d_val;
		++p_dyn;
	}
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
		ElfW(Rela) *p_rela = rela_dyn_start + i;
		if (ELF64_R_TYPE(p_rela->r_info) == R_X86_64_RELATIVE)
		{
			// r_offset is the virtual address of the reloc site, in the library's vaddr space
			Elf64_Addr *reloc_addr = (Elf64_Addr *)(at_base + p_rela->r_offset);
			*reloc_addr = (Elf64_Addr)(at_base + p_rela->r_addend); // no symbol addr, because we're RELATIVE
		}
	}
}

/* The function prologue pushes rbp on entry, decrementing the stack
 * pointer by 8. Then it saves rsp into rbp. So by the time we see rbp, 
 * it holds the entry stack pointer *minus 8 bytes*. */
#define BP_TO_SP_FIXUP 0x8

/* This isn't the usual "main"; it's the raw entry point of the application. 
 * We link with -nostartfiles. We then define our own main. */
int _start(void)
{
	/* gcc doesn't let us disable prologue/epilogue, so we have to fudge it.
	 * We assume rsp is saved into rbp in the prologue. */
	register unsigned char *bp_after_main_prologue;
	__asm__("movq %%rbp, %0\n" : "=r"(bp_after_main_prologue));
	
	preinit(bp_after_main_prologue + BP_TO_SP_FIXUP); // get us a sane environment
	
	printf("Hello from donald!\n");
	
	int ret = main();
	
	/* We're executing without startfile code, so returning 0 would not make sense. 
	 * Calling exit() brings a dependency on fini_array stuff that we need to avoid
	 * since it depends on the startup files. So just do the syscall directly. */
	syscall(SYS_exit, ret);
}
