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
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    uint8_t bootsec[SECTOR_SIZE];
    if (bios_sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }

    int os_size = 0;
    int appinfo_size = 0;
    int tasknum = 0;

    memcpy((uint8_t *)&os_size, bootsec + OS_SIZE_LOC, sizeof(short));
    memcpy((uint8_t *)&appinfo_size, bootsec + APPINFO_SIZE_LOC, sizeof(int));
    memcpy((uint8_t *)&tasknum, bootsec + TASKNUM_LOC, sizeof(short));

    int appinfo_off = SECTOR_SIZE + os_size;
    int max_bytes   = TASK_MAXNUM * (int)sizeof(task_info_t);
    int read_bytes  = appinfo_size > max_bytes ? max_bytes : appinfo_size;

    read_bytes = (read_bytes / (int)sizeof(task_info_t)) * (int)sizeof(task_info_t);
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

static void batch_write(void)
{
    uint8_t bootsec[SECTOR_SIZE];
    if (bios_sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }

    int batch_off = 0;
    memcpy((uint8_t *)&batch_off, bootsec + BATCH_OFFSET_LOC, sizeof(int));
    if (batch_off <= 0) { 
        bios_putstr("no batch offset found\n\r"); 
        return; 
    }

    bios_putstr("Enter batch (task names, space separated): ");
    char line[256]; int len = 0;
    while (1) {
        char ch = bios_getchar();
        if (ch == '\r' || ch == '\n') { 
            line[len] = '\0'; 
            bios_putstr("\n\r"); 
            break; 
        }
        if (ch == 127 && len > 0) { 
            len--; 
            bios_putchar('\b'); 
            bios_putchar(' '); 
            bios_putchar('\b'); 
        }
        if (ch >= ' ' && ch <= '~' && len < (int)sizeof(line) - 1) { 
            line[len++] = ch; 
            bios_putchar(ch); 
        }
    }

    static char out[BATCH_AREA_SIZE];
    memset((uint8_t *)out, 0, sizeof(out));
    unsigned used = 0;

    const char *p = line; 
    char name[64];
    while (*p) {
        while (*p==' '||*p=='\t') ++p;
        if (!*p) break;
        int k = 0;
        while (*p && *p!=' ' && *p!='\t' && k < (int)sizeof(name)-1) name[k++] = *p++;
        name[k] = '\0';

        if (!task_exists(name)) {
            bios_putstr("batch-write: no such task: "); 
            bios_putstr(name); 
            bios_putstr("\n\r");
            return;
        }
        unsigned n = (unsigned)strlen(name);
        if (used + n + 1 >= sizeof(out)) { 
            bios_putstr("batch-write: too long\n\r"); 
            return; 
        }
        memcpy((uint8_t *)out + used, (const uint8_t *)name, n);
        used += n;
        out[used++] = ' ';
    }
    if (used && out[used-1]==' ') out[--used] = '\n';

    unsigned blk = (unsigned)(batch_off / SECTOR_SIZE);
    unsigned cnt = (unsigned)(BATCH_AREA_SIZE / SECTOR_SIZE);

    if (bios_sd_write((unsigned)(uintptr_t)out, cnt, blk) < 0) {
        bios_putstr("batch-write: bios_sd_write failed\n\r");
        return;
    }

    bios_putstr("batch written\n\r");
}

static void batch_run(void)
{
    uint8_t bootsec[SECTOR_SIZE];
    if (bios_sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }
    int batch_off = 0;
    memcpy((uint8_t *)&batch_off, bootsec + BATCH_OFFSET_LOC, sizeof(int));
    if (batch_off <= 0) { 
        bios_putstr("no batch offset found\n\r"); return; 
    }

    static char buf[BATCH_AREA_SIZE];
    memset((uint8_t *)buf, 0, sizeof(buf));
    unsigned blk = (unsigned)(batch_off / SECTOR_SIZE);
    unsigned cnt = (unsigned)(BATCH_AREA_SIZE / SECTOR_SIZE);
    if (bios_sd_read((unsigned)(uintptr_t)buf, cnt, blk) < 0) {
        bios_putstr("batch-run: sd_read failed\n\r");
        return;
    }

    char *p = buf;
    while (*p) {
        while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p;
        if (!*p) break;
        char *s = p;
        while (*p && *p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n') ++p;
        char c = *p; 
        *p = 0;

        if (!task_exists(s)) {
            bios_putstr("batch-run: no such task: "); 
            bios_putstr(s); 
            bios_putstr("\n\r");
            *p = c; 
            return;
        }
        bios_putstr("Run: "); 
        bios_putstr(s); 
        uint64_t entry = load_task_img(s);
        if (!entry) { 
            bios_putstr("load failed\n\r"); *p = c; return; 
        }
        ((void(*)(void))entry)();
        bios_putstr("\n\r");
        *p = c;
    }
    bios_putstr("batch done\n\r");
}


/************************************************************/
static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
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
    pt_regs->regs[4] = (uint64_t)pcb;         // tp
    pt_regs->sepc = entry_point;
    pt_regs->sstatus = SR_SPIE; 

    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pt_switchto->regs[0] = (uint64_t)ret_from_exception; // ra
    pt_switchto->regs[1] = kernel_stack;
    pcb->kernel_sp = (ptr_t)pt_switchto;
    pcb->user_sp = user_stack;
}

