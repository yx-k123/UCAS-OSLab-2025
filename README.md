# Project1:
## 地址空间
本实验当前阶段各关键内存/镜像布局（物理地址）说明：

| 区域/符号 | 物理地址 | 大小 / 说明 | 产生方式 |
|-----------|----------|-------------|----------|
| BIOS 功能入口 (bios_func_entry) | 0x50150000 | 固定（课程环境提供） | 通过 `jalr` 调用 BIOS API |
| Bootblock (第 0 扇区) | 0x50200000 | 512B | QEMU/U-Boot 读取到内存并执行 |
| os_size_loc | 0x502001FC | 2 字节 (uint16, 单位=扇区) | createimage 在镜像末 4B 中写入，bootblock 用 `lh` 读取 |
| Kernel 加载基址 (kernel) | 0x50201000 | 若干扇区（os_size 指定） | bootblock 通过 BIOS_SDREAD 搬运 |
| `_start` (head.S) | 0x50201000 | 内核入口 (.entry_function 放在 .text 前) | 链接脚本 `TEXT_START` |
| 内核栈顶 (KERNEL_STACK) | 0x50500000 | 向下增长 | head.S: `la sp, KERNEL_STACK` |

## 任务1：第一个引导块的制作
`msg_print_boot`定义：
```asm
.data
msg_print_boot: .string "It's kouyixin's bootblock...\n\r"
```

使用 BIOS_PUTSTR 功能号，通过 BIOS 打印字符串 "It's kouyixin's bootblock...":
```asm
...
la a0, msg_print_boot    # 加载字符串地址到 a0
li a7, BIOS_PUTSTR       # 功能号：BIOS_PUTSTR
la t0, bios_func_entry   # BIOS 功能入口地址
jalr t0                  # 跳转到 BIOS
...
```
注意：关于 API 规则可以参看 [common.c](./arch/riscv/bios/common.c) 文件。

## 任务2：加载和初始化内存

### [bootblock.S](./arch/riscv/boot/bootblock.S)
将起始于 SD 卡第二个扇区的 kernel 代码段移动至内存并跳转到 kernel：
```asm
...
la a0, kernel            # 目标内存地址
la t1, os_size_loc
lh a1, 0(t1)             # 读取 OS 大小
li a2, 1                 # 起始块号
li a7, BIOS_SDREAD       # 功能号
la t0, bios_func_entry
jalr t0

la t0, kernel
jalr t0
...
```
需要注意的是，内存扇区是从 0 开始计数，所以第二扇区使用的起始块号是 1，笔者在此卡住很久 QAQ。

对于读取 OS 大小，如果使用 `lw` 会报错 "size 太大超出扇区的范围"，这是因为 `os_size_loc` 位于镜像的第一个扇区的倒数第 4 个字节（物理地址 `0x502001FC`），它存储的是 2 字节的扇区数（单位为扇区）。因此，必须使用 `lh` 指令读取这 2 字节数据，确保读取到的值正确：

---

### [head.S](./arch/riscv/kernel/head.S)
清空 BSS 段，设置栈指针，跳转到内核 main 函数：
```asm
...
  la t0, __bss_start
  la t1, __BSS_END__
  bge t0, t1, set_stack  
clear_bss:
  sw zero, 0(t0)
  addi t0, t0, 4
  blt t0, t1, clear_bss

  /* TODO: [p1-task2] setup C environment */
set_stack:
  la sp, KERNEL_STACK

step_main:
  jal ra, main
...
```

---

### [main.c](./init/main.c)
打印 "bss check: t version: X" 之后，调用跳转表 API 读取键盘输入，并回显到屏幕上：
```C
...
while (1)
{   
    int ch = bios_getchar();
    if (ch != -1) {
        bios_putchar(ch);
    }
    // asm volatile("wfi");
}
...
```
如果键盘没有任何键被按下，会返回-1；如果有某个键被按下，则返回对应的 ASCII 码。因此，在做键盘输入相关的动作时需要处理掉-1 的情况，避免将-1当作真正的输入直接使用，否则屏幕上会看到很奇怪的输出（持续输出一个乱码）。

