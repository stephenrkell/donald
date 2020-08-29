default: donald.so 

include ../config.mk
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

export OCAMLFIND := 
export OCAMLPATH := $(OCAML_PREFIX)/lib/ocaml:$(BATTERIES):$(BITSTRING):$(UINT):$(LEM_OCAML_LIB):$(OCAMLPATH)

CFLAGS += -std=gnu99 -g -fPIC
CFLAGS += -fno-stack-protector

# Thanks to Dan Williams for the recipes that my link commands were based on.
# http://www.cs.virginia.edu/~dww4s/articles/ld_linux.html

# Make sure you've got a libc.a file available when linking this!

# Also, you must use the BFD-based linker (ld.bfd) -- gold doesn't work
# (it won't generate the donald.lds linker script, though I'm not yet sure why).

GLIBC_BUILD ?= /usr/local/src/glibc-build
LIBC_ARCHIVE ?= -lc
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

# NOTE: the USE_LD var here is supported by a simple script which 
# should be installed as (or symlinke from) /usr/bin/ld, assuming you also have /usr/bin/ld.bfd
#
##!/bin/sh
# 
#exec /usr/bin/${USE_LD:-ld.bfd} "$@"

OBJS := premain.o main.o entry.o load.o
C_DEPS := $(patsubst %.o,.%.d,$(OBJS))
DEPS := $(C_DEPS)

FINDLIB_PREFIX ?= /usr/local

# OCaml stuff follows
OCAMLFIND ?= $(FINDLIB_PREFIX)/bin/ocamlfind
OCAMLDEP ?= $(OCAMLFIND) ocamldep.opt
OCAMLC ?= $(OCAMLFIND) ocamlc.opt
OCAMLOPT ?= $(OCAMLFIND) ocamlopt.opt
OCAMLLIB ?= /usr/lib/ocaml
OCAML_LIBASMRUN_ARCHIVE ?= $(OCAMLLIB)/libasmrun.a
OCAML_STDLIB_ARCHIVE ?= $(OCAMLLIB)/stdlib.a 

BITSTRING ?= /usr/lib/ocaml

CAMLP4FLAGS += -nolib
CAMLP4FLAGS += -I $(BITSTRING)
CAMLP4FLAGS += -parser o -parser op -printer p
CAMLP4FLAGS += unix.cma
CAMLP4FLAGS += bitstring.cma
CAMLP4FLAGS += bitstring_persistent.cma
CAMLP4FLAGS += pa_bitstring.cmo

OCAMLOPTFLAGS += -g -fPIC
# translate OCAMLPATH into -I options
OCAMLCFLAGS += $(patsubst %,-I %,$(subst :, ,$(OCAMLPATH)))
OCAMLCFLAGS += -pp 'env CAML_LD_LIBRARY_PATH=$(BITSTRING) camlp4o $(CAMLP4FLAGS)'
OCAMLCFLAGS += -I $(ELF_LEM)/ocaml -I $(ELF_LEM) -I .
#$(warning OCAMLOPTFLAGS are $(OCAMLOPTFLAGS))
#$(warning OCAMLCFLAGS are $(OCAMLCFLAGS))

# HACK: get the right libdir for ocaml
OCAML_LIBDIR ?= $(shell dirname `which ocamlc`)/../lib/ocaml

# We have to ocamlopt everything from source
vpath %.ml $(ELF_LEM)
vpath %.ml $(UINT)
vpath %.ml $(BITSTRING)

# We also link against the archives of these libraries
vpath %.a $(UINT)
vpath %.a $(BITSTRING)
vpath %.a $(BATTERIES)


