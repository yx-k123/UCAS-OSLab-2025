#include "pgtable.h"
#include "type.h"
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <os/task.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/smp.h>
#include <csr.h>

pcb_t * current_running[CPU_CORE_NUM];

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack
};

const ptr_t s_pid0_stack = INIT_KERNEL_STACK + 2 * PAGE_SIZE;
pcb_t s_pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)s_pid0_stack,
    .user_sp = (ptr_t)s_pid0_stack
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    pcb_t *prev = current_running[cpu_id];

    if (!list_empty(&ready_queue)) {
        list_node_t *next_node = ready_queue.next;
        current_running[cpu_id] = list_entry(next_node, pcb_t, list);
        if (prev->status == TASK_RUNNING) {
            prev->status = TASK_READY;
            list_add_tail(&prev->list, &ready_queue);
        }
        current_running[cpu_id]->status = TASK_RUNNING;
        list_del(next_node);
    } else {
        if (cpu_id == 0) {
            current_running[cpu_id] = &pid0_pcb;
        } else {
            current_running[cpu_id] = &s_pid0_pcb;
        }
    }
    /************************************************************/
    // TODO: [p5-task3] Check send/recv queue to unblock PCBs
    /************************************************************/

    pcb_t *next = current_running[cpu_id];

    if (next == prev)
        return;

    // 切换地址空间：只有用户进程有独立 pgdir，idle 用内核 pgdir
    uintptr_t next_pgdir = next->pgdir;
    if (!next_pgdir) {
        next_pgdir = pa2kva(PGDIR_PA);
    }

    set_satp(
        SATP_MODE_SV39, 
        current_running[get_current_cpu_id()]->pid,
        kva2pa(current_running[get_current_cpu_id()]->pgdir) >> NORMAL_PAGE_SHIFT
    );
    local_flush_tlb_all();

    switch_to(prev, next);
}

void do_sleep(uint32_t sleep_time)
{   
    uint64_t cpu_id = get_current_cpu_id();
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    current_running[cpu_id]->status = TASK_BLOCKED;
    list_add_tail(&current_running[cpu_id]->list, &sleep_queue);
    current_running[cpu_id]->wakeup_time = get_timer() + sleep_time;
    do_scheduler();
}

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
    pcb_t *pcb = list_entry(pcb_node, pcb_t, list);                         // get pcb from pcb_node
    pcb->status = TASK_READY;                                               // change status to READY
    list_del(pcb_node);                                              // remove from block queue
    list_add_tail(pcb_node, &ready_queue);                   // add to ready_queue
}

