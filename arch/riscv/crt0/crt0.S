#include <asm.h>
#define KERNEL_STACK 0x50500000
#define SYS_EXIT 93

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
    addi sp, sp, -16 
    sd s0, 8(sp)            // save s0 = frame pointer
    sd ra, (sp)             // save ra = return address
    addi s0, sp, 16         // set frame pointer

    /* clear .bss section */
    la t0, __bss_start         // bss start address
    la t1, __BSS_END__         // bss end address
do_clear:  
    sw zero, (t0)          // write 0 to address pointed by t0
    addi t0, t0, 4         // point to next word
    blt t0, t1, do_clear

    /* TODO: [p1-task3] enter main function */
enter_main:
    call main

    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
finish_task:
    ld ra, (sp)            // restore return address
    ld s0, 8(sp)           // restore frame pointer
    addi sp, sp, 16        // restore stack pointer
    jr ra                  // return to caller

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