static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    char task_name[][32] = {
        // "print1",
        // "print2",
        // "lock1",
        // "lock2",
        // "sleep",
        // "timer",
        "fly1",
        "fly2",
        "fly3",
        "fly4",
        "fly5",
    };

    int task_idx = 0;
    pid0_pcb.pid = 0;
    pid0_pcb.user_sp = (ptr_t)pid0_stack;
    pid0_pcb.kernel_sp = (ptr_t)pid0_stack;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.cursor_x = 0;
    pid0_pcb.cursor_y = 0;

    pid0_pcb.flag_position = 0;
    pid0_pcb.time_slice = 1;
    pid0_pcb.time_slice_remain = 0;
    pid0_pcb.if_switch = 0;

    for (int i = 0; i < sizeof(task_name) / sizeof(task_name[0]); i++)
    {
        uint64_t entry = load_task_img(task_name[i]);
        if (!entry)
        {
            bios_putstr("> [INIT] Load task ");
            bios_putstr(task_name[i]);
            bios_putstr(" failed.\n");
            continue;
        }
        bios_putstr("> [INIT] Load task ");
        bios_putstr(task_name[i]);
        bios_putstr(" succeeded.\n");

        ptr_t user_stack = allocUserPage(1) + PAGE_SIZE;
        ptr_t kernel_stack = allocKernelPage(1) + PAGE_SIZE;

        pcb[i + 1].pid = i + 1;
        pcb[i + 1].status = TASK_READY;
        pcb[i + 1].cursor_x = 0;
        pcb[i + 1].cursor_y = 0;

        pcb[i + 1].flag_position = 10 * (i + 1);
        pcb[i + 1].time_slice = 1;
        pcb[i + 1].time_slice_remain = 0;
        pcb[i + 1].if_switch = 0;

        init_list_head(&pcb[i + 1].list);

        init_pcb_stack(kernel_stack, user_stack, entry, &pcb[i + 1]);
        list_add_tail(&pcb[i + 1].list, &ready_queue);
    }

    /* TODO: [p2-task1] remember to initialize 'current_running' */
    current_running = &pid0_pcb;
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
    syscall[SYSCALL_SET_SCHED_WORKLOAD] = (long (*)())do_set_sche_workload;
}
/************************************************************/

int main(void)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    // print_task_names();

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

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    bios_set_timer(get_ticks() + TIMER_INTERVAL);
    // printk("> [INIT] Timer interrupt initialized successfully.\n");

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {   
        // bios_putstr("\n\rEnter task name: "); 

        // char task_name[32];
        // int task_name_len = 0;

        // while (1) {
        //     char ch = bios_getchar();
        //     if (ch == '\r' || ch == '\n') {
        //         task_name[task_name_len] = '\0';
        //         break;
        //     } else if ((ch == 127) && task_name_len > 0) {
        //         task_name_len--;
        //         bios_putchar('\b');
        //         bios_putchar(' ');
        //         bios_putchar('\b');
        //     } else if (ch >= ' ' && ch <= '~' && task_name_len < 31) {
        //         task_name[task_name_len++] = ch;
        //         bios_putchar(ch);
        //     }
        // }

        // if (!strcmp(task_name, "ls")) {
        //     bios_putstr("\n\r");
        //     print_task_names();
        //     continue;
        // }
        // if (!strcmp(task_name, "batch-write")) {
        //     bios_putstr("\n\r");
        //     batch_write();
        //     continue;
        // }
        // if (!strcmp(task_name, "batch-run")) {
        //     bios_putstr("\n\r");
        //     batch_run();
        //     continue;
        // }

        // uint64_t entry = load_task_img(task_name);
        // if (entry) {
        //     void (*task_entry)() = (void (*)())entry;
        //     task_entry();
        // } else {
        //     bios_putstr("\n\rFailed to load task!");
        // }

        // If you do non-preemptive scheduling, it's used to surrender control
        // do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        enable_preempt();
        // asm volatile("wfi");
    
    }
    return 0;
}