pid_t do_exec(char *name, int argc, char **argv)
{
    int idx = -1;
    // 寻找空闲的 PCB
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;

    pcb_t *p = &pcb[idx];

    char k_taskname[64]; // 内核栈上的缓冲区

    uint64_t cpu_id = get_current_cpu_id();
    // uintptr_t current_pgdir = current_running[cpu_id]->pgdir;
    // alloc_page_helper((uintptr_t)name, current_pgdir);
    // local_flush_tlb_all();
    
    // 开启 SUM 位，允许内核读取用户指针 (name, argv)

    // 拷贝文件名
    // strncpy(k_taskname, name, 63);
    // k_taskname[63] = '\0';

    // 开启 SUM 位，允许内核读取用户指针 (name, argv)
    uint64_t old_sstatus = get_sstatus();
    if (!(old_sstatus & SR_SUM)) {
        set_sstatus(old_sstatus | SR_SUM);
    }

    // 拷贝文件名
    strncpy(k_taskname, name, 63);
    k_taskname[63] = '\0';

    ptr_t pgdir_pa = allocPage(1);
    uintptr_t pgdir_kva = pa2kva(pgdir_pa);
    memset((void *)pgdir_kva, 0, PAGE_SIZE);

    // 2. 拷贝内核映射 (Share Kernel Mapping)
    share_pgtable(pgdir_kva, pa2kva(PGDIR_PA));
    p->pgdir = pgdir_kva; 

    // 3. 加载程序 (Load Task Image)
    uint64_t entry = load_task_img(k_taskname, pgdir_kva);
    if (!entry) {
        set_sstatus(old_sstatus);
        return -1;
    }
    
    // 必须刷新 I-Cache，因为我们刚刚写入了指令
    local_flush_icache_all();

    ptr_t kstack_pa = allocPage(1);
    uintptr_t kstack_kva = pa2kva(kstack_pa);
    memset((void *)kstack_kva, 0, PAGE_SIZE);
    p->kernel_sp = kstack_kva + PAGE_SIZE;

    uintptr_t user_stack_top = USER_STACK_ADDR;
    // alloc_page_helper 返回 KVA
    uintptr_t ustack_kva_base = alloc_page_helper(user_stack_top - PAGE_SIZE, pgdir_kva);
    memset((void *)ustack_kva_base, 0, PAGE_SIZE);

    // 5.1 处理 argv
    uintptr_t sp_kva = ustack_kva_base + PAGE_SIZE;
    uintptr_t argv_uva_arr[argc + 1];
    
    // 此时 sstatus.SUM 依然是开启的，可以安全读取 argv[i]
    for (int i = argc - 1; i >= 0; --i) {
        // 读取用户态字符串长度
        size_t len = strlen(argv[i]) + 1;
        sp_kva -= len;
        // 将用户态字符串拷贝到内核映射的用户栈 (sp_kva)
        memcpy((void *)sp_kva, argv[i], len);

        uintptr_t offset = sp_kva - ustack_kva_base;
        argv_uva_arr[i] = (user_stack_top - PAGE_SIZE) + offset;
    }
    argv_uva_arr[argc] = 0; 

    // 对齐
    sp_kva &= ~((uintptr_t)0xF);

    // 压入指针数组
    sp_kva -= (argc + 1) * sizeof(uintptr_t);
    memcpy((void *)sp_kva, (void *)argv_uva_arr, (argc + 1) * sizeof(uintptr_t));

    // 计算最终的用户栈顶指针 (UVA)
    uintptr_t offset_final = sp_kva - ustack_kva_base;
    uintptr_t user_sp_new = (user_stack_top - PAGE_SIZE) + offset_final;
    p->user_sp = user_sp_new;

    set_sstatus(old_sstatus);

    // 6. 初始化 PCB 其他字段
    p->pid        = process_id++;
    p->status     = TASK_READY;
    p->cursor_x   = 0;
    p->cursor_y   = 0;
    p->wakeup_time = 0;
    init_list_head(&p->wait_list);

    // 建立初始内核栈上的寄存器上下文
    // a0 = argc, a1 = argv (user_sp_new 指向 argv 指针数组)
    init_pcb_stack(p->kernel_sp, p->user_sp, entry, p, argc, (char **)user_sp_new);

    // 7. 加入 ready_queue
    list_add_tail(&p->list, &ready_queue);

    printk("DEBUG: do_exec success pid=%d name=%s\n", p->pid, k_taskname);

    return p->pid;
}

void do_exit(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    current_running[cpu_id]->status = TASK_EXITED;
    release_resource(current_running[cpu_id]);
    do_scheduler();
}

int do_kill(pid_t pid)
{
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (pcb[i].pid == pid && pcb[i].status != TASK_EXITED) {
            pcb[i].status = TASK_EXITED;
            release_resource(&pcb[i]);
            return 0;
        }
    }
    return -1;
}

int do_waitpid(pid_t pid)
{   
    uint64_t cpu_id = get_current_cpu_id();
    for(int i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].pid == pid){
            if(pcb[i].status != TASK_EXITED){
                do_block(&(current_running[cpu_id]->list), &(pcb[i].wait_list));
                do_scheduler();
                return pid;
            }
        }
    }
    return 0;
}

void do_process_show(){
    int i;
    static char *stat_str[3]={
        "BLOCKED","RUNNING","READY"
    };
    screen_write("[Process table]:\n");
    for(i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].status==TASK_EXITED)
            continue;
        else
            printk("[%d] PID : %d  STATUS : %s \n", i, pcb[i].pid, stat_str[pcb[i].status]);
    }
}

void release_resource(pcb_t *pcb)
{   
    uint64_t cpu_id = get_current_cpu_id();
    if (pcb != current_running[cpu_id]) {
         list_del(&pcb->list);
    }

    for(int i=0; i<LOCK_NUM; i++){
        if (mlocks[i].pid == pcb->pid) {
            mlocks[i].pid = current_running[cpu_id]->pid;
            do_mutex_lock_release(i);
        }
    }

    list_node_t* p, *next;
    for(p = pcb->wait_list.next; p != &pcb->wait_list; p = next){
        next = p->next;
        do_unblock(p);
    }

    if (pcb->pgdir) {
        free_page_helper(pcb->pgdir);
        pcb->pgdir = 0;
    }

    if (pcb->kernel_sp) {
        freePage(kva2pa(pcb->kernel_sp - PAGE_SIZE));
        pcb->kernel_sp = 0;
    }
}

int do_getpid()
{   
    uint64_t cpu_id = get_current_cpu_id();
    return current_running[cpu_id]->pid;
}