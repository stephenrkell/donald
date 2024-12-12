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
#include <link.h>
#include "donald.h"

/* The purpose of this code is to set the *executable's* DT_DEBUG entry
 * to point to the *inferior ld.so*'s struct r_debug. It only makes sense
 * when there is an inferior, i.e. when we are chain-loading. */
#ifdef CHAIN_LOADER
__attribute__((visibility("hidden")))
ElfW(Dyn) *find_or_create_dt_debug(uintptr_t inferior_load_addr, uintptr_t inferior_dynamic_vaddr,
	size_t our_dynamic_size, uintptr_t inferior_r_debug_vaddr)
{
	/* This used to say:
	 * PROBLEM: can't use _DYNAMIC because there is no way to
	 * --export-dynamic it. Instead we use PT_DYNAMIC.
	 * BUT shouldn't the linker let us reference our own _DYNAMIC at link time?
	 * This seems to work. */
	ElfW(Dyn) *d = &_DYNAMIC[0];
	// seek forwards until we see the null terminator OR existing DT_DEBUG
	for (; (uintptr_t) d - (uintptr_t) &_DYNAMIC[0] < our_dynamic_size && d->d_tag != DT_NULL
			&& d->d_tag != DT_DEBUG; ++d);
	// do we have spare space?
	if ((intptr_t) d + sizeof (ElfW(Dyn)) - (intptr_t) &_DYNAMIC[0] >= our_dynamic_size)
	{
		// no space!
		return NULL;
	}
	else if (d->d_tag == DT_NULL)
	{
		/* Need to create the DT_DEBUG */
		*d = (ElfW(Dyn)) { .d_tag = DT_DEBUG };
		/* Ensure _DYNAMIC still has a terminator. */
		*(d+1) = (ElfW(Dyn)) { .d_tag = DT_NULL, .d_un = { d_ptr: 0x0 } };
	}
	assert(d->d_tag == DT_DEBUG);
	struct r_debug *r = (struct r_debug *)(inferior_load_addr + inferior_r_debug_vaddr);
	// make *our* _DYNAMIC point to the *inferior*'s _r_debug
	d->d_un.d_ptr = (uintptr_t) r;
	return d;
}

// other client code may want this
__attribute__((visibility("hidden")))
struct link_map fake_ld_so_link_map;

__attribute__((visibility("hidden")))
void populate_dt_debug(ElfW(Dyn) *d, uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr)
{
	struct r_debug *r = (struct r_debug *)(inferior_load_addr + inferior_r_debug_vaddr);
	/* What about the contents of the inferior's r_debug? To enable
	 * debugging early in the ld.so, we could create a fake entry. Let's try it. */
	fake_ld_so_link_map = (struct link_map) {
		.l_addr = 0 /* inferior_load_addr */,
		.l_name = SYSTEM_LDSO_PATH,
		.l_ld = 0 /* (ElfW(Dyn) *) inferior_dynamic_vaddr */,
		.l_prev = NULL,
		.l_next = NULL
	};
	extern void _dl_debug_state(void);
	fake_ld_so_link_map.l_addr = inferior_load_addr;
	fake_ld_so_link_map.l_ld = (ElfW(Dyn) *) (inferior_load_addr + inferior_dynamic_vaddr);
	// FIXME: this causes glibc's ld.so to jump to (void*)-1... understand why
#if 0
	*r = (struct r_debug) {
		.r_version = 1,
		.r_map = &fake_ld_so_link_map,
		.r_brk = (uintptr_t) _dl_debug_state,
		.r_state = RT_CONSISTENT,
		.r_ldbase = inferior_load_addr
	};
#endif
	_dl_debug_state(); // trigger the attached debugger, if any
//#endif
}
#endif
