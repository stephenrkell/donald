.globl _start
.text
.align 16
_start:
#if defined(__x86_64__)
     mov %rsp, %rdi      # "sp_on_entry [to _start]" is the argument to start_donald
     push %rsp           # Linux ABI gives us 16-byte-aligned %rsp, but C psABI needs 8 modulo 16
     jmp start_donald
#elif defined(__i386__)
     push %esp          # first arg
     call start_donald  # not 'jmp'; callee expects saved eip stack slot, to locate the argument we just pushed
#else
#error "Unrecognised architecture."
#endif
