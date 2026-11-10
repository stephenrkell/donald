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

struct load_ctxt load_ctxt __attribute__((visibility("hidden")));

int main(int argc_, char **argv_)
{
	load_ctxt.argc = argc_;
	load_ctxt.argv = argv_;
	load_ctxt.argv0_at_startup = argv_[0];
	// were we invoked by name, or as a .interp?
	// use AT_ENTRY to find out: it's _start if we were invoked as a program,
	// otherwise it's the program's _start
	load_ctxt.entry = (uintptr_t) &_start;
	load_ctxt.we_are_the_program = 1; // may be overridden below on walking the auxv
	/* We need to do a sweep of auxv just to determine whether we_are_the_program,
	 * because the interpretation of other auxv content will depend on this. */
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_ENTRY:
				if (p->a_un.a_val != (uintptr_t) &_start) load_ctxt.we_are_the_program = 0;
				// note that p->a_un.a_val should be a fully relocated address,
				// so there is no danger in cases where &_start, as a vaddr, is coincidentally
				// the same as &_start in the actual program
				load_ctxt.entry = p->a_un.a_val;
				break;
			default:
				break;
		}
	}
	load_ctxt.our_load_address = 0; // may be overridden below on walking the auxv
	for (ElfW(auxv_t) *p = p_auxv; p->a_type; ++p)
	{
		switch (p->a_type)
		{
			case AT_EXECFN:
				load_ctxt.execfn = (char*) p->a_un.a_val;
				break;
			case AT_BASE: // "invoked" case: AT_BASE is null and we figure out our own base addr
				if (p->a_un.a_val == 0)
				{
					assert(load_ctxt.we_are_the_program);
					load_ctxt.our_load_address = (uintptr_t) &_begin & ~(page_size-1);
				}
				else // "requested" case -- AT_BASE gives the *interpreter* base address, i.e. ours
				{
					load_ctxt.our_load_address = p->a_un.a_val;
				}
				break;
			case AT_PHDR:
				load_ctxt.phdr_auxv_ent = p;
				if (load_ctxt.we_are_the_program) load_ctxt.our_phdrs = (ElfW(Phdr)*) p->a_un.a_val;
				break;
			case AT_PHNUM:
				load_ctxt.phnum_auxv_ent = p;
				if (load_ctxt.we_are_the_program) load_ctxt.our_nphdr = p->a_un.a_val;
				break;
			default:
				break;
		}
	}
	// TODO: if we are not the program, we did not snarf our own phdrs just now.
	// How can we get them?

	debug_printf(1, "We think we are%sthe program\n", load_ctxt.we_are_the_program ? " " : " not ");

	load_ctxt.loadee_path = get_loadee_path(&load_ctxt);
	load_ctxt.loadee_base_addr_hint = get_loadee_base_addr_hint(&load_ctxt);

	struct loadee_info loadee = load_file(load_ctxt.loadee_path, load_ctxt.loadee_base_addr_hint,
			&load_ctxt.phdrs[0], &load_ctxt.n_phdrs);
	load_ctxt.loadee = &loadee;
	if (!load_ctxt.loadee->dynamic_vaddr) die("%s", load_ctxt.loadee->errmsg);

	// TODO: do relocations!

	/* grab the entry point of the loadee... note that if we are a
	 * chain loader, the picture looks like this:
	 *             us             inferior loader  executable
	               +---------+     +---------+     +---------+
	kernel loads-->|entry    |---->|entry    |---->|entry    |
	               +---------+     +---------+     +---------+
	 * and of the *three* entry points, 'entry' is always that of
	 * us or of the executable but never of the inferior loader. */
	load_ctxt.loadee_entry_point = load_ctxt.loadee->base_addr + load_ctxt.loadee->ehdr.e_entry;
#ifdef CHAIN_LOADER
	assert(load_ctxt.loadee_entry_point != load_ctxt.entry);
	// Q. What if we're being used to chain-load a statically linked executable?
	// A. That only works if we are the program, so 'entry' is ours, so the assertion holds.
#endif

	// jump to the entry point
	enter(&load_ctxt);
}
