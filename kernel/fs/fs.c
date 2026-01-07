#include "pgtable.h"
#include <os/string.h>
#include <os/fs.h>

superblock_t current_superblock;
uint32_t current_cwd_inode;
static fdesc_t file_descriptor_table[NUM_FDESCS];
int page_cache_policy = CACHE_WT; // Default to Write Through
int write_back_freq = 30; // Default 30s
int sync_timer = 0;

void init_fs(){
    for (int i = 0; i < NUM_FDESCS; i++) {
        file_descriptor_table[i].valid = 0;
    }
}

void init_cache() {
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        cache_pool[i].valid = 0;
        cache_pool[i].dirty = 0;
    }
}

int check_and_restore_fs(void) {
    // 1. 读取 Superblock 所在的第0块
    static uint8_t buf[FS_BLOCK_SIZE];
    
    sd_read(pa2kva((uintptr_t)buf), SECTORS_PER_BLOCK, FS_START_SEC);
    
    superblock_t *sb = (superblock_t *)buf;

    // 2. 检查 Magic Number
    if (sb->magic_number == SUPERBLOCK_MAGIC) {
        printk("[FS]: Found valid filesystem. Loading superblock...\n");
        // 恢复 superblock 到全局变量
        current_superblock = *sb;
        
        // 恢复 CWD 为根目录
        current_cwd_inode = current_superblock.root_inode;
        
        // 重新初始化内存中的结构 (如 cache, fds)
        init_cache();
        init_fs();
        
        printk("[FS]: Filesystem checked ok.\n");
        return 0; // 已存在且有效
    } else {
        printk("[FS]: No valid filesystem found (Magic: 0x%X). \n", sb->magic_number);
        return -1; // 不存在或损坏，需要 mkfs
    }
}

int do_mkfs(void)
{   
    // TODO [P6-task1]: Implement do_mkfs 
    init_cache();
    init_fs();

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
    sb.inode_table_start  = 5;
    
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

    do_mkdir("/proc");
    do_mkdir("/proc/sys");
    do_touch("/proc/sys/vm");
    char *page_cache_policy = "page_cache_policy = write back\n";
    char *write_back_freq = "write_back_freq = 30";
    int fd = do_open("/proc/sys/vm", O_RDWR);
    do_write(fd, page_cache_policy, strlen(page_cache_policy));
    do_write(fd, write_back_freq, strlen(write_back_freq));
    do_close(fd);

    return 0;  // do_mkfs succeeds
}

int do_statfs(void)
{
    static uint8_t buffer[FS_BLOCK_SIZE]; 
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
    
    // 基本信息
    printk("[Geometry]\n");
    printk("  Magic Number:       0x%08x\n", sb->magic_number);
    printk("  Block Size:         %d bytes\n", sb->block_size);
    printk("  Sector Size:        %d bytes\n", FS_BLOCK_SIZE);
    printk("  Sectors Per Block:  %d\n", SECTORS_PER_BLOCK);
    printk("  Total Size:         %d MB\n", total_size_mb);
    printk("\n");
    // 布局信息
    printk("[Layout - Block Offsets]\n");
    printk("  Superblock:         Block 0\n");
    printk("  Inode Bitmap:       Block %d\n", sb->inode_bitmap_start);
    printk("  Block Bitmap:       Block %d\n", sb->block_bitmap_start);
    printk("  Inode Table:        Block %d\n", sb->inode_table_start);
    printk("  Data Start:         Block %d\n", sb->data_start_block);
    printk("\n");

    // Inodes
    printk("[Inodes]\n");
    printk("  Total Inodes:       %d\n", sb->total_inodes);
    printk("  Inode Size:         %d bytes\n", sb->inode_size);
    printk("  Free Inodes:        %d\n", sb->free_inode_count);
    printk("\n");

    // 数据块
    printk("[Blocks]\n");
    printk("  Total Blocks:       %d\n", sb->total_blocks);
    printk("  Free Blocks:        %d\n", sb->free_block_count);
    printk("\n");

    // 物理扇区
    // 这里的 block_id 是相对于 FS_START_SEC 的
    printk("[Physical Sectors\n");
    printk("  Total Sectors:      %d\n", sb->total_blocks * SECTORS_PER_BLOCK);
    printk("  Free Sectors:       %d\n", sb->free_block_count * SECTORS_PER_BLOCK);

    return 0;  // do_statfs succeeds
}

int do_cd(char *path)
{
    // 1. 解析路径，找到目标 Inode
    uint32_t target_inode_num = lookup_path(path);

    if (target_inode_num == 0) {
        // printf("cd: no such file or directory: %s\n", path);
        return -1;
    }

    // 2. 读取 Inode 校验类型
    inode_t target_inode;
    get_inode(target_inode_num, &target_inode);

    if (target_inode.file_mode != IM_DIR) {
        // printf("cd: not a directory: %s\n", path);
        return -1;
    }

    // 3. 切换状态 (核心步骤)
    // 只需要修改内存中的全局变量即可
    current_cwd_inode = target_inode_num;

    return 0;  // do_cd succeeds
}

