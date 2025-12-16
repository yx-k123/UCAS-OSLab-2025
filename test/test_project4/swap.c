#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// 假设物理内存只有 16 页，我们申请 32 页的大小来强制换页
#define PAGE_SIZE 4096
#define TEST_PAGES 32
#define TEST_SIZE (TEST_PAGES * PAGE_SIZE)

int main() {
    printf("Starting Swap Test...\n");

    // 1. 申请大内存（在你的 OS 中可能需要多次 malloc 或大数组）
    // 注意：如果你的 malloc 不支持大内存，可以用静态大数组
    // static char big_array[TEST_SIZE]; 
    
    // 这里假设用大数组模拟
    int *array = (int *)0x50000000; // 找一个未使用的用户虚存区域
    // 或者如果你实现了 sys_shmpage_get 等，也可以用那个

    printf("Writing data to %d pages...\n", TEST_PAGES);
    
    // 2. 顺序写入，触发缺页和分配
    for (int i = 0; i < TEST_SIZE / sizeof(int); i++) {
        // 每一页打印一次日志，方便观察
        if (i % (PAGE_SIZE / sizeof(int)) == 0) {
            printf("Writing page %d\n", i / (PAGE_SIZE / sizeof(int)));
        }
        array[i] = i; // 写入数据
    }

    printf("Write done. Now verifying data...\n");

    // 3. 顺序读取（或随机读取），验证数据是否正确
    // 这会触发大量的 swap_in
    for (int i = 0; i < TEST_SIZE / sizeof(int); i++) {
        if (array[i] != i) {
            printf("ERROR at index %d: expected %d, got %d\n", i, i, array[i]);
            assert(0);
        }
        if (i % (PAGE_SIZE / sizeof(int)) == 0) {
            printf("Verified page %d\n", i / (PAGE_SIZE / sizeof(int)));
        }
    }
    printf("All data verified successfully!\n");
    return 0;
}