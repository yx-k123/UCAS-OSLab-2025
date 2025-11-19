# Project2

## 推荐的地址空间安排

| 地址范围               | 建议用途                       |
|------------------------|-------------------------------|
| 0x50000000–0x50200000  | BBL代码及其运行所需的内存       |
| 0x50200000–0x50500000  | Kernel的数据段/代码段等         |
| 0x50500000–0x52000000  | 供内核动态分配使用的内存       |
| 0x52000000–0x52500000  | 用户程序的数据段/代码段等       |
| 0x52500000–0x60000000  | 使用用户动态分配的内存         |

## task1 任务启动与非抢占式调度
这个任务需要的操作并不多，主要是实现 PCB 的创建与任务的切换。

对于PCB的创建，我们需要为每一个任务分配一个 PCB 结构体，并初始化其各个字段（用户栈、内核栈、状态等）。要注意的是，初始化的时候我们需要设置一个假现场，以用于正确的初始运行程序上下文，即把程序入口地址放在 ra 寄存器中，并设置好初始的栈指针。
```C
// main.c  
// static void init_pcb_stack()
...
pt_switchto->regs[0] = entry_point;
pt_switchto->regs[1] = user_stack;
pcb->kernel_sp = (ptr_t)pt_switchto;
pcb->user_sp = user_stack;
...
```

其次，我们需要给每一个程序分配一个PCB，分配内核栈和用户栈，并设置好pid等信息。
```C
// main.c
static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    char task_name[][32] = {
        "print1",
        "print2",
        "fly",
    };

    int task_idx = 0;
    pid0_pcb.pid = 0;
    pid0_pcb.user_sp = (ptr_t)pid0_stack;
    pid0_pcb.kernel_sp = (ptr_t)pid0_stack;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.cursor_x = 0;
    pid0_pcb.cursor_y = 0;

    for (int i = 0; i < 3; i++)
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
        init_list_head(&pcb[i + 1].list);

        init_pcb_stack(kernel_stack, user_stack, entry, &pcb[i + 1]);
        list_add_tail(&pcb[i + 1].list, &ready_queue);
    }

    /* TODO: [p2-task1] remember to initialize 'current_running' */
    current_running = &pid0_pcb;

}
```
其实这里更好的做法是把 PCB 的创建和初始化封装，当我们调用一个程序的时候，就创建一个 PCB 并分配给这个程序（笔者后续会尝试完善此处(๑•̀ㅂ•́)و✧）。

然后是do_scheduler函数用于实现任务的切换。
```C
// sched.c
void do_scheduler(void)
{
    check_sleeping();
    pcb_t *prev_running = current_running; 
    if (!list_empty(&ready_queue)) {
        list_node_t *next_node = ready_queue.next;
        list_del(next_node);
        current_running = list_entry(next_node, pcb_t, list);
        if (prev_running->status == TASK_RUNNING) {
            prev_running->status = TASK_READY;
            list_add_tail(&prev_running->list, &ready_queue);
        }
        current_running->status = TASK_RUNNING;
    } else {
        current_running = &pid0_pcb;
    }

    switch_to(prev_running, current_running);

}
```
这里的基本想法就是：把当前运行的任务放回就绪队列，然后从就绪队列中取出下一个任务运行。如果就绪队列为空，则继续运行 pid0 任务。

最后是任务切换的实现 switch_to.S 。这个函数需要保存当前任务的上下文，并加载下一个任务的上下文，具体实现如下：
```Assembly
# entry.S
ENTRY(switch_to)
  // 保存 prev 的内核上下文到其内核栈
  addi sp, sp, -(SWITCH_TO_SIZE)
  sd ra, SWITCH_TO_RA(sp)
  sd s0, SWITCH_TO_S0(sp)
  sd s1, SWITCH_TO_S1(sp)
  sd s2, SWITCH_TO_S2(sp)
  sd s3, SWITCH_TO_S3(sp)
  sd s4, SWITCH_TO_S4(sp)
  sd s5, SWITCH_TO_S5(sp)
  sd s6, SWITCH_TO_S6(sp)
  sd s7, SWITCH_TO_S7(sp)
  sd s8, SWITCH_TO_S8(sp)
  sd s9, SWITCH_TO_S9(sp)
  sd s10, SWITCH_TO_S10(sp)
  sd s11, SWITCH_TO_S11(sp)
  sd sp, PCB_KERNEL_SP(a0) // 保存内核栈指针到 prev 的 PCB

  // 切换到 next 的内核栈并恢复其上下文
  ld sp, PCB_KERNEL_SP(a1) // 恢复 next 的内核栈指针
  ld ra, SWITCH_TO_RA(sp)  // 恢复 next 的返回地址
  ld s0, SWITCH_TO_S0(sp)
  ld s1, SWITCH_TO_S1(sp)
  ld s2, SWITCH_TO_S2(sp)
  ld s3, SWITCH_TO_S3(sp)
  ld s4, SWITCH_TO_S4(sp)
  ld s5, SWITCH_TO_S5(sp)
  ld s6, SWITCH_TO_S6(sp)
  ld s7, SWITCH_TO_S7(sp)
  ld s8, SWITCH_TO_S8(sp)
  ld s9, SWITCH_TO_S9(sp)
  ld s10, SWITCH_TO_S10(sp)
  ld s11, SWITCH_TO_S11(sp)
  addi sp, sp, SWITCH_TO_SIZE
  jr ra
ENDPROC(switch_to)
```
此处也有一个待优化的点，我们可以把这些寄存器信息存在用户栈中，而不是内核栈中。