## 任务3：加载并选择启动多个用户程序之一

该任务为任务4的简化版，故此处不赘述，详见任务4说明。

## 任务4：镜像文件的紧密排列
为了节约空间，镜像文件需要紧密排列。具体来说，除了 bootblock 占用第 0 扇区外，kernel 和 task1、task2... 需要在第 1 扇区及之后的扇区中连续排列。
同时我们希望用task1、task2...的名字来区分不同的用户程序，而不是用 task1.bin、task2.bin... 这种不太友好的文件名。

故首先，我们需要定义一个数据集用来存储用户程序的信息（名字、起始扇区、大小）：
```c
// createimage.c
typedef struct {
    char name[32];      // Task name
    int offset;       // Offset in the image file
    int size;         // Size of the task
} task_info_t;

#define TASK_MAXNUM 16
static task_info_t taskinfo[TASK_MAXNUM];
```

自然的，我们需要考虑，该怎么把这些信息存储到镜像文件中呢？一种简单的办法：效仿kernel信息的存储，我们将这些信息在镜像文件中的偏移量和大小写入到 bootblock 的最后几个字节中，这样 bootblock 就可以读取到这些信息了。
很好的是，通过 readelf/objdump 我们可以看到在 bootblock 的最后 1f0-1ff 的位置都是空的，因此我们可以将这些信息存储在这里。

```c
// createimage.c
/* write padding bytes */
/**
* TODO:
* 1. [p1-task3] do padding so that the kernel and every app program
*  occupies the same number of sectors
* 2. [p1-task4] only padding bootblock is allowed!
*/
if (strcmp(*files, "bootblock") == 0) {
    write_padding(img, &phyaddr, SECTOR_SIZE);
} 
if (strcmp(*files, "main") == 0) {
    appinfo_off = phyaddr;                 
    write_padding(img, &phyaddr, appinfo_off + appinfo_size);
} 
if (taskidx >= 0 && taskidx < tasknum) {
    strncpy(taskinfo[taskidx].name, *files, sizeof(taskinfo[taskidx].name) - 1);
    taskinfo[taskidx].name[sizeof(taskinfo[taskidx].name) - 1] = '\0';
    taskinfo[taskidx].offset = start_addr;
    taskinfo[taskidx].size = phyaddr - start_addr;
    printf("task %d: %s, offset: %d, size: %d\n", taskidx, taskinfo[taskidx].name,
           taskinfo[taskidx].offset, taskinfo[taskidx].size);
}
```

注意到，在kernel之后笔者紧跟着padding了一段空间，这是用来写入taskinfo数组的信息的。

同时，由于扇区是以512B为单位的读取的，因此我们需要在最后把bootblock补齐到512B的整数倍：
```c
// createimage.c
/* padding for left space */
fseek(img, phyaddr, SEEK_SET);
write_padding(img, &phyaddr, NBYTES2SEC(phyaddr) * SECTOR_SIZE);
```

