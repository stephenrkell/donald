#include <sys/types.h>
#include <unistd.h>
#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/memory.h>
#include <caml/custom.h>
#include <caml/callback.h>

#include "uint32.h"
#include "uint64.h"
#include "donald.h"



int main(int argc, char **argv)
{
	// call into ocaml 
	caml_main(argv);
}

CAMLprim value 
caml_enter(value entrypt_int64)
{
	unsigned long addr = Int64_val(entrypt_int64);
	enter((void*) addr);
}

/*
   external load_one: ((* base_addr : *) int64 * (* fd : *) int
               * (* vaddr : *) int64 * (* offset : *) int64
               * (* memsz : *) int64 * (* filesz : *) int64
               * (* read : *) bool * (* write : *) bool * (* exec : *) bool ) -> unit = "caml_load"
*/
CAMLprim value 
caml_load(value bigtuple)
{
	CAMLparam1(bigtuple);
	CAMLlocal5(base_addr, fd, vaddr, offset, memsz);
	CAMLlocal4(filesz, read, write, exec);

	base_addr = Field(bigtuple, 0);
	fd = Field(bigtuple, 1);
	vaddr = Field(bigtuple, 2);
	offset = Field(bigtuple, 3);
	memsz = Field(bigtuple, 4);
	filesz = Field(bigtuple, 5);
	read = Field(bigtuple, 6);
	write = Field(bigtuple, 7);
	exec = Field(bigtuple, 8);

	int ret = load_one_phdr(
		Uint64_val(base_addr),
		Int_val(fd),
		Uint64_val(vaddr),
		Uint64_val(offset),
		Uint64_val(memsz),
		Uint64_val(filesz),
		Bool_val(read),
		Bool_val(write),
		Bool_val(exec)
	);
	
	CAMLreturn(Val_int(ret));
}