int do_mkdir(char *path)
{
    // 1. 路径解析：分离 Parent Path 和 New Dir Name
    char parent_path[256] = {0};
    char dirname[64] = {0};
    
    // 找到最后一个 '/'
    char *last_slash = strrchr(path, '/');
    
    if (last_slash == NULL) {
        // 情况 A: "newdir" (相对路径，在当前目录下创建)
        strcpy(parent_path, "."); 
        strcpy(dirname, path);
    } else if (last_slash == path) {
        // 情况 B: "/newdir" (在根目录下创建)
        strcpy(parent_path, "/");
        strcpy(dirname, path + 1);
    } else {
        // 情况 C: "/home/user/newdir"
        int len = last_slash - path;
        strncpy(parent_path, path, len);
        strcpy(dirname, last_slash + 1);
    }

    // 2. 查找父目录
    uint32_t parent_inode_num;
    if (strcmp(parent_path, ".") == 0) {
        parent_inode_num = current_cwd_inode;
    } else {
        parent_inode_num = lookup_path(parent_path);
    }

    if (parent_inode_num == 0) {
        // printf("Error: Parent directory not found.\n");
        return -1;
    }

    inode_t parent_inode;
    get_inode(parent_inode_num, &parent_inode);

    // 检查父节点是否为目录
    if (parent_inode.file_mode != IM_DIR) return -1;

    // 检查是否重名 (find_entry 是上一节定义的函数)
    if (find_entry(&parent_inode, dirname) != 0) {
        printk("Directory already exists.\n");
        return -1; // 已存在
    }

    // 3. 分配资源 (Inode 和 Data Block)
    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == 0) return -1; // No inodes left

    uint32_t new_block_id = alloc_block();
    if (new_block_id == 0) return -1; // No blocks left

    // 4. 初始化新目录的 Inode
    inode_t new_inode;
    new_inode.file_mode = IM_DIR;
    new_inode.file_size = FS_BLOCK_SIZE; // 逻辑大小通常是一个块
    new_inode.link_count = 2;         // 链接数：1(父目录的entry) + 1(自己的.)
    new_inode.direct_blocks[0] = new_block_id;
    // 其他 block 指针清零...
    for(int i=1; i<12; i++) new_inode.direct_blocks[i] = 0;
    
    sync_inode(new_inode_num, &new_inode);

    // 5. 初始化新目录的数据块 (. 和 ..)
    static uint8_t buf[FS_BLOCK_SIZE];
    memset(buf, 0, FS_BLOCK_SIZE);
    dentry_t *dentries = (dentry_t *)buf;

    // Entry 0: "." 指向自己
    dentries[0].inode_number = new_inode_num;
    strcpy(dentries[0].file_name, ".");
    dentries[0].file_type = IM_DIR;

    // Entry 1: ".." 指向父目录
    dentries[1].inode_number = parent_inode_num;
    strcpy(dentries[1].file_name, "..");
    dentries[1].file_type = IM_DIR;

    fs_write_block(new_block_id, buf);

    // 6. 更新父目录 (添加目录项 + 更新链接数)
    // 6.1 添加目录项
    if (add_entry_to_parent(&parent_inode, new_inode_num, dirname) < 0) {
        return -1;
    }

    // 6.2 更新父目录 Inode 元数据
    // 父目录的链接数要 +1，因为新目录里有一个 ".." 指向它
    parent_inode.link_count++; 
    // 父目录大小通常不需要变（因为是用 Block 管理的）
    
    sync_inode(parent_inode_num, &parent_inode);

    return 0;  // do_mkdir succeeds
}

int do_rmdir(char *path)
{
    // 1. 路径解析：分离父目录和目标目录名
    char parent_path[256];
    char dirname[64];
    // ... Copy parsing logic from mkdir ...
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        strcpy(parent_path, "."); strcpy(dirname, path);
    } else if (last_slash == path) {
        strcpy(parent_path, "/"); strcpy(dirname, path + 1);
    } else {
        int len = last_slash - path;
        strncpy(parent_path, path, len); parent_path[len] = '\0';
        strcpy(dirname, last_slash + 1);
    }

    // 2. 找到父目录 Inode
    uint32_t parent_inode_num = (strcmp(parent_path, ".") == 0) ? current_cwd_inode : lookup_path(parent_path);
    if (parent_inode_num == 0) return -1;
    
    inode_t parent_inode;
    get_inode(parent_inode_num, &parent_inode);

    // 3. 在父目录中查找目标目录
    // 这里我们需要知道它在父目录哪个块、哪个位置，以便稍后删除
    // 为了简化，我们先用 find_entry 拿到 Inode 号
    uint32_t target_inode_num = find_entry(&parent_inode, dirname);
    if (target_inode_num == 0) return -1; // 目录不存在

    // 4. 读取目标 Inode 进行校验
    inode_t target_inode;
    get_inode(target_inode_num, &target_inode);

    // 校验 A: 必须是目录
    if (target_inode.file_mode != IM_DIR) return -1;
    
    // 校验 B: 不能删除根目录 (根目录 inode 通常是 1)
    if (target_inode_num == current_superblock.root_inode) return -1;

    // 校验 C: 必须是空目录
    if (!is_dir_empty(&target_inode)) return -1;

    // 5. 开始删除：回收资源
    // 5.1 回收目标目录占用的数据块
    for (int i = 0; i < 12; i++) {
        if (target_inode.direct_blocks[i] != 0) {
            free_block(target_inode.direct_blocks[i]);
            target_inode.direct_blocks[i] = 0;
        }
    }
    // 5.2 回收目标 Inode
    free_inode(target_inode_num);

    // 6. 更新父目录
    // 6.1 从父目录的数据块中清除该 dentry
    static uint8_t buf[FS_BLOCK_SIZE];
    int found = 0;
    for (int i = 0; i < 12; i++) {
        if (parent_inode.direct_blocks[i] == 0) break;
        
        fs_read_block(parent_inode.direct_blocks[i], buf);
        dentry_t *dentries = (dentry_t *)buf;
        
        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            if (dentries[j].inode_number == target_inode_num &&
                strcmp(dentries[j].file_name, dirname) == 0) {
                // 清除条目
                dentries[j].inode_number = 0; 
                memset(dentries[j].file_name, 0, MAX_FILE_NAME);
                fs_write_block(parent_inode.direct_blocks[i], buf);
                found = 1;
                break;
            }
        }
        if (found) break;
    }

    // 6.2 更新父目录 Inode 元数据
    // 目标目录被删了，它里面的 ".." 也没了，所以父目录链接数 -1
    if (parent_inode.link_count > 0) {
        parent_inode.link_count--;
    }
    sync_inode(parent_inode_num, &parent_inode);

    return 0;
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
        printk("ls: cannot access '%s': No such file or directory\n", path);
        return -1;
    }

    // 2. 读取目标目录的 Inode
    inode_t dir_inode;
    if (get_inode(dir_inode_num, &dir_inode) < 0) {
        printk("[Error] do_ls: get_inode failed for inode %d\n", dir_inode_num);
        return -1;
    }

    printk("[Debug] ls dir_inode: mode=%d, block0=%d\n", 
           dir_inode.file_mode, dir_inode.direct_blocks[0]);

    // 校验它是否真的是一个目录
    if (dir_inode.file_mode != IM_DIR) {
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
                // ls
                printk("%s ", entries[j].file_name);
            } 
            else {
                // ls -l
                // 需要读取该文件的 Inode 来获取详细信息
                inode_t file_inode;
                get_inode(entries[j].inode_number, &file_inode);

                // 打印格式：Inode号  链接数  大小(字节)  文件名
                printk("%d\t%d\t%d\t%s\n", 
                       entries[j].inode_number, // Inode 号
                       file_inode.link_count,   // 链接数
                       file_inode.file_size,    // 大小
                       entries[j].file_name     // 文件名
                );
            }
        }
    }

    // ls下，最后补一个换行
    if (option == 0) {
        printk("\n");
    }

    return 0;  // do_ls succeeds
}

