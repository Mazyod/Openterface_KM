#include "km_ring.h"
#include "ch32v20x_usbfs_device.h"
#include <string.h>

/* 全局队列 */
KB_Queue_t kb_queue;
MS_Rel_Queue_t ms_rel_queue;
MS_Abs_Queue_t ms_abs_queue;

/* 初始化队列 */
void Queue_Init(void) {
    kb_queue.head = kb_queue.tail = 0;
    ms_rel_queue.head = ms_rel_queue.tail = 0;
    ms_abs_queue.head = ms_abs_queue.tail = 0;
}

/* 入队函数 — 如果满则覆盖最旧 */
void Queue_Push_Keyboard(uint8_t *data) {
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) { kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE; }
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[kb_queue.tail].data));
    kb_queue.tail = next;
}

void Queue_Push_MouseRel(uint8_t *data) {
    int next = (ms_rel_queue.tail + 1) % MS_REL_QUEUE_SIZE;
    if (next == ms_rel_queue.head) { ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE; }
    memcpy(ms_rel_queue.buf[ms_rel_queue.tail].data, data, sizeof(ms_rel_queue.buf[ms_rel_queue.tail].data));
    ms_rel_queue.tail = next;
}

void Queue_Push_MouseAbs(uint8_t *data) {
    int next = (ms_abs_queue.tail + 1) % MS_ABS_QUEUE_SIZE;
    if (next == ms_abs_queue.head) { ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE; }
    memcpy(ms_abs_queue.buf[ms_abs_queue.tail].data, data, sizeof(ms_abs_queue.buf[ms_abs_queue.tail].data));
    ms_abs_queue.tail = next;
}

/* 发送队列数据包（只尝试队列头，发送失败保留） */
void USB_SendFromQueues(void) {
    // 键盘队列
    if (kb_queue.head != kb_queue.tail && USBFS_Endp_Busy[DEF_UEP1] == 0) {
        KB_Report_t *pkt = &kb_queue.buf[kb_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP1, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
        }
    }

    // 鼠标相对队列
    if (ms_rel_queue.head != ms_rel_queue.tail && USBFS_Endp_Busy[DEF_UEP2] == 0) {
        MS_Rel_Report_t *pkt = &ms_rel_queue.buf[ms_rel_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP2, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
        }
    }

    // 鼠标绝对队列
    if (ms_abs_queue.head != ms_abs_queue.tail && USBFS_Endp_Busy[DEF_UEP3] == 0) {
        MS_Abs_Report_t *pkt = &ms_abs_queue.buf[ms_abs_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP3, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
        }
    }
}
