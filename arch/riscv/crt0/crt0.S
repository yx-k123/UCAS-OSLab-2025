#include <asm.h>
#define KERNEL_STACK 0x50500000
#define SYS_EXIT 93

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
setup_c_runtime:
    la t0, __bss_start
    la t1, __BSS_END__
    bge t0, t1, setup_stack
clear_bss:
    sw zero, 0(t0)
    addi t0, t0, 4
    blt t0, t1, clear_bss
setup_stack:
    la sp, KERNEL_STACK

    /* TODO: [p1-task3] enter main function */
enter_main:
    call main

    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
finish_task:
    li a7, SYS_EXIT  
    ecall

    /************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