int do_touch(char *path)
{
    // 1. 路径解析：分离父目录路径和文件名
    char parent_path[256];
    char filename[64];
    
    // (这里复用之前的路径拆分逻辑)
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        strcpy(parent_path, "."); strcpy(filename, path);
    } else if (last_slash == path) {
        strcpy(parent_path, "/"); strcpy(filename, path + 1);
    } else {
        int len = last_slash - path;
        strncpy(parent_path, path, len); parent_path[len] = '\0';
        strcpy(filename, last_slash + 1);
    }

    // 2. 找到父目录 Inode
    uint32_t parent_inode_num = (strcmp(parent_path, ".") == 0) ? current_cwd_inode : lookup_path(parent_path);
    if (parent_inode_num == 0) return -1; // 父目录不存在

    inode_t parent_inode;
    get_inode(parent_inode_num, &parent_inode);

    // 3. 检查是否重名
    if (find_entry(&parent_inode, filename) != 0) {
        printk("File already exists.\n");
        return -1; 
    }

    // 4. 分配新 Inode
    uint32_t new_inode_num = alloc_inode();
    if (new_inode_num == 0) return -1; // Inode 耗尽

    // 5. 初始化新文件的 Inode
    inode_t new_inode;
    new_inode.file_mode = IM_REG;  // 标记为普通文件 (Regular File)
    new_inode.file_size = 0;       // 初始大小为 0
    new_inode.link_count = 1;      // 只有父目录里的文件名指向它
    
    // 作为一个空文件，它不需要分配任何数据块，指针全设为 0
    memset(new_inode.direct_blocks, 0, sizeof(new_inode.direct_blocks));
    new_inode.single_indirect = 0;
    new_inode.double_indirect = 0;

    // 写回新 Inode
    sync_inode(new_inode_num, &new_inode);

    // 6. 在父目录中添加目录项
    // (使用 do_mkdir 中实现的 add_entry_to_parent)
    if (add_entry_to_parent(&parent_inode, new_inode_num, filename) < 0) {
        // 如果目录满了无法添加，应该回滚 free_inode(new_inode_num)
        return -1;
    }

    // 注意：父目录的 link_count 不需要增加（只有子目录会导致父目录引用增加）
    // 但父目录的 modify_time 应该更新（如果你的 inode 有时间字段）

    return 0; // do_touch succeeds
}

int do_cat(char *path)
{
    // 1. 查找文件 Inode
    uint32_t inode_num = lookup_path(path);
    if (inode_num == 0) {
        // printf("cat: %s: No such file or directory\n", path);
        return -1;
    }

    // 2. 读取 Inode
    inode_t inode;
    get_inode(inode_num, &inode);

    // 3. 校验类型
    if (inode.file_mode == IM_DIR) {
        // printf("cat: %s: Is a directory\n", path);
        return -1;
    }

    // 4. 准备缓冲区
    static uint8_t buffer[FS_BLOCK_SIZE];
    uint32_t current_pos = 0;
    uint32_t total_size = inode.file_size;

    // 5. 遍历并打印
    while (current_pos < total_size) {
        uint32_t logical_block_idx = current_pos / FS_BLOCK_SIZE;
        uint32_t offset_in_block = current_pos % FS_BLOCK_SIZE;
        uint32_t bytes_left = total_size - current_pos;
        
        // 确定本次打印长度
        uint32_t chunk_size = FS_BLOCK_SIZE - offset_in_block;
        if (chunk_size > bytes_left) chunk_size = bytes_left;

        // 获取物理块 (不支持写回，所以 allocate=0)
        uint32_t phys_block_id = get_block_addr(&inode, logical_block_idx, 0, NULL);
        
        if (phys_block_id == 0) {
             // 稀疏文件空洞，打印 0
             for (uint32_t j = 0; j < chunk_size; j++) printk("%c", 0);
        } else {
             // 读取物理块
             fs_read_block(phys_block_id, buffer);
             // 打印
             for (uint32_t j = 0; j < chunk_size; j++) {
                 printk("%c", buffer[offset_in_block + j]);
             }
        }
        
        current_pos += chunk_size;
    }

    printk("\n");

    return 0; // do_cat succeeds
}

