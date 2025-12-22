#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define PACKET_SIZE 64  // 每个数据包的大小
#define PACKET_COUNT 1000  // 要发送的数据包数量

int main()
{
    char packet[PACKET_SIZE];
    memset(packet, 0xAB, sizeof(packet));  // 填充数据包内容为 0xAB

    for (int i = 0; i < PACKET_COUNT; i++) {
        // 在数据包中加入序号，方便调试
        ((int *)packet)[0] = i;

        // 调用发送系统调用
        sys_net_send(packet, PACKET_SIZE);
    }

    printf("Finished sending packets.\n");
    return 0;
}