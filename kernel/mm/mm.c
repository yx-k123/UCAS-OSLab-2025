#include "os/kernel.h"
#include <os/mm.h>
#include <os/lock.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static const ptr_t page_start_addr = (ptr_t)FREEMEM_KERNEL;
static const ptr_t page_end_addr = (ptr_t)FREEMEM_KERNEL + (ptr_t)(PAGE_SIZE * MAX_PHY_PAGES);
static ptr_t phy_mem_start = 0;

spin_lock_t os_mm_lock;

frame_t frame_table[MAX_PHY_PAGES];
list_head clock_queue;
list_head free_list;

int alloc_swap_slot() {
    return swap_idx++; // 返回的是槽位号 (slot index)，不是扇区号
}

uint64_t get_swap_sector(int slot) {
    return SWAP_START_SEC + slot * SECTORS_PER_PAGE;
}

void pmm_init() {
    spin_lock_init(&os_mm_lock);
    init_list_head(&free_list); 
    init_list_head(&clock_queue);

    // 1. 获取物理地址
    ptr_t start_pa = kva2pa(FREEMEM_KERNEL);
    
    // 强制 4KB 对齐
    start_pa = ROUND(start_pa, PAGE_SIZE);

    // 保存对齐后的物理起始地址，供 freePage 计算下标使用
    phy_mem_start = start_pa; 

    for (int i = 0; i < MAX_PHY_PAGES; i++) {
        frame_table[i].pa = start_pa + i * PAGE_SIZE;
        
        frame_table[i].va = 0;
        frame_table[i].pgdir = 0;
        frame_table[i].pte = NULL;
        
        list_add_tail(&frame_table[i].qnode, &free_list);
    }
}