int do_open(char *name, int access)
{
    // 1. 寻找空闲的文件描述符
    int fd = -1;
    for (int i = 0; i < NUM_FDESCS; i++) {
        if (file_descriptor_table[i].valid == 0) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1; // 打开文件太多，表满了

    // 2. 解析路径找到 Inode
    uint32_t inode_num = lookup_path(name);
    if (inode_num == 0) return -1; // 文件不存在

    // 3. 填充 FD 表
    file_descriptor_table[fd].inode_id = inode_num;
    file_descriptor_table[fd].offset = 0; // 默认指向文件开头
    file_descriptor_table[fd].access = access;
    file_descriptor_table[fd].valid = 1;

    return fd; // 返回下标
}

uint32_t get_block_addr(inode_t *inode, uint32_t logical_block, int allocate, int *is_new) {
    if (is_new) *is_new = 0;
    static uint8_t indirect_buf[FS_BLOCK_SIZE];
    uint32_t *indices = (uint32_t *)indirect_buf;

    // 1. Direct Blocks (0-11)
    if (logical_block < 12) {
        uint32_t phys = inode->direct_blocks[logical_block];
        if (phys == 0 && allocate) {
             phys = alloc_block();
             if (phys == 0) return 0; 
             inode->direct_blocks[logical_block] = phys;
             if (is_new) *is_new = 1;
        }
        return phys;
    }

    // 2. Single Indirect (12 - 1035)
    logical_block -= 12;
    if (logical_block < 1024) {
        uint32_t indirect_block = inode->single_indirect;
        if (indirect_block == 0) {
            if (!allocate) return 0;
            indirect_block = alloc_block();
            if (indirect_block == 0) return 0;
            inode->single_indirect = indirect_block;
            memset(indirect_buf, 0, FS_BLOCK_SIZE);
            fs_write_block(indirect_block, indirect_buf);
        } else {
            fs_read_block(indirect_block, indirect_buf);
        }
        
        uint32_t phys = indices[logical_block];
        if (phys == 0 && allocate) {
            phys = alloc_block();
            if (phys == 0) return 0;
            indices[logical_block] = phys;
            fs_write_block(indirect_block, indirect_buf);
            if (is_new) *is_new = 1;
        }
        return phys;
    }

    // 3. Double Indirect
    logical_block -= 1024;
    // Capacity usually 1024*1024
    if (logical_block < 1024 * 1024) {
        uint32_t l1_idx = logical_block / 1024;
        uint32_t l2_idx = logical_block % 1024;
        
        uint32_t double_indirect_block = inode->double_indirect;
        if (double_indirect_block == 0) {
            if (!allocate) return 0;
            double_indirect_block = alloc_block();
            if (double_indirect_block == 0) return 0;
            inode->double_indirect = double_indirect_block;
            memset(indirect_buf, 0, FS_BLOCK_SIZE);
            fs_write_block(double_indirect_block, indirect_buf);
        } else {
            fs_read_block(double_indirect_block, indirect_buf);
        }
        
        uint32_t single_indirect_block = indices[l1_idx];
        if (single_indirect_block == 0) {
            if (!allocate) return 0;
            single_indirect_block = alloc_block();
            if (single_indirect_block == 0) return 0;
            indices[l1_idx] = single_indirect_block;
            fs_write_block(double_indirect_block, indirect_buf);
            
            memset(indirect_buf, 0, FS_BLOCK_SIZE);
            fs_write_block(single_indirect_block, indirect_buf);
        } else {
            fs_read_block(single_indirect_block, indirect_buf);
        }
        
        uint32_t phys = indices[l2_idx];
        if (phys == 0 && allocate) {
            phys = alloc_block();
            if (phys == 0) return 0;
            indices[l2_idx] = phys;
            fs_write_block(single_indirect_block, indirect_buf);
            if (is_new) *is_new = 1;
        }
        return phys;
    }
    
    return 0;
}

int do_read(int fd, char *buff, int size)
{
    // 1. 校验 FD
    if (fd < 0 || fd >= NUM_FDESCS || !file_descriptor_table[fd].valid) return -1;
    
    fdesc_t *desc = &file_descriptor_table[fd];
    
    // 2. 读取 Inode 获取文件大小和数据块索引
    inode_t inode;
    get_inode(desc->inode_id, &inode);

    // 3. 调整读取大小，防止越界
    uint32_t current_pos = desc->offset;
    uint32_t file_size = inode.file_size;
    
    if (current_pos >= file_size) return 0; // 已经读到末尾了
    if (current_pos + size > file_size) {
        size = file_size - current_pos; // 只能读剩下的部分
    }

    int bytes_read = 0;
    uint8_t block_buf[FS_BLOCK_SIZE];

    // 4. 循环读取
    while (bytes_read < size) {
        uint32_t logical_block_idx = current_pos / FS_BLOCK_SIZE; // 当前是文件的第几个块
        uint32_t offset_in_block = current_pos % FS_BLOCK_SIZE;   // 在该块内的偏移
        // 本次能读多少？ min(剩余需读, 该块剩余空间)
        uint32_t bytes_to_copy = size - bytes_read;
        if (bytes_to_copy > (FS_BLOCK_SIZE - offset_in_block)) {
            bytes_to_copy = FS_BLOCK_SIZE - offset_in_block;
        }

        // 获取物理块号
        uint32_t phys_block_id = get_block_addr(&inode, logical_block_idx, 0, NULL);

        if (phys_block_id == 0) {
            memset(buff + bytes_read, 0, bytes_to_copy);
        } else {
            // 读物理块
            fs_read_block(phys_block_id, block_buf);
            // 拷贝数据到用户 buffer
            memcpy((void *)buff + bytes_read, block_buf + offset_in_block, bytes_to_copy);
        }

        bytes_read += bytes_to_copy;
        current_pos += bytes_to_copy;
    }

    // 5. 更新文件描述符的 offset
    desc->offset = current_pos;

    return bytes_read;
}

int do_write(int fd, char *buff, int size)
{
    // 1. 校验 FD
    if (fd < 0 || fd >= NUM_FDESCS || !file_descriptor_table[fd].valid) return -1;
    // 检查权限 (如果是只读打开，不能写)
    if (file_descriptor_table[fd].access == O_RDONLY) return -1;

    fdesc_t *desc = &file_descriptor_table[fd];
    
    // 2. 读取 Inode
    inode_t inode;
    get_inode(desc->inode_id, &inode);

    // 禁止对目录进行写操作
    if (inode.file_mode == IM_DIR) return -1;

    int bytes_written = 0;
    uint32_t current_pos = desc->offset;
    uint8_t block_buf[FS_BLOCK_SIZE];

    // 3. 循环写入
    while (bytes_written < size) {
        uint32_t logical_block_idx = current_pos / FS_BLOCK_SIZE;
        uint32_t offset_in_block = current_pos % FS_BLOCK_SIZE;
        uint32_t bytes_to_copy = size - bytes_written;
        
        // 限制本次写入不超过本块结尾
        if (bytes_to_copy > (FS_BLOCK_SIZE - offset_in_block)) {
            bytes_to_copy = FS_BLOCK_SIZE - offset_in_block;
        }

        // 获取或分配物理块
        int is_new_block = 0;
        uint32_t phys_block_id = get_block_addr(&inode, logical_block_idx, 1, &is_new_block);
        
        if (phys_block_id == 0) {
             // 磁盘满了
             return bytes_written;
        }

        if (is_new_block) {
            // 新块清零
            memset(block_buf, 0, FS_BLOCK_SIZE);
        } else {
            // 块已存在，先读出来 (Read-Modify-Write)
            // 除非我们要重写整个块，否则必须读旧数据
            fs_read_block(phys_block_id, block_buf);
        }

        // 修改缓冲区
        memcpy(block_buf + offset_in_block, (void *)buff + bytes_written, bytes_to_copy);

        // 写回磁盘
        fs_write_block(phys_block_id, block_buf);

        bytes_written += bytes_to_copy;
        current_pos += bytes_to_copy;
    }

    // 4. 更新 Inode
    desc->offset = current_pos;
    if (current_pos > inode.file_size) {
        inode.file_size = current_pos;
    }
    
    // 写回 Inode
    sync_inode(desc->inode_id, &inode);

    return bytes_written;
}

int do_close(int fd)
{
    if (fd < 0 || fd >= NUM_FDESCS) return -1; // Invalid fd
    if (file_descriptor_table[fd].valid == 0) return -1; // Already closed
    
    // 简单地标记为无效
    file_descriptor_table[fd].valid = 0;
    file_descriptor_table[fd].inode_id = 0;
    file_descriptor_table[fd].offset = 0;

    return 0; // do_close succeeds
}

int do_ln(char *src_path, char *dst_path)
{
    // 1. 查找源文件 Inode
    uint32_t src_inode_num = lookup_path(src_path);
    if (src_inode_num == 0) return -1; // 源文件不存在

    inode_t src_inode;
    get_inode(src_inode_num, &src_inode);

    // 2. 禁止对目录建立硬链接
    if (src_inode.file_mode == IM_DIR) return -1;

    // 3. 解析目标路径 (分离 Parent 和 Filename)
    char parent_path[256];
    char filename[64];
    char *last_slash = strrchr(dst_path, '/');
    if (last_slash == NULL) {
        strcpy(parent_path, "."); strcpy(filename, dst_path);
    } else if (last_slash == dst_path) {
        strcpy(parent_path, "/"); strcpy(filename, dst_path + 1);
    } else {
        int len = last_slash - dst_path;
        strncpy(parent_path, dst_path, len); parent_path[len] = '\0';
        strcpy(filename, last_slash + 1);
    }

    // 4. 获取目标父目录
    uint32_t parent_inode_num = (strcmp(parent_path, ".") == 0) ? current_cwd_inode : lookup_path(parent_path);
    if (parent_inode_num == 0) return -1;

    inode_t parent_inode;
    get_inode(parent_inode_num, &parent_inode);

    // 5. 检查目标处是否有重名文件
    if (find_entry(&parent_inode, filename) != 0) return -1; // 已存在

    // 6. 在父目录添加目录项
    // 注意：这里传入的是 src_inode_num，而不是 alloc_inode()
    if (add_entry_to_parent(&parent_inode, src_inode_num, filename) < 0) {
        return -1; // 目录满或其他错误
    }

    // 7. 增加源文件的链接数并写回
    src_inode.link_count++;
    sync_inode(src_inode_num, &src_inode);

    return 0; // do_ln succeeds
}

int do_rm(char *path)
{
    // 1. 路径解析
    char parent_path[256];
    char filename[64];
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        strcpy(parent_path, "."); strcpy(filename, path);
    } else if (last_slash == path) {
        strcpy(parent_path, "/"); strcpy(filename, path + 1);
    } else {
        int len = last_slash - path;
        strncpy(parent_path, path, len); parent_path[len] = '\0';
        strcpy(filename, last_slash + 1);
    }

    // 2. 获取父目录
    uint32_t parent_inode_num = (strcmp(parent_path, ".") == 0) ? current_cwd_inode : lookup_path(parent_path);
    if (parent_inode_num == 0) return -1;

    inode_t parent_inode;
    get_inode(parent_inode_num, &parent_inode);

    // 3. 检查目标类型（先查找，但不移除）
    // 为了严谨，应该先 read inode 检查是不是 directory。
    // 这里简化：利用 remove_entry_from_parent 的返回值
    uint32_t target_inode_num = find_entry(&parent_inode, filename);
    if (target_inode_num == 0) return -1;

    inode_t target_inode;
    get_inode(target_inode_num, &target_inode);
    
    // 如果是目录，rm 应该拒绝
    if (target_inode.file_mode == IM_DIR) return -1;

    // 4. 从父目录移除目录项
    // 这一步之后，文件系统树上已经看不到这个文件了，但 Inode 还在
    remove_entry_from_parent(&parent_inode, filename);

    // 5. 处理引用计数
    target_inode.link_count--;

    if (target_inode.link_count > 0) {
        // 情况 1: 还有其他硬链接指向它
        // 只更新 link_count 并写回
        sync_inode(target_inode_num, &target_inode);
    } 
    else {
        // 情况 2: 最后一个链接被删除了 -> 彻底释放资源
        
        // a. 释放数据块 (Direct)
        for (int i = 0; i < 12; i++) {
            if (target_inode.direct_blocks[i] != 0) {
                free_block(target_inode.direct_blocks[i]);
                target_inode.direct_blocks[i] = 0;
            }
        }
        
        // b. 释放 Single Indirect
        if (target_inode.single_indirect != 0) {
            static uint8_t ind_buf[FS_BLOCK_SIZE];
            fs_read_block(target_inode.single_indirect, ind_buf);
            uint32_t *indices = (uint32_t *)ind_buf;
            for (int i = 0; i < DENTRIES_PER_BLOCK; i++) { // DENTRIES_PER_BLOCK is technically for dentries but here size matches 4096/4=1024
                if (indices[i] != 0) free_block(indices[i]);
            }
            free_block(target_inode.single_indirect);
            target_inode.single_indirect = 0;
        }

        // c. 释放 Double Indirect
        if (target_inode.double_indirect != 0) {
            static uint8_t d_ind_buf[FS_BLOCK_SIZE];
            static uint8_t s_ind_buf[FS_BLOCK_SIZE]; 
            
            fs_read_block(target_inode.double_indirect, d_ind_buf);
            uint32_t *d_indices = (uint32_t *)d_ind_buf;
            
            for (int i = 0; i < (FS_BLOCK_SIZE/4); i++) {
                if (d_indices[i] != 0) {
                    // 读取这一个一级间接块
                    fs_read_block(d_indices[i], s_ind_buf);
                    uint32_t *s_indices = (uint32_t *)s_ind_buf;
                    for(int j = 0; j < (FS_BLOCK_SIZE/4); j++) {
                        if (s_indices[j] != 0) free_block(s_indices[j]);
                    }
                    free_block(d_indices[i]);
                    // 重读外层 buffer (虽然通常不变，但可以增加稳健性，不过这里为避免性能开销假设不变)
                    // fs_read_block(target_inode.double_indirect, d_ind_buf); 
                }
            }
            free_block(target_inode.double_indirect);
            target_inode.double_indirect = 0;
        }

        // d. 释放 Inode 本身 (在位图中置 0)
        free_inode(target_inode_num);
        
        // 既然 Inode 释放了，就不需要调用 sync_inode 了
    }

    return 0; // do_rm succeeds
}

