#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50
#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)
#define APPINFO_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 6)
#define TASKNUM_LOC (BOOT_LOADER_SIG_OFFSET - 8)
#define BATCH_OFFSET_LOC 0x1f0
#define BATCH_AREA_SIZE  512   // 保证为 SECTOR_SIZE 的整数倍且镜像中已预留

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];

static void int_to_str(int num, char *str)
{
    char *p = str;
    char *p1, *p2;
    int is_negative = 0;

    // Handle negative numbers
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    // Convert integer to string
    do {
        *p++ = (num % 10) + '0';
        num /= 10;
    } while (num > 0);

    // Add negative sign if needed
    if (is_negative) {
        *p++ = '-';
    }

    *p = '\0';

    // Reverse the string
    p1 = str;
    p2 = p - 1;
    while (p1 < p2) {
        char temp = *p1;
        *p1 = *p2;
        *p2 = temp;
        p1++;
        p2--;
    }
}

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
}

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
    // bios_putstr("os_size: ");
    // int_to_str(os_size, num_str);
    // bios_putstr(num_str);
    // bios_putstr("\n\rappinfo_size: ");
    // int_to_str(appinfo_size, num_str);
    // bios_putstr(num_str);
    // bios_putstr("\n\rtasknum: ");
    // int_to_str(tasknum, num_str);
    // bios_putstr(num_str);
    // bios_putstr("\n\r");

    int appinfo_off = SECTOR_SIZE + os_size;
    int max_bytes   = TASK_MAXNUM * (int)sizeof(task_info_t);
    int read_bytes  = appinfo_size > max_bytes ? max_bytes : appinfo_size;

    bios_putstr("\n\rappinfo_off: ");
    int_to_str(appinfo_off, num_str);
    bios_putstr(num_str);
    bios_putstr("\n\rread_bytes: ");
    int_to_str(read_bytes, num_str);
    bios_putstr(num_str);
    bios_putstr("\n\r");

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

static void print_task_names(void)
{
    bios_putstr("Available tasks:\n\r");
    for (int i = 0; i < 8; ++i)
    {
        if (tasks[i].name[0] != '\0') 
        {
            bios_putstr(" - ");
            bios_putstr(tasks[i].name);
            bios_putstr("\n\r");
        }
    }
}

static int task_exists(const char *name) {
    for (int i = 0; i < TASK_MAXNUM; ++i) {
        if (tasks[i].name[0] == '\0') break;
        if (!strcmp(tasks[i].name, name)) return 1;
    }
    return 0;
}

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
        if (used + n + 1 >= sizeof(out)) { bios_putstr("batch-write: too long\n\r"); return; }
        memcpy((uint8_t *)out + used, (const uint8_t *)name, n);
        used += n;
        out[used++] = ' ';
    }
    if (used && out[used-1]==' ') out[--used] = '\n';

    unsigned blk = (unsigned)(batch_off / SECTOR_SIZE);
    unsigned cnt = (unsigned)(BATCH_AREA_SIZE / SECTOR_SIZE);

    // bios_putstr("batch_off: ");
    // int_to_str(batch_off, buf);
    // bios_putstr(buf);
    // bios_putstr("\n\r");

    // bios_putstr("BATCH_AREA_SIZE: ");
    // int_to_str(BATCH_AREA_SIZE, buf);
    // bios_putstr(buf);
    // bios_putstr("\n\r");

    if (sd_write((unsigned)(uintptr_t)out, cnt, blk) < 0) {
        bios_putstr("batch-write: sd_write failed\n\r");
        return;
    }

    // bios_putstr("sd_write: mem_address=");
    // int_to_str((unsigned)(uintptr_t)out, buf);
    // bios_putstr(buf);
    // bios_putstr(", cnt=");
    // int_to_str(cnt, buf);
    // bios_putstr(buf);
    // bios_putstr(", blk=");
    // int_to_str(blk, buf);
    // bios_putstr(buf);
    // bios_putstr("\n\r");

    // int ret = sd_write((unsigned)(uintptr_t)out, cnt, blk);
    // if (ret < 0) {
    //     bios_putstr("sd_write failed with code ");
    //     int_to_str(ret, buf);
    //     bios_putstr(buf);
    //     bios_putstr("\n\r");
    // }

    bios_putstr("batch written\n\r");
}

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


/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(void)
{
    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    print_task_names();

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {   
        bios_putstr("\n\rEnter task name: "); 

        char task_name[32];
        int task_name_len = 0;

        while (1) {
            char ch = port_read_ch();
            if (ch == '\r' || ch == '\n') {
                task_name[task_name_len] = '\0';
                break;
            } else if (ch == '\b' && task_name_len > 0) {
                task_name_len--;
                port_write_ch('\b');
                port_write_ch(' ');
                port_write_ch('\b');
            } else if (ch >= ' ' && ch <= '~' && task_name_len < 31) {
                task_name[task_name_len++] = ch;
                port_write_ch(ch);
            }
        }

        if (!strcmp(task_name, "ls")) {
            bios_putstr("\n\r");
            print_task_names();
            continue;
        }
        if (!strcmp(task_name, "batch-write")) {
            bios_putstr("\n\r");
            batch_write();
            continue;
        }
        if (!strcmp(task_name, "batch-run")) {
            bios_putstr("\n\r");
            batch_run();
            continue;
        }

        uint64_t entry = load_task_img(task_name);
        if (entry) {
            void (*task_entry)() = (void (*)())entry;
            task_entry();
        } else {
            bios_putstr("\n\rFailed to load task!");
        }
    }

    return 0;
}
