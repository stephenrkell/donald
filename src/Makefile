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
DEPS := $(patsubst %.o,.%.d,$(OBJS))

$(DEPS): .%.d: %.c
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
	rm -f donald.lds donald.so *.o $(DEPS)
