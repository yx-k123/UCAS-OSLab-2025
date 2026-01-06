#include <asm/biosdef.h>
#include <csr.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// app size location
.equ app_size_loc, 0x502001fa

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50202000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.data
msg_print_boot: .string "It's kouyixin's bootblock...\n\r"

.text
.global main

main:
	// fence on all memory and I/O
	fence
    // a0 is mhartid
	bnez a0, secondary

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
	lhu a1, 0(t1)             # 读取OS大小（字节数）

	# 将字节数转换为扇区数
	li t2, 512               # 每扇区字节数（SECTOR_SIZE）
	addi a1, a1, 511         # a1 = a1 + 511（向上取整）
	div a1, a1, t2           # a1 = a1 / 512（字节数转扇区数）

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
secondary:
	/* TODO [P3-task3]: 
	 * 1. Mask all interrupts
	 * 2. let stvec pointer to kernel_main
	 * 3. enable software interrupt for ipi
	 */
	
	// 1. Mask all interrupts
	li t0, 0
	csrw sie, t0
	// 2. let stvec pointer to kernel_main
	la t0, kernel
	csrw stvec, t0
	// 3. enable software interrupt for ipi
	li t0, SIE_SSIE
	csrs sie, t0		// sie
	li t0, SR_SIE
	csrs sstatus, t0	// sstatus

wait_for_wakeup:
	wfi
	j wait_for_wakeup

	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop


