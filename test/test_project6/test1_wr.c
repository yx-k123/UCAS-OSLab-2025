#include <time.h>
#include <unistd.h>
#include <stdio.h>
#define BUF_SIZE 256
#define TEST_FILE "large_file.txt"

int main() {
    sys_touch("/large_file.txt");
    // 向文件中写入数据
    int fd = sys_open(TEST_FILE, O_RDWR);
    if (fd < 0) {
        printf("Failed to open %s for writing.\n", TEST_FILE);
        return -1;
    } else {
        char write_buf[BUF_SIZE];
        for (int i = 0; i < BUF_SIZE - 1; i++) {
            write_buf[i] = 'A' + (i % 26);
        }
        write_buf[BUF_SIZE - 1] = '\0';

        // 写入多次以增加文件大小
        for (int i = 0; i < 1024; i++) { // 大约写入 256KB
            sys_write(fd, write_buf, BUF_SIZE - 1);
        }
        sys_close(fd);
        printf("Wrote data to %s successfully.\n", TEST_FILE);
    }
}