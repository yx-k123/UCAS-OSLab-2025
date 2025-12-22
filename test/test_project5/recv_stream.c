#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define RECV_BUF_SIZE 65536 // 64KB 接收缓冲区

// Fletcher-16 校验和算法
// 这里的实现参考了常见的 Fletcher-16 算法 (Mod 255)
uint16_t fletcher16(uint8_t *data, int len) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (int i = 0; i < len; ++i) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (sum2 << 8) | sum1;
}

void memmove(uint8_t *dest, const uint8_t *src, int len) {
    if (dest < src) {
        for (int i = 0; i < len; ++i) {
            dest[i] = src[i];
        }
    } else if (dest > src) {
        for (int i = len - 1; i >= 0; --i) {
            dest[i] = src[i];
        }
    }
}

int main(int argc, char *argv[]) {
    int total_len = 0;
    int file_size = 0x7FFFFFFF; // 默认接收无限多
    int use_size_header = 0;

    // 如果指定了参数，则尝试解析大小头
    if (argc > 1 && strcmp(argv[1], "-f") == 0) {
        use_size_header = 1;
    }

    static char buffer[RECV_BUF_SIZE]; 
    
    printf("Start receiving stream...\n");

    while (1) {
        int nbytes = RECV_BUF_SIZE; // 每次尝试接收最大缓冲区大小
        sys_net_recv_stream(buffer, &nbytes);

        if (nbytes <= 0) {
            // 暂时没有数据
            continue;
        }

        // 处理第一个包的 Size 头
        if (total_len == 0 && use_size_header) {
            if (nbytes >= 4) {
                file_size = *(int *)buffer;
                printf("File size from header: %d bytes\n", file_size);
                // 移除头部 4 字节
                memmove(buffer, buffer + 4, nbytes - 4);
                nbytes -= 4;
            }
        }

        total_len += nbytes;
        printf("Received %d bytes, Total: %d\n", nbytes, total_len);
        if (use_size_header && total_len >= file_size) {
            printf("Receive complete. Total %d bytes.\n", total_len);
            printf("fletcher16 = %d", fletcher16((uint8_t *)buffer, nbytes));
            return 0;
        }
        
        if (total_len > 10 * 1024 * 1024) { // 10MB limit for test
            printf("Limit reached.\n");
            break;
        }
    }

    printf("Receive complete. Total %d bytes.\n", total_len);
    return 0;
}