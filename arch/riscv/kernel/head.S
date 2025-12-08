/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000 // + 0xffffffc0_00000000
#define SECOND_KERNEL_STACK	0x50600000 // + 0xffffffc0_00000000

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  csrr t0, CSR_MHARTID
  bnez t0, second_start

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */
  la t0, __bss_start
  la t1, __BSS_END__
  bge t0, t1, set_stack  
clear_bss:
  sw zero, 0(t0)
  addi t0, t0, 4
  blt t0, t1, clear_bss

  /* TODO: [p1-task2] setup C environment */
set_stack:
  la sp, KERNEL_STACK
  li t0, 0xffffffc0
  slli t0, t0, 32
  add sp, sp, t0
  la tp, pid0_pcb

step_main:
  jal ra, main

second_start:
  la sp, SECOND_KERNEL_STACK
  li t0, 0xffffffc0
  slli t0, t0, 32
  add sp, sp, t0
  la tp, s_pid0_pcb
  call main

loop:
  wfi
  j loop

END(_start)
