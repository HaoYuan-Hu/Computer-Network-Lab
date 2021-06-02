/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 *              In this implementation, the packet format is laid out as the following:
 *       
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rdt_struct.h"
#include "rdt_sender.h"

#define TIMEOUT 0.3
#define HEADER_SIZE 7
#define MAX_WINDOW_SIZE 10
#define MAX_BUFFER_SIZE 15000
#define MAX_PAYLOAD_SIZE (RDT_PKTSIZE - HEADER_SIZE)

// 消息缓存
static message* message_buffer;

// message_buffer中消息的数量
static int num_message;

// 当前正在处理的消息序号
static int current_message_seq;

// 下一个进入message_buffer的消息的序号
static int next_message_seq;

// 当前message已被拆分的byte数量
static int message_buffer_cursor;

// 存储待发送的数据包，大小为MAX_WINDOW_SIZE
static packet* packet_window;

// packet_window中的包的数量
static int num_packet;

// 下一个进入packet_window的数据包的序号
static int next_packet_seq;

// 下一个要被发送的数据包序号
static int next_packet_to_send_seq;

// 应该接收到的ack序号
static int expected_ack_seq;

static short Checksum(struct packet *pkt) {
    unsigned long checksum = 0; // 32位
    // 前两个字节为checksum区域，需要跳过
    for (int i = 2; i < RDT_PKTSIZE; i += 2) {
        checksum += *(short *)(&(pkt->data[i]));
    }
    while (checksum >> 16) { // 若sum的高16位非零
        checksum = (checksum >> 16) + (checksum & 0xffff);
    }
    return ~checksum;
}

void Chunk_Message() {
    int current_message_index = current_message_seq % MAX_BUFFER_SIZE;
    message msg = message_buffer[current_message_index];
    packet pkt;
    short checksum;
    
    // 每次拆出一个数据包并存入packct_window，直到packet_window满了，或message_buffer中不再有待拆分的消息
    while (num_packet < MAX_WINDOW_SIZE && current_message_seq < next_message_seq) {
        // 若剩余待拆分的byte数量太多，不能装入一个packet的payload
        if (msg.size - message_buffer_cursor > MAX_PAYLOAD_SIZE) {
            // 写包头和数据
            memcpy(pkt.data + sizeof(short), &next_packet_seq, sizeof(int));
            pkt.data[HEADER_SIZE - 1] = MAX_PAYLOAD_SIZE;
            memcpy(pkt.data + HEADER_SIZE, msg.data + message_buffer_cursor, MAX_PAYLOAD_SIZE);
            // 计算checksum
            checksum = Checksum(&pkt);
            memcpy(pkt.data, &checksum, sizeof(short));
            // 将pkt的内容存入packet_window，位置为next_packet_index
            int next_packet_index = next_packet_seq % MAX_WINDOW_SIZE;
            memcpy(packet_window[next_packet_index].data, &pkt, sizeof(packet));
            message_buffer_cursor += MAX_PAYLOAD_SIZE;
            ++next_packet_seq;
            ++num_packet;
        }
        // 剩余待拆分的byte可以一次性装入一个packet
        else if (message_buffer_cursor < msg.size) {
            // 写包头和数据
            memcpy(pkt.data + sizeof(short), &next_packet_seq, sizeof(int));
            pkt.data[HEADER_SIZE - 1] = msg.size - message_buffer_cursor;
            memcpy(pkt.data + HEADER_SIZE, msg.data + message_buffer_cursor, msg.size - message_buffer_cursor);
            // 计算checksum
            checksum = Checksum(&pkt);
            memcpy(pkt.data, &checksum, sizeof(short));
            // 将pkt的内容存入packet_window，位置为next_packet_index
            int next_packet_index = next_packet_seq % MAX_WINDOW_SIZE;
            memcpy(packet_window[next_packet_index].data, &pkt, sizeof(packet));
            ++next_packet_seq;
            ++num_packet;
            // 当前消息拆分完毕，将其从message_buffer中移除 
            ++current_message_seq;
            message_buffer_cursor = 0;
            --num_message;
            // 判断message_buffer中是否还有未拆分的message
            assert(current_message_seq + num_message == next_message_seq);
            if (current_message_seq < next_message_seq) {
                int current_message_index = current_message_seq % MAX_BUFFER_SIZE;
                msg = message_buffer[current_message_index];
            }
        }
        else {
            // SHOULD NOT BE HERE
            printf("ERROR: should not reach here in Chunk_Message() !");
            assert(0);
        }
    }
}

void Send_Packet() {
    packet pkt;
    // 每次发一个包，直到packet_window中的包全部发完
    while (next_packet_to_send_seq < next_packet_seq) {
        memcpy(&pkt, &(packet_window[next_packet_to_send_seq % MAX_WINDOW_SIZE]), sizeof(packet));
        Sender_ToLowerLayer(&pkt);
        ++next_packet_to_send_seq;
    }
}

/* sender initialization, called once at the very beginning */
void Sender_Init() {
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
    // 初始化message_buffer
    message_buffer = (message *)malloc((MAX_BUFFER_SIZE) * sizeof(message));
    memset(message_buffer, 0, (MAX_BUFFER_SIZE) * sizeof(message));
    num_message = 0;
    current_message_seq = 0;
    next_message_seq = 0;
    message_buffer_cursor = 0;

    // 初始化pakcet_window
    packet_window = (packet *)malloc(MAX_WINDOW_SIZE * sizeof(packet));
    memset(packet_window, 0, MAX_WINDOW_SIZE * sizeof(packet));
    num_packet = 0;
    next_packet_seq = 0;
    next_packet_to_send_seq = 0;
    expected_ack_seq = 0;
}

/* sender finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to take this opportunity to release some 
   memory you allocated in Sender_init(). */
void Sender_Final() {
    fprintf(stdout, "At %.2fs: sender finalizing ...\n", GetSimulationTime());
    for (int i = 0; i < MAX_BUFFER_SIZE; ++i) {
        if (message_buffer[i].size > 0) {
            free(message_buffer[i].data);
        }
    }
    free(message_buffer);
    free(packet_window);
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer(struct message *msg) {
    // 判断message_buffer是否还有余量
    if (num_message >= MAX_BUFFER_SIZE) {
        ASSERT(0);
    }

    // 将消息存入message_buffer，位置为next_message_index
    int next_message_index = next_message_seq % MAX_BUFFER_SIZE;
    if (message_buffer[next_message_index].size > 0) {
        message_buffer[next_message_index].size = 0;
        free(message_buffer[next_message_index].data);
    }
    // 因为msg拆分后的第一个数据包的payload中包含了msg.size的信息，所以比msg原本的size大了4个byte
    message_buffer[next_message_index].size = msg->size + sizeof(int);
    message_buffer[next_message_index].data = (char *)malloc(message_buffer[next_message_index].size);
    memcpy(message_buffer[next_message_index].data, &(msg->size), sizeof(int)); // 先将msg->size保存到data中
    memcpy(message_buffer[next_message_index].data + sizeof(int), msg->data, msg->size);
    ++next_message_seq;
    ++num_message;
    
    // 若计时器仍在计时，则不用操作，新到的message老老实实待在buffer里
    if (Sender_isTimerSet() == true) {
        return ;
    }

    // 计时器没有工作，则开始新一轮发包
    Sender_StartTimer(TIMEOUT);
    Chunk_Message();
    Send_Packet();
}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt) {
    // 检查checksum，校验失败则直接抛弃
    short checksum;
    memcpy(&checksum, pkt->data, sizeof(short));
    if (checksum != Checksum(pkt)) { // 校验失败
        return ;
    }

    int ack_seq;
    memcpy(&ack_seq, pkt->data + sizeof(short), sizeof(int));
    // 若收到的ack_seq在正常范围，则说明小于ack的packet都已经被receiver正常接收
    if (expected_ack_seq <= ack_seq && ack_seq < next_packet_seq) {
        num_packet -= (ack_seq - expected_ack_seq + 1); // 确认接收的包均从packet_window中可移除
        expected_ack_seq = ack_seq + 1;
        // 开始新一轮发包
        Sender_StartTimer(TIMEOUT); // 若上一轮计时没有结束，则该命令重置计时器
        Chunk_Message();
        Send_Packet();
    }
    // 若packet_window中所有的包都已确认被receiver接收，则结束计时器
    if (ack_seq == next_packet_seq - 1) {
        Sender_StopTimer();
    }
}

/* event handler, called when the timer expires */
void Sender_Timeout() {
    // 超时，该收到的ack包没收到，重发
    next_packet_to_send_seq = expected_ack_seq;
    Sender_StartTimer(TIMEOUT);
    Chunk_Message();
    Send_Packet();
}
