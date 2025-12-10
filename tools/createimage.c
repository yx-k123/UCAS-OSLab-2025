#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_FILE "./image"
#define ARGS "[--extended] [--vm] <bootblock> <executable-file> ..."

#define SECTOR_SIZE 512
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2)       // 0x1fc..0x1fd
#define APPINFO_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 6)  // 0x1f8..0x1fb
#define TASKNUM_LOC (BOOT_LOADER_SIG_OFFSET - 8)       // 0x1f6..0x1f7
#define BOOT_LOADER_SIG_1 0x55
#define BOOT_LOADER_SIG_2 0xaa
#define BATCH_OFFSET_LOC 0x1f0                         // 0x1f0..0x1f4
#define BATCH_SIZE 512

#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] design your own task_info_t */
typedef struct {
    char name[32];      // Task name
    int offset;       // Offset in the image file
    int size;         // Size of the task
    uint64_t entry_point; // Entry point of the task
    uint64_t p_filesz;   // Size of the segment in the file
    uint64_t p_memsz;    // Memory size required
} task_info_t;

#define TASK_MAXNUM 16
static task_info_t taskinfo[TASK_MAXNUM];

/* structure to store command line options */
static struct {
    int vm;
    int extended;
} options;

/* prototypes of local functions */
static void create_image(int nfiles, char *files[]);
static void error(char *fmt, ...);
static void read_ehdr(Elf64_Ehdr *ehdr, FILE *fp);
static void read_phdr(Elf64_Phdr *phdr, FILE *fp, int ph, Elf64_Ehdr ehdr);
static uint64_t get_entrypoint(Elf64_Ehdr ehdr);
static uint32_t get_filesz(Elf64_Phdr phdr);
static uint32_t get_memsz(Elf64_Phdr phdr);
static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr);
static void write_padding(FILE *img, int *phyaddr, int new_phyaddr);
static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE *img, int batch_offset);

int main(int argc, char **argv)
{
    char *progname = argv[0];

    /* process command line options */
    options.vm = 0;
    options.extended = 0;
    while ((argc > 1) && (argv[1][0] == '-') && (argv[1][1] == '-')) {
        char *option = &argv[1][2];

        if (strcmp(option, "vm") == 0) {
            options.vm = 1;
        } else if (strcmp(option, "extended") == 0) {
            options.extended = 1;
        } else {
            error("%s: invalid option\nusage: %s %s\n", progname,
                  progname, ARGS);
        }
        argc--;
        argv++;
    }
    if (options.vm == 1) {
        error("%s: option --vm not implemented\n", progname);
    }
    if (argc < 3) {
        /* at least 3 args (createimage bootblock main) */
        error("usage: %s %s\n", progname, ARGS);
    }
    create_image(argc - 1, argv + 1);
    return 0;
}

/* TODO: [p1-task4] assign your task_info_t somewhere in 'create_image' */
static void create_image(int nfiles, char *files[])
{
    int tasknum = nfiles - 2;
    int nbytes_kernel = 0;
    int phyaddr = 0;
    int appinfo_off = -1;
    int appinfo_size = (int)(sizeof(task_info_t) * tasknum);
    printf("tasknum: %d\n", tasknum);

    FILE *fp = NULL, *img = NULL;
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;

    /* open the image file */
    img = fopen(IMAGE_FILE, "w");
    assert(img != NULL);

    /* for each input file */
    for (int fidx = 0; fidx < nfiles; ++fidx) {

        int taskidx = fidx - 2;
        int start_addr = phyaddr;
        uint64_t total_filesz = 0, total_memsz = 0;

        // 用来记录 entry 段在大镜像中的真实 offset
        uint32_t entry_seg_img_off = 0;
        uint64_t entry_point;

        /* open input file */
        fp = fopen(*files, "r");
        assert(fp != NULL);

        /* read ELF header */
        read_ehdr(&ehdr, fp);
        entry_point = ehdr.e_entry;
        printf("0x%04lx: %s\n", ehdr.e_entry, *files);

        /* for each program header */
        for (int ph = 0; ph < ehdr.e_phnum; ph++) {

            /* read program header */
            read_phdr(&phdr, fp, ph, ehdr);

            if (phdr.p_type == PT_LOAD) {
                // 记录总大小
                total_filesz += phdr.p_filesz;
                total_memsz  += phdr.p_memsz;

                // 在写 segment 前，当前 phyaddr 就是它在大镜像中的偏移
                int seg_img_off = phyaddr;

                write_segment(phdr, fp, img, &phyaddr);

                // 如果这个段包含 entry_point（最常见情况是 p_vaddr == entry）
                // 就把它的镜像偏移记下来
                if (phdr.p_vaddr <= entry_point &&
                    entry_point < phdr.p_vaddr + phdr.p_memsz) {
                    // entry_vaddr - phdr.p_vaddr = 在该段内部的偏移
                    uint64_t inner = entry_point - phdr.p_vaddr;
                    entry_seg_img_off = seg_img_off + inner;
                }
            }
        }

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
            nbytes_kernel = phyaddr - SECTOR_SIZE; // 记录内核大小
            appinfo_off = phyaddr;                 
            write_padding(img, &phyaddr, appinfo_off + appinfo_size);
        } 
        if (taskidx >= 0 && taskidx < tasknum) {
            strncpy(taskinfo[taskidx].name, *files, sizeof(taskinfo[taskidx].name) - 1);
            taskinfo[taskidx].name[sizeof(taskinfo[taskidx].name) - 1] = '\0';

            // 这里的 offset 记录的是 entry 段在大镜像中的真实 offset
            taskinfo[taskidx].offset      = entry_seg_img_off;
            taskinfo[taskidx].size        = phyaddr - start_addr;
            taskinfo[taskidx].p_filesz    = total_filesz;
            taskinfo[taskidx].p_memsz     = total_memsz;
            taskinfo[taskidx].entry_point = entry_point;
        }

        fclose(fp);
        files++;
    }
    /* padding for left space */
    fseek(img, phyaddr, SEEK_SET);
    write_padding(img, &phyaddr, NBYTES2SEC(phyaddr) * SECTOR_SIZE);

    /* padding for batch */
    int batch_offset = phyaddr;
    fseek(img, phyaddr, SEEK_SET);
    write_padding(img, &phyaddr, phyaddr + BATCH_SIZE);

    write_img_info(nbytes_kernel, taskinfo, tasknum, img, batch_offset);

    fclose(img);
}