ptr_t allocPage(int numPage) {
    spin_lock_acquire(&os_mm_lock);
    while (list_empty(&free_list)) {
        spin_lock_release(&os_mm_lock);
        printk("DEBUG: allocPage free_list empty, calling swap_out()\n");
        swap_out(); 
        spin_lock_acquire(&os_mm_lock);
    }
    
    // 二次检查，防止 swap_out 失败
    if (list_empty(&free_list)) {
         // 可以在这里 panic 或者再次 swap
         spin_lock_release(&os_mm_lock);
         printk("FATAL: allocPage failed after swap_out!\n");
         assert(0 && "Out of memory!");
    }

    list_node_t *node = free_list.next;
    list_del(node); 
    
    spin_lock_release(&os_mm_lock);

    frame_t *frame = (frame_t *)node; 

    memset((void*)pa2kva(frame->pa), 0, PAGE_SIZE);
    // printk("DEBUG: allocPage success pa=0x%lx\n", frame->pa);

    return frame->pa;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void freePage(ptr_t baseAddr)
{
    // baseAddr 是物理地址，必须用物理起始地址来计算下标
    if (baseAddr < phy_mem_start) return;
    
    int index = (baseAddr - phy_mem_start) / PAGE_SIZE;

    if (index >= MAX_PHY_PAGES) return;

    frame_t *frame = &frame_table[index];

    spin_lock_acquire(&os_mm_lock);

    frame->va = 0;
    frame->pgdir = 0;
    frame->pte = NULL;

    if (!list_empty(&frame->qnode)) {
        list_del(&frame->qnode);
    }

    list_add_tail(&frame->qnode, &free_list);

    spin_lock_release(&os_mm_lock);
}

void swap_out() {
    spin_lock_acquire(&os_mm_lock);

    if (list_empty(&clock_queue)) {
        spin_lock_release(&os_mm_lock);
        printk("FATAL: swap_out failed, clock_queue is empty!\n");
        assert(0 && "Swap out failed: No victim page found!");
    }

    // Clock Algorithm Implementation
    list_node_t *node = clock_queue.next;
    frame_t *victim = NULL;

    // 遍历队列寻找合适的 victim
    while (node != &clock_queue) {
        victim = (frame_t *)node;
        
        // 检查 Accessed 位 (Bit 6 in RISC-V PTE)
        if (victim->pte) {
             // 验证 pte 指针是否合法 (简单检查是否在内核空间)
            if ((uintptr_t)victim->pte < 0xffffffc000000000) {
                printk("WARNING: victim->pte is suspicious: 0x%lx\n", victim->pte);
            }

            if (*victim->pte & _PAGE_ACCESSED) {
                // 如果被访问过，清除 Accessed 位，并将其移到队尾（给予第二次机会）
                *victim->pte &= ~_PAGE_ACCESSED;
                
                list_node_t *next_node = node->next;
                list_del(node);
                list_add_tail(node, &clock_queue);
                node = next_node;
            } else {
                // 找到没有被访问过的页面
                break;
            }
        } else {
            // pte 为 NULL，跳过或作为 victim? 
            // 正常情况下不应该为 NULL，如果为 NULL 说明它可能没被正确映射，可以直接置换
            printk("WARNING: victim->pte is NULL in clock loop\n");
            break;
        }
    }

    // 如果遍历一圈都没找到（所有页都被访问过），node 会回到 clock_queue
    // 此时取队头（它是最早被清除 Accessed 位的，或者本来就是最早的）
    if (node == &clock_queue) {
        if (list_empty(&clock_queue)) {
             spin_lock_release(&os_mm_lock);
             printk("FATAL: clock_queue became empty unexpectedly!\n");
             assert(0);
        }
        node = clock_queue.next;
        victim = (frame_t *)node;
    }

    list_del(node); // Remove from queue inside lock

    int slot = alloc_swap_slot();
    
    // 1. 先修改 PTE，标记为 Swap Slot，防止其他核心继续访问
    if (victim->pte) {
        // 再次检查 pte 指针
        if ((uintptr_t)victim->pte < 0xffffffc000000000) {
             printk("FATAL: victim->pte is invalid: 0x%lx\n", victim->pte);
             assert(0);
        }

        PTE original_pte = *victim->pte;
        uint64_t perms = original_pte & 0x3FF; 
        perms &= ~_PAGE_PRESENT; 
        *victim->pte = (slot << 10) | perms; 
        
        // 刷新 TLB，确保修改生效
        local_flush_tlb_all();
        local_flush_icache_all();
    } else {
        printk("FATAL: swap_out victim->pte is NULL! pa=0x%lx\n", victim->pa);
    }

    printk("DEBUG: swap_out victim pa=0x%lx va=0x%lx slot=%d\n", victim->pa, victim->va, slot);

    uint64_t sector = get_swap_sector(slot);
    
    bios_sd_write(pa2kva(victim->pa), SECTORS_PER_PAGE, sector);
    
    printk("DEBUG: swap_out write done for slot=%d\n", slot);

    // 3. 回收物理页 (手动执行 freePage 的逻辑，因为我们已经持有锁)
    victim->va = 0;
    victim->pgdir = 0;
    victim->pte = NULL;
    // node 已经从 clock_queue 删除了，现在加入 free_list
    list_add_tail(&victim->qnode, &free_list);

    spin_lock_release(&os_mm_lock);
}

void swap_in(PTE *pte, uintptr_t va) {
    uint64_t pte_val = *pte;
    int slot = pte_val >> 10;
    uint64_t sector = get_swap_sector(slot);

    printk("DEBUG: swap_in slot=%d va=0x%lx\n", slot, va);

    ptr_t new_pa = allocPage(1); 

    bios_sd_read(pa2kva(new_pa), SECTORS_PER_PAGE, sector);

    spin_lock_acquire(&os_mm_lock);

    if (*pte & _PAGE_PRESENT) {
        spin_lock_release(&os_mm_lock);
        freePage(new_pa);
        return;
    }

    uint64_t perm = pte_val & 0x3FF; 
    perm |= _PAGE_PRESENT; 
    *pte = ((new_pa >> 12) << 10) | perm;

    // 使用 phy_mem_start 计算下标
    int frame_idx = (new_pa - phy_mem_start) / PAGE_SIZE;
    frame_t *frame = &frame_table[frame_idx];

    frame->va = va;
    frame->pte = pte;
    frame->pgdir = current_running[get_current_cpu_id()]->pgdir; 

    list_add_tail(&frame->qnode, &clock_queue);
    
    // DEBUG: Print clock_queue size
    int q_size = 0;
    list_node_t *curr = clock_queue.next;
    while(curr != &clock_queue) { q_size++; curr = curr->next; }
    // printk("DEBUG: clock_queue size=%d\n", q_size);

    spin_lock_release(&os_mm_lock);

    local_flush_tlb_all();
    local_flush_icache_all();
}

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // TODO [P4-task1] share_pgtable:
    PTE *src = (PTE *)src_pgdir;
    PTE *dest = (PTE *)dest_pgdir;

    // Kernel addresses occupy the upper half of Sv39 virtual space (VPN2 >= 256)
    for (int i = NUM_PTE_ENTRY / 2; i < NUM_PTE_ENTRY; ++i) {
        dest[i] = src[i];
    }
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1ff;
    uint64_t vpn1 = (va >> 21) & 0x1ff;
    uint64_t vpn0 = (va >> 12) & 0x1ff;

    PTE *pgd = (PTE*)pgdir;
    
    if ((pgd[vpn2] & _PAGE_PRESENT) == 0) {
        ptr_t pa = allocPage(1); 
        set_pfn(&pgd[vpn2], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgd[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(pa));
    }

    PTE *pmd = (PTE *)pa2kva(get_pa(pgd[vpn2]));

    if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
        ptr_t pa = allocPage(1);
        set_pfn(&pmd[vpn1], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
        clear_pgdir(pa2kva(pa));
    }

    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));

    if ((pte[vpn0] & _PAGE_PRESENT) == 0) {
        ptr_t pa = allocPage(1);
        
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(
            &pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                        _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_USER | _PAGE_GLOBAL);

        // 使用 phy_mem_start 计算下标
        int frame_idx = (pa - phy_mem_start) / PAGE_SIZE;
        
        if (frame_idx >= 0 && frame_idx < MAX_PHY_PAGES) {
            frame_t *frame = &frame_table[frame_idx];
            
            spin_lock_acquire(&os_mm_lock);

            frame->va = va;
            frame->pte = &pte[vpn0];
            frame->pgdir = pgdir;

            // 直接添加到队列，不要调用 list_del，防止破坏 free_list
            list_add_tail(&frame->qnode, &clock_queue);

            spin_lock_release(&os_mm_lock);
        }
    }
    
    return pa2kva(get_pa(pte[vpn0]));
}

uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
}

PTE *get_pte(uintptr_t pgdir, uintptr_t va) {
    uint64_t vpn2 = (va >> 30) & 0x1ff;
    uint64_t vpn1 = (va >> 21) & 0x1ff;
    uint64_t vpn0 = (va >> 12) & 0x1ff;

    PTE *page_dir = (PTE *)pgdir;
    
    if (!(page_dir[vpn2] & _PAGE_PRESENT)) return NULL;
    PTE *pmd = (PTE *)pa2kva(get_pa(page_dir[vpn2]));

    if (!(pmd[vpn1] & _PAGE_PRESENT)) return NULL;
    PTE *pte_table = (PTE *)pa2kva(get_pa(pmd[vpn1]));

    return &pte_table[vpn0];
}
