#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>
#include <os/kernel.h>
#include <printk.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

#define FS_BLOCK_SIZE 4096
#define DISK_SECTOR_SIZE 512
#define SECTORS_PER_BLOCK (FS_BLOCK_SIZE / DISK_SECTOR_SIZE)
#define MAX_FILE_NAME 24
#define MAX_DATA_BLOCKS 12
#define FS_SIZE (1024 * 1024 * 1024) // 1 GB

#define IM_REG 1
#define IM_DIR 2

// #define KERNEL_START_SEC 0
// #define SWAP_START_SEC 20000
#define FS_START_SEC 200000 // from 100MB
#define SUPER_BLOCK_SIZE SECTOR_SIZE

/* data structures of file system */
typedef struct superblock {
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic_number;      // 用于识别这是否是你的文件系统
    uint32_t total_blocks;      // 磁盘总块数
    uint32_t total_inodes;      // Inode 总数（决定了能存多少个文件）
    
    uint32_t block_size;        // 每个块的大小 4096
    uint32_t inode_size;        // 每个 Inode 的大小（字节），如 128 或 256
    uint32_t dentry_size;       // 

    uint32_t free_block_count;  // 当前空闲块数量
    uint32_t free_inode_count;  // 当前空闲 Inode 数量

    uint32_t inode_bitmap_start; // Inode 位图在磁盘的起始块号
    uint32_t block_bitmap_start; // 数据块位图在磁盘的起始块号
    uint32_t inode_table_start;  // Inode 表在磁盘的起始块号
    uint32_t data_start_block;   // 数据区在磁盘的起始块号

    uint32_t root_inode;         // 根目录的 Inode 编号 1
} superblock_t;

typedef struct dentry {
    // TODO [P6-task1]: Implement the data structure of directory entry
    uint32_t inode_number;  // 该文件的 Inode 编号
    uint16_t entry_len;     // 这个目录项的总长度
    uint8_t  name_len;      // 文件名长度
    uint8_t  file_type;     // 辅助标记（是文件还是目录，加速 `ls` 命令）
    char     file_name[MAX_FILE_NAME];   // 文件名
} dentry_t;

typedef struct inode { 
    // TODO [P6-task1]: Implement the data structure of inode
    uint16_t file_mode;    // 文件类型（文件/目录/链接）及权限（rwx）
    uint32_t file_size;    // 文件大小（字节数）
    uint16_t link_count;   // 硬链接计数（有多少个文件名指向这个 Inode）

    uint32_t direct_blocks[12];   // 直接指针：直接指向存数据的块号
    uint32_t single_indirect;     // 一级间接指针
    uint32_t double_indirect;     // 二级间接指针
    // uint32_t triple_indirect;  // 三级间接指针
} inode_t;

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
} fdesc_t;

/* modes of do_open */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* fs function declarations */
extern int do_mkfs(void);
extern int do_statfs(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_open(char *path, int mode);
extern int do_read(int fd, char *buff, int length);
extern int do_write(int fd, char *buff, int length);
extern int do_close(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);

extern void fs_write_block(uint32_t block_num, const void *buf);
extern void fs_read_block(uint32_t block_num, void *buf);
#endif