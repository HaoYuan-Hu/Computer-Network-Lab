/*
 * FILE: rdt_receiver.cc
 * DESCRIPTION: Reliable data transfer receiver. 
 *              In this implementation, the packet format is laid out as the following:
 *       
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rdt_struct.h"
#include "rdt_receiver.h"

#define HEADER_SIZE (sizeof(short) + sizeof(int) + sizeof(char))
#define WINDOW_SIZE 10

// 当前正在接受的message
static message* current_message;

// 当前message已经构建完的byte数量
static int message_cursor;

// 接收方的数据包缓存
static packet* receiver_buffer;

// 数据包缓存的有效位
static char* buffer_validation;

// 应该收到的packet seq
static int expected_packet_seq;

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

void Send_Ack(int ack) {
    packet ack_packet; // 其余位不用置零，有checksum保证正确性
    memcpy(ack_packet.data + sizeof(short), &ack, sizeof(int));
    short checksum = Checksum(&ack_packet);
    memcpy(ack_packet.data, &checksum, sizeof(short));
    Receiver_ToLowerLayer(&ack_packet);
}


/* receiver initialization, called once at the very beginning */
void Receiver_Init() {
    fprintf(stdout, "At %.2fs: receiver initializing ...\n", GetSimulationTime());

    // 初始化current_message
    current_message = (message *)malloc(sizeof(message));
    memset(current_message, 0, sizeof(message));

    // 初始化receiver_buffer和buffer_validation
    receiver_buffer = (packet *)malloc(WINDOW_SIZE * sizeof(packet));
    buffer_validation = (char *)malloc(WINDOW_SIZE);
    memset(buffer_validation, 0, WINDOW_SIZE);

    // 其他初始化
    expected_packet_seq = 0;
    message_cursor = 0;
}

/* receiver finalization, called once at the very end.
   you may find that you don't need it, in which case you can leave it blank.
   in certain cases, you might want to use this opportunity to release some 
   memory you allocated in Receiver_init(). */
void Receiver_Final() {
    free(current_message);
    free(receiver_buffer);
    free(buffer_validation);
    fprintf(stdout, "At %.2fs: receiver finalizing ...\n", GetSimulationTime());
}

/* event handler, called when a packet is passed from the lower layer at the 
   receiver */
void Receiver_FromLowerLayer(struct packet *pkt)
{
    // 检查checksum，校验失败则直接抛弃
    short checksum;
    memcpy(&checksum, pkt->data, sizeof(short));
    if (checksum != Checksum(pkt)) { // 校验失败
        return ;
    }
    
    int current_packet_seq;
    memcpy(&current_packet_seq, pkt->data + sizeof(short), sizeof(int));
    if (expected_packet_seq < current_packet_seq && 
        current_packet_seq < expected_packet_seq + WINDOW_SIZE) {
        // 若条件允许，则存入接受者缓存
        int buffer_index = current_packet_seq % WINDOW_SIZE;
        if (buffer_validation[buffer_index] == 0) {
            memcpy(receiver_buffer[buffer_index].data, pkt->data, RDT_PKTSIZE);
            buffer_validation[buffer_index] = 1;
        }
        Send_Ack(expected_packet_seq - 1);
        return ;
    }
    else if (current_packet_seq != expected_packet_seq) {
        Send_Ack(expected_packet_seq - 1);
        return ;
    }
    else if (current_packet_seq == expected_packet_seq) { // 收到了想要的数据包
        int payload_size;
        while(1) {
            ++expected_packet_seq;
            // memcpy(&payload_size, pkt->data + sizeof(short) + sizeof(int), sizeof(char));
            // WAERNING: 如果用上面的方法给payload_size赋值会出错！
            payload_size = pkt->data[HEADER_SIZE - 1];

            // 判断是不是第一个包，并将payload写入current_message
            if (message_cursor == 0) {
                if (current_message->size != 0) {
                    current_message->size = 0;
                    free(current_message->data);
                }
                payload_size -= sizeof(int); // 减去message_size占用的4个byte
                memcpy(&(current_message->size), pkt->data + HEADER_SIZE, sizeof(int));
                current_message->data = (char *)malloc(current_message->size);
                memcpy(current_message->data + message_cursor, pkt->data + HEADER_SIZE + sizeof(int), payload_size);
                message_cursor += payload_size;
            }
            else {
                memcpy(current_message->data + message_cursor, pkt->data + HEADER_SIZE, payload_size);
                message_cursor += payload_size;
            }

            // 检查current_message是否构建完成
            if (message_cursor == current_message->size) {
                Receiver_ToUpperLayer(current_message);
                message_cursor = 0;
            }

            // 检查receiver_buffer中有无可用的、且刚好和expected_seq对应的数据包
            int buffer_index = expected_packet_seq % WINDOW_SIZE;
            if (buffer_validation[buffer_index] == 1) {
                pkt = &receiver_buffer[buffer_index];
                memcpy(&current_packet_seq, pkt->data + sizeof(short), sizeof(int));
                buffer_validation[buffer_index] = 0;
            }
            else { // 缓存中没有可用数据包，则发回ack，结束
                Send_Ack(current_packet_seq);
                return ;
            }
        }
    }
    else {
        // SHOULD NOT BE HERE !
        printf("ERROR: SHOULD NOT REACH HERE IN RECEIVER !");
        assert(0);
    }
}