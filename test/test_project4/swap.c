#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

#define PAGE_SIZE 4096
// 假设可用物理页约为 30 页 (除去内核占用)
// 我们用 26 页来填满它
#define MAX_MEM_PAGES 14

int main() {
    sys_move_cursor(0, 0);
    printf("=== Clock Algorithm Verification Test ===\n");

    volatile int *array = (int *)0x50000000;
    int i;
    volatile int temp;

    // Step 1: Fill the memory (Page 0 ~ Page 25)
    // 此时 Page 0 是最老的页面
    printf("[Step 1] Filling memory with %d pages...\n", MAX_MEM_PAGES);
    for (i = 0; i < MAX_MEM_PAGES * PAGE_SIZE / sizeof(int); i += PAGE_SIZE / sizeof(int)) {
        array[i] = i; // Write to allocate page
    }
    printf("Memory filled. Page 0 is the oldest.\n");

    // Step 2: Make Page 0 HOT
    // 访问 Page 0，使其 Accessed = 1
    printf("[Step 2] Accessing Page 0 to make it HOT...\n");
    temp = array[0]; 
    temp = array[0];
    
    // 此时：
    // Page 0: Oldest, Hot
    // Page 1: 2nd Oldest, Cold (只在 Step 1 写过一次)

    // Step 3: Trigger Swap (Allocate Page 26)
    // 这将强制系统选择一个受害者
    printf("[Step 3] Allocating one more page to trigger swap...\n");
    // 写入一个新的地址，触发缺页和置换
    int new_index = MAX_MEM_PAGES * PAGE_SIZE / sizeof(int);
    for (int i = 0; i < 6; i++) {
        array[new_index + i * (PAGE_SIZE / sizeof(int))] = new_index + i * (PAGE_SIZE / sizeof(int));
    }
    // 观察内核日志：
    // 应该打印 "Page 0 is hot, skip it"，然后换出 Page 1

    // Step 4: Verify Page 0 is still in memory (Fast Access)
    // 如果 Page 0 被换出了，这里会触发 Swap In (慢)
    // 如果 Page 0 还在，这里会很快
    printf("[Step 4] Accessing Page 0 again...\n");
    temp = array[0];
    if (temp == 0) {
        printf("Page 0 data is correct.\n");
    }

    // Step 5: Verify Page 1 was swapped out
    // 如果 Clock 工作正常，Page 1 应该是受害者
    printf("[Step 5] Accessing Page 1...\n");
    temp = array[PAGE_SIZE / sizeof(int)];
    // 这里应该会触发 Swap In
    
    printf("Test finished. Please check kernel log.\n");
    return 0;
}