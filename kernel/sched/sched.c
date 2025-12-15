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
    for (int i = 0; i < NUM_MAX_TASK; ++i) {
        if (pcb[i].status == TASK_EXITED) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;

    pcb_t *p = &pcb[idx];

    // 1. 分配用户页表根页
    uintptr_t new_pgdir = allocPage(1);      // 返回 KVA
    memset((void *)new_pgdir, 0, PAGE_SIZE);

    // 2. 拷贝内核映射
    share_pgtable(new_pgdir, pa2kva(PGDIR_PA));
    p->pgdir = (uintptr_t)new_pgdir;

    // 3. 加载程序到这个 pgdir
    uint64_t entry = load_task_img(name, new_pgdir);
    if (!entry)
        return -1;

    // 4. 分配内核栈
    p->kernel_sp = allocPage(1) + PAGE_SIZE;

    // 5. 分配用户栈（这里先映射 1 页；若 argv 很大需多页）
    uintptr_t user_stack_top = USER_STACK_ADDR;
    uintptr_t ustack_kva_base = alloc_page_helper(user_stack_top - PAGE_SIZE, new_pgdir);
    p->user_sp = user_stack_top;

    // 5.1 将 argv 与字符串拷到用户栈
    // 栈从高地址向低地址增长
    uintptr_t sp_kva = ustack_kva_base + PAGE_SIZE;

    // 先拷贝各字符串，记录其用户地址
    uintptr_t argv_uva_arr[argc + 1];
    for (int i = argc - 1; i >= 0; --i) {
        size_t len = strlen(argv[i]) + 1;    // 含 '\0'
        sp_kva -= len;
        // 简单对齐可选：不要求对齐字符串
        memcpy((void *)sp_kva, argv[i], len);

        // 由 KVA 偏移换算回 UVA
        uintptr_t offset = sp_kva - ustack_kva_base;
        argv_uva_arr[i] = (user_stack_top - PAGE_SIZE) + offset;
    }
    argv_uva_arr[argc] = 0; // argv 终止 NULL

    // 让栈 16 字节对齐（RISC-V ABI）
    sp_kva &= ~((uintptr_t)0xF);

    // 再压入 argv 指针数组
    sp_kva -= (argc + 1) * sizeof(uintptr_t);
    memcpy((void *)sp_kva, (void *)argv_uva_arr, (argc + 1) * sizeof(uintptr_t));

    // 计算此时用户态的 argv 数组地址与新的用户栈顶
    uintptr_t argv_uva = (user_stack_top - PAGE_SIZE) + (sp_kva - ustack_kva_base);
    uintptr_t user_sp_new = (user_stack_top - PAGE_SIZE) + (sp_kva - ustack_kva_base);
    p->user_sp = user_sp_new;

    // 6. 初始化 PCB 其他字段
    p->pid        = process_id++;
    p->status     = TASK_READY;
    p->cursor_x   = 0;
    p->cursor_y   = 0;
    p->wakeup_time = 0;
    init_list_head(&p->wait_list);

    // 建立初始内核栈上的寄存器上下文
    // a0=argc, a1=argv(uva)
    init_pcb_stack(p->kernel_sp, p->user_sp, entry, p, argc, (char **)argv_uva);

    // 7. 加入 ready_queue
    list_add_tail(&p->list, &ready_queue);

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
}

int do_getpid()
{   
    uint64_t cpu_id = get_current_cpu_id();
    return current_running[cpu_id]->pid;
}