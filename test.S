.data
result_addr: .word 0x50001000   

.text
.global main

main:
    la t0, result_addr       
    lw t1, 0(t0)              
    
    li t2, 0                 
    li t3, 1
    li t4, 50               
    
loop:
    add t2, t2, t3           
    addi t3, t3, 1          
    ble t3, t4, loop    

    sw t2, 0(t1)             

exit:
    li a7, 93                 
    li a0, 0                 
    ecall







    