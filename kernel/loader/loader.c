#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#include <common.h>

#define USER_MEM_BASE 0x52000000  
#define KERNEL_SECTORS 15         
#define TASK_SECTORS 15           
#define SECTOR_SIZE 512           
#define KERNEL_SEC 0x1fc   
#define TASKNUM    0x1fe   

static uint8_t sector0[SECTOR_SIZE] __attribute__((aligned(16)));

uint64_t load_task_img(int taskid)
{
    if (sd_read((unsigned)sector0, 1, 0) != 0) {
        bios_putstr("Read sector0 failed!\n\r");
        return 0;
    }
    uint16_t kernel_sectors = *(uint16_t *)(sector0 + KERNEL_SEC);
    uint16_t tasknum        = *(uint16_t *)(sector0 + TASKNUM);

    if (taskid < 0 || taskid >= tasknum) {
        bios_putstr("\n\rInvalid task id!\n\r");
        return 0;
    }
    int idx = taskid; 

    char info[] = "\n\rLoading task _ ...\n\r";
    for (size_t i = 0; i < strlen(info); i++)
        bios_putchar(info[i] == '_' ? ('0' + taskid) : info[i]);

    uint64_t load_addr   = USER_MEM_BASE + (uint64_t)idx * TASK_SECTORS * SECTOR_SIZE;
    unsigned start_sector = 1 + (unsigned)kernel_sectors + (unsigned)idx * TASK_SECTORS; 
    unsigned num_sectors  = TASK_SECTORS;

    if (sd_read((unsigned)load_addr, num_sectors, start_sector) != 0) {
        bios_putstr("SD read error!\n\r");
        return 0;
    }
    return load_addr;
}