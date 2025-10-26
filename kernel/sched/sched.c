#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

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

    // TODO: [p2-task1] switch_to current_running
    switch_to(prev_running, current_running);

}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
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
    list_add_tail(pcb_node, &ready_queue);                   // add to ready_queue
    list_del(pcb_node);                                              // remove from block queue
}
