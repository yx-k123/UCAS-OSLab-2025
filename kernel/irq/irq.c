#include "sys/syscall.h"
#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>
#include <os/smp.h>
#include <os/mm.h>
#include <plic.h>
#include <e1000.h>
#include <os/net.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    // cpu_id = get_current_cpu_id();
    if (scause & (1UL<<63)) {
        uint64_t irq = scause & 0xFFF;
        if (irq < IRQC_COUNT && irq_table[irq])
            irq_table[irq](regs, stval, scause);
        else
            handle_other(regs, stval, scause);
        return;
    }
    uint64_t exc = scause & 0xFFFF;
    if (exc < EXCC_COUNT && exc_table[exc])
        exc_table[exc](regs, stval, scause);
    else
        handle_other(regs, stval, scause);
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    bios_set_timer(get_ticks() + TIMER_INTERVAL);
    do_scheduler();
}

void handle_irq_ext(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p5-task4] external interrupt handler.
    // Note: plic_claim and plic_complete will be helpful ...
    // 获取当前挂起的中断源 ID
    uint32_t claim_id = plic_claim();
    // if (claim_id) printk("IRQ %d\n", claim_id);
    // 判断是否是 E1000 的中断
    if(claim_id == PLIC_E1000_PYNQ_IRQ || claim_id == PLIC_E1000_QEMU_IRQ){
        net_handle_irq();
    } else {
        handle_other(regs, stval, scause);
    }
    plic_complete(claim_id);
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    exc_table[EXCC_INST_MISALIGNED] = handle_other;     
    exc_table[EXCC_INST_ACCESS] = handle_other;   
    exc_table[EXCC_BREAKPOINT] = handle_other;       
    exc_table[EXCC_LOAD_ACCESS] = handle_other;      
    exc_table[EXCC_STORE_ACCESS] = handle_other;   
    exc_table[EXCC_SYSCALL] = handle_syscall;      
    exc_table[EXCC_INST_PAGE_FAULT] = handle_page_fault;    
    exc_table[EXCC_LOAD_PAGE_FAULT] = handle_page_fault;       
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_page_fault; 

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    irq_table[IRQC_U_SOFT] = handle_other;
    irq_table[IRQC_S_SOFT] = handle_other;
    irq_table[IRQC_M_SOFT] = handle_other;
    irq_table[IRQC_U_TIMER] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;
    irq_table[IRQC_M_TIMER] = handle_other;
    irq_table[IRQC_U_EXT] = handle_irq_ext;
    irq_table[IRQC_S_EXT] = handle_irq_ext;
    irq_table[IRQC_M_EXT] = handle_irq_ext;

    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    // setup_exception();
}

void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    uintptr_t fault_addr = stval;
    uintptr_t pgdir = current_running[get_current_cpu_id()]->pgdir;

    // DEBUG: Print fault info
    // printk("Page Fault: addr=0x%lx, scause=%lu, sepc=0x%lx\n", fault_addr, scause, regs->sepc);

    // 1. 获取对应虚拟地址的页表项 (PTE) 指针
    PTE *pte = get_pte(pgdir, fault_addr);

    if (pte != NULL && (*pte & _PAGE_PRESENT)) {
        printk("FATAL: Page Fault on present page! addr=0x%lx, pte=0x%lx, scause=%lu, sepc=0x%lx\n", 
               fault_addr, *pte, scause, regs->sepc);
        assert(0);
    }

    // 2. 判断是否是【换入 (Swap In)】情况
    // 条件：PTE 存在 + Valid 位是 0 + 内容不为 0 (说明存了磁盘 slot 号)
    if (pte != NULL && !(*pte & _PAGE_PRESENT) && (*pte != 0)) {
        // 执行换入逻辑：分配内存 -> 读盘 -> 恢复PTE -> 加入FIFO队列
        // printk("DEBUG: Page Fault Swap In: addr=0x%lx pte=0x%lx\n", fault_addr, *pte);
        swap_in(pte, fault_addr); 
    }
    // 3. 判断是否是【首次访问/按需分配 (Lazy Allocation)】情况
    // 条件：PTE 不存在 或者 内容全为 0
    else {
        // printk("DEBUG: Page Fault Lazy Alloc: addr=0x%lx\n", fault_addr);
        alloc_page_helper(fault_addr, pgdir);
    }

    // 4. 刷新 TLB
    local_flush_tlb_all(); 
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}
