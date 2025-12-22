#include <e1000.h>
#include <os/net.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <printk.h>
#include <os/mm.h>
#include <os/time.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

// 协议定义
#define STREAM_MAGIC 0x45
#define STREAM_FLAG_DAT 0x01
#define STREAM_FLAG_RSD 0x02
#define STREAM_FLAG_ACK 0x04
#define STREAM_HDR_OFFSET 54

// 字节序转换宏
#define ntohs(x) (((x) << 8) | ((x) >> 8))
#define htons(x) ntohs(x)
#define ntohl(x) ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | (((x) & 0xff0000) >> 8) | (((x) >> 24) & 0xff))
#define htonl(x) ntohl(x)

struct stream_hdr {
    uint8_t magic;
    uint8_t flags;
    uint16_t len; // Big Endian
    uint32_t seq; // Big Endian
} __attribute__((packed));

// 接收缓存节点
typedef struct {
    uint32_t seq;
    uint16_t len;
    uint8_t data[1500];
    list_node_t list;
} stream_node_t;

static LIST_HEAD(stream_list);
static uint32_t expected_seq = 0;
static uint8_t cached_headers[54]; // 缓存 Ethernet + IP + TCP/UDP 头部
static int has_cached_headers = 0;

// 超时控制
static pcb_t *net_blocked_task = NULL;
static uint64_t net_wakeup_time = 0;
#define NET_TIMEOUT_TICKS 10000 // 10ms 

// 简单的内存池，避免频繁 malloc
#define NODE_POOL_SIZE 64
static stream_node_t node_pool[NODE_POOL_SIZE];
static int node_used[NODE_POOL_SIZE];

static stream_node_t* alloc_node() {
    for(int i=0; i<NODE_POOL_SIZE; i++) {
        if(!node_used[i]) {
            node_used[i] = 1;
            return &node_pool[i];
        }
    }
    return NULL; // Pool full
}

static void free_node(stream_node_t* node) {
    int index = node - node_pool;
    if(index >= 0 && index < NODE_POOL_SIZE) {
        node_used[index] = 0;
    }
}

// 发送控制包 (ACK 或 RSD)
static void send_control(uint8_t flags, uint32_t seq) {
    if (!has_cached_headers) return;

    uint8_t tx_buf[100];
    // 1. 复制头部
    memcpy(tx_buf, cached_headers, 54);

    // 2. 交换 MAC 地址
    // Dst = Src
    memcpy(tx_buf, cached_headers + 6, 6);
    // Src = Ours (00:0a:35:00:1e:53)
    uint8_t my_mac[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};
    memcpy(tx_buf + 6, my_mac, 6);

    // 3. 交换 IP 地址 (IP Header start at 14)
    // Src IP at 14+12=26, Dst IP at 14+16=30
    memcpy(tx_buf + 26, cached_headers + 30, 4);
    memcpy(tx_buf + 30, cached_headers + 26, 4);

    // 4. 交换端口 (TCP/UDP Header start at 34)
    // Src Port at 34, Dst Port at 36
    memcpy(tx_buf + 34, cached_headers + 36, 2);
    memcpy(tx_buf + 36, cached_headers + 34, 2);

    // 5. 构造自定义协议头
    struct stream_hdr *hdr = (struct stream_hdr *)(tx_buf + 54);
    hdr->magic = STREAM_MAGIC;
    hdr->flags = flags;
    hdr->len = 0;
    hdr->seq = htonl(seq);

    // 6. 发送
    e1000_transmit(tx_buf, 54 + 8);
}

