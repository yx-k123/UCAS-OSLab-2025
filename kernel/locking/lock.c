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
        list_node_t *next_node = mlock->block_queue.next;
        do_unblock(next_node);
        mlock->pid = ((pcb_t *)list_entry(next_node, pcb_t, list))->pid;
    }
}
