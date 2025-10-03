#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.data
msg_print_boot: .string "It's kouyixin's bootblock...\n\r"

.text
.global main

main:
	// fence on all memory and I/O
	fence

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
    la a0, msg_print_boot    # 字符串地址
    li a1, 0                 # IGNORE
    li a2, 0                 # IGNORE
    li a3, 0                 # IGNORE
    li a4, 0                 # IGNORE
    li a5, 0                 # IGNORE
    li a6, 0                 # IGNORE
    li a7, BIOS_PUTSTR       # 功能号
    la t0, bios_func_entry
    jalr t0

	// TODO: [p1-task2] call BIOS to read kernel in SD card
	la a0, kernel            # 目标内存地址
	la t1, os_size_loc
	lh a1, 0(t1)             # 读取OS大小
	li a2, 1                 # 起始块号
	li a3, 0                 # IGNORE
	li a4, 0                 # IGNORE
	li a5, 0                 # IGNORE
	li a6, 0                 # IGNORE
	li a7, BIOS_SDREAD       # 功能号
	la t0, bios_func_entry
	jalr t0

	// TODO: [p1-task4] load task-related arguments and pass them to kernel


	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	la t0, kernel
	jalr t0

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop


