default: donald.so

CFLAGS += -std=gnu99 -g -fPIC

# Thanks to Dan Williams for the recipes that my link commands were based on.
# http://www.cs.virginia.edu/~dww4s/articles/ld_linux.html

# Make sure you've got a libc.a file available when linking this!

GLIBC_BUILD ?= /usr/local/src/glibc-build
LIBC_ARCHIVE ?= -lc
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

donald.o: premain.o main.o
	ld -r -o "$@" $+ -L/usr/lib/x86_64-linux-gnu --as-needed $(LIBC_ARCHIVE)
	# FIXME: localize all global symbols except maybe _start

donald.so: donald.o donald.lds
	$(CC) $(CFLAGS) -Bsymbolic -nostdlib -nostartfiles -shared -o "$@" \
            "$<"  \
            -Wl,-Bstatic $(LIBC_ARCHIVE) -Wl,-Bsymbolic \
            -T donald.lds #-Wl,-soname=ld-linux.so.2 

donald.lds: $(shell which ld) $(THIS_MAKEFILE)
	$(CC)   -nostdlib -nostartfiles -shared \
      -Wl,--verbose 2>&1 |  \
        LC_ALL=C \
          sed -e '/^=========/,/^=========/!d;/^=========/d'    \
              -e 's/\. = .* + SIZEOF_HEADERS;/& _begin = . - SIZEOF_HEADERS;/' \
          > "$@"
#      -Wl,-z,combreloc -Wl,-z,relro -Wl,--hash-style=both -Wl,-z,defs \

clean:
	rm -f donald.lds donald.so *.o