由于taskinfo的偏移量和bootblock一起写入到了镜像文件中，因此我们就不需要修改bootblock.S了。接下来，我需要考虑如何让内核读取这些信息。
```c
// main.c
static void init_task_info(void)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    uint8_t bootsec[SECTOR_SIZE];
    if (sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }

    int os_size = 0;
    int appinfo_size = 0;
    int tasknum = 0;

    memcpy((uint8_t *)&os_size,      bootsec + OS_SIZE_LOC,                 sizeof(short));
    memcpy((uint8_t *)&appinfo_size, bootsec + APPINFO_SIZE_LOC,            sizeof(int));
    memcpy((uint8_t *)&tasknum,      bootsec + TASKNUM_LOC, sizeof(short));

    char num_str[16];

    int appinfo_off = SECTOR_SIZE + os_size;
    int max_bytes   = TASK_MAXNUM * (int)sizeof(task_info_t);
    int read_bytes  = appinfo_size > max_bytes ? max_bytes : appinfo_size;

    read_bytes = (read_bytes / (int)sizeof(task_info_t)) * (int)sizeof(task_info_t);
    if (read_bytes <= 0)
        return;

    unsigned head_off   = (unsigned)(appinfo_off % SECTOR_SIZE);
    unsigned start_lba  = (unsigned)(appinfo_off / SECTOR_SIZE);
    unsigned total_bytes = head_off + (unsigned)read_bytes;
    unsigned nsec       = NBYTES2SEC(total_bytes);

    uint8_t tmpbuf[2 * SECTOR_SIZE + TASK_MAXNUM * sizeof(task_info_t)];
    if (sd_read((unsigned)(uintptr_t)tmpbuf, nsec, start_lba) < 0) {
        bios_putstr("sd_read app-info failed\n\r");
        return;
    }
    memcpy((uint8_t *)tasks, tmpbuf + head_off, (size_t)read_bytes);
}
```
整体思路就是按照先将bootblock整个搬到缓冲区，读取bootblock中存储的偏移量和大小，计算出taskinfo的起始扇区和需要读取的扇区数，然后将这些扇区搬到缓冲区，最后将taskinfo搬到tasks数组中。
这里需要注意的是，taskinfo的起始位置未必是对齐的，因此缓冲区需要多申请两个扇区的空间以防万一。

接着就是选择启动哪个用户程序了，只需要实现一个读取用户输入的函数即可，此处略。

关键在于如何加载用户程序：
```c
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

            int task_entry = TASK_MEM_BASE + TASK_SIZE * i;

            int block_id = tasks[i].offset / SECTOR_SIZE;
            int inblk_off  = tasks[i].offset % SECTOR_SIZE;

            unsigned total_bytes  = inblk_off + tasks[i].size;
            unsigned num_of_blks  = (total_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

            if (sd_read((unsigned)task_entry, (unsigned)num_of_blks, (unsigned)block_id) < 0) {
                bios_putstr("\n\rFailed to load task!");
                return 0;
            }

            return task_entry + inblk_off;
        }
    }

    bios_putstr("\n\rTask not found!");
    return 0;
}
```
我们遍历tasks数组，找到对应名字的用户程序，然后计算出它的起始扇区和偏移量，最后将它搬到内存中指定的位置。这里每个用户程序都被设置了固定的位置（根据任务书）。
此处需要注意的是在返回函数入口地址的时候需要加上偏移量，因为用户程序的起始位置未必是对齐的。

回到main函数中，调用load_task_img函数加载用户程序，并跳转到它的入口地址：
```c
...
// main.c
uint64_t entry = load_task_img(task_name);
    if (entry) {
        void (*task_entry)() = (void (*)())entry;
        task_entry();
    } else {
        bios_putstr("\n\rFailed to load task!");
    }
...
```
将内存地址转化为函数指针调用即可。

这样就完成了任务4的要求。

## 任务5：列出用户程序和批处理

首先是列出任务程序，这是很好实现的，只需要定义一个函数打印tasks数组即可，这里笔者偷懒直接在函数中使用了数组有效元素个数作为循环上限，实际上最好是定义一个全局变量存储这个值。

其次是批处理功能的实现，任务要求设定一块镜像可以存储一段批处理文本，且要求在内核中可写可读。对于批处理程序的存储，笔者的思路是复用taskinfo数组，这样在写入的时候还可以使用数组的字段作为参数
写入批处理文本。对于批处理文本的位置，笔者选择将它放在用户程序之后，同时为了便于读取，将它放在一个单独的扇区中，大小为一个扇区（512B）。他的偏移量和大小同样可以仿照前文存储在 bootblock 的最后几个字节中。
这里就不多赘述了，关键代码如下：

```c
// main.c
static void batch_write(void)
{
    uint8_t bootsec[SECTOR_SIZE];
    if (sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }

    int batch_off = 0;
    memcpy((uint8_t *)&batch_off, bootsec + BATCH_OFFSET_LOC, sizeof(int));
    if (batch_off <= 0) { bios_putstr("no batch offset found\n\r"); return; }

    bios_putstr("Enter batch (task names, space separated): ");
    char line[256]; int len = 0;
    while (1) {
        char ch = port_read_ch();
        if (ch == '\r' || ch == '\n') { line[len] = '\0'; bios_putstr("\n\r"); break; }
        if (ch >= ' ' && ch <= '~' && len < (int)sizeof(line) - 1) { line[len++] = ch; port_write_ch(ch); }
    }

    static char out[BATCH_AREA_SIZE];
    memset((uint8_t *)out, 0, sizeof(out));
    unsigned used = 0;

    const char *p = line; char name[64];
    while (*p) {
        while (*p==' '||*p=='\t') ++p;
        if (!*p) break;
        int k = 0;
        while (*p && *p!=' ' && *p!='\t' && k < (int)sizeof(name)-1) name[k++] = *p++;
        name[k] = '\0';

        if (!task_exists(name)) {
            bios_putstr("batch-write: no such task: "); bios_putstr(name); bios_putstr("\n\r");
            return;
        }
        unsigned n = (unsigned)strlen(name);
        if (used + n + 1 >= sizeof(out)) { 
            bios_putstr("batch-write: too long\n\r"); 
            return; 
        }
        memcpy((uint8_t *)out + used, (const uint8_t *)name, n);
        used += n;
        out[used++] = ' ';
    }
    if (used && out[used-1]==' ') 
        out[--used] = '\n';

    unsigned blk = (unsigned)(batch_off / SECTOR_SIZE);
    unsigned cnt = (unsigned)(BATCH_AREA_SIZE / SECTOR_SIZE);

    if (sd_write((unsigned)(uintptr_t)out, cnt, blk) < 0) {
        bios_putstr("batch-write: sd_write failed\n\r");
        return;
    }

    bios_putstr("batch written\n\r");
}
```

```c
// main.c
static void batch_run(void)
{
    uint8_t bootsec[SECTOR_SIZE];
    if (sd_read((unsigned)(uintptr_t)bootsec, 1, 0) < 0) {
        bios_putstr("sd_read boot sector failed\n\r");
        return;
    }
    int batch_off = 0;
    memcpy((uint8_t *)&batch_off, bootsec + BATCH_OFFSET_LOC, sizeof(int));
    if (batch_off <= 0) { 
        bios_putstr("no batch offset found\n\r"); return; 
    }

    static char buf[BATCH_AREA_SIZE];
    memset((uint8_t *)buf, 0, sizeof(buf));
    unsigned blk = (unsigned)(batch_off / SECTOR_SIZE);
    unsigned cnt = (unsigned)(BATCH_AREA_SIZE / SECTOR_SIZE);
    if (sd_read((unsigned)(uintptr_t)buf, cnt, blk) < 0) {
        bios_putstr("batch-run: sd_read failed\n\r");
        return;
    }

    char *p = buf;
    while (*p) {
        while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') ++p;
        if (!*p) break;
        char *s = p;
        while (*p && *p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n') ++p;
        char c = *p; *p = 0;

        if (!task_exists(s)) {
            bios_putstr("batch-run: no such task: "); bios_putstr(s); bios_putstr("\n\r");
            *p = c; return;
        }
        bios_putstr("Run: "); 
        bios_putstr(s); 
        uint64_t entry = load_task_img(s);
        if (!entry) { 
            bios_putstr("load failed\n\r"); *p = c; return; 
        }
        ((void(*)(void))entry)();
        bios_putstr("\n\r");
        *p = c;
    }
    bios_putstr("batch done\n\r");
}
```
批处理的写入和运行逻辑大致相同，都是先读取bootblock获取偏移量，然后搬运到缓冲区，最后进行处理。不同的是，写入时需要从键盘获取输入并存储到缓冲区，而运行时则是解析缓冲区中的内容并逐个加载运行用户程序。
笔者这里写入的格式是以空格分隔的任务名列表，运行时会逐个加载运行这些任务。记得在main主函数中添加对这两个函数的调用以及命令行解析。

最后，任务要求我们依次启动四个测试程序，对一个数进行操作并输出。这里只需要多增加四个简单的task就行了。