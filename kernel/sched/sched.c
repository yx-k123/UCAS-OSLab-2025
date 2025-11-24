#include "type.h"
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <os/task.h>
#include <os/loader.h>
#include <os/string.h>

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.
    check_sleeping();
    pcb_t *prev_running = current_running; 

    if (!list_empty(&ready_queue)) {
        list_node_t *next_node = ready_queue.next;
        current_running = list_entry(next_node, pcb_t, list);
        if (prev_running->status == TASK_RUNNING) {
            prev_running->status = TASK_READY;
            list_add_tail(&prev_running->list, &ready_queue);
        }
        current_running->status = TASK_RUNNING;
        list_del(next_node);
    } else {
        current_running = &pid0_pcb;
    }

    // TODO: [p2-task1] switch_to current_running
    switch_to(prev_running, current_running);

    if (prev_running->status == TASK_EXITED) {
        release_resource(prev_running);
    }
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    current_running->status = TASK_BLOCKED;
    list_add_tail(&current_running->list, &sleep_queue);
    current_running->wakeup_time = get_timer() + sleep_time;
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
    int index = -1;
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (pcb[i].status == TASK_EXITED) {
            index = i;
            break;
        } else {
            return -1;
        }
    }

    uint64_t entry_point;
    entry_point = load_task_img(name);
    if (entry_point == 0) {
        return -1;
    }

    pcb[index].kernel_sp = (reg_t)(allocKernelPage(1)+PAGE_SIZE); 
    pcb[index].user_sp = (reg_t)(allocUserPage(1)+PAGE_SIZE);
    uint64_t user_sp = pcb[index].user_sp;
    pcb[index].pid = task_num + 1; 
    pcb[index].status = TASK_READY;
    pcb[index].cursor_x = 0;
    pcb[index].cursor_y = 0;

    user_sp -= sizeof(char*) * argc;
    char **argv_user = (char **)user_sp;
    for(int i=argc-1; i>=0; i--){
        int len = strlen(argv[i]) + 1;
        user_sp -=len;
        argv_user[i] = (char*)user_sp;
        strcpy((char*)user_sp, argv[i]);
    }

    pcb[index].user_sp = (reg_t)ROUNDDOWN(user_sp, 128);
    init_pcb_stack(
        pcb[index].kernel_sp, pcb[index].user_sp, entry_point,
        &pcb[index], argc, argv_user
    );
    list_add_tail(&pcb[index].list, &ready_queue);
    task_num++;
    return pcb[index].pid;
}

void do_exit(void)
{
    current_running->status = TASK_EXITED;
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
    for(int i=0; i<NUM_MAX_TASK; i++){
        if(pcb[i].pid == pid){
            if(pcb[i].status != TASK_EXITED){
                do_block(&(current_running->list), &(pcb[i].wait_list));
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
    list_del(&pcb->list);

    for(int i=0; i<LOCK_NUM; i++){
        if (mlocks[i].pid == pcb->pid) {
            do_mutex_lock_release(i);
        }
    }

    list_node_t* p, *next;
    for(p = pcb->wait_list.next; p != &pcb->wait_list; p = next){
        next = p->next;
        do_unblock(p);
    }

    freeKernelPage((ptr_t)(pcb->kernel_sp - PAGE_SIZE), 1);
    freeUserPage((ptr_t)(pcb->user_sp - PAGE_SIZE), 1);
}