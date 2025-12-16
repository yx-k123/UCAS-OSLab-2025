#include <os/mm.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static const ptr_t page_start_addr = (ptr_t)FREEMEM_KERNEL;
static const ptr_t page_end_addr = (ptr_t)FREEMEM_KERNEL + (ptr_t)(PAGE_SIZE * MAX_PHY_PAGES);
static ptr_t phy_mem_start = 0;

frame_t frame_table[MAX_PHY_PAGES];
list_head fifo_queue;
list_head free_list;

int alloc_swap_slot() {
    return swap_idx++; // 返回的是槽位号 (slot index)，不是扇区号
}

uint64_t get_swap_sector(int slot) {
    return SWAP_START_SEC + slot * SECTORS_PER_PAGE;
}

void pmm_init() {
    init_list_head(&free_list); 
    init_list_head(&fifo_queue);

    // 1. 计算并保存物理起始地址
    phy_mem_start = kva2pa(FREEMEM_KERNEL); 

    for (int i = 0; i < MAX_PHY_PAGES; i++) {
        // 使用保存的物理起始地址
        frame_table[i].pa = phy_mem_start + i * PAGE_SIZE;
        
        frame_table[i].va = 0;
        frame_table[i].pgdir = 0;
        frame_table[i].pte = NULL;
        
        list_add_tail(&frame_table[i].qnode, &free_list);
    }
}

ptr_t allocPage(int numPage) {
    if (list_empty(&free_list)) {
        swap_out(); 
    }
    
    // 二次检查，防止 swap_out 失败
    if (list_empty(&free_list)) {
         // 可以在这里 panic 或者再次 swap
         assert(0 && "Out of memory!");
    }

    list_node_t *node = free_list.next;
    list_del(node); 
    
    frame_t *frame = (frame_t *)node; 

    memset((void*)pa2kva(frame->pa), 0, PAGE_SIZE);

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

    frame->va = 0;
    frame->pgdir = 0;
    frame->pte = NULL;

    if (!list_empty(&frame->qnode)) {
        list_del(&frame->qnode);
    }

    list_add_tail(&frame->qnode, &free_list);
}

void swap_out() {
    // 这里 assert 失败是因为之前所有的页都没加进去
    if (list_empty(&fifo_queue)) {
        assert(0 && "Swap out failed: No victim page found!");
    }

    list_node_t *node = fifo_queue.next; 
    frame_t *victim = (frame_t *)node; 

    int slot = alloc_swap_slot();
    uint64_t sector = get_swap_sector(slot);

    sd_write(victim->pa, SECTORS_PER_PAGE, sector);

    if (victim->pte) {
        PTE original_pte = *victim->pte;
        uint64_t perms = original_pte & 0x3FF; 
        perms &= ~_PAGE_PRESENT; 
        *victim->pte = (slot << 10) | perms; 
    }

    local_flush_tlb_all();
    freePage(victim->pa);
}

void swap_in(PTE *pte, uintptr_t va) {
    uint64_t pte_val = *pte;
    int slot = pte_val >> 10;
    uint64_t sector = get_swap_sector(slot);

    ptr_t new_pa = allocPage(1); 

    sd_read(new_pa, SECTORS_PER_PAGE, sector);

    uint64_t perm = pte_val & 0x3FF; 
    perm |= _PAGE_PRESENT; 
    *pte = ((new_pa >> 12) << 10) | perm;

    // 使用 phy_mem_start 计算下标
    int frame_idx = (new_pa - phy_mem_start) / PAGE_SIZE;
    frame_t *frame = &frame_table[frame_idx];

    frame->va = va;
    frame->pte = pte;
    frame->pgdir = current_running[get_current_cpu_id()]->pgdir; 

    list_add_tail(&frame->qnode, &fifo_queue);

    local_flush_tlb_all();
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
            frame->va = va;
            frame->pte = &pte[vpn0];
            frame->pgdir = pgdir;

            // 确保安全添加
            if (!list_empty(&frame->qnode)) list_del(&frame->qnode);
            list_add_tail(&frame->qnode, &fifo_queue);
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
