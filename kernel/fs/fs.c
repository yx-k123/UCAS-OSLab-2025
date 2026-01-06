#include "pgtable.h"
#include <os/string.h>
#include <os/fs.h>

superblock_t current_superblock;
uint32_t current_cwd_inode;

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
    // 新增：清空栈上的随机值
    memset(dentries, 0, sizeof(dentries));

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
    current_cwd_inode = sb.root_inode;

    return 0;  // do_mkfs succeeds
}

int do_statfs(void)
{
    uint8_t buffer[FS_BLOCK_SIZE]; 
    superblock_t *sb = (superblock_t *)buffer;

    fs_read_block(0, buffer);

    if (sb->magic_number != SUPERBLOCK_MAGIC) {
        printk("[Error] Invalid File System! Magic: 0x%x (Expected: 0x%x)\n", 
               sb->magic_number, SUPERBLOCK_MAGIC);
        return -1;
    }
    
    // 计算总容量 (以 MB 为单位，方便阅读)
    // total_blocks * 4KB / 1024 / 1024
    uint32_t total_size_mb = (sb->total_blocks * (FS_BLOCK_SIZE / 1024)) / 1024;
    
    // --- 基本几何信息 ---
    printk("[Geometry]\n");
    printk("  Magic Number:       0x%08x\n", sb->magic_number);
    printk("  Block Size:         %d bytes\n", sb->block_size);
    printk("  Sector Size:        %d bytes\n", FS_BLOCK_SIZE);
    printk("  Sectors Per Block:  %d\n", SECTORS_PER_BLOCK);
    printk("  Total Size:         %d MB\n", total_size_mb);
    printk("\n");
    // --- 布局信息 (Layout) ---
    printk("[Layout - Block Offsets]\n");
    printk("  Superblock:         Block 0\n");
    printk("  Inode Bitmap:       Block %d\n", sb->inode_bitmap_start);
    printk("  Block Bitmap:       Block %d\n", sb->block_bitmap_start);
    printk("  Inode Table:        Block %d\n", sb->inode_table_start);
    printk("  Data Start:         Block %d\n", sb->data_start_block);
    printk("\n");

    // --- Inode 使用情况 ---
    printk("[Inodes]\n");
    printk("  Total Inodes:       %d\n", sb->total_inodes);
    printk("  Inode Size:         %d bytes\n", sb->inode_size);
    printk("  Free Inodes:        %d\n", sb->free_inode_count);
    printk("\n");

    // --- 数据块使用情况 ---
    printk("[Blocks]\n");
    printk("  Total Blocks:       %d\n", sb->total_blocks);
    printk("  Free Blocks:        %d\n", sb->free_block_count);
    printk("\n");

    // --- 物理扇区视角 (应题目要求) ---
    // 这里的 block_id 是相对于 FS_START_SEC 的
    // 如果你想显示绝对扇区号，需要加上 FS_START_SEC
    printk("[Physical Sectors\n");
    printk("  Total Sectors:      %d\n", sb->total_blocks * SECTORS_PER_BLOCK);
    printk("  Free Sectors:       %d\n", sb->free_block_count * SECTORS_PER_BLOCK);

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
    // 1. 确定目标目录的 Inode 号
    uint32_t dir_inode_num;
    
    if (path == NULL || strlen(path) == 0 || strcmp(path, ".") == 0) {
        // 如果没有路径，或者路径是 "."，则列出当前目录
        dir_inode_num = current_cwd_inode;
    } else if (strcmp(path, "..") == 0) {
        // 特殊处理 ".."：先读取当前目录，找到 ".." 指向的 inode
        dir_inode_num = lookup_path(".."); 
    } else {
        // 其他路径，解析路径获取 Inode
        dir_inode_num = lookup_path(path);
    }

    // 路径无效，返回错误
    if (dir_inode_num == 0) {
        // printf("ls: cannot access '%s': No such file or directory\n", path);
        return -1;
    }

    // 2. 读取目标目录的 Inode
    inode_t dir_inode;
    if (get_inode(dir_inode_num, &dir_inode) < 0) {
        printk("[Error] do_ls: get_inode failed for inode %d\n", dir_inode_num);
        return -1;
    }

    // --- DEBUG START ---
    printk("[Debug] ls dir_inode: mode=%d, block0=%d\n", 
           dir_inode.file_mode, dir_inode.direct_blocks[0]);
    // --- DEBUG END ---

    // 校验它是否真的是一个目录
    if (dir_inode.file_mode != IM_DIR) {
        // 如果是普通文件，ls 应该只打印这一个文件的信息（选做逻辑）
        // 这里简化处理：直接报错
        // printf("ls: '%s': Not a directory\n", path);
        return -1;
    }

    // 3. 准备缓冲区 (使用 static 避免爆栈)
    static uint8_t ls_buf[FS_BLOCK_SIZE];
    dentry_t *entries = (dentry_t *)ls_buf;

    // 4. 遍历目录的所有数据块
    for (int i = 0; i < MAX_DATA_BLOCKS; i++) {
        uint32_t block_id = dir_inode.direct_blocks[i];
        
        // 如果指针为0，说明后面没有数据块了，直接结束
        if (block_id == 0) break;

        // 读取该数据块
        fs_read_block(block_id, ls_buf);

        // 5. 遍历块内的所有目录项
        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            // 跳过无效项
            if (entries[j].inode_number == 0) continue;

            if (option == 0) {
                // --- 简单模式 (ls) ---
                // 只打印文件名，不换行（Linux 风格）或者每行一个
                printk("%s ", entries[j].file_name);
            } 
            else {
                // --- 详细模式 (ls -l) ---
                // 需要读取该文件的 Inode 来获取详细信息
                inode_t file_inode;
                get_inode(entries[j].inode_number, &file_inode);

                // 打印格式：Inode号  链接数  大小(字节)  文件名
                // 这里的格式可以根据你的需求微调，比如用 \t 对齐
                printk("%d\t%d\t%d\t%s\n", 
                       entries[j].inode_number, // Inode 号
                       file_inode.link_count,   // 链接数
                       file_inode.file_size,    // 大小
                       entries[j].file_name     // 文件名
                );
            }
        }
    }

    // 简单模式下，最后补一个换行
    if (option == 0) {
        printk("\n");
    }

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

