.data
result_array: .space 800      

.text
.global main

main:
    la s0, result_array      
    li s1, 1                 
    li s2, 200               
    
main_loop:
    mv a0, s1               
    jal ra, is_prime         
    
    slli t0, s1, 2   # int        
    add t0, s0, t0   # &array[i]      
    sw a0, 0(t0)           
    
    addi s1, s1, 1
    
    ble s1, s2, main_loop    

    li a7, 93                
    li a0, 0                 
    ecall

is_prime:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    sw s2, 0(sp)
    
    mv s0, a0   # s0 = p               
    
    li a0, 0                 
    
    li t0, 1
    ble s0, t0, is_prime_end
    
    li t0, 2
    beq s0, t0, is_prime_true

    andi t0, s0, 1   #  p % 2 ?= 0      
    beqz t0, is_prime_end    
    
    li s1, 3                 
    
    mv s2, s0              
    
is_prime_loop:
    mul t0, s1, s1    # \sqrt(p)       
    bgt t0, s0, is_prime_true 
    
    rem t1, s0, s1    # s0 % s1        
    beqz t1, is_prime_end   
    
    addi s1, s1, 2          
    
    j is_prime_loop

is_prime_true:
    li a0, 1                  

is_prime_end:
    lw ra, 12(sp)
    lw s0, 8(sp)
    lw s1, 4(sp)
    lw s2, 0(sp)
    addi sp, sp, 16
    
    ret






    