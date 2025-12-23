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

// ==========================================
// 1. 协议定义 (强制对齐)
// ==========================================

#define STREAM_MAGIC      0x45
#define STREAM_FLAG_DAT   0x01
#define STREAM_FLAG_RSD   0x02
#define STREAM_FLAG_ACK   0x04
#define HEADERS_OFFSET    54 

// 字节序转换宏
#define ntohs(x) (((x) << 8) | ((x) >> 8))
#define htons(x) ntohs(x)
#define ntohl(x) ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | (((x) & 0xff0000) >> 8) | (((x) >> 24) & 0xff))
#define htonl(x) ntohl(x)

#pragma pack(1)
struct stream_hdr {
    uint8_t magic;
    uint8_t flags;
    uint16_t len;
    uint32_t seq;
};
#pragma pack()

// ==========================================
// 2. 数据结构
// ==========================================

typedef struct {
    uint32_t seq;
    uint16_t len;
    uint16_t offset;
    uint8_t data[1500];
    list_node_t list;
} stream_node_t;

static LIST_HEAD(stream_list);
static uint32_t expected_seq = 0;
// [新增] 记录上一个成功接收并处理的包的序号
static uint32_t last_valid_seq = 0; 
// [新增] 标记是否收到过数据，用于启动阶段判断
static int has_received_any = 0;

static uint8_t cached_headers[HEADERS_OFFSET]; 
static int has_cached_headers = 0;

#define NODE_POOL_SIZE 128
static stream_node_t node_pool[NODE_POOL_SIZE];
static int node_used[NODE_POOL_SIZE];

static stream_node_t* alloc_node() {
    for(int i=0; i<NODE_POOL_SIZE; i++) {
        if(!node_used[i]) {
            node_used[i] = 1;
            node_pool[i].offset = 0;
            return &node_pool[i];
        }
    }
    return NULL;
}

static void free_node(stream_node_t* node) {
    int index = node - node_pool;
    if(index >= 0 && index < NODE_POOL_SIZE) node_used[index] = 0;
}

static void send_control(uint8_t flags, uint32_t seq) {
    if (!has_cached_headers) return;
    uint8_t tx_buf[128];
    memcpy(tx_buf, cached_headers, HEADERS_OFFSET);

    // Swap MAC
    memcpy(tx_buf, cached_headers + 6, 6);
    uint8_t my_mac[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};
    memcpy(tx_buf + 6, my_mac, 6);

    // Swap IP
    memcpy(tx_buf + 26, cached_headers + 30, 4);
    memcpy(tx_buf + 30, cached_headers + 26, 4);

    // Swap Port
    memcpy(tx_buf + 34, cached_headers + 36, 2);
    memcpy(tx_buf + 36, cached_headers + 34, 2);

    struct stream_hdr *hdr = (struct stream_hdr *)(tx_buf + HEADERS_OFFSET);
    hdr->magic = STREAM_MAGIC;
    hdr->flags = flags;
    hdr->len = 0;
    hdr->seq = htonl(seq);

    e1000_transmit(tx_buf, HEADERS_OFFSET + sizeof(struct stream_hdr));
}

// 超时控制
static pcb_t *net_blocked_task = NULL;
static uint64_t net_wakeup_time = 0;
#define NET_TIMEOUT_TICKS 2000 

void net_check_timeout(void) {
    if (net_blocked_task && get_ticks() > net_wakeup_time) {
        do_unblock(&net_blocked_task->list);
        net_blocked_task = NULL;
    }
}

