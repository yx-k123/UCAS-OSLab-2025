#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#include <common.h>

extern task_info_t tasks[TASK_MAXNUM];

static void *my_memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i != 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

uint64_t load_task_img(char *taskname)
{   
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */
    for (int i = 0; i < TASK_MAXNUM; ++i) {
        if (strcmp(tasks[i].name, taskname) == 0) {
            bios_putstr("\n\rLoading task: ");
            bios_putstr(tasks[i].name);
            bios_putstr("\n\r");

            uint64_t task_entry = TASK_MEM_BASE + TASK_SIZE * i;

            int block_id = tasks[i].offset / SECTOR_SIZE;
            int inblk_off  = tasks[i].offset % SECTOR_SIZE;

            unsigned total_bytes  = inblk_off + tasks[i].size;
            unsigned num_of_blks  = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

            if (sd_read((unsigned)task_entry, (unsigned)num_of_blks, (unsigned)block_id) < 0) {
                bios_putstr("\n\rFailed to load task!");
                return 0;
            }
            if (inblk_off != 0) {
                my_memmove((void *)task_entry,(void *)(task_entry + inblk_off),tasks[i].size);
            }
            return task_entry;
        }
    }

    bios_putstr("\n\rTask not found!");
    return 0;
}