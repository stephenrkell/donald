CFLAGS += -g

# true is the smallest dynamically linked executable we can build
true: true.s
	$(CC) -Wl,--export-dynamic -o "$@" -nostartfiles -nostdlib -Bdynamic "$<" /lib64/ld-linux-x86-64.so.2

.PHONY: clean
clean:
	rm -f true