## task2 锁的实现
在这个任务中，我们需要实现一个互斥锁。我们看lock.h文件中给出的锁结构体定义：
```C
typedef enum {
    UNLOCKED,
    LOCKED,
} lock_status_t;

typedef struct spin_lock
{
    volatile lock_status_t status;
} spin_lock_t;

typedef struct mutex_lock
{
    spin_lock_t lock;
    list_head block_queue;
    int key;
} mutex_lock_t;
```
可以看到，互斥锁其实就是包含了一个自旋锁和一个阻塞队列。这样看其实互斥锁和自旋锁的区别就是忙等待和阻塞等待的区别。想清楚之后后续的代码其实比较简单。

首先，我们需要实现自旋锁的初始化，申请锁，加锁和解锁操作。
```C
// lock.c
#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        spin_lock_init(&mlocks[i].lock);
        init_list_head(&mlocks[i].block_queue);
        mlocks[i].key = -1;
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    if (lock->status == UNLOCKED) {
        lock->status = LOCKED;
        return 1;
    }
    return 0;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    while (lock->status == LOCKED) {
        // busy wait
    }
    lock->status = LOCKED;
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    for (int i = 0; i < LOCK_NUM; i++) {
        if (mlocks[i].key == key) {
            return i;
        }
    }

    for (int i = 0; i < LOCK_NUM; i++) {
        if (mlocks[i].key == -1) {
            spin_lock_init(&mlocks[i].lock);
            init_list_head(&mlocks[i].block_queue);
            mlocks[i].key = key;
            return i;
        }
    }

    return -1;
}

void do_mutex_lock_acquire(int mlock_idx)
{
    /* TODO: [p2-task2] acquire mutex lock */
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (spin_lock_try_acquire(&mlock->lock)) {
        return;
    } else {
        current_running->status = TASK_BLOCKED;
        do_block(&current_running->list, &mlock->block_queue);
        do_scheduler();
    } 
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (list_empty(&mlock->block_queue)) {
        spin_lock_release(&mlock->lock);
    } else {
        list_node_t *next_node = mlock->block_queue.next;
        do_unblock(next_node);
    }
}
```

其次要完成阻塞和唤醒：
```C
// sched.c
void do_block(list_node_t *pcb_node, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    pcb_t *pcb = list_entry(pcb_node, pcb_t, list);
    pcb->status = TASK_BLOCKED;
    list_add_tail(pcb_node, queue);
}

void do_unblock(list_node_t *pcb_node)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    pcb_t *pcb = list_entry(pcb_node, pcb_t, list);                        
    pcb->status = TASK_READY;                                               
    list_del(pcb_node);                                              
    list_add_tail(pcb_node, &ready_queue);                   
}
```

最后，这个任务中有个比较容易出bug的地方，就是lock1和lock2初始化的时候发现，这两个函数的入口地址居然不是对齐的！如果P1的代码没有写的很到位的话，就会发生错误。
我们可以添加一个简单的搬运函数，把不对齐的代码搬运到对齐的位置上去：
```C
// loader.c
static void *my_memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i != 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}
```
然后在load_task_img函数中调用这个搬运函数：
```C
// loader.c
...
bios_putstr("\n\rLoading task: ");
bios_putstr(tasks[i].name);
bios_putstr("\n\r");
uint64_t task_entry = TASK_MEM_BASE + TASK_SIZE * i;

int block_id = tasks[i].offset / SECTOR_SIZE;
int inblk_off  = tasks[i].offset % SECTOR_SIZE;

unsigned total_bytes  = inblk_off + tasks[i].size;
unsigned num_of_blks  = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

if (sd_read((unsigned)task_entry, (unsigned)num_of_blks, (unsigned)block_id) < 0) {
    bios_putstr("\n\rFailed to load task!");
    return 0;
}
if (inblk_off != 0) {
    my_memmove((void *)task_entry,(void *)(task_entry + inblk_off),tasks[i].size);
}
return task_entry;
...
```

## task3 系统调用
之所以我们需要重新实现系统调用，是因为我们希望将用户态和内核态区分开来。此时，当用户程序使用系统调用的时候，需要先保存一次上下文，然后陷入内核态，执行完系统调用后再恢复上下文返回用户态。同样的，当进程切换的时候，就需要两次上下文切换(这是比较关键的一点)。

