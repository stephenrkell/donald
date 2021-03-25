CFLAGS += -g

LD := ld.gold

ARCH := $(shell gcc -print-multiarch | sed 's/-.*//' )

ifeq ($(ARCH),i386)
LD_SO := /lib/ld-linux.so.2
CPPFLAGS += -DSYSCALL_RET_REGISTER=eax -DSYSCALL_ARG1_REGISTER=ebx -DSYSCALL_INSTRUCTION='int $$0x80'
else
ifeq ($(ARCH),x86_64)
LD_SO := /lib64/ld-linux-x86-64.so.2
CPPFLAGS += -DSYSCALL_RET_REGISTER=rax -DSYSCALL_ARG1_REGISTER=rdi -DSYSCALL_INSTRUCTION='syscall'
endif
endif

ifeq ($(LD_SO),)
$(error "Unrecognised architecture.")
endif

true: true.o
	$(LD) --eh-frame-hdr --hash-style=gnu \
    -dynamic-linker $(LD_SO) \
    --export-dynamic -no-pie "$<" \
    -Bdynamic --no-as-needed $(LD_SO) \
    -o $@

true.o: CPPFLAGS += -include sys/syscall.h

.PHONY: clean
clean:
	rm -f true true.o