int do_lseek(int fd, int offset, int whence)
{
    // 1. 校验 FD
    if (fd < 0 || fd >= NUM_FDESCS || !file_descriptor_table[fd].valid) return -1;
    
    fdesc_t *desc = &file_descriptor_table[fd];
    inode_t inode;
    get_inode(desc->inode_id, &inode);
    
    int32_t new_offset = desc->offset;

    switch (whence) {
    case SEEK_SET:
        new_offset = offset;
        break;
    case SEEK_CUR:
        new_offset += offset;
        break;
    case SEEK_END:
        new_offset = inode.file_size + offset;
        break;
    default:
        return -1;
    }

    if (new_offset < 0) return -1;
    
    desc->offset = new_offset;

    return new_offset;
}

void disk_read_block(uint32_t block_num, void *buf)
{
    // 1. 计算物理起始扇区号
    // 绝对扇区号 = 文件系统起始扇区 + (逻辑块号 * 8)
    unsigned start_sector_id = FS_START_SEC + (block_num * SECTORS_PER_BLOCK);

    // 2. 调用底层驱动
    // mem_address: 强转为 unsigned
    // num_of_blocks: 这里指底层的块数，即8个扇区
    // block_id: 起始扇区号
    sd_read(kva2pa((uintptr_t)buf), SECTORS_PER_BLOCK, start_sector_id);
}

