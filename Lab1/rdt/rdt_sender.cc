/*
 * FILE: rdt_sender.cc
 * DESCRIPTION: Reliable data transfer sender.
 * NOTE: This implementation assumes there is no packet loss, corruption, or 
 *       reordering.  You will need to enhance it to deal with all these 
 *       situations.  In this implementation, the packet format is laid out as 
 *       the following:
 *       
 *       |<-  1 byte  ->|<-             the rest            ->|
 *       | payload size |<-             payload             ->|
 *
 *       The first byte of each packet indicates the size of the payload
 *       (excluding this single-byte header)
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdt_struct.h"
#include "rdt_sender.h"


/* 滑动窗口图示 (MAX_SEQ = 4)： 
 *  ————————————————————————————————————————
 * - | 0 - 1 - *2 - 3 | - 4 - 0 - 1 - 2 - 3 -
 *  ————————————————————————————————————————
 * 
 * ack_expected = 0,
 * next_frame_to_send = 3,
 * nbuffered = 4,
*/

/* sender 的滑动窗口 */
packet sender_buffer[MAX_SEQ + 1];

/* sender 下一个要发的 frame 的编号(0 ~ MAX_SEQ)，也可以理解为滑动窗口指针的下一个元素 */
unsigned int next_frame_to_send;

/* sender 期待的下一个从 receiver 接收的 ack(0 ~ MAX_SEQ)，也可以理解为滑动窗口的第一个元素 */
unsigned int ack_expected;

/* sender 的滑动窗口目前的大小(0 ~ MAX_SEQ)，也可以理解为指针目前包括了多少元素 */
unsigned int nbuffered;


/* sender initialization, called once at the very beginning */
void Sender_Init()
{
    fprintf(stdout, "At %.2fs: sender initializing ...\n", GetSimulationTime());
    
    // 启用网络
    // enable_network_layer();
    
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
    
    // 禁用网络 
    // disable_network_layer();

    fprintf(stdout, "At %.2fs: sender finalize finished ...\n", GetSimulationTime());
}

/* event handler, called when a message is passed from the upper layer at the 
   sender */
void Sender_FromUpperLayer(struct message *msg)
{
    /* 1-byte header indicating the size of the payload */
    int header_size = 1;

    /* maximum payload size */
    int maxpayload_size = RDT_PKTSIZE - header_size;

    /* split the message if it is too big */
    // maxpayload_size 为 127，把 msg 打碎成很多段长为 127 的片段

    /* reuse the same packet data structure */
    packet pkt;

    /* the cursor always points to the first unsent byte in the message */
    int cursor = 0;

    while (msg->size-cursor > maxpayload_size) {
        /* fill in the packet */
        pkt.data[0] = maxpayload_size;
        memcpy(pkt.data+header_size, msg->data+cursor, maxpayload_size);

        /* send it out through the lower layer */
        Sender_ToLowerLayer(&pkt);

        /* move the cursor */
        cursor += maxpayload_size;
    }

    /* send out the last packet */
    if (msg->size > cursor) {
	/* fill in the packet */
	pkt.data[0] = msg->size-cursor;
	memcpy(pkt.data+header_size, msg->data+cursor, pkt.data[0]);

	/* send it out through the lower layer */
	Sender_ToLowerLayer(&pkt);
    }
}

/* event handler, called when a packet is passed from the lower layer at the 
   sender */
void Sender_FromLowerLayer(struct packet *pkt)
{
}

/* event handler, called when the timer expires */
void Sender_Timeout()
{
}
