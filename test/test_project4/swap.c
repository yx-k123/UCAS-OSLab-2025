#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

// 假设物理内存只有 100 页，我们申请 200 页的大小来强制换页
#define PAGE_SIZE 4096
#define TEST_PAGES 200
#define TEST_SIZE (TEST_PAGES * PAGE_SIZE)

int main() {
    sys_move_cursor(0, 0);
    printf("Starting Swap Test (Total %d Pages)...\n", TEST_PAGES);

    // 这里假设用大数组模拟
    int *array = (int *)0x50000000; // 找一个未使用的用户虚存区域

    // printf("Step 1: Writing data...\n");
    
    // 2. 顺序写入，触发缺页和分配
    for (int i = 0; i < TEST_SIZE / sizeof(int); i++) {
        // 每 40 页打印一次日志，减少刷屏
        if (i % (40 * PAGE_SIZE / sizeof(int)) == 0) {
            printf("> Writing page %d / %d\n", i / (PAGE_SIZE / sizeof(int)), TEST_PAGES);
        }
        array[i] = i; // 写入数据
    }
    printf("> Writing done.\n");

    // printf("Step 2: Verifying data...\n");

    // 3. 顺序读取（或随机读取），验证数据是否正确
    // 这会触发大量的 swap_in
    for (int i = 0; i < TEST_SIZE / sizeof(int); i++) {
        if (array[i] != i) {
            printf("ERROR at index %d: expected %d, got %d\n", i, i, array[i]);
            assert(0);
        }
        if (i % (40 * PAGE_SIZE / sizeof(int)) == 0) {
            printf("> Verified page %d / %d\n", i / (PAGE_SIZE / sizeof(int)), TEST_PAGES);
        }
    }
    printf("All data verified successfully!\n");
    return 0;
}