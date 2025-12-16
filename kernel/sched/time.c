#include <os/list.h>
#include <os/sched.h>
#include <type.h>

uint64_t time_elapsed = 0;
uint64_t time_base = 0;

uint64_t get_ticks()
{
    __asm__ __volatile__(
        "rdtime %0"
        : "=r"(time_elapsed));
    return time_elapsed;
}

uint64_t get_timer()
{
    return get_ticks() / time_base;
}

uint64_t get_time_base()
{
    return time_base;
}

void latency(uint64_t time)
{
    uint64_t begin_time = get_timer();

    while (get_timer() - begin_time < time);
    return;
}

void check_sleeping(void)
{
    // TODO: [p2-task3] Pick out tasks that should wake up from the sleep queue
    spin_lock_acquire(&sched_lock);

    list_head *head = &sleep_queue;
    list_node_t *node = head->next;
    while (node != head) {
        pcb_t *task = list_entry(node, pcb_t, list);
        list_node_t *next_node = node->next;
        if (task->wakeup_time <= get_timer()) {
            task->status = TASK_READY;
            list_del(&task->list);
            list_add_tail(&task->list, &ready_queue);
        }
        node = next_node;
    }

    spin_lock_release(&sched_lock);
}