#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];
barrier_t barriers[BARRIER_NUM];
condition_t conditions[CONDITION_NUM];

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
    // if (lock->status == UNLOCKED) {
    //     lock->status = LOCKED;
    //     return 1;
    // }
    return atomic_swap(LOCKED, (ptr_t)&lock->status) == UNLOCKED;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    // while (lock->status == LOCKED) {
    //     // busy wait
    // }
    // lock->status = LOCKED;
    while (atomic_swap(LOCKED, (ptr_t)&lock->status) == LOCKED) {
        // busy wait
    }
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
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (spin_lock_try_acquire(&mlock->lock)) {
        mlock->pid = current_running->pid;
        return;
    } else {
        current_running->status = TASK_BLOCKED;
        do_block(&current_running->list, &mlock->block_queue);
        do_scheduler();
    } 
}

void do_mutex_lock_release(int mlock_idx)
{
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (mlock->pid != current_running->pid) {
        return;
    }
    mlock->pid = -1;  
    if (list_empty(&mlock->block_queue)) {
        spin_lock_release(&mlock->lock);
    } else {
        mlock->pid = list_entry(mlock->block_queue.next, pcb_t, list)->pid;
        list_node_t* next_node = mlock->block_queue.next;
        do_unblock(next_node);
    }
}

void init_barriers(void){
    for (int i = 0; i < BARRIER_NUM; i++) {
        barriers[i].count = 0;
        barriers[i].goal = 0;
        barriers[i].valid = 0;
        barriers[i].key = -1;
        init_list_head(&barriers[i].wait_queue);
        spin_lock_init(&barriers[i].lock);
    }
}

int do_barrier_init(int key, int goal){
    for (int i = 0; i < BARRIER_NUM; i++) {
        if (barriers[i].key == key && barriers[i].valid == 1) {
            return i;
        }
    }

    for (int i = 0; i < BARRIER_NUM; i++) {
        if (barriers[i].valid == 0) {
            barriers[i].key = key;
            barriers[i].goal = goal;
            barriers[i].count = 0;
            barriers[i].valid = 1;
            init_list_head(&barriers[i].wait_queue);
            spin_lock_init(&barriers[i].lock);
            return i;
        }
    }

    return -1;
}

void do_barrier_wait(int bar_idx){
    spin_lock_acquire(&barriers[bar_idx].lock);
    barriers[bar_idx].count++;
    if (barriers[bar_idx].count == barriers[bar_idx].goal) {
        // 唤醒所有等待线程
        list_node_t* p, *next;
        for(p = barriers[bar_idx].wait_queue.next; p != &barriers[bar_idx].wait_queue; p = next){
            next = p->next;
            do_unblock(p);
        }
        barriers[bar_idx].count = 0;
    } else {
        // 阻塞当前线程
        current_running->status = TASK_BLOCKED;
        do_block(&current_running->list, &barriers[bar_idx].wait_queue);
        spin_lock_release(&barriers[bar_idx].lock);
        do_scheduler();
        return;
    }
    spin_lock_release(&barriers[bar_idx].lock);
}

void do_barrier_destroy(int bar_idx){
    spin_lock_acquire(&barriers[bar_idx].lock);
    barriers[bar_idx].valid = 0;
    barriers[bar_idx].key = -1;
    barriers[bar_idx].count = 0;
    barriers[bar_idx].goal = 0;
    // 唤醒所有等待线程
    list_node_t* p, *next;
    for(p = barriers[bar_idx].wait_queue.next; p != &barriers[bar_idx].wait_queue; p = next){
        next = p->next;
        do_unblock(p);
    }
    spin_lock_release(&barriers[bar_idx].lock);
}