/* 
 * 将 buf 写入文件系统的第 block_num 号逻辑块
 */
void disk_write_block(uint32_t block_num, const void *buf)
{
    // 1. 计算物理起始扇区号
    unsigned start_sector_id = FS_START_SEC + (block_num * SECTORS_PER_BLOCK);

    // 2. 调用底层驱动
    sd_write(kva2pa((uintptr_t)buf), SECTORS_PER_BLOCK, start_sector_id);
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

void sync_inode(uint32_t inode_num, inode_t *target) {
    uint32_t inode_idx = inode_num - 1;
    uint32_t block_offset = inode_idx / INODES_PER_BLOCK;
    uint32_t physical_block_id = current_superblock.inode_table_start + block_offset;
    uint32_t inner_idx = inode_idx % INODES_PER_BLOCK;

    static uint8_t buffer[FS_BLOCK_SIZE];
    fs_read_block(physical_block_id, buffer); // Read
    inode_t *dest_ptr = (inode_t *)buffer + inner_idx;
    memcpy((uint8_t *)dest_ptr, (uint8_t *)target, sizeof(inode_t)); // Modify
    fs_write_block(physical_block_id, buffer); // Write
}

// 向目录的数据块中添加一个 entry
// 返回 0 成功，-1 失败（如目录满了）
int add_entry_to_parent(inode_t *parent_inode, uint32_t new_inode_num, char *name) {
    static uint8_t buffer[FS_BLOCK_SIZE];
    
    // 遍历父目录的所有直接块，寻找空槽位
    for (int i = 0; i < 12; i++) {
        uint32_t block_id = parent_inode->direct_blocks[i];
        
        // 如果该指针还没分配，需要先分配一个数据块给父目录（这里简化处理：假设父目录已分配且未满）
        // 严谨写法：如果 block_id == 0，alloc_block 并关联
        if (block_id == 0) {
            block_id = alloc_block();
            parent_inode->direct_blocks[i] = block_id;
            // 初始化新块全0
            memset(buffer, 0, FS_BLOCK_SIZE);
            fs_write_block(block_id, buffer);
        }

        fs_read_block(block_id, buffer);
        dentry_t *dentries = (dentry_t *)buffer;

        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            // 找到一个空闲位置 (inode_number == 0)
            if (dentries[j].inode_number == 0) {
                dentries[j].inode_number = new_inode_num;
                strcpy(dentries[j].file_name, name);
                dentries[j].file_type = IM_DIR; // 标记这是个目录
                // 写回磁盘
                fs_write_block(block_id, buffer);
                return 0;
            }
        }
    }
    return -1; // 目录彻底满了
}

static int get_bit(uint8_t *buf, uint32_t index) {
    uint32_t byte_offset = index / BITS_PER_BYTE;
    uint32_t bit_offset  = index % BITS_PER_BYTE;
    return (buf[byte_offset] >> bit_offset) & 1;
}

