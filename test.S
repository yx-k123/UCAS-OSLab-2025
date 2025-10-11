.data
result_array: .space 800

.bss
.align 16
stack_mem:
    .space 8192                # 8KB 栈
stack_top:

.text
.global main

main:
    la sp, stack_top          # 初始化栈指针

    la s0, result_array
    li s1, 1
    li s2, 200

main_loop:
    mv a0, s1
    jal ra, is_prime

    slli t0, s1, 2
    add t0, s0, t0
    sw a0, 0(t0)

    addi s1, s1, 1
    ble s1, s2, main_loop

    li a7, 93
    li a0, 0
    ecall

is_prime:
    addi sp, sp, -32
    sd ra, 24(sp)
    sd s0, 16(sp)
    sd s1, 8(sp)
    sd s2, 0(sp)

    mv s0, a0
    li a0, 0

    li t0, 1
    ble s0, t0, is_prime_end

    li t0, 2
    beq s0, t0, is_prime_true

    andi t0, s0, 1
    beqz t0, is_prime_end

    li s1, 3
    mv s2, s0

is_prime_loop:
    mul t0, s1, s1
    bgt t0, s0, is_prime_true

    rem t1, s0, s1
    beqz t1, is_prime_end

    addi s1, s1, 2
    j is_prime_loop

is_prime_true:
    li a0, 1

is_prime_end:
    ld ra, 24(sp)
    ld s0, 16(sp)
    ld s1, 8(sp)
    ld s2, 0(sp)
    addi sp, sp, 32
    ret






