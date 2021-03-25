	.globl _start
_start:
	movq $60, %rax		# exit
    movq $0x0, %rdi
	syscall