// 辅助：将 buffer 中第 index 位置为 1
static void set_bit(uint8_t *buf, uint32_t index) {
    uint32_t byte_offset = index / BITS_PER_BYTE;
    uint32_t bit_offset  = index % BITS_PER_BYTE;
    buf[byte_offset] |= (1 << bit_offset);
}

uint32_t alloc_inode() 
{
    // 1. 检查是否有空闲资源
    if (current_superblock.free_inode_count == 0) {
        return 0; // 分配失败
    }

    static uint8_t buf[FS_BLOCK_SIZE];
    uint32_t total_inodes = current_superblock.total_inodes;
    
    // 2. 遍历 Inode Bitmap
    
    // 计算位图占多少个块
    uint32_t bitmap_blocks = (total_inodes + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;

    for (uint32_t b = 0; b < bitmap_blocks; b++) {
        // 读取第 b 个位图块
        uint32_t bitmap_block_id = current_superblock.inode_bitmap_start + b;
        fs_read_block(bitmap_block_id, buf);

        // 遍历该块内的每一位
        for (uint32_t i = 0; i < BITS_PER_BLOCK; i++) {
            uint32_t global_inode_index = b * BITS_PER_BLOCK + i;
            
            // 越界检查
            if (global_inode_index >= total_inodes) break;

            // 3. 找到空闲位 (0)
            if (get_bit(buf, i) == 0) {
                // 3.1 修改位图
                set_bit(buf, i);
                fs_write_block(bitmap_block_id, buf); // 立即写回磁盘

                // 3.2 更新 Superblock
                current_superblock.free_inode_count--;
                
                // 将 Superblock 写回磁盘 (块 0)
                // 注意：这里需要先把 Superblock 放到一个 4K buffer 里再写，或者依赖写函数的处理
                // 假设直接用 struct 指针强转写入是安全的(需填充至4K):
                static uint8_t sb_buf[FS_BLOCK_SIZE];
                memset(sb_buf, 0, FS_BLOCK_SIZE);
                memcpy(sb_buf, (void *)&current_superblock, sizeof(superblock_t));
                fs_write_block(0, sb_buf);

                // 3.3 返回 Inode 编号
                // 索引 0 对应 Inode 1
                return global_inode_index + 1;
            }
        }
    }

    return 0; // 分配失败
}
uint32_t alloc_block() 
{
    // 1. 检查是否有空闲资源
    if (current_superblock.free_block_count == 0) {
        return 0; // 分配失败
    }

    static uint8_t buf[FS_BLOCK_SIZE];
    // 数据区能够容纳的总块数 = 总块数 - 数据区起始位置
    uint32_t max_data_blocks = current_superblock.total_blocks - current_superblock.data_start_block;
 
    uint32_t bitmap_blocks = (max_data_blocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;

    for (uint32_t b = 0; b < bitmap_blocks; b++) {
        uint32_t bitmap_block_id = current_superblock.block_bitmap_start + b;
        fs_read_block(bitmap_block_id, buf);

        for (uint32_t i = 0; i < BITS_PER_BLOCK; i++) {
            uint32_t bit_index = b * BITS_PER_BLOCK + i;
            
            if (bit_index >= max_data_blocks) break;

            // 2. 找到空闲位
            if (get_bit(buf, i) == 0) {
                // 2.1 修改位图并写回
                set_bit(buf, i);
                fs_write_block(bitmap_block_id, buf);
                // 2.2 更新 Superblock 并写回
                current_superblock.free_block_count--;
                static uint8_t sb_buf[FS_BLOCK_SIZE];
                memset(sb_buf, 0, FS_BLOCK_SIZE);
                memcpy(sb_buf, (void *)&current_superblock, sizeof(superblock_t));
                fs_write_block(0, sb_buf);

                // 2.3 计算实际物理块号
                uint32_t physical_block_id = current_superblock.data_start_block + bit_index;

                // 2.4 清零新分配的块 (Zero out)
                static uint8_t zero_buf[FS_BLOCK_SIZE];
                memset(zero_buf, 0, FS_BLOCK_SIZE);
                fs_write_block(physical_block_id, zero_buf);

                return physical_block_id;
            }
        }
    }

    return 0;
}

// 辅助：判断目录是否为空 (除了 . 和 .. 是否还有其他项)
// 返回 1 (空)，0 (不空)
int is_dir_empty(inode_t *dir_inode) {
    static uint8_t buffer[FS_BLOCK_SIZE];
    
    for (int i = 0; i < 12; i++) {
        uint32_t block_id = dir_inode->direct_blocks[i];
        if (block_id == 0) break;

        fs_read_block(block_id, buffer);
        dentry_t *dentries = (dentry_t *)buffer;

        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            if (dentries[j].inode_number != 0) {
                // 如果发现有效项，且名字不是 . 也不是 ..，说明不为空
                if (strcmp(dentries[j].file_name, ".") != 0 && 
                    strcmp(dentries[j].file_name, "..") != 0) {
                    return 0; 
                }
            }
        }
    }
    return 1;
}

// 辅助：释放 Inode (位图置0)
void free_inode(uint32_t inode_num) {
    static uint8_t buf[FS_BLOCK_SIZE];
    // 定位位图块
    // (简化逻辑：假设位图只占1块，更复杂的逻辑参考 alloc_inode)
    fs_read_block(current_superblock.inode_bitmap_start, buf);
    
    // 清除位 (inode_num - 1)
    uint32_t idx = inode_num - 1;
    uint32_t byte_off = idx / 8;
    uint32_t bit_off = idx % 8;
    buf[byte_off] &= ~(1 << bit_off);
    
    fs_write_block(current_superblock.inode_bitmap_start, buf);

    // 更新 Superblock
    current_superblock.free_inode_count++;
    // write_superblock...
}

// 辅助：释放 Block (位图置0)
void free_block(uint32_t block_id) {
    uint8_t buf[FS_BLOCK_SIZE];
    // 物理块号 -> 位图索引
    uint32_t bitmap_idx = block_id - current_superblock.data_start_block;
    
    fs_read_block(current_superblock.block_bitmap_start, buf);
    
    uint32_t byte_off = bitmap_idx / 8;
    uint32_t bit_off = bitmap_idx % 8;
    buf[byte_off] &= ~(1 << bit_off);
    
    fs_write_block(current_superblock.block_bitmap_start, buf);

    current_superblock.free_block_count++;
    // write_superblock...
}

// 从父目录 parent_inode 中删除名为 name 的目录项
// 成功返回目标文件的 inode 号，失败返回 0
uint32_t remove_entry_from_parent(inode_t *parent_inode, char *name) {
    uint8_t buffer[FS_BLOCK_SIZE];
    
    for (int i = 0; i < 12; i++) {
        uint32_t block_id = parent_inode->direct_blocks[i];
        if (block_id == 0) break;

        fs_read_block(block_id, buffer);
        dentry_t *dentries = (dentry_t *)buffer;

        for (int j = 0; j < DENTRIES_PER_BLOCK; j++) {
            if (dentries[j].inode_number != 0 && 
                strcmp(dentries[j].file_name, name) == 0) {
                
                uint32_t target_inode = dentries[j].inode_number;

                // 删除
                dentries[j].inode_number = 0; // 标记为空闲
                memset(dentries[j].file_name, 0, MAX_FILE_NAME);
                
                // 写回磁盘
                fs_write_block(block_id, buffer);
                return target_inode;
            }
        }
    }
    return 0; // Not found
}

// 返回缓存池中的下标，如果没有找到返回 -1
int cache_lookup(uint32_t block_id) {
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        if (cache_pool[i].valid && cache_pool[i].block_id == block_id) {
            return i; // Cache Hit
        }
    }
    return -1; // Cache Miss
}

