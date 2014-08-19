#include <stdio.h>
#include "donald.h"

void __attribute__((noreturn)) enter(void *entry_point)
{
	fprintf(stderr, "donald: jumping to entry point %p\n", (void*) entry_point);
	fflush(stderr);
	__asm__("jmpq *%0\n" : : "r"(entry_point));
	__builtin_unreachable();
}
