#include "os/smp.h"
#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#include <common.h>
#include <os/mm.h>
#include <pgtable.h>
#include <printk.h>

#define PAGE_SIZE 4096
#define CPU_NUM 2

extern task_info_t tasks[TASK_MAXNUM];

// 每个 CPU 一个对齐缓冲区
static uint8_t buffer[CPU_NUM][PAGE_SIZE + SECTOR_SIZE] __attribute__((aligned(16)));

uint64_t load_task_img(char *taskname, uintptr_t pgdir)
{
    for (int i = 0; i < TASK_MAXNUM; ++i) {
        if (strcmp(tasks[i].name, taskname) != 0)
            continue;

        uint64_t va_start   = tasks[i].entry_point; // 0x10000
        uint64_t p_memsz    = tasks[i].p_memsz;
        uint64_t p_filesz   = tasks[i].p_filesz;
        uint64_t file_off   = tasks[i].offset;      // 在 image 中的偏移
        int      cpu_id     = get_current_cpu_id();

        for (uint64_t va = va_start; va < va_start + p_memsz; va += PAGE_SIZE) {
            uintptr_t kva = alloc_page_helper(va, pgdir);
            uint64_t offset_in_task = va - va_start;

            if (offset_in_task < p_filesz) {
                uint64_t left   = p_filesz - offset_in_task;
                uint64_t copy_len = left > PAGE_SIZE ? PAGE_SIZE : left;

                uint64_t cur_file_off = file_off + offset_in_task;
                int block_id  = cur_file_off / SECTOR_SIZE;
                int inblk_off = cur_file_off % SECTOR_SIZE;
                int num_blks  = (inblk_off + copy_len + SECTOR_SIZE - 1) / SECTOR_SIZE;

                sd_read(kva2pa((uintptr_t)buffer[cpu_id]), num_blks, block_id);
                memcpy((void *)kva, buffer[cpu_id] + inblk_off, copy_len);

                if (copy_len < PAGE_SIZE) {
                    memset((void *)(kva + copy_len), 0, PAGE_SIZE - copy_len);
                }
            } else {
                memset((void *)kva, 0, PAGE_SIZE);
            }
        }

        return tasks[i].entry_point;
    }

    return 0;
}