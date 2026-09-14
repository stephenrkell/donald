#ifndef DONALD_CHAIN_H_
#define DONALD_CHAIN_H_

#ifndef CHAIN_LOADER
#error "Including chain.h but not building with -DCHAIN_LOADER"
#endif

#include "donald.h"

struct chain_load_ctxt
{
	struct load_ctxt *load;
	ElfW(Phdr) *program_phdrs;     // <-- only if CHAIN_LOADER...
	unsigned program_phentsize;    // ditto
	unsigned program_phnum;        // ditto
	ElfW(Phdr) *our_phdrs;         // ditto
	unsigned our_phentsize;        // ditto
	unsigned our_phnum;            // ditto <----------these only sometimes get filled in
	ElfW(Dyn) *our_dt_debug;       // ditto
	ElfW(Dyn) *program_dt_debug;   // ditto
	ElfW(Sym) *inferior_symtab;             // ditto  ... we use these for finding _r_debug in the inferior
	ElfW(Sym) *inferior_symtab_end;         // ditto
	const unsigned char *inferior_strtab;   // ditto
	ElfW(Sym) *inferior_r_debug_sym;        // ditto
};

#endif
