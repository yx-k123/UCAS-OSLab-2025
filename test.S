.data
result_array: .space 800      

.text
.global main

main:
    # 初始化
    la s0, result_array      
    li s1, 1                 
    li s2, 200               
    
main_loop:
    # 调用质数判断函数
    mv a0, s1               
    jal ra, is_prime         
    
    # 存储结果
    slli t0, s1, 2           
    add t0, s0, t0         
    sw a0, 0(t0)           
    
    # 递增数字
    addi s1, s1, 1
    
    # 检查是否完成
    ble s1, s2, main_loop    
    # 程序结束
    li a7, 93                
    li a0, 0                 
    ecall

# 质数判断函数
# 参数：a0 - 要判断的数字
# 返回值：a0 - 1表示质数，0表示非质数
is_prime:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    sw s2, 0(sp)
    
    mv s0, a0                 # s0 = n
    
    li a0, 0                 
    
    # 如果 n <= 1，不是质数
    li t0, 1
    ble s0, t0, is_prime_end
    
    # 如果 n == 2，是质数
    li t0, 2
    beq s0, t0, is_prime_true
    
    # 如果 n 是偶数，不是质数
    andi t0, s0, 1            # 检查最低位
    beqz t0, is_prime_end     # 如果是偶数，不是质数
    
    # 检查从3到sqrt(n)的奇数因子
    li s1, 3                  # i = 3
    
    # 计算 sqrt(n) 的近似值 (i*i <= n)
    mv s2, s0                 # s2 = n
    
is_prime_loop:
    # 检查 i*i <= n
    mul t0, s1, s1            # t0 = i*i
    bgt t0, s0, is_prime_true # 如果 i*i > n，是质数
    
    # 检查 n 是否能被 i 整除
    rem t1, s0, s1            # t1 = n % i
    beqz t1, is_prime_end     # 如果余数为0，不是质数
    
    # 递增到下一个奇数
    addi s1, s1, 2            # i += 2
    
    j is_prime_loop

is_prime_true:
    li a0, 1                  # 返回1（是质数）

is_prime_end:
    # 恢复寄存器
    lw ra, 12(sp)
    lw s0, 8(sp)
    lw s1, 4(sp)
    lw s2, 0(sp)
    addi sp, sp, 16
    
    ret






    