那么首先我门需要补充pcb结构体的初始化函数，添加pt_regs字段，用于保存用户态的寄存器上下文：
```C
// main.c
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
```
几个关键点：
1. 在初始化用户态寄存器上下文的时候，我们需要把 entry_point 放在 sepc 寄存器中，这样才能正确地从内核态返回到用户态。
2. sstatus 寄存器需要设置 SR_SPIE 位，以确保从内核态返回到用户态时中断是开启的。
3. 在初始化内核栈的时候，我们需要把 ret_from_exception 的地址放在 pt_switchto 的 ra 寄存器中，这样才能正确地从内核态返回到用户态。
4. pt_switchto 的 sp 寄存器需要指向内核栈的顶部。

可以这样想，当进程切换时，先恢复内核栈的上下文，然后通过 ret_from_exception 再恢复用户栈的上下文，最后返回用户态。

接下来，我们初始化异常中断处理入口，并打开中断：
```asm
# trap.S
ENTRY(setup_exception)
  la t0, exception_handler_entry
  csrw stvec, t0
  ret
ENDPROC(setup_exception)
```

然后初始化例外入口表和例外入口地址：
```c
// irq.c
void init_exception()
{
    exc_table[EXCC_INST_MISALIGNED] = handle_other;     
    exc_table[EXCC_INST_ACCESS] = handle_other;   
    exc_table[EXCC_BREAKPOINT] = handle_other;       
    exc_table[EXCC_LOAD_ACCESS] = handle_other;      
    exc_table[EXCC_STORE_ACCESS] = handle_other;   
    exc_table[EXCC_SYSCALL] = handle_syscall;      
    exc_table[EXCC_INST_PAGE_FAULT] = handle_other;    
    exc_table[EXCC_LOAD_PAGE_FAULT] = handle_other;       
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_other; 

    setup_exception();
}
```
这里只有syscall异常需要我们处理，其他异常我们暂时不考虑。

接着，我们需要实现中断处理函数 interrupt_helper：
```c
// irq.c
void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    if (scause & (1UL<<63)) {
        return;
    }
    uint64_t exc = scause & 0xFFF;
    if (exc < EXCC_COUNT && exc_table[exc])
        exc_table[exc](regs, stval, scause);
    else
        handle_other(regs, stval, scause);
}
```
这里，请读者根据手册查看 scause 寄存器的格式，判断是中断还是异常，并提取异常号。

我们还需要实现对于上下文的保存和恢复，相关函数在entry.S中，这里不做赘述。但是在实现的过程中有一个关键点在于
sp寄存器的设置，我们需要时刻注意sp指向位置是内核栈还是用户栈。同时对于之前的switch_to函数也需要做一些修改，以适应新的上下文切换需求。
在保存寄存器的时候，我们可以利用一个辅助寄存器 sscratch 来暂存一些值，这个寄存器在用户态和内核态切换时不会被修改，非常适合用来保存一些临时值。

最后只需要把系统调用的实现补充完整即可：
```c
// kernel/syscall.c
void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    regs->sepc += 4; // skip ecall
    uint64_t sysno = regs->regs[17];   // a7
    uint64_t arg0 = regs->regs[10];    // a0
    uint64_t arg1 = regs->regs[11];    // a1
    uint64_t arg2 = regs->regs[12];    // a2
    uint64_t arg3 = regs->regs[13];    // a3
    uint64_t arg4 = regs->regs[14];    // a4
    long ret = syscall[sysno](arg0, arg1, arg2, arg3, arg4);
    regs->regs[10] = ret;              // a0
}
```
这里的sepc需要加4，以跳过ecall指令，这是一个容易忽略的点。

```c
static long invoke_syscall(long sysno, long arg0, long arg1, long arg2,
                           long arg3, long arg4)
{
    long result;
    asm volatile(
        "mv a7, %1\n\t"
        "mv a0, %2\n\t"
        "mv a1, %3\n\t"
        "mv a2, %4\n\t"
        "mv a3, %5\n\t"
        "mv a4, %6\n\t"
        "ecall\n\t"
        "mv %0, a0\n\t"
        : "=r"(result)
        : "r"(sysno), "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4)
    );

    return result;
}
```

我们还需要在 do_scheduler 中添加一个 do_sleep 函数，用于实现任务的睡眠功能：
```c
void do_sleep(uint32_t sleep_time)
{
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, &sleep_queue);
    current_running->wakeup_time = get_timer() + sleep_time;
    do_scheduler();
}
```
其实是一个比较简单的实现，就是把当前任务放入睡眠队列，并设置唤醒时间，然后调用调度器进行任务切换。

## task4 定时器中断、抢占式调度
任务的切换本不应该是由任务程序自己调用调度器来实现的，而是应该由内核来决定什么时候切换任务。本节通过实现定时器中断来实现抢占式调度。

首先我们需要打开全局中断使能：
```asm
# trap.S
...
addi sp, sp, -8
sd ra, 0(sp)
call enable_interrupt
ld ra, 0(sp)
addi sp, sp, 8
...
```

接着初始化中断入口表
## task5 复杂调度算法