int do_net_recv_stream(void *buffer, int *nbytes)
{
    int wanted = *nbytes;
    int received = 0;
    uint8_t rx_temp[2048];

    if (wanted <= 0 || buffer == NULL) return -1;

    // 每次调用重置唤醒时间
    net_wakeup_time = 0;

    while (received < wanted) {
        
        // ---------------------------------------------------
        // 1. 尝试从缓冲队列消费数据
        // ---------------------------------------------------
        while (!list_empty(&stream_list)) {
            stream_node_t *node = list_entry(stream_list.next, stream_node_t, list);

            if (node->seq == expected_seq) {
                // 计算可拷贝大小
                int available = node->len - node->offset;
                int need = wanted - received;
                int copy_len = (need < available) ? need : available;

                memcpy((uint8_t*)buffer + received, node->data + node->offset, copy_len);
                
                received += copy_len;
                expected_seq += copy_len; // 期望序号推进
                node->offset += copy_len;

                // 节点耗尽才释放
                if (node->offset >= node->len) {
                    list_del(&node->list);
                    free_node(node);
                }

                if (received >= wanted) {
                    *nbytes = received;
                    return 0;
                }
            } else {
                // 遇到空洞 (Gap): node->seq > expected_seq
                break; 
            }
        }

        // ---------------------------------------------------
        // 2. 轮询网卡
        // ---------------------------------------------------
        int poll_len = e1000_poll(rx_temp);
        if (poll_len > 0) {
            if (poll_len > HEADERS_OFFSET + sizeof(struct stream_hdr)) {
                struct stream_hdr *hdr = (struct stream_hdr *)(rx_temp + HEADERS_OFFSET);
                
                if (hdr->magic == STREAM_MAGIC && (hdr->flags & STREAM_FLAG_DAT)) {
                    uint32_t seq = ntohl(hdr->seq);
                    uint16_t dlen = ntohs(hdr->len);

                    if (!has_cached_headers) {
                        memcpy(cached_headers, rx_temp, HEADERS_OFFSET);
                        has_cached_headers = 1;
                    }

                    // 接收处理：只处理 seq >= expected_seq 的包
                    if (seq >= expected_seq) {
                        // 查重
                        int is_duplicate = 0;
                        list_node_t *pos;
                        list_for_each(pos, &stream_list) {
                            stream_node_t *curr = list_entry(pos, stream_node_t, list);
                            if (curr->seq == seq) {
                                is_duplicate = 1;
                                break;
                            }
                        }

                        if (!is_duplicate) {
                            stream_node_t *new_node = alloc_node();
                            if (new_node) {
                                new_node->seq = seq;
                                new_node->len = dlen;
                                memcpy(new_node->data, rx_temp + HEADERS_OFFSET + sizeof(struct stream_hdr), dlen);
                                
                                // 插入排序
                                list_node_t *p = stream_list.next;
                                int inserted = 0;
                                while (p != &stream_list) {
                                    stream_node_t *curr = list_entry(p, stream_node_t, list);
                                    if (curr->seq > seq) {
                                        list_add_tail(&new_node->list, p);
                                        inserted = 1;
                                        break;
                                    }
                                    p = p->next;
                                }
                                if (!inserted) {
                                    list_add_tail(&new_node->list, &stream_list);
                                }
                            }
                        }
                    } else {
                        // 收到旧包 (seq < expected_seq)
                        // 这通常意味着我们的 ACK 丢了，发送方重传了。
                        // 我们应该立即补发一个 ACK，告诉它我们已经到了 expected_seq
                        send_control(STREAM_FLAG_ACK, expected_seq);
                    }
                }
            }
            continue; // 继续轮询直到读空
        }

        // ---------------------------------------------------
        // 3. 阻塞与控制包逻辑
        // ---------------------------------------------------
        
        if (received > 0) {
            break; // 已经读到部分数据，返回用户
        }

        // 此时 received == 0，准备阻塞
        
        // 策略：
        // 1. 如果链表非空，且第一个包序号 > expected_seq -> 说明丢包了 -> 发 RSD(expected)
        // 2. 如果链表为空 -> 可能是发得慢，也可能是丢包但还没收到后续包 -> 发 ACK(expected) 催促
        
        int need_rsd = 0;
        if (!list_empty(&stream_list)) {
            stream_node_t *head = list_entry(stream_list.next, stream_node_t, list);
            if (head->seq > expected_seq) {
                need_rsd = 1;
            }
        }

        if (need_rsd) {
            send_control(STREAM_FLAG_RSD, expected_seq);
        } else {
            // [关键修正] 发送 ACK expected_seq
            // 告诉发送方："我期望接收 expected_seq，请确认你是否发过或者窗口是否满了"
            send_control(STREAM_FLAG_ACK, expected_seq);
        }

        // 阻塞
        e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
        local_flush_dcache();

        net_blocked_task = current_running[get_current_cpu_id()];
        net_wakeup_time = get_ticks() + NET_TIMEOUT_TICKS; // 2ms - 10ms
        
        do_block(&net_blocked_task->list, &recv_block_queue);
        do_scheduler();

        net_blocked_task = NULL;
    }

    *nbytes = received;
    return 0;
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