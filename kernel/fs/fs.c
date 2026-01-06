#include "pgtable.h"
#include <os/string.h>
#include <os/fs.h>

superblock_t current_superblock;

int do_mkfs(void)
{
    // TODO [P6-task1]: Implement do_mkfs
    superblock_t sb;
    memset(&sb, 0, sizeof(superblock_t));
    printk("[FS]: Start initialize filesystem!\n");
    printk("[FS]: Setting superblock...\n");

    // Initialize superblock fields
    sb.magic_number = SUPERBLOCK_MAGIC;
    sb.block_size = FS_BLOCK_SIZE;
    sb.inode_size = sizeof(inode_t);
    sb.dentry_size = sizeof(dentry_t);
    sb.total_blocks = FS_SIZE / FS_BLOCK_SIZE;

    printk("      Magic number: 0x%X\n", sb.magic_number);
    printk("      Total sectors: %u, Start sector: %u\n", sb.total_blocks * SECTORS_PER_BLOCK, FS_START_SEC);

    // 0:superblock + 1:inode_bitmap + 2:block_bitmap + 3-N:inode_table + N+1:data_blocks
    sb.inode_bitmap_start = 1; 
    sb.block_bitmap_start = 2; 
    sb.inode_table_start  = 3;
    
    printk("      Inode map offset: %d\n", sb.inode_bitmap_start);
    printk("      Block map offset: %d\n", sb.block_bitmap_start);
    printk("      Inode offset: %d\n", sb.inode_table_start);

    sb.total_inodes = 4096; 
    int inode_table_blocks = (sb.total_inodes * sizeof(inode_t) + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    sb.data_start_block = sb.inode_table_start + inode_table_blocks;

    printk("      Data offset: %d\n", sb.data_start_block);
    printk("      Inode entry size: %d\n", sb.inode_size);
    printk("      dir entry size: %d\n", sb.dentry_size);

    sb.free_inode_count = sb.total_inodes - 1;
    sb.free_block_count = sb.total_blocks - sb.data_start_block - 1;
    sb.root_inode = 1;

    static char zero_buf[FS_BLOCK_SIZE];
    memset(zero_buf, 0, FS_BLOCK_SIZE);
    
    fs_write_block(sb.block_bitmap_start, (const void *)zero_buf);
    for (int i = 0; i < inode_table_blocks; i++) {
        fs_write_block(sb.inode_table_start + i, zero_buf);
    }

    static uint8_t bitmap_buf[FS_BLOCK_SIZE];
    memset(bitmap_buf, 0, FS_BLOCK_SIZE); // 确保每次使用前清零
    
    bitmap_buf[0] |= 0x01;
    fs_write_block(sb.inode_bitmap_start, (const void *)bitmap_buf);
    printk("[FS]: Setting inode bitmap...\n");

    memset(bitmap_buf, 0, FS_BLOCK_SIZE);
    bitmap_buf[0] |= 0x01; 
    fs_write_block(sb.block_bitmap_start, (const void *)bitmap_buf);
    printk("[FS]: Setting block bitmap...\n");

    inode_t root_inode;
    memset(&root_inode, 0, sizeof(inode_t));
    root_inode.file_mode = IM_DIR;
    root_inode.file_size = 2 * sizeof(dentry_t); // . and ..
    root_inode.link_count = 2; // . and ..
    root_inode.direct_blocks[0] = sb.data_start_block;
    memset(zero_buf, 0, FS_BLOCK_SIZE);
    memcpy((uint8_t *)zero_buf, (uint8_t *)&root_inode, sizeof(inode_t));
    fs_write_block(sb.inode_table_start, zero_buf);

    printk("[FS]: Setting inode...\n");

    dentry_t dentries[2];
    strcpy(dentries[0].file_name, ".");
    dentries[0].inode_number = 1;
    dentries[0].file_type = IM_DIR;

    strcpy(dentries[1].file_name, "..");
    dentries[1].inode_number = 1; // 根目录的父目录是自己
    dentries[1].file_type = IM_DIR;

    memset(zero_buf, 0, FS_BLOCK_SIZE);
    memcpy(zero_buf, (const void *)dentries, sizeof(dentries));
    fs_write_block(sb.data_start_block, zero_buf);
    printk("[FS]: Setting root directory...\n");

    // 写入 Superblock
    // 写入块 0
    fs_write_block(0, &sb);
    printk("[FS]: Writing superblock to disk...\n");

    // 更新缓存
    current_superblock = sb; 
    printk("[FS]: Filesystem initialized successfully!\n");

    return 0;  // do_mkfs succeeds
}

int do_statfs(void)
{
    // TODO [P6-task1]: Implement do_statfs

    return 0;  // do_statfs succeeds
}

int do_cd(char *path)
{
    // TODO [P6-task1]: Implement do_cd

    return 0;  // do_cd succeeds
}

int do_mkdir(char *path)
{
    // TODO [P6-task1]: Implement do_mkdir

    return 0;  // do_mkdir succeeds
}

int do_rmdir(char *path)
{
    // TODO [P6-task1]: Implement do_rmdir

    return 0;  // do_rmdir succeeds
}

int do_ls(char *path, int option)
{
    // TODO [P6-task1]: Implement do_ls
    // Note: argument 'option' serves for 'ls -l' in A-core

    return 0;  // do_ls succeeds
}

int do_open(char *path, int mode)
{
    // TODO [P6-task2]: Implement do_open

    return 0;  // return the id of file descriptor
}

int do_read(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_read

    return 0;  // return the length of trully read data
}

int do_write(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_write

    return 0;  // return the length of trully written data
}

int do_close(int fd)
{
    // TODO [P6-task2]: Implement do_close

    return 0;  // do_close succeeds
}

int do_ln(char *src_path, char *dst_path)
{
    // TODO [P6-task2]: Implement do_ln

    return 0;  // do_ln succeeds 
}

int do_rm(char *path)
{
    // TODO [P6-task2]: Implement do_rm

    return 0;  // do_rm succeeds 
}

int do_lseek(int fd, int offset, int whence)
{
    // TODO [P6-task2]: Implement do_lseek

    return 0;  // the resulting offset location from the beginning of the file
}

void fs_read_block(uint32_t block_num, void *buf)
{
    // 1. 计算物理起始扇区号
    // 绝对扇区号 = 文件系统起始扇区 + (逻辑块号 * 8)
    unsigned start_sector_id = FS_START_SEC + (block_num * SECTORS_PER_BLOCK);

    // 2. 调用底层驱动
    // mem_address: 强转为 unsigned
    // num_of_blocks: 这里指底层的块数，即8个扇区
    // block_id: 起始扇区号
    sd_read((unsigned)buf, SECTORS_PER_BLOCK, start_sector_id);
}

/* 
 * 将 buf 写入文件系统的第 block_num 号逻辑块
 */
void fs_write_block(uint32_t block_num, const void *buf)
{
    // 1. 计算物理起始扇区号
    unsigned start_sector_id = FS_START_SEC + (block_num * SECTORS_PER_BLOCK);

    // 2. 调用底层驱动
    sd_write((unsigned)buf, SECTORS_PER_BLOCK, start_sector_id);
}