// 返回 0 成功，-1 失败
int get_inode(uint32_t inode_num, inode_t *target) 
{
    // 1. 基础校验
    if (inode_num == 0 || inode_num > current_superblock.total_inodes) {
        return -1;
    }

    // 2. 计算偏移量
    // 既然 inode 从 1 开始，那么它在数组中的下标是 num - 1
    uint32_t inode_idx = inode_num - 1;

    // 3. 计算它在 Inode Table 中的哪个块
    // block_offset = 它是第几个 inode / 一个块能装多少个 inode
    uint32_t block_offset = inode_idx / INODES_PER_BLOCK;
    
    // 物理块号 = Inode Table 起始块号 + 偏移块数
    uint32_t physical_block_id = current_superblock.inode_table_start + block_offset;

    // 4. 读取该块数据
    // 使用 static 避免爆栈，且命名 unique 防止冲突
    static uint8_t inode_buf[FS_BLOCK_SIZE]; 
    fs_read_block(physical_block_id, inode_buf);

    // 5. 定位块内位置
    // inner_idx = 它是该块内的第几个 inode
    uint32_t inner_idx = inode_idx % INODES_PER_BLOCK;

    // 指针强转并移动到对应位置
    inode_t *source_ptr = (inode_t *)inode_buf + inner_idx;

    // 6. 拷贝到目标内存
    memcpy((uint8_t *)target, (const void *)source_ptr, sizeof(inode_t));

    return 0;
}

// 在 dir_inode 指向的目录中查找名为 name 的文件/目录
// 找到返回 inode 号，没找到返回 0
uint32_t find_entry(inode_t *dir_inode, char *name)
{
    // 校验：必须是目录才能查找
    if (dir_inode->file_mode != IM_DIR) {
        return 0;
    }

    // 使用 static 避免爆栈
    static uint8_t find_buf[FS_BLOCK_SIZE];
    
    // 遍历该目录所有的直接指针指向的数据块
    for (int i = 0; i < 12; i++) { 
        uint32_t block_id = dir_inode->direct_blocks[i];
        
        // 如果指针为0，说明后面没有数据块了，直接结束
        if (block_id == 0) break;

        // 读出目录的内容（即 dentry 数组）
        fs_read_block(block_id, find_buf);
        dentry_t *dentries = (dentry_t *)find_buf;

        // 遍历块里的每一个目录项
        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            // 1. 检查 inode_number 是否有效 (0表示空闲)
            // 2. 检查名字是否匹配
            if (dentries[j].inode_number != 0 && 
                strcmp(dentries[j].file_name, name) == 0) {
                return dentries[j].inode_number; // 找到了！
            }
        }
    }
    
    return 0; // 找遍了所有块都没找到
}

uint32_t lookup_path(char *path)
{
    // 1. 安全检查
    if (path == NULL || strlen(path) == 0) return 0;

    // 2. 确定起始 Inode
    uint32_t current_inode_num;
    if (path[0] == '/') {
        // 绝对路径：从根目录开始
        current_inode_num = current_superblock.root_inode;
    } else {
        // 相对路径：从当前工作目录开始
        current_inode_num = current_cwd_inode;
    }

    // 3. 复制路径字符串
    // strtok 会修改原字符串，所以必须复制一份到栈上
    char path_copy[256]; 
    strncpy(path_copy, path, 255);
    path_copy[255] = '\0'; // 确保结尾

    // 4. 解析每一层路径
    inode_t current_inode;
    char *token = strtok(path_copy, "/"); // 获取第一层目录名

    while (token != NULL) {
        // 读取当前所在目录的 Inode 信息
        if (get_inode(current_inode_num, &current_inode) < 0) {
            return 0; // 读取失败
        }

        // 在当前目录中查找 token (下一级名字)
        uint32_t next_inode_num = find_entry(&current_inode, token);

        if (next_inode_num == 0) {
            return 0; // 路径中断，找不到该文件/目录
        }

        // 步进到下一层
        current_inode_num = next_inode_num;
        
        // 继续取下一段路径
        token = strtok(NULL, "/");
    }

    // 循环结束，current_inode_num 指向的就是路径的最后一级
    return current_inode_num;
}