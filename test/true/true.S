	.globl _start
_start:
    mov $SYS_exit, %SYSCALL_RET_REGISTER		# exit
    mov $0x0, %SYSCALL_ARG1_REGISTER
    SYSCALL_INSTRUCTION