OCAML_SRC := $(filter-out sail_interface.ml,$(filter-out pre_main.ml,$(notdir $(wildcard $(ELF_LEM)/*.ml)))) $(wildcard *.ml)
# $(warning OCAML_SRC is $(OCAML_SRC))

OCAML_CMX := $(patsubst %.ml,%.cmx,$(OCAML_SRC))
OCAML_OBJ := $(patsubst %.cmx,%.o,$(OCAML_CMX))
# $(warning OCAML_CMXS is $(OCAML_CMXS))
OCAML_DEP := $(patsubst %.ml,.%.d,$(OCAML_SRC))
DEPS += $(OCAML_DEP)

$(OCAML_DEP): .%.d: %.ml
	$(OCAMLDEP) -native $(filter-out -g,$(OCAMLCFLAGS)) "$<" > "$@"
#$(OCAML_DEP): .%.d: %.ml
#	$(OCAMLDEP) -native $(filter-out -g,$(OCAMLCFLAGS)) "$<" | sed 's^/[^ \t]*/^^' > "$@"

$(C_DEPS): .%.d: %.c
	$(CC) -MM $(CFLAGS) "$<" > "$@" || rm -f "$@"
	
-include $(DEPS)

# FIXME: localize all global symbols except maybe _start?
donald.so: donald.lds premain.o main.o entry.o load.o
	USE_LD=ld.bfd $(CC) $(CFLAGS) -Bsymbolic -nostdlib -nostartfiles -shared -o "$@" \
            $(filter-out %.lds,$+)  \
            -Wl,-Bstatic $(LIBC_ARCHIVE) -Wl,-Bsymbolic \
            -T $(filter %.lds,$+) #-Wl,-soname=ld-linux.so.2 

donald.lds: $(shell which ld) $(THIS_MAKEFILE)
	USE_LD=ld.bfd $(CC)   -nostdlib -nostartfiles -shared \
      -Wl,--verbose 2>&1 |  \
        LC_ALL=C \
          sed -e '/^=========/,/^=========/!d;/^=========/d'    \
              -e 's/\. = .* + SIZEOF_HEADERS;/& _begin = . - SIZEOF_HEADERS;/' \
          > "$@"
#      -Wl,-z,combreloc -Wl,-z,relro -Wl,--hash-style=both -Wl,-z,defs \

clean:
	rm -f donald.lds donald.so *.o $(DEPS) donald-ml.so *.cmi *.cmo *.cmx

# $(warning OCAMLPATH is $(OCAMLPATH))

# Bow to ocaml's stupid insistence on using its own ".cmx" object files
%.cmx: %.ml
	which ocamlopt.opt
	$(OCAMLOPT) -c -o "$@" $(OCAMLCFLAGS) $(OCAMLOPTFLAGS) $< 
%.o: %.cmx
	$(OCAMLOPT) -c -o "$@" $(OCAMLCFLAGS) $(OCAMLOPTFLAGS) $< 
%.cmo: %.ml
	$(OCAMLC) -c -o "$@" $< $(OCAMLCFLAGS)
	
# make archives for batteries, uint
#batteries.a: $(patsubst %.ml,%.o,$(wildcard $(BATTERIES)/*.ml))
#	ar r "$@" $+
#bitstring.a: $(patsubst %.ml,%.o,$(wildcard $(BITSTRING)/*.ml))
#	ar r "$@" $+
#uint.a: $(patsubst %.ml,%.o,$(wildcard $(BATTERIES)/*.ml))
#	ar r "$@" $+

OCAML_A := $(BATTERIES)/batteries.a $(BITSTRING)/bitstring.a $(UINT)/uint.a
OCAMLLIBS_CMXA := $(BATTERIES)/batteries.cmxa $(BITSTRING)/bitstring.cmxa $(UINT)/uint.cmxa

# If I pass it OCAML_OBJ, it hardly writes anything.
# If I pass it OCAML_CMXS, it complains about a load of missing stuff.
# I think I have to make it not complain
donald-ml.o: $(OCAML_CMX) $(wildcard $(LEM_OCAML_LIB)/*.cmx) $(OCAMLLIBS_CMXA)
	$(OCAMLOPT) -o "$@" -output-obj $+

# FIXME: localize all global symbols except maybe _start?
donald-ml.so: donald.lds $(OCAML_OBJS) premain.o ocaml-externals.o entry.o load.o donald-ml.o
	USE_LD=ld.bfd $(CC) $(CFLAGS) -Wl,-z,muldefs \
	    -Wl,-Bsymbolic -nostdlib -nostartfiles -shared -o "$@" \
            $(filter-out %.lds,$+) \
            -Wl,-Bstatic \
	    -L$(OCAML_LIBDIR) \
	    $(LIBC_ARCHIVE) \
	    -Wl,-Bsymbolic \
            -T $(filter %.lds,$+) #-Wl,-soname=ld-linux.so.2
	readelf -Ws "$@" | grep 'UND'
	

donald-startup.s: $(OCAML_CMX)
	$(OCAMLOPT) -dstartup -o donald-startup-dummy $(OCAML_CMX) || true
	mv $$( ls -1t camlstartup*.s | head -n1 ) "$@"
	rm -f donald-startup-dummy

dummy.ml:
	touch "$@"

donald-ml-hack.so.startup.s: donald.lds dummy.ml $(OCAML_OBJ) premain.o ocaml-externals.o entry.o load.o batteries.a bitstring.a uint.a libuint_stubs.a libbitstring.a
	USE_LD=ld.bfd  ocamlopt.opt dummy.ml -S -dstartup -ccopt -I/var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/uint/_build/lib//../../lib  -ccopt -fPIC  -ccopt -fno-stack-protector  -ccopt -Wl,-Bsymbolic  -ccopt -nostdlib  -ccopt -nostartfiles  -ccopt -shared  -o "donald-ml-hack.so"  bitstring_local.o bytestring.o default_printing.o elf_executable_file2.o elf_executable_file3.o elf_file1.o elf_header.o elf_interpreted_segment.o elf_linking_file2.o elf_linking_file3.o elf_program_header_table.o elf_relocation.o elf_section_header_table.o elf_symbol_table.o elf_types.o endianness.o error.o main_elf.o missing_pervasives.o ml_bindings.o ml_bindings_camlp4_sugared.o show.o string_table.o utility.o loading.o premain.o ocaml-externals.o entry.o load.o /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/batteries/_build/src/batteries.a /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/bitstring/bitstring.a /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/uint/_build/lib/uint.a /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/uint/_build/lib/libuint_stubs.a /var/local/stephen/work/devel/donald.hg/contrib/system-v-abi/src/src_elf_libraries/bitstring/libbitstring.a -ccopt -Wl,-Bstatic /usr/local/src/uClibc-0.9.33.2/lib/libc.a  -ccopt -L/usr/local/bin/../lib/ocaml  /var/local/stephen/work/devel/donald.hg/contrib/ocaml-4.01.0/install/lib/ocaml/libasmrun.a  -ccopt -Wl,-Bsymbolic  -ccopt -T -ccopt donald.lds; echo "Return value was $$?"

