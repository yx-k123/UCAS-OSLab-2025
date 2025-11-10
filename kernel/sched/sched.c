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

// void do_scheduler(void)
// {
//     // TODO: [p2-task3] Check sleep queue to wake up PCBs

//     /************************************************************/
//     /* Do not touch this comment. Reserved for future projects. */
//     /************************************************************/

//     // TODO: [p2-task1] Modify the current_running pointer.
//     check_sleeping();
//     pcb_t *prev_running = current_running; 

//     if (!list_empty(&ready_queue)) {
//         list_node_t *next_node = ready_queue.next;
//         current_running = list_entry(next_node, pcb_t, list);
//         if (prev_running->status == TASK_RUNNING) {
//             prev_running->status = TASK_READY;
//             list_add_tail(&prev_running->list, &ready_queue);
//         }
//         current_running->status = TASK_RUNNING;
//         list_del(next_node);
//     } else {
//         current_running = &pid0_pcb;
//     }

//     // TODO: [p2-task1] switch_to current_running
//     switch_to(prev_running, current_running);
// }

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

void do_set_sche_workload(int remain_length) {
    pcb_t *p = current_running;
    if (p->flag_position <= 0)
        return;

    int ts = calculate_time_slice(60 - remain_length, p->flag_position);
    if (ts < 1) ts = 1;

    p->time_slice = ts - 1;

    if (p->time_slice_remain > p->time_slice)
        p->time_slice_remain = p->time_slice;
}

void do_scheduler(void)
{
    check_sleeping();
    pcb_t *prev_running = current_running;

    if (current_running->if_switch == 0) {
        if (current_running->time_slice_remain > 0) {
            current_running->time_slice_remain--;
            return;
        } else {
            current_running->if_switch = 1;
        }
    }

    current_running->time_slice_remain = current_running->time_slice;
    current_running->if_switch = 0;

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

    switch_to(prev_running, current_running);
}

int calculate_time_slice(int position, int flag) {
    const int END_POS = 60;     
    const int MIN_TS = 1;       
    const int MAX_TS = 10;             

    static int fly_flag[5] = {0};         
    static int fly_position[5] = {0};
    static int fly_count[5] = {0};
    static int current_phase = 0;  // 0:去检查点, 1:去终点
    static int pre_phase = 0;

    pre_phase = current_phase;

    int index = flag / 10 - 1;
    fly_flag[index] = flag;
    fly_position[index] = position;

    // 确定当前目标
    int target = (current_phase == 0) ? fly_flag[index] : END_POS;
    
    // 检查是否需要阶段切换
    int all_reached = 1;
    for (int i = 0; i < 5; i++) {
        int i_target = (current_phase == 0) ? fly_flag[i] : END_POS;
        if (fly_position[i] < i_target) {
            all_reached = 0;
            break;
        }
    }
    
    if (all_reached) {
        current_phase = !current_phase;  // 切换阶段
        if (current_phase == 0) {
            // 新循环开始，重置所有位置
            for (int i = 0; i < 5; i++) fly_position[i] = 0;
        }
        // 重新计算目标
        target = (current_phase == 0) ? fly_flag[index] : END_POS;
    }

    if (pre_phase == 1 && current_phase == 0) {
        fly_count[index]++;
    }

    // 计算时间片（落后越多，时间片越大）
    int remain = target - position;
    if (remain < 0) remain = 0;
    
    // 找到最大剩余距离作为基准
    int max_remain = 0;
    for (int i = 0; i < 5; i++) {
        int i_target = (current_phase == 0) ? fly_flag[i] : END_POS;
        int i_remain = i_target - fly_position[i];
        if (i_remain < 0) i_remain = 0;
        if (i_remain > max_remain) max_remain = i_remain;
    }

    if (max_remain == 0) return MIN_TS;
    
    int ts = MIN_TS + (remain * (MAX_TS - MIN_TS)) / max_remain;
    if (ts < MIN_TS) ts = MIN_TS;
    if (ts > MAX_TS) ts = MAX_TS;
    return ts;
}