#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0x52000000
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000

#define SECTOR_SIZE 512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    char name[32];      // Task name
    int offset;       // Offset in the image file
    int size;         // Size of the task
    uint64_t entry_point; // Entry point of the task
    uint64_t p_filesz;   // Size of the segment in the file
    uint64_t p_memsz;    // Memory size required
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];
extern int task_num;

#endif