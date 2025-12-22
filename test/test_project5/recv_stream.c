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

int main() {
    int total_len = 0;
    int file_size = 0;
    int received_bytes = 0;
    
    // 分配大缓冲区用于存储整个文件（或者分块计算校验和）
    // 为了简化，这里假设内存足够存下整个文件，或者你可以边收边算
    // 这里演示先接收一部分解析头部，然后继续接收
    static char buffer[RECV_BUF_SIZE]; 
    
    printf("Start receiving stream...\n");

    // 1. 接收第一个包，解析文件大小
    int nbytes = RECV_BUF_SIZE;
    // 调用系统调用
    sys_net_recv_stream(buffer, &nbytes);

    if (nbytes >= 4) {
        // 解析文件大小 (假设是小端序或者协议规定的字节序，这里假设本地字节序与协议一致或已转换)
        // 协议中 seq 是 Big Endian，但 size 是应用层数据。
        // 题目描述："size 域不属于报头，按照本地字节序处理"
        file_size = *(int *)buffer;
        printf("File size: %d bytes\n", file_size);
        
        // 除去前4字节的 size，剩下的都是文件内容
        // 移动数据，覆盖掉 size，方便后续计算校验和（或者单独处理）
        // 这里我们把 size 算作文件内容之外的元数据，不参与校验和计算
        memmove(buffer, buffer + 4, nbytes - 4);
        total_len = nbytes - 4;
    } else {
        printf("Error: First packet too small to contain size.\n");
        return 0;
    }

    // 2. 循环接收剩余数据
    while (total_len < file_size) {
        int wanted = RECV_BUF_SIZE - total_len;
        // 如果缓冲区不够大，这里应该处理分块。为简化演示，假设缓冲区够大。
        // 实际大文件传输建议：边接收边 update checksum，不存储整个文件。
        
        // 这里演示边接收边计算校验和的逻辑会更通用：
        // 但为了简单，我们假设 pktRxTx 发送的文件不会超过 RECV_BUF_SIZE (64KB)
        // 如果超过，请自行改为增量计算 sum1 和 sum2
        
        int ret_len = wanted;
        sys_net_recv_stream(buffer + total_len, &ret_len);
        
        if (ret_len > 0) {
            total_len += ret_len;
            // printf("Received %d bytes, total %d/%d\n", ret_len, total_len, file_size);
        }
    }

    printf("Receive complete. Total %d bytes.\n", total_len);

    // 3. 计算校验和
    uint16_t checksum = fletcher16((uint8_t *)buffer, total_len);
    printf("Fletcher-16 Checksum: 0x%04x\n", checksum);

    return 0;
}