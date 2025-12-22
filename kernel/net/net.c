#include "os/irq.h"
#include <e1000.h>
#include <os/net.h>
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
    while (1) {
        // 尝试发送数据包
        int ret = e1000_transmit(txpacket, length);
        
        if (ret > 0) {
            return ret;  // 成功发送，返回发送的字节数
        } else {
            // 发送队列满，开启 TXQE 中断以便在队列有空位时被唤醒
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_TXQE);
            local_flush_dcache();
            // 阻塞当前进程，加入发送阻塞队列
            do_block(&current_running[get_current_cpu_id()]->list, &send_block_queue);
            do_scheduler();

            // 被唤醒后，循环继续，再次尝试 e1000_transmit
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
            // 尝试轮询接收数据包
            length = e1000_poll(rxbuffer + total_received_bytes);
            
            if (length > 0) {
                // 成功接收到一个包
                pkt_lens[i] = length;
                total_received_bytes += length;
                break; // 跳出 while 循环，准备接收下一个包（for 循环）
            } else {
                // 当前没有数据包，开启接收中断 (RXDMT0)
                e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
                local_flush_dcache();
                // 阻塞当前进程，加入接收阻塞队列
                do_block(&current_running[get_current_cpu_id()]->list, &recv_block_queue);
                do_scheduler();

                // 被唤醒后，循环继续，再次尝试 e1000_poll
            }
        }
    }

    return total_received_bytes;
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device
    // 读取中断原因寄存器 (ICR)，读操作会自动清除中断位
    local_flush_dcache();
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);

    // printk("icr = %d\n", icr);

    // 处理发送队列空 (TXQE)
    if (icr & E1000_ICR_TXQE) {
        e1000_handle_txqe();
    }

    // 处理接收相关中断 (RXDMT0)
    if (icr & E1000_ICR_RXDMT0) {
        e1000_handle_rxdmt0();
    }
}

void e1000_handle_txqe(void){
    if (!list_empty(&send_block_queue)) {
        list_node_t *node_to_wake = send_block_queue.next;
        do_unblock(node_to_wake);
    }
    e1000_write_reg(e1000, E1000_IMC, E1000_IMS_TXQE);
    local_flush_dcache();
}

void e1000_handle_rxdmt0(void){
    // printk("e1000_handle_rxdmt0\n");
    if (!list_empty(&recv_block_queue)) {
        list_node_t *node_to_wake = recv_block_queue.next;
        do_unblock(node_to_wake);
    }
    e1000_write_reg(e1000, E1000_IMC, E1000_IMS_RXDMT0);
    local_flush_dcache();
}