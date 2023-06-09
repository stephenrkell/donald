#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <err.h>
#include <assert.h>
#include <string.h> /* for strcmp() */
#include <linux/auxvec.h>  /* For AT_xxx definitions */
#include "donald.h"

/* The purpose of this code is to set the *executable's* DT_DEBUG entry
 * to point to the *inferior ld.so*'s struct r_debug. */
__attribute__((visibility("hidden")))
ElfW(Dyn) *create_dt_debug(uintptr_t inferior_load_addr, uintptr_t inferior_dynamic_vaddr,
	size_t our_dynamic_size, uintptr_t inferior_r_debug_vaddr)
{
	/* This used to say:
	 * PROBLEM: can't use _DYNAMIC because there is no way to
	 * --export-dynamic it. Instead we use PT_DYNAMIC.
	 * BUT shouldn't the linker let us reference our own _DYNAMIC at link time?
	 * This seems to work. */
	ElfW(Dyn) *d = &_DYNAMIC[0];
	// seek forwards until we see the null terminator
	for (; (uintptr_t) d - (uintptr_t) &_DYNAMIC[0] < our_dynamic_size && d->d_tag != DT_NULL; ++d);
	// do we have spare space?
	if ((intptr_t) d + sizeof (ElfW(Dyn)) - (intptr_t) &_DYNAMIC[0] >= our_dynamic_size)
	{
		// no space!
		return NULL;
	}
	struct r_debug *r = (struct r_debug *)(inferior_load_addr + inferior_r_debug_vaddr);
	// make *our* _DYNAMIC point to the *inferior*'s _r_debug
	*d++ = (ElfW(Dyn)) { .d_tag = DT_DEBUG, .d_un = {
		/* First thought: let's use need the offset from *our*
		 * load address to the in-ld.so _r_debug, i.e.
		 *
		 * (1)  r-debug-ldso-addr - chain-loader-begin-addr
		 *
		 * .... a large-magnitude positive or negative number.
		 * When it gets ADJUST_DYN_INFO'd, it'll be
		 *
		 * (2)  r-debug-ldso-addr - chain-loader-begin-addr
		 *         + chain-loader-begin-addr
		 *
		 * i.e. exactly what we want.
		 *
		 * PROBLEM: we can't use (1) with RELF_MAYBE_ADJUST,
		 * because the large-magnitude number will be interpreted
		 * as not in need of relocation.
		 *
		 * We could hack RELF_MAYBE_ADJUST to narrow the "no
		 * relocation needed" window, so it's less likely to cover
		 * the large-magnitude number we end up with. How do we
		 * narrow it? We could only deem it already-relocated if
		 * it fits into a narrow [load-addr, limit-addr) range.
		 * That assumes we know the limit addr.
		 *
		 * Also, why does this work on 32-bit? Presumably, the
		 * large-magnitude number *is* interpreted as in need of
		 * relocation, because it happens to be less than the
		 * load address of the chain loader. Indeed it seems
		 * the chain loader gets an address in the f7a..... region,
		 * whereas the inferior is at 55556000. The subtraction
		 * gives roughly 0x5db56000, clearly less than f7a.....
		 *
		 * Here we are relying on a strange property: that
		 * when we add this value to our own load address,
		 * we get the structure within the inferior ld.so.
		 * PROBLEM: this will confuse RELF_MAYBE_ADJUST
		 * if it is a very high value (i.e. a negative offset),
		 * i.e. if we are loaded *higher* than the inferior.
		 * In turn, it means our own find_r_debug() logic,
		 * which uses RELF_MAYBE_ADJUST will omit to
		 * relocate it. So maybe... if the unsigned delta
		 * is bigger than our load address, don't relocate it?
		 * PROBLEM: what if it gets ADJUST_DYN_INFO'd? Seems not to.
		 * PROBLEM: this is still broken if we don't do the subtraction.
		 * RELF_MAYBE_ADJUST(this-value, chain-ld-so-load-address) is broken
		 * because if the inferior ld.so was loaded below us,
		 * it passes the 'should relocate' test, so gets a bogus
		 * offset added.
		 * It's also broken if we *do* do the subtraction, if the
		 * difference still looks like an address above our load address
		 * or (maybe) the ld.so's load address. That might happen if we
		 * are loaded low. */
		// in our _DYNAMIC, the r_debug must be an offset relative to
		// *our* base address, not the inferior's
		d_ptr: ((uintptr_t) r - (uintptr_t) &_begin)
	} };
	ElfW(Dyn) *dt_dyn = d-1;
	/* Ensure _DYNAMIC has a terminator. */
	*d = (ElfW(Dyn)) { .d_tag = DT_NULL, .d_un = { d_ptr: 0x0 } };
	return dt_dyn;
}