int do_net_recv_stream(void *buffer, int *nbytes)
{
    int wanted = *nbytes;
    int received = 0;
    uint8_t rx_temp[2048];

    while (received < wanted) {
        // 1. 检查 stream_list 是否有连续数据
        while (!list_empty(&stream_list)) {
            stream_node_t *node = list_entry(stream_list.next, stream_node_t, list);
            if (node->seq == expected_seq) {
                // 复制数据到用户 buffer
                int copy_len = (wanted - received) < node->len ? (wanted - received) : node->len;
                memcpy((uint8_t*)buffer + received, node->data, copy_len);
                
                received += copy_len;
                expected_seq += node->len; // 注意：这里假设 seq 是字节偏移，且我们消耗了整个包的逻辑长度
                
                // 移除节点
                list_del(&node->list);
                free_node(node);

                if (received >= wanted) 
                {
                    *nbytes = received;
                    return 0;
                }
            } else {
                break; // 序号不连续（有空洞）
            }
        }

        // 2. 轮询网卡
        int len = e1000_poll(rx_temp);
        if (len > 0) {
            // 解析包
            if (len > STREAM_HDR_OFFSET + 8) {
                struct stream_hdr *hdr = (struct stream_hdr *)(rx_temp + STREAM_HDR_OFFSET);
                if (hdr->magic == STREAM_MAGIC && (hdr->flags & STREAM_FLAG_DAT)) {
                    uint32_t seq = ntohl(hdr->seq);
                    uint16_t dlen = ntohs(hdr->len);

                    // 缓存头部用于回包
                    if (!has_cached_headers) {
                        memcpy(cached_headers, rx_temp, 54);
                        has_cached_headers = 1;
                    }

                    if (seq == expected_seq) {
                        // 正好是期望的包，直接放入 list 头部处理（简化逻辑，统一走 list）
                        stream_node_t *node = alloc_node();
                        if (node) {
                            node->seq = seq;
                            node->len = dlen;
                            memcpy(node->data, rx_temp + STREAM_HDR_OFFSET + 8, dlen);
                            list_add_head(&node->list, &stream_list); // 加到头部
                        }
                    } else if (seq > expected_seq) {
                        // 乱序包，插入排序
                        stream_node_t *new_node = alloc_node();
                        if (new_node) {
                            new_node->seq = seq;
                            new_node->len = dlen;
                            memcpy(new_node->data, rx_temp + STREAM_HDR_OFFSET + 8, dlen);
                            
                            // 查找插入位置
                            list_node_t *pos = stream_list.next;
                            int inserted = 0;
                            while (pos != &stream_list) {
                                stream_node_t *curr = list_entry(pos, stream_node_t, list);
                                if (curr->seq == seq) {
                                    free_node(new_node); // 重复包
                                    inserted = 1;
                                    break;
                                }
                                if (curr->seq > seq) {
                                    list_add_tail(&new_node->list, pos);
                                    inserted = 1;
                                    break;
                                }
                                pos = pos->next;
                            }
                            if (!inserted) {
                                list_add_tail(&new_node->list, &stream_list);
                            }
                        }
                    }
                    // seq < expected_seq 是旧包，忽略
                }
            }
            continue; // 继续轮询，直到读空
        }

        // 3. 如果没有读到数据，且还没有满足用户请求
        if (received == 0) {
            // 发送 ACK 确认当前进度
            send_control(STREAM_FLAG_ACK, expected_seq);

            // 如果有空洞（list 不为空但头部不是 expected），发送 RSD
            if (!list_empty(&stream_list)) {
                stream_node_t *head = list_entry(stream_list.next, stream_node_t, list);
                if (head->seq > expected_seq) {
                    send_control(STREAM_FLAG_RSD, expected_seq);
                }
            }

            // 阻塞等待（带超时）
            e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
            local_flush_dcache();
            
            net_blocked_task = current_running[get_current_cpu_id()];
            net_wakeup_time = get_ticks() + NET_TIMEOUT_TICKS;
            
            do_block(&net_blocked_task->list, &recv_block_queue);
            do_scheduler();
            // 唤醒后清除超时标记
            net_blocked_task = NULL;
        } else {
            // 已经收到了一些数据，返回给用户
            break;
        }
    }

    *nbytes = received;
    return 0;
}

// 在定时器中断中调用
void net_check_timeout(void) {
    if (net_blocked_task && get_ticks() > net_wakeup_time) {
        // 超时，唤醒任务以发送 RSD
        do_unblock(&net_blocked_task->list);
        net_blocked_task = NULL;
    }
}

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
            printk("block on send\n");
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