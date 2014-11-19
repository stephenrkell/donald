default: donald.so 

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

# OCaml stuff follows
OCAMLFIND ?= ocamlfind
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

OCAMLCFLAGS += -g
# translate OCAMLPATH into -I options
OCAMLCFLAGS += $(patsubst %,-I %,$(subst :, ,$(OCAMLPATH)))
#OCAMLCFLAGS += -package batteries 
#OCAMLCFLAGS += -package bitstring,bitstring.syntax -syntax bitstring.syntax 
OCAMLCFLAGS += -pp 'env CAML_LD_LIBRARY_PATH=$(BITSTRING) camlp4o $(CAMLP4FLAGS)'
OCAMLCFLAGS += -I $(ELF_LEM)/ocaml -I $(ELF_LEM) -I .
#$(warning OCAMLCFLAGS are $(OCAMLCFLAGS))
# OCAMLLDFLAGS += -linkpkg

# HACK: get the right libdir for ocaml
OCAML_LIBDIR ?= $(shell dirname `which ocamlc`)/../lib/ocaml

# We have to ocamlopt everything from source
vpath %.ml $(ELF_LEM)
vpath %.ml $(UINT)
vpath %.ml $(BITSTRING)

OCAML_SRC := $(filter-out sail_interface.ml,$(filter-out pre_main.ml,$(notdir $(wildcard $(ELF_LEM)/*.ml)))) $(wildcard *.ml)
$(warning OCAML_SRC is $(OCAML_SRC))

OCAML_CMXS := $(patsubst %.ml,%.cmx,$(OCAML_SRC))
OCAML_OBJS := $(patsubst %.cmx,%.o,$(OCAML_CMXS))
# $(warning OCAML_CMXS is $(OCAML_CMXS))
OCAML_DEPS := $(patsubst %.ml,.%.d,$(OCAML_SRC))
DEPS += $(OCAML_DEPS)

$(OCAML_DEPS): .%.d: %.ml
	$(OCAMLDEP) -native $(filter-out -g,$(OCAMLCFLAGS)) "$<" > "$@"
#$(OCAML_DEPS): .%.d: %.ml
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
	$(OCAMLOPT) -c -o "$@" $(OCAMLCFLAGS) $< $(OCAMLLDFLAGS) $(OCAMLLIBS)
%.o: %.cmx
	$(OCAMLOPT) -c -o "$@" $(OCAMLCFLAGS) $< $(OCAMLLDFLAGS) $(OCAMLLIBS)
%.cmo: %.ml
	$(OCAMLC) -c -o "$@" $< $(OCAMLCFLAGS) $(OCAMLLDFLAGS) $(OCAMLLIBS)

# FIXME: localize all global symbols except maybe _start?
donald-ml.so: donald.lds $(OCAML_OBJS) premain.o externals.o entry.o load.o
	USE_LD=ld.bfd $(CC) $(CFLAGS) -Wl,-Bsymbolic -nostdlib -nostartfiles -shared -o "$@" \
            $(filter-out %.lds,$+) \
            -Wl,-Bstatic $(LIBC_ARCHIVE) -L$(OCAML_LIBDIR) $(OCAML_LIBASMRUN_ARCHIVE) -Wl,-Bsymbolic \
            -T $(filter %.lds,$+) #-Wl,-soname=ld-linux.so.2 
