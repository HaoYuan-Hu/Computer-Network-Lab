/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-  1 byte  ->|<-  1 byte  ->|<-  1 byte  ->|<-             the rest            ->|
 *       | payload size | packet  kind |seq/ack number|  check  sum  |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <queue>

#include "rdt_struct.h"
#include "rdt_sender.h"

using namespace std;


/* 滑动窗口图示 (MAX_SEQ = 4)： 
 *  ————————————————————————————————————————
 * - | 0 - 1 - *2 - 3 | - 4 - 0 - 1 - 2 - 3 -
 *  ————————————————————————————————————————
 * 
 * ack_expected = 0,
 * next_frame_to_send = 3,
 * nbuffered = 4,
*/

/* sender 用来存储 message 信息的 buffer */
queue<packet> sender_buffer;

/* sender 发送 packet 时的滑动窗口 */
queue<packet> sliding_window;

/* sender 下一个要发的 frame 的编号(0 ~ MAX_SEQ)，也可以理解为滑动窗口指针的下一个元素 */
unsigned int next_frame_to_send;

/* sender 期待的下一个从 receiver 接收的 ack(0 ~ MAX_SEQ)，也可以理解为滑动窗口的第一个元素 */
unsigned int ack_expected;

/* sender 的滑动窗口目前的大小(0 ~ MAX_SEQ)，也可以理解为指针目前包括了多少元素 */
unsigned int nbuffered;

/* 判断 b 是否被 a 和 c 夹在中间（想象一个圆的情况） */
bool between(unsigned int a, unsigned int b, unsigned int c)
{
    if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a))) {
        return true;
    }
    else {
        return false;
    }
}

/* 计算校验和 */
unsigned short Caulate_CheckSum(unsigned short *addr,int len) 
{
    /* 拿到数据地址，这里的地址被强制转换成 unsigned short */
    unsigned short *w = addr;

    /* 要校验的数据的长度 */
    unsigned int len_tmp = len;

    /* 计算数据中每两个 short 的和 */
    unsigned int sum = 0;
    short answer = 0;

    while (len_tmp > 1) {
        sum = sum + *(w++);
        len_tmp = len_tmp - 2;
    }

    /* 如果长度为奇数个，计算最后一个 byte */
    if (len_tmp == 1) {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum = sum + answer;
    }


    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);
    answer = ~sum;

    return answer;
}


/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
    
    // 初始化 sender 的全局变量
    ack_expected = 0;
    next_frame_to_send = 0;
    nbuffered = 0;

    fprintf(stdout, "At %.2fs: sender initialize finished ...\n", GetSimulationTime());
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some 
   memory you allocated in Sender_init(). */
