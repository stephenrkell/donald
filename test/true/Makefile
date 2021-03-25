CFLAGS += -g

# HAVING TROUBLE making 'true' build on gcc-8 and binutils 2.31.1
# The following built:
# ld --eh-frame-hdr -m elf_x86_64 "--hash-style=gnu" -dynamic-linker /lib64/ld-linux-x86-64.so.2  -L/usr/lib/gcc/x86_64-linux-gnu/8 -L/usr/lib/gcc/x86_64-linux-gnu/8/../../../x86_64-linux-gnu -L/usr/lib/gcc/x86_64-linux-gnu/8/../../../../lib -L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/x86_64-linux-gnu -L/usr/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/8/../../.. --export-dynamic -no-pie true.o -Bdynamic --no-as-needed /lib64/ld-linux-x86-64.so.2 -o true
# ... but segfaults in ld.so.
# Turns out the PHDR and LOAD phdrs are borked.
# but ld.gold works correctly.
LD := ld.gold

# true is the smallest dynamically linked executable we can build
#true: true.s
#	$(CC) -\#\#\# -Wl,--export-dynamic -no-pie -fno-lto -Wl,-no-pie -o "$@" -nostartfiles -nostdlib "$<" -Bdynamic -Wl,--no-as-needed /lib64/ld-linux-x86-64.so.2

true: true.o
	$(LD) --eh-frame-hdr -m elf_x86_64 --hash-style=gnu \
    -dynamic-linker  /lib64/ld-linux-x86-64.so.2 \
    --export-dynamic -no-pie "$<" \
    -Bdynamic --no-as-needed /lib64/ld-linux-x86-64.so.2 \
    -o $@

.PHONY: clean
clean:
	rm -f true
