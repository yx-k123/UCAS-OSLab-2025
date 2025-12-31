#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

#define BLOCK_SIZE 4096
#define SECTOR_SIZE 512
#define MAX_FILE_NAME 28
#define MAX_DATA_BLOCKS 12

#define IM_REG 1
#define IM_DIR 2

// #define KERNEL_START_SEC 0
// #define SWAP_START_SEC 20000
#define FS_START_SEC 200000 // from 100MB
#define SUPER_BLOCK_SIZE SECTOR_SIZE

/* data structures of file system */
typedef struct superblock {
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic;
    uint32_t size;
    uint32_t start_sector;
    
    uint32_t block_map_offset;
    uint32_t inode_map_offset;
    uint32_t inode_offset;
    uint32_t data_offset;
    
    uint32_t inode_count;
    uint32_t block_count;
} superblock_t;

typedef struct dentry {
    // TODO [P6-task1]: Implement the data structure of directory entry
    char name[MAX_FILE_NAME];
    uint32_t ino;
} dentry_t;

typedef struct inode { 
    // TODO [P6-task1]: Implement the data structure of inode
    uint32_t mode;          // IM_REG or IM_DIR
    uint32_t nlinks;        // Hard link count
    uint32_t size;          // File size in bytes
    uint32_t blocks[MAX_DATA_BLOCKS]; // Direct pointers
    uint32_t indirect;   // Optional for larger files
} inode_t;

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
    uint8_t  used;          // Is this descriptor active?
    uint32_t ino;           // Inode number
    uint32_t pos;           // Current file offset
    uint32_t mode;          // O_RDONLY, etc
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

#endif