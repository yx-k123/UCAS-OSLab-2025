/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include "os/list.h"
#include <type.h>
#include <pgtable.h>
#include <assert.h>
#include <common.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK 0xffffffc052000000
#define FREEMEM_KERNEL (INIT_KERNEL_STACK+PAGE_SIZE)

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

extern ptr_t allocPage(int numPage);
// TODO [P4-task1] */
void freePage(ptr_t baseAddr);
void free_page_helper(uintptr_t pgdir);

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR 0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

// TODO [P4-task1] */
extern void* kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);

// TODO [P4-task4]: shm_page_get/dt */
uintptr_t shm_page_get(int key);
void shm_page_dt(uintptr_t addr);

typedef struct frame {
    list_node_t qnode;   // FIFO 队列节点
    
    uintptr_t   pa;      // 该帧对应的物理地址 (用于 sd_write)
    uintptr_t   va;      // 对应的虚拟地址
    
    // 进程标识
    // 存 asid 或者 pgdir 都可以，主要用于 flush_tlb 时区分是哪个进程的
    uintptr_t   pgdir;   
    
    // 核心指针
    PTE        *pte;     // 指向页表项
} frame_t;

#define MAX_PHY_PAGES 200
extern frame_t frame_table[MAX_PHY_PAGES];
extern list_head clock_queue;
extern list_head free_list;

void pmm_init();
void swap_out();
void swap_in(PTE *pte, uintptr_t va);

// 假设 SD 卡从第 20000 个扇区开始用作 swap，避免覆盖内核或文件系统
#define SWAP_START_SEC 20000
// 每个页需要 8 个扇区 (4096 / 512 = 8)
#define SECTORS_PER_PAGE 8

// 简单的 swap 槽位分配器
static int swap_idx = 0;
int alloc_swap_slot();

// 根据槽位号计算 SD 卡扇区号
uint64_t get_swap_sector(int slot);

PTE *get_pte(uintptr_t pgdir, uintptr_t va);

#endif /* MM_H */
