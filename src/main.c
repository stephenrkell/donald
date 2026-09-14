#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <err.h>
#include <assert.h>
#include <string.h> /* for strcmp() */
#include <linux/auxvec.h>  /* For AT_xxx definitions */
#include "donald.h"

extern int _start(void);

// in main() we have another special way to die
#define die(s, ...) do { debug_printf(0, s , ##__VA_ARGS__); return -1; } while(0)

int main(int argc_, char **argv_)
{
	struct load_ctxt ctxt;
	bzero(&ctxt, sizeof ctxt);
	ctxt.argc = argc_;
	ctxt.argv = argv_;
	// were we invoked by name, or as a .interp?
	// use AT_ENTRY to find out: it's _start if we were invoked as a program,
	// otherwise it's the program's _start
	ctxt.entry = (uintptr_t) &_start;
	ctxt.we_are_the_program = 1; // may be overridden below on walking the auxv
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_ENTRY:
				if (p->a_un.a_val != (uintptr_t) &_start) ctxt.we_are_the_program = 0;
				// note that p->a_un.a_val should be a fully relocated address,
				// so there is no danger in cases where &_start, as a vaddr, is coincidentally
				// the same as &_start in the actual program
				ctxt.entry = p->a_un.a_val;
				break;
			default:
				break;
		}
	}
	ctxt.our_load_address = 0; // may be overridden below on walking the auxv
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_BASE:
				if (p->a_un.a_val == 0)
				{
					assert(ctxt.we_are_the_program);
					ctxt.our_load_address = (uintptr_t) &_begin & ~(page_size-1);
				}
				else
				{
					ctxt.our_load_address = p->a_un.a_val;
				}
				break;
			case AT_PHDR:
				ctxt.phdr_ent = p;
				break;
			case AT_PHNUM:
				ctxt.phnum_ent = p;
				break;
			default:
				break;
		}
	}

	debug_printf(1, "We think we are%sthe program\n", ctxt.we_are_the_program ? " " : " not ");

	ctxt.loadee_path = get_loadee_path(&ctxt);
	ctxt.loadee_base_addr_hint = get_loadee_base_addr_hint(&ctxt);

	struct loadee_info loadee = load_file(ctxt.loadee_path, ctxt.loadee_base_addr_hint,
			&ctxt.phdrs[0], &ctxt.n_phdrs);
	ctxt.loadee = &loadee;
	if (!ctxt.loadee->dynamic_vaddr) die("%s", ctxt.loadee->errmsg);

	// TODO: do relocations!

	/* grab the entry point of the loadee... note that if we are a
	 * chain loader, the picture looks like this:
	 *             us             inferior loader  executable
	               +---------+     +---------+     +---------+
	kernel loads-->|entry    |---->|entry    |---->|entry    |
	               +---------+     +---------+     +---------+
	 * and of the *three* entry points, 'entry' is always that of
	 * us or of the executable but never of the inferior loader. */
	ctxt.loadee_entry_point = ctxt.loadee->base_addr + ctxt.loadee->ehdr.e_entry;
#ifdef CHAIN_LOADER
	assert(ctxt.loadee_entry_point != ctxt.entry);
	// Q. What if we're being used to chain-load a statically linked executable?
	// A. That only works if we are the program, so 'entry' is ours, so the assertion holds.
#endif

	// jump to the entry point
	enter(&ctxt);
}
