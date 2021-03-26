#include <stdio.h>
#include "donald.h"

void __attribute__((noreturn)) enter(void *entry_point)
{
	fprintf(stderr, DONALD_NAME ": jumping to system ld.so entry point %p with rsp %p\n",
	(void*) entry_point, sp_on_entry);
	fflush(stderr);
	__asm__ volatile (
#if defined(__x86_64__)
	  "movq %0, %%rsp\n"
	  "xorq %%rbp, %%rbp\n" /* clear rbp to avoid confusing stack walkers */
#elif defined(__i386__)
	  "mov %0, %%esp\n"
	  "xor %%ebp, %%ebp\n" /* clear ebp to avoid confusing stack walkers */
#else
#error "Unrecognised architecture."
#endif
	  "jmp *%1\n" : : "m"(sp_on_entry), "r"(entry_point)
	);
	__builtin_unreachable();
}
