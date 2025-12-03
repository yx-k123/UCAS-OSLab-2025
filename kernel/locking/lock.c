#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <os/string.h>
#include <atomic.h>
#include <os/smp.h>

mutex_lock_t mlocks[LOCK_NUM];
barrier_t barriers[BARRIER_NUM];
condition_t conditions[CONDITION_NUM];
mailbox_t mailboxs[MBOX_NUM];

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
    uint64_t cpu_id = get_current_cpu_id();
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (spin_lock_try_acquire(&mlock->lock)) {
        mlock->pid = current_running[cpu_id]->pid;
        return;
    } else {
        current_running[cpu_id]->status = TASK_BLOCKED;
        do_block(&current_running[cpu_id]->list, &mlock->block_queue);
        do_scheduler();
    } 
}

void do_mutex_lock_release(int mlock_idx)
{   
    uint64_t cpu_id = get_current_cpu_id();
    mutex_lock_t *mlock = &mlocks[mlock_idx];
    if (mlock->pid != current_running[cpu_id]->pid) {
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
    uint64_t cpu_id = get_current_cpu_id();
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
        current_running[cpu_id]->status = TASK_BLOCKED;
        do_block(&current_running[cpu_id]->list, &barriers[bar_idx].wait_queue);
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


void init_conditions(void){
    for (int i = 0; i < CONDITION_NUM; i++) {
        conditions[i].key = -1;
        conditions[i].valid = 0;
        init_list_head(&conditions[i].wait_queue);
    }
}

int do_condition_init(int key){
    for (int i = 0; i < CONDITION_NUM; i++) {
        if (conditions[i].key == key && conditions[i].valid == 1) {
            return i;
        }
    }

    for (int i = 0; i < CONDITION_NUM; i++) {
        if (conditions[i].valid == 0) {
            conditions[i].key = key;
            conditions[i].valid = 1;
            init_list_head(&conditions[i].wait_queue);
            return i;
        }
    }

    return -1;
}

void do_condition_wait(int cond_idx, int mutex_idx){
    uint64_t cpu_id = get_current_cpu_id();
    current_running[cpu_id]->status = TASK_BLOCKED;
    do_block(&current_running[cpu_id]->list, &conditions[cond_idx].wait_queue);
    do_mutex_lock_release(mutex_idx);
    do_scheduler();
}

void do_condition_signal(int cond_idx){
    if (list_empty(&conditions[cond_idx].wait_queue)) {
        return;
    } else {
        list_node_t* next_node = conditions[cond_idx].wait_queue.next;
        do_unblock(next_node);
    }
}

void do_condition_broadcast(int cond_idx){
    list_node_t* p, *next;
    for(p = conditions[cond_idx].wait_queue.next; p != &conditions[cond_idx].wait_queue; p = next){
        next = p->next;
        do_unblock(p);
    }
}

void do_condition_destroy(int cond_idx){
    conditions[cond_idx].valid = 0;
    conditions[cond_idx].key = -1;
    do_condition_broadcast(cond_idx);
}

void init_mbox(){
    for (int i = 0; i < MBOX_NUM; i++) {
        mailboxs[i].valid = 0;
        mailboxs[i].ref_count = 0;
        mailboxs[i].head = 0;
        mailboxs[i].tail = 0;
        mailboxs[i].data_count = 0;
        mailboxs[i].name[0] = '\0';

        spin_lock_init(&mailboxs[i].lock);
        init_list_head(&mailboxs[i].recv_queue);
        init_list_head(&mailboxs[i].send_queue);
    }
}

int do_mbox_open(char *name){
    for (int i = 0; i < MBOX_NUM; i++) {
        if (mailboxs[i].valid == 1 && strcmp(mailboxs[i].name, name) == 0) {
            mailboxs[i].ref_count++;
            return i;
        }
    }

    for (int i = 0; i < MBOX_NUM; i++) {
        if (mailboxs[i].valid == 0) {
            mailboxs[i].valid = 1;
            mailboxs[i].ref_count = 1;
            strcpy(mailboxs[i].name, name);
            mailboxs[i].head = 0;
            mailboxs[i].tail = 0;
            mailboxs[i].data_count = 0;

            init_list_head(&mailboxs[i].recv_queue);
            init_list_head(&mailboxs[i].send_queue);
            spin_lock_init(&mailboxs[i].lock);
            return i;
        }
    }

    return -1;
}

void do_mbox_close(int mbox_idx){
    mailbox_t *mbox = &mailboxs[mbox_idx];
    mbox->ref_count--;
    if (mbox->ref_count == 0) {
        mbox->valid = 0;
        mbox->name[0] = '\0';
        mbox->head = 0;
        mbox->tail = 0;
        mbox->data_count = 0;
    }
}

int do_mbox_send(int mbox_idx, void * msg, int msg_length){
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || !mailboxs[mbox_idx].valid) {
        return 0;
    }

    mailbox_t *mbox = &mailboxs[mbox_idx];
    char *data = (char *)msg;
    
    spin_lock_acquire(&mbox->lock);

    while (MAX_MBOX_LENGTH - mbox->data_count < msg_length) {
        uint64_t cpu_id = get_current_cpu_id();
        current_running[cpu_id]->status = TASK_BLOCKED;
        do_block(&current_running[cpu_id]->list, &mbox->send_queue);
        spin_lock_release(&mbox->lock); 
        do_scheduler();
        spin_lock_acquire(&mbox->lock);
    }

    for (int i = 0; i < msg_length; i++) {
        mbox->buffer[mbox->tail] = data[i];
        mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->data_count += msg_length;

    while (!list_empty(&mbox->recv_queue)) {
        do_unblock(mbox->recv_queue.next);
        break; 
    }

    spin_lock_release(&mbox->lock);
    return msg_length;
}

int do_mbox_recv(int mbox_idx, void * msg, int msg_length){
    uint64_t cpu_id = get_current_cpu_id();
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM || mailboxs[mbox_idx].valid == 0) {
        return -1;
    }

    mailbox_t *mbox = &mailboxs[mbox_idx];
    char *data = (char *)msg;

    spin_lock_acquire(&mbox->lock);

    while (mbox->data_count < msg_length) {
        current_running[cpu_id]->status = TASK_BLOCKED;
        do_block(&current_running[cpu_id]->list, &mbox->recv_queue);
        spin_lock_release(&mbox->lock);
        do_scheduler();
        spin_lock_acquire(&mbox->lock);
    }

    for (int i = 0; i < msg_length; i++) {
        data[i] = mbox->buffer[mbox->head];
        mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
    }
    
    mbox->data_count -= msg_length;

    while (!list_empty(&mbox->send_queue)) {
        do_unblock(mbox->send_queue.next);
        break; 
    }

    spin_lock_release(&mbox->lock);
    return msg_length;
}

