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
 * when there is an inferior, i.e. when we are chain-loading.
 *
 * NOTE that the approach in this file is not quite enough to work fully
 * in the "requested" case. Currently, the inferior ld.so's own path in the
 * link map will be wrong if we're the requested loader (it will point to
 * us! I think... and this *possibly* affects the _dl_debug_state() protocol
 * also). There is a solution in allocsld, but very specific to that project.
 * See allocsld/chain.c.
 *
 * XXX: even in the "invoked" case, what we have here seems to be not quite
 * enough. I have to use 'add-symbol-file' manually to get the inferior
 * ld.so's functions to show up in the backtrace. Prior to that, I don't have
 * an "_r_debug" symbol according to gdb, although the below code has set the
 * DT_DEBUG to point to the _r_debug in the ld.so... 'info shared' gives:
 *
	(gdb) info shared
	From                To                  Syms Read   Shared Object Library
	0x00007ffff58ae060  0x00007ffff58d2f51  No          /lib64/ld-linux-x86-64.so.2

 * After manually loading symbols, I do get _r_debug but it is unrelocated.
 
(gdb) print _r_debug
Cannot access memory at address 0x33118
(gdb) print &_r_debug
$2 = (struct r_debug *) 0x33118 <_r_debug>
(gdb) shell readelf -WS /lib64/ld-linux-x86-64.so.2 | grep _r_debug
(gdb) shell readelf -Ws /lib64/ld-linux-x86-64.so.2 | grep _r_debug
    16: 0000000000033118    40 OBJECT  GLOBAL DEFAULT   21 _r_debug@@GLIBC_2.2.5

 * ... is this normally ADJUST_DYN_INFO'd? Not that I can see.
 * The same unrelocatedness problem is evident for other data symbols in the ld.so,
 * although not for function symbols.
 * AHAH. I can fix this by manually loading the symbols appropriately, using the '-o'
 * option to add-symbol-file. I've updated my add-ld-so-symbol-file to match.
 * So the remaining problem is really why we get "Syms Read: No".
 *
 * libsystrap's example/chain.c notes:
 * /* In our loader's _DYNAMIC as we create it initially, the d_ptr is already
 *  * a pointer to the inferior ld.so's r_debug. It will never get ADJUST_DYN_INFO'd
 *  * by the ld.so because that ld.so does not know that our _DYNAMIC exists.
 *
 * ... i.e. the chain loader's DT_DEBUG is *populated on startup* to point to
 * the inferior ld.so's _r_debug, just like the executable's DT_DEBUG is by the
 * real ld.so). So let's check it really is set to that?
 *
 * (gdb) print _DYNAMIC[20]
$13 = {
  d_tag = 21,
  d_un = {
    d_val = 140737313112344,
    d_ptr = 140737313112344
  }
}
(gdb) print (void*) 140737313112344
$14 = (void *) 0x7ffff58e0118

 * Looks OK! Maybe the issue is just that we had interrupted the ld.so while
 * it was doing its work? Indeed it is still in the RT_ADD state.
 *
 * To test this thoroughly, there are many cases to consider.
 *
 *   {requested, invoked} x {start-under-gdb, attach-gdb} x {during-chain-load, during-ld-so-startup, during-program-execution}
 *
 * To test these, we could build
 *  -- a version of ourselves that stops itself just before running the executable
 *  -- ... and that loads the ld.so at a known address, easily breakpointed without symbols
 *  -- a simple binary that optionally stops itself on startup, 
 * A sane initial test would be that we could get a backtrace including the top frame (_dl_start?)
 */
#ifdef CHAIN_LOADER
// FIXME FIXME: what is this doing here? trap-syscalls has its own fakery...
// I suppose this is an attempt at a generic version that might work in other
// loaders built using donald. As far as I know, it never worked. Used to be mixed
// in with populate_dt_debug(), the old name for point_dt_debug_to_inferior_r_debug()...
__attribute__((visibility("hidden")))
void install_fake_singleton_inferior_link_map_in_r_debug(uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr)
{
		 	/* What about the contents of the inferior's r_debug? To enable
	 * debugging early in the ld.so, we could create a fake entry. Let's try it. */
	// FIXME: this causes glibc's ld.so to jump to (void*)-1... understand why
#if 0
	static struct link_map fake_ld_so_link_map;
	fake_ld_so_link_map = (struct link_map) {
		.l_addr = 0 /* inferior_load_addr */,
		.l_name = SYSTEM_LDSO_PATH,
		.l_ld = 0 /* (ElfW(Dyn) *) inferior_dynamic_vaddr */,
		.l_prev = NULL,
		.l_next = NULL
	};
	fake_ld_so_link_map.l_addr = inferior_load_addr;
	fake_ld_so_link_map.l_ld = (ElfW(Dyn) *) (inferior_load_addr + inferior_dynamic_vaddr);
	*r = (struct r_debug) {
		.r_version = 1,
		.r_map = &fake_ld_so_link_map,
		.r_brk = (uintptr_t) _dl_debug_state,
		.r_state = RT_CONSISTENT,
		.r_ldbase = inferior_load_addr
	};
#endif
}

__attribute__((visibility("hidden")))
void point_dt_debug_to_inferior_r_debug(ElfW(Dyn) *d, uintptr_t inferior_load_addr,
	uintptr_t inferior_dynamic_vaddr, uintptr_t inferior_r_debug_vaddr)
{
	struct r_debug *r = (struct r_debug *)(inferior_load_addr + inferior_r_debug_vaddr);
	d->d_un.d_ptr = (uintptr_t) r;
	// FIXME: should we really do this triggering?
	extern void _dl_debug_state(void) __attribute__((weak));
	if (_dl_debug_state) _dl_debug_state(); // trigger the attached debugger, if any
}
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
#define ADDR_IS_END_OF_DYNAMIC(d) \
    ((uintptr_t) (d) - (uintptr_t) &_DYNAMIC[0] >= our_dynamic_size)

	for (; !ADDR_IS_END_OF_DYNAMIC(d) && d->d_tag != DT_NULL && d->d_tag != DT_DEBUG; ++d);
	// do we have spare space?
	if (ADDR_IS_END_OF_DYNAMIC(d)) /* no space! */ return NULL;
	else if (d->d_tag == DT_NULL)
	{
		/* Need to create the DT_DEBUG */
		*d = (ElfW(Dyn)) { .d_tag = DT_DEBUG };
		/* Ensure _DYNAMIC still has a terminator. */
		*(d+1) = (ElfW(Dyn)) { .d_tag = DT_NULL, .d_un = { d_ptr: 0x0 } };
	}
	/* If we got here, we have a DT_DEBUG; we may or may not have just created it. */
	assert(d->d_tag == DT_DEBUG);
	// make *our* _DYNAMIC point to the *inferior*'s _r_debug
	point_dt_debug_to_inferior_r_debug(d, inferior_load_addr,
		inferior_dynamic_vaddr, inferior_r_debug_vaddr);
	return d;
}

#endif