// 分配一个新的空闲缓存块 (假设永远不会满)
int cache_alloc() {
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        if (!cache_pool[i].valid) {
            return i;
        }
    }
    return 0;
}

void fs_read_block(uint32_t block_num, void *buf)
{
    // 1. 先查缓存
    int idx = cache_lookup(block_num);

    if (idx != -1) {
        // [Cache Hit] 命中：直接从内存拷贝到用户 buffer
        memcpy(buf, cache_pool[idx].data, FS_BLOCK_SIZE);
        return;
    }

    // 2. [Cache Miss] 未命中：调用底层驱动读取
    // 先找个空闲的缓存槽位
    int new_idx = cache_alloc();
    
    // 调用刚才改名的底层函数，把数据读到缓存里
    // 注意：disk_read_block 内部会处理 kva2pa
    disk_read_block(block_num, cache_pool[new_idx].data);

    // 更新元数据
    cache_pool[new_idx].block_id = block_num;
    cache_pool[new_idx].valid = 1;
    cache_pool[new_idx].dirty = 0; // 刚读上来是干净的

    // 3. 将数据拷贝给用户
    memcpy(buf, cache_pool[new_idx].data, FS_BLOCK_SIZE);
}

void fs_write_block(uint32_t block_num, const void *buf)
{
    // 1. 查缓存
    int idx = cache_lookup(block_num);

    // 如果没命中，分配一个新块
    // (Write Allocate 策略：先占个坑，后面直接覆盖数据)
    if (idx == -1) {
        idx = cache_alloc();
        cache_pool[idx].block_id = block_num;
        cache_pool[idx].valid = 1;
        // 如果是部分写，应该先 read 进来再修改。
        // 但 fs_write_block 接口语义是覆盖整个块，所以直接覆盖即可，不需要预读。
    }

    // 2. 更新缓存内存
    memcpy(cache_pool[idx].data, buf, FS_BLOCK_SIZE);

    // 3. 根据策略处理
    if (page_cache_policy == CACHE_WT) {
        // [Write Through] 立即写穿到磁盘
        disk_write_block(block_num, cache_pool[idx].data);
        cache_pool[idx].dirty = 0;
    } 
    else {
        // [Write Back] 标记为脏，稍后处理
        cache_pool[idx].dirty = 1;
    }
}

void fs_sync(void)
{
    for (int i = 0; i < CACHE_CAPACITY; i++) {
        // 只有 有效 且 脏 的块才写回
        if (cache_pool[i].valid && cache_pool[i].dirty) {
            // 调用底层驱动写回磁盘
            disk_write_block(cache_pool[i].block_id, cache_pool[i].data);
            
            // 清除脏标记
            cache_pool[i].dirty = 0;
        }
    }
}

void format_write_back_freq(char *buffer, const char *prefix, int time) {
    // 1. 写入前缀字符串
    int i = 0;
    while (prefix[i] != '\0') {
        buffer[i] = prefix[i];
        i++;
    }

    // 2. 将整数转换为字符串
    int temp = time;
    char num_buf[12]; // 假设整数不会超过 12 位
    int j = 0;

    if (temp == 0) {
        num_buf[j++] = '0';
    } else {
        if (temp < 0) {
            buffer[i++] = '-'; // 处理负号
            temp = -temp;
        }
        while (temp > 0) {
            num_buf[j++] = '0' + (temp % 10); // 提取最低位
            temp /= 10;
        }
    }

    // 3. 反转数字字符串
    while (j > 0) {
        buffer[i++] = num_buf[--j];
    }

    // 4. 添加字符串结束符
    buffer[i] = '\0';
}

void do_set_cache_policy(int policy, int time)
{   
    char *policy_str;
    int time_s;
    if (policy == CACHE_WT) {
        page_cache_policy = CACHE_WT;
        policy_str = "page_cache_policy = write through\n";
    } else {
        page_cache_policy = CACHE_WB;
        policy_str = "page_cache_policy = write back\n";
    }

    if (time > 0) {
        write_back_freq = time;
        time_s = time;
    } else {
        time_s = write_back_freq;
    }

    char buff[64];
    format_write_back_freq(buff, "write_back_freq = ", time_s);

    int fd = do_open("/proc/sys/vm", O_RDWR);
    do_lseek(fd, 0, SEEK_SET);
    do_write(fd, policy_str, strlen(policy_str));
    do_write(fd, buff, strlen(buff));
}