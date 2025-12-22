#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <printk.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

int do_net_send(void *txpacket, int length)
{
    // TODO: [p5-task1] Transmit one network packet via e1000 device
    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full

    while (1){
        int ret = e1000_transmit(txpacket, length);
        if (ret > 0) {
            return ret;  // 成功发送，返回发送的字节数
        } else {
            // 发送队列满，循环等待或阻塞当前任务
            // ...
            printk("> [NET] Transmit queue full, retrying...\n");
        }
    }
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    // TODO: [p5-task2] Receive one network packet via e1000 device
    // TODO: [p5-task3] Call do_block when there is no packet on the way
    int total_received_bytes = 0;

    for (int i = 0; i < pkt_num; i++) {
        int length = 0;
        while (1) {
            length = e1000_poll(rxbuffer + total_received_bytes);
            if (length > 0) {
                pkt_lens[i] = length;
                total_received_bytes += length;
                break; 
            } else {
                ;
            }
        }
    }

    return total_received_bytes;
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device
}