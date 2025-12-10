#include "os/list.h"
#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>
#include <os/smp.h>
#include <pgtable.h>

#define VERSION_BUF 50
#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)
#define APPINFO_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 6)
#define TASKNUM_LOC (BOOT_LOADER_SIG_OFFSET - 8)
#define BATCH_OFFSET_LOC 0x1f0
#define BATCH_AREA_SIZE  512   // 保证为 SECTOR_SIZE 的整数倍且镜像中已预留

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];
extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];
int task_num = 0;

volatile int cpu1_ready = 0; 

static void cancel_mapping()
{
    uint64_t va;
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);
    // 遍历 0x50000000 到 0x51000000 的范围
    for (va = 0x50000000lu; va < 0x51000000lu; va += 0x200000lu) {
        uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
        uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
        
        // 检查一级页表项是否存在
        if (pgdir[vpn2] & _PAGE_PRESENT) {
            // 获取二级页表地址
            PTE *pmd = (PTE *)pa2kva(get_pa(pgdir[vpn2]));
            // 清除二级页表项 (2MB 大页)
            pmd[vpn1] = 0;
        }
    }
    // 刷新 TLB
    local_flush_tlb_all();
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (volatile long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (volatile long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (volatile long (*)())port_read_ch;
    jmptab[SD_READ]         = (volatile long (*)())sd_read;
    jmptab[SD_WRITE]        = (volatile long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (volatile long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (volatile long (*)())set_timer;
    jmptab[READ_FDT]        = (volatile long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (volatile long (*)())screen_move_cursor;
    jmptab[PRINT]           = (volatile long (*)())printk;
    jmptab[YIELD]           = (volatile long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (volatile long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (volatile long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (volatile long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.
    jmptab[REFLUSH]         = (volatile long (*)())screen_reflush;

}

static void init_task_info(void)
{
    uint8_t bootsec[SECTOR_SIZE];
    if (bios_sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }

    uint16_t os_size = 0;
    uint32_t appinfo_off = 0;
    uint16_t tasknum = 0;

    memcpy((uint8_t *)&os_size, bootsec + OS_SIZE_LOC, sizeof(os_size));
    memcpy((uint8_t *)&appinfo_off, bootsec + APPINFO_SIZE_LOC, sizeof(appinfo_off));
    memcpy((uint8_t *)&tasknum, bootsec + TASKNUM_LOC, sizeof(tasknum));

    if (tasknum > TASK_MAXNUM)
        tasknum = TASK_MAXNUM;

    if (appinfo_off == 0)
        return;

    int read_bytes = tasknum * (int)sizeof(task_info_t);
    if (read_bytes <= 0)
        return;

    unsigned head_off   = (unsigned)(appinfo_off % SECTOR_SIZE);
    unsigned start_lba  = (unsigned)(appinfo_off / SECTOR_SIZE);
    unsigned total_bytes = head_off + (unsigned)read_bytes;
    unsigned nsec       = NBYTES2SEC(total_bytes);

    uint8_t tmpbuf[2 * SECTOR_SIZE + TASK_MAXNUM * sizeof(task_info_t)];
    if (bios_sd_read((unsigned)(uintptr_t)tmpbuf, nsec, start_lba) < 0) {
        bios_putstr("sd_read app-info failed\n\r");
        return;
    }
    memcpy((uint8_t *)tasks, tmpbuf + head_off, (size_t)read_bytes);
}

static void print_task_names(void)
{
    bios_putstr("Available tasks:\n\r");
    for (int i = 0; i < 8; ++i)
    {
        if (tasks[i].name[0] != '\0') 
        {
            bios_putstr(" - ");
            bios_putstr(tasks[i].name);
            bios_putstr("\n\r");
        }
    }
}

static int task_exists(const char *name) {
    for (int i = 0; i < TASK_MAXNUM; ++i) {
        if (tasks[i].name[0] == '\0') break;
        if (!strcmp(tasks[i].name, name)) return 1;
    }
    return 0;
}

/************************************************************/
void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb, int argc, char **argv)
{
     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    pt_regs->regs[1] = entry_point; // repc
    pt_regs->regs[2] = user_stack;  // sp
    pt_regs->regs[4] = (reg_t)pcb;         // tp
    pt_regs->sepc = entry_point;
    pt_regs->sstatus = SR_SPIE; 
    pt_regs->regs[10] = argc;
    pt_regs->regs[11] = (reg_t)argv;

    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pt_switchto->regs[0] = (ptr_t)ret_from_exception; // ra
    pt_switchto->regs[1] = kernel_stack;
    pcb->kernel_sp = (ptr_t)pt_switchto;
    pcb->user_sp = user_stack;
}

static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */

    pid0_pcb.pid = 0;
    pid0_pcb.user_sp = (ptr_t)pid0_stack;
    pid0_pcb.kernel_sp = (ptr_t)pid0_stack;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.cursor_x = 0;
    pid0_pcb.cursor_y = 0;
    pid0_pcb.pgdir = pa2kva(PGDIR_PA);

    s_pid0_pcb.pid = 0;
    s_pid0_pcb.user_sp = (ptr_t)s_pid0_stack;
    s_pid0_pcb.kernel_sp = (ptr_t)s_pid0_stack;
    s_pid0_pcb.status = TASK_RUNNING;
    s_pid0_pcb.cursor_x = 0;
    s_pid0_pcb.cursor_y = 0;
    s_pid0_pcb.pgdir = pa2kva(PGDIR_PA);

    for (int i = 0; i < TASK_MAXNUM; i++) {
        pcb[i].status = TASK_EXITED;
    }

    /* TODO: [p2-task1] remember to initialize 'current_running' */
    current_running[0] = &pid0_pcb;
    current_running[1] = &s_pid0_pcb;
}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_SLEEP] = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD] = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE] = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR] = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH] = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK] = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT] = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_GETPID]         = (long (*)())do_getpid;
    syscall[SYSCALL_KILL]           = (long (*)())do_kill;
    syscall[SYSCALL_PS]             = (long (*)())do_process_show;
    syscall[SYSCALL_WAITPID]        = (long (*)())do_waitpid;
    syscall[SYSCALL_EXEC]           = (long (*)())do_exec;
    syscall[SYSCALL_EXIT]           = (long (*)())do_exit;
    syscall[SYSCALL_READCH]         = (long (*)())bios_getchar;
    syscall[SYSCALL_CLEAR]          = (long (*)())screen_clear;
    syscall[SYSCALL_BARR_INIT]      = (long (*)())do_barrier_init;
    syscall[SYSCALL_BARR_WAIT]      = (long (*)())do_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY]   = (long (*)())do_barrier_destroy;

    syscall[SYSCALL_COND_INIT]        = (long (*)())do_condition_init;
    syscall[SYSCALL_COND_WAIT]        = (long (*)())do_condition_wait;
    syscall[SYSCALL_COND_SIGNAL]      = (long (*)())do_condition_signal;
    syscall[SYSCALL_COND_BROADCAST]   = (long (*)())do_condition_broadcast;
    syscall[SYSCALL_COND_DESTROY]     = (long (*)())do_condition_destroy;

    syscall[SYSCALL_MBOX_OPEN]    = (long (*)())do_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE]   = (long (*)())do_mbox_close;
    syscall[SYSCALL_MBOX_SEND]    = (long (*)())do_mbox_send;
    syscall[SYSCALL_MBOX_RECV]    = (long (*)())do_mbox_recv;
}
/************************************************************/

/*
 * Once a CPU core calls this function,
 * it will stop executing!
 */
static void kernel_brake(void)
{
    disable_interrupt();
    while (1)
        __asm__ volatile("wfi");
}

int main(void)
{   
    int curr_cpu_id = get_current_cpu_id();
    if (curr_cpu_id == 0) {
        smp_init();
        lock_kernel();

        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();

        // Init task information (〃'▽'〃)
        init_task_info();

        print_task_names();

        // Output 'Hello OS!', bss check result and OS version
        char output_str[] = "bss check: _ version: _\n\r";
        char output_val[2] = {0};
        int i, output_val_pos = 0;
        // Init Process Control Blocks |•'-'•) ✧
        init_pcb();
        printk("> [INIT] PCB initialization succeeded.\n");

        // Read CPU frequency (｡•ᴗ-)_
        time_base = bios_read_fdt(TIMEBASE);

        // Init lock mechanism o(´^｀)o
        init_locks();
        printk("> [INIT] Lock mechanism initialization succeeded.\n");

        init_barriers();
        printk("> [INIT] Barrier initialization succeeded.\n");

        init_conditions();
        printk("> [INIT] Condition variable initialization succeeded.\n");

        init_mbox();
        printk("> [INIT] Mailbox initialization succeeded.\n");

        // Init interrupt (^_^)
        init_exception();
        printk("> [INIT] Interrupt processing initialization succeeded.\n");

        // Init system call table (0_0)
        init_syscall();
        printk("> [INIT] System call initialized successfully.\n");

        // Init screen (QAQ)
        init_screen();
        printk("> [INIT] SCREEN initialization succeeded.\n");
        // printk("> [INIT] Timer interrupt initialized successfully.\n");

        // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
        //   and then execute them.

        // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
        unlock_kernel();
        wakeup_other_hart();
        while (!cpu1_ready);
        lock_kernel();
        cancel_mapping();
        // cpu_id = 0;
        current_running[curr_cpu_id]->status = TASK_RUNNING;
    } else {
        cpu1_ready = 1;
        lock_kernel();
        // cpu_id = 1;
        current_running[curr_cpu_id]->status = TASK_RUNNING;
    }

    setup_exception();

    /*
     * Just start kernel with VM and print this string
     * in the first part of task 1 of project 4.
     * NOTE: if you use SMP, then every CPU core should call
     *  `kernel_brake()` to stop executing!
     */
    printk("> [INIT] CPU #%u has entered kernel with VM!\n", (unsigned int)get_current_cpu_id());
    // TODO: [p4-task1 cont.] remove the brake and continue to start user processes.
    // kernel_brake();

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    bios_set_timer(get_ticks() + TIMER_INTERVAL);

    if(curr_cpu_id == 0)
        printk("> [INIT] CPU 0 initialization succeeded.\n");
    else 
        printk("> [INIT] CPU 1 initialization succeeded.\n");

    unlock_kernel();

    if (get_current_cpu_id() == 0) {
        do_exec("shell", 0, NULL);
    }

    while (1)
    {   
        enable_preempt();
        asm volatile("wfi");
    }
    return 0;
}
