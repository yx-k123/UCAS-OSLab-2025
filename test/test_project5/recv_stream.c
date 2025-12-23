#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define RECV_BUF_SIZE 65536 // 64KB

// Fletcher-16 校验和
uint16_t fletcher16(uint8_t *data, int len) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (int i = 0; i < len; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

// 简单的内存移动
void my_memmove(uint8_t *dest, const uint8_t *src, int len) {
    if (dest < src) {
        for (int i = 0; i < len; ++i) dest[i] = src[i];
    } else if (dest > src) {
        for (int i = len - 1; i >= 0; --i) dest[i] = src[i];
    }
}

int main(int argc, char *argv[]) {
    int total_len = 0;
    int file_size = 0x7FFFFFFF; // 默认很大
    int use_size_header = 0;
    static uint8_t buffer[RECV_BUF_SIZE]; // 使用静态分配或全局变量
    
    printf("Start receiving stream...\n");

    while (1) {
        // [关键] 计算剩余空间
        int max_recv = RECV_BUF_SIZE - total_len;
        if (max_recv <= 0) break;

        // [关键] 传入 buffer + total_len
        int nbytes = max_recv;
        sys_net_recv_stream(buffer + total_len, &nbytes);

        if (nbytes <= 0) continue;

        // 处理文件大小头 (仅第一次)
        if (total_len == 0 && nbytes >= 4) {
            // 解析前4字节为 int
            file_size = *(int *)buffer - 4; // 减去头部大小
            printf("File size from header: %d bytes\n", file_size);
            
            // 移除头部，将数据前移
            my_memmove(buffer, buffer + 4, nbytes - 4);
            nbytes -= 4; // 修正本次接收长度
            use_size_header = 1;
        }

        total_len += nbytes;
        printf("Received %d bytes, Total: %d / %d\n", nbytes, total_len, file_size);

        // 检查是否收完
        if (use_size_header && total_len >= file_size) {
            printf("Receive complete. Total %d bytes.\n", total_len);
            
            // 计算校验和
            uint16_t checksum = fletcher16(buffer, file_size);
            printf("fletcher16 = %d\n", checksum);
            break;
        }
    }
    return 0;
}