static void read_ehdr(Elf64_Ehdr * ehdr, FILE * fp)
{
    int ret;

    ret = fread(ehdr, sizeof(*ehdr), 1, fp);
    assert(ret == 1);
    assert(ehdr->e_ident[EI_MAG1] == 'E');
    assert(ehdr->e_ident[EI_MAG2] == 'L');
    assert(ehdr->e_ident[EI_MAG3] == 'F');
}

static void read_phdr(Elf64_Phdr * phdr, FILE * fp, int ph,
                      Elf64_Ehdr ehdr)
{
    int ret;

    fseek(fp, ehdr.e_phoff + ph * ehdr.e_phentsize, SEEK_SET);
    ret = fread(phdr, sizeof(*phdr), 1, fp);
    assert(ret == 1);
    if (options.extended == 1) {
        printf("\tsegment %d\n", ph);
        printf("\t\toffset 0x%04lx", phdr->p_offset);
        printf("\t\tvaddr 0x%04lx\n", phdr->p_vaddr);
        printf("\t\tfilesz 0x%04lx", phdr->p_filesz);
        printf("\t\tmemsz 0x%04lx\n", phdr->p_memsz);
    }
}

static uint64_t get_entrypoint(Elf64_Ehdr ehdr)
{
    return ehdr.e_entry;
}

static uint32_t get_filesz(Elf64_Phdr phdr)
{
    return phdr.p_filesz;
}

static uint32_t get_memsz(Elf64_Phdr phdr)
{
    return phdr.p_memsz;
}

static void write_segment(Elf64_Phdr phdr, FILE *fp, FILE *img, int *phyaddr)
{
    if (phdr.p_memsz != 0 && phdr.p_type == PT_LOAD) {
        /* write the segment itself */
        /* NOTE: expansion of .bss should be done by kernel or runtime env! */
        if (options.extended == 1) {
            printf("\t\twriting 0x%04lx bytes\n", phdr.p_filesz);
        }
        fseek(fp, phdr.p_offset, SEEK_SET);
        while (phdr.p_filesz-- > 0) {
            fputc(fgetc(fp), img);
            (*phyaddr)++;
        }
    }
}

static void write_padding(FILE *img, int *phyaddr, int new_phyaddr)
{
    if (options.extended == 1 && *phyaddr < new_phyaddr) {
        printf("\t\twrite 0x%04x bytes for padding\n", new_phyaddr - *phyaddr);
    }

    while (*phyaddr < new_phyaddr) {
        fputc(0, img);
        (*phyaddr)++;
    }
}

static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE * img, int batch_offset)
{
    uint16_t kernel_bytes = (uint16_t)nbytes_kernel;
    uint16_t tnum = (uint16_t)tasknum;
    uint32_t appinfo_off = (uint32_t)(SECTOR_SIZE + nbytes_kernel);

    // OS 占用字节数放在 0x1fc..0x1fd
    fseek(img, OS_SIZE_LOC, SEEK_SET);
    fwrite(&kernel_bytes, sizeof(kernel_bytes), 1, img);
    printf("kernel size: %d bytes\n", kernel_bytes);

    // 任务数量放在 0x1f6..0x1f7，避免覆盖 0x55AA
    fseek(img, TASKNUM_LOC, SEEK_SET);
    fwrite(&tnum, sizeof(tnum), 1, img);
    printf("task num: %d\n", tnum);

    // appinfo 偏移放在 0x1f8..0x1fb（4 字节）
    fseek(img, APPINFO_SIZE_LOC, SEEK_SET);
    fwrite(&appinfo_off, sizeof(appinfo_off), 1, img);
    printf("appinfo off: %d bytes\n", appinfo_off);

    // 写入taskinfo数组
    fseek(img, appinfo_off, SEEK_SET);
    fwrite(taskinfo, sizeof(task_info_t), tasknum, img);
    printf("appinfo size: %d bytes\n", (int)(sizeof(task_info_t) * tasknum));

    // batch offset 放在 0x1f0..0x1f4（4 字节）
    fseek(img, BATCH_OFFSET_LOC, SEEK_SET);
    fwrite(&batch_offset, sizeof(batch_offset), 1, img);
    printf("batch offset: %d bytes\n", batch_offset);
}

/* print an error message and exit */
static void error(char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    if (errno != 0) {
        perror(NULL);
    }
    exit(EXIT_FAILURE);
}
