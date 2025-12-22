#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

void *ioremap(unsigned long phys_addr, unsigned long size)
{
    // TODO: [p5-task1] map one specific physical region to virtual address
    uintptr_t pgdir = pa2kva(PGDIR_PA);
    uintptr_t start_va = io_base;
    uintptr_t offset = phys_addr % PAGE_SIZE;
    uintptr_t paddr_aligned = phys_addr - offset;
    uintptr_t size_aligned = ROUND(size + offset, PAGE_SIZE);

    io_base += size_aligned;

    for (uintptr_t i = 0; i < size_aligned; i += PAGE_SIZE) {
        uintptr_t va = start_va + i;
        uintptr_t pa = paddr_aligned + i;

        uint64_t vpn2 = (va >> 30) & 0x1ff;
        uint64_t vpn1 = (va >> 21) & 0x1ff;
        uint64_t vpn0 = (va >> 12) & 0x1ff;

        PTE *pgd = (PTE *)pgdir;
        if ((pgd[vpn2] & _PAGE_PRESENT) == 0) {
            ptr_t new_page_pa = allocPage(1);
            set_pfn(&pgd[vpn2], new_page_pa >> NORMAL_PAGE_SHIFT);
            set_attribute(&pgd[vpn2], _PAGE_PRESENT);
            clear_pgdir(pa2kva(new_page_pa));
        }

        PTE *pmd = (PTE *)pa2kva(get_pa(pgd[vpn2]));
        if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
            ptr_t new_page_pa = allocPage(1);
            set_pfn(&pmd[vpn1], new_page_pa >> NORMAL_PAGE_SHIFT);
            set_attribute(&pmd[vpn1], _PAGE_PRESENT);
            clear_pgdir(pa2kva(new_page_pa));
        }

        PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
        set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
        set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                                  _PAGE_ACCESSED | _PAGE_DIRTY);
    }

    local_flush_tlb_all();
    return (void *)(start_va + offset);
}

void iounmap(void *io_addr)
{
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
}