void Sender_Final()
{
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());

    // 清空 sender_buffer
    while (!sender_buffer.empty()) {
        sender_buffer.pop();
    }

    fprintf(stdout, "At %.2fs: sender finalize finished ...\n", GetSimulationTime());
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer(struct message *msg)
{
    /* 用 1 个 byte 表示 payload 的长度 */
    int header_size = 1;

    /* 用 1 个 byte 表示 packet 的种类 */
    int header_kind = 1;

    /* 用 1 个 byte 表示 seq 数 */
    int header_seq = 1;

    /* 用 1 个 byte 表示 cksum */
    int header_cksum = 1;

    /* payload 的最大长度 */
    int maxpayload_size = RDT_PKTSIZE - header_size - header_kind - header_seq - header_cksum;

    /* 可重用的 packet 数据结构 */
    packet pkt;

    /* cursor 指向 message 的第一个没被发送的字符 */
    int cursor = 0;

    /* 把过大的 message 分成 maxpayload_size 大小的 packet，全部填充进 sender_buffer */
    while (msg->size-cursor > maxpayload_size) {
        
        /* 填充 packet */
        pkt.data[0] = maxpayload_size;
        pkt.data[1] = DATA;
        pkt.data[2] = next_frame_to_send;
        pkt.data[3] = Calculate_Checksum();
        memcpy(pkt.data+header_size+header_kind+header_seq, msg->data+cursor, maxpayload_size);

        /* 把 packet 压入缓存队列中 */
        sender_buffer.push(pkt);

        /* 移动 cursor */
        cursor += maxpayload_size;

        /* next_frame_to_send 移动到下一个 */
        inc(next_frame_to_send);
    }

    /* 填充最后一个 packet 进 sender_buffer，这里一定不够一个 maxpayload_size 了 */
    if (msg->size > cursor) {
        
        /* 填充 packet */
        pkt.data[0] = msg->size-cursor;
        pkt.data[1] = DATA;
        pkt.data[2] = next_frame_to_send;
        memcpy(pkt.data+header_size+header_kind+header_seq, msg->data+cursor, pkt.data[0]);

        /* 把 packet 压入缓存队列中 */
        sender_buffer.push(pkt);
    }


    /* 检查 sender_buffer 中是否还有数据，有的话就继续填充滑动窗口 */
    while ((!sender_buffer.empty())) {

        /* 如果滑动窗口已满，则跳出 */
        if (nbuffered >= MAX_SEQ) {
            break;
        }

        /* 如果滑动窗口未满，则填入数据并发送 */

        /* 取 sender_buffer 中的数据 */
        pkt = sender_buffer.front();
        sender_buffer.pop();

        /* 把 sender_buffer 中的数据添加到滑动窗口中 */
        sliding_window.push(pkt);

        /* 更新窗口大小，发送! */
        nbuffered = nbuffered + 1;
        Sender_ToLowerLayer(&pkt);

        /* 需要注意的是，为了避免实现复杂的一个 clock 实现多个 timer 的操作 */
        /* 这里我们直接取 SenderTimer 最早的那个作为 SenderTime，也就是设置的第一个 SenderTimer */
        /* 因为第一个窗口 TIME_OUT 是最严格的那个 */
        if (!Sender_isTimerSet()) {
            Sender_StartTimer(TIME_OUT);
        }
    }

}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
    /* 可重用的 packet 数据结构 */
    packet pkt_tmp;

    /* 我们接收到了一个 pkt，首先解析它 */
    int pkt_size = pkt->data[0];
    int pkt_kind = pkt->data[1];
    int pkt_ack = pkt->data[2];

    /* 类型如果是 ACK */
    if (pkt_kind == ACK) {

        /* 窗口开始滑动，直到滑动到 pkt_ack 作为滑动窗口的头部 */
        while (between(ack_expected, pkt_ack, next_frame_to_send)) {
            
            /* 当前窗口大小 - 1 */
            nbuffered = nbuffered - 1;
            
            /* 滑动窗口头部出队 */
            sliding_window.pop();
            
            /* 停止第一个窗口的计时 */
            if (Sender_isTimerSet()) {
                Sender_StopTimer();
            }

            /* 窗口向右滑动一格 */
            inc(ack_expected);
        }

        /* 检查 sender_buffer 中是否还有数据，有的话就继续填充滑动窗口 */
        while ((!sender_buffer.empty())) {

            /* 如果滑动窗口已满，则跳出 */
            if (nbuffered >= MAX_SEQ) {
                break;
            }

            /* 如果滑动窗口未满，则填入数据 */

            /* 获得 sender_buffer 的队列头部数据 */
            pkt_tmp = sender_buffer.front();
            sender_buffer.pop();

            /* 把 sender_buffer 中的数据添加到滑动窗口中 */
            sliding_window.push(pkt_tmp);
            nbuffered = nbuffered + 1;
        }

        /* 用来拿数据的临时结构 */
        queue<packet> sliding_window_tmp = sliding_window;

        /* 把当前滑动窗口的全部发送出去 */
        /* 比如原窗口为 1234 ，我们 ack 为 2，滑动窗口此时应为 3401 */
        /* 那么我们之前发过的 34 也要重发，因为我们要设置 3 的 Timer */
        for(int i = 0; i < nbuffered; i++) {
            
            /* 获取滑动窗口中的数据 */
            pkt_tmp = sliding_window_tmp.front();
            sliding_window_tmp.pop();

            /* 发送！*/
            Sender_ToLowerLayer(&pkt_tmp);

            /* 这里我们设置第一个窗口 TIME_OUT！ */
            if (!Sender_isTimerSet()) {
                Sender_StartTimer(TIME_OUT);
            }
        }

        return;
    }
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
    /* TimeOut 是残酷的，一旦发生，我们要将当前滑动窗口里的所有 packet 重发 */

    /* 我们先将当前的 Sender_Timer 停下来 */
    if (Sender_isTimerSet()) {
        Sender_StopTimer();
    }

    /* 可重用的 packet 数据结构 */
    packet pkt;

    /* 用来拿数据的临时结构 */
    queue<packet> sliding_window_tmp = sliding_window;

    for (int i = 0; i < nbuffered; i++) {
        
        /* 获取滑动窗口中的数据 */
        pkt = sliding_window_tmp.front();
        sliding_window_tmp.pop();

        /* 发送！ */
        Sender_ToLowerLayer(&pkt);

        /* 这里我们同样设置 TIME_OUT！ */
        if (!Sender_isTimerSet()) {
            Sender_StartTimer(TIME_OUT);
        }
    }
}
