#include <time.h>
#include <unistd.h>
#include <stdio.h>
#define BUF_SIZE 256
#define TEST_FILE "large_file.txt"

int main() {
    char buf[BUF_SIZE];
    int fd;
    long start, end;
    
    // --- 第一次读取 (Cold Read) ---
    start = clock();
    fd = sys_open(TEST_FILE, O_RDONLY);
    while (sys_read(fd, buf, BUF_SIZE) > 0); // 读完整个文件
    sys_close(fd);
    end = clock();
    printf("Cold Read Time: %ld ticks\n", end - start);

    // --- 第二次读取 (Warm Read) ---
    start = clock();
    fd = sys_open(TEST_FILE, O_RDONLY);
    while (sys_read(fd, buf, BUF_SIZE) > 0); // 再次读完
    sys_close(fd);
    end = clock();
    printf("Warm Read Time: %ld ticks\n", end - start);
}