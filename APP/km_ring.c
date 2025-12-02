#include "km_ring.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"
#include "SD_SWITCH.h"
#include "DS18B20.h"

#include <string.h>


/* Global queues */
KB_Queue_t kb_queue;
MS_Rel_Queue_t ms_rel_queue;
MS_Abs_Queue_t ms_abs_queue;
Resp_Queue_t resp_queue;

/* Initialize queues */
void Queue_Init(void) {
    kb_queue.head = kb_queue.tail = 0;
    ms_rel_queue.head = ms_rel_queue.tail = 0;
    ms_abs_queue.head = ms_abs_queue.tail = 0;
    resp_queue.head = resp_queue.tail = 0;
}

/* Push functions ¡ª overwrite oldest when full */
void Queue_Push_Keyboard(uint8_t *data) {
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[kb_queue.tail].data));
    kb_queue.tail = next;
}

void Queue_Push_MouseRel(uint8_t *data) {
    int next = (ms_rel_queue.tail + 1) % MS_REL_QUEUE_SIZE;
    if (next == ms_rel_queue.head) ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
    memcpy(ms_rel_queue.buf[ms_rel_queue.tail].data, data, sizeof(ms_rel_queue.buf[ms_rel_queue.tail].data));
    ms_rel_queue.tail = next;
}

void Queue_Push_MouseAbs(uint8_t *data) {
    int next = (ms_abs_queue.tail + 1) % MS_ABS_QUEUE_SIZE;
    if (next == ms_abs_queue.head) ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
    memcpy(ms_abs_queue.buf[ms_abs_queue.tail].data, data, sizeof(ms_abs_queue.buf[ms_abs_queue.tail].data));
    ms_abs_queue.tail = next;
}

void Queue_Push_Response(uint8_t *data, uint8_t len) {
    int next = (resp_queue.tail + 1) % RESP_QUEUE_SIZE;
    if (next == resp_queue.head) resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
    memcpy(resp_queue.buf[resp_queue.tail].data, data, len);
    resp_queue.buf[resp_queue.tail].len = len;
    resp_queue.tail = next;
}

/* Pop functions */
bool Queue_Pop_Keyboard(KB_Report_t *out) {
    if (kb_queue.head == kb_queue.tail) return false;
    memcpy(out->data, kb_queue.buf[kb_queue.head].data, sizeof(out->data));
    kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    return true;
}

bool Queue_Pop_MouseRel(MS_Rel_Report_t *out) {
    if (ms_rel_queue.head == ms_rel_queue.tail) return false;
    memcpy(out->data, ms_rel_queue.buf[ms_rel_queue.head].data, sizeof(out->data));
    ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
    return true;
}

bool Queue_Pop_MouseAbs(MS_Abs_Report_t *out) {
    if (ms_abs_queue.head == ms_abs_queue.tail) return false;
    memcpy(out->data, ms_abs_queue.buf[ms_abs_queue.head].data, sizeof(out->data));
    ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
    return true;
}

bool Queue_Pop_Response(Resp_Report_t *out) {
    if (resp_queue.head == resp_queue.tail) return false;
    memcpy(out->data, resp_queue.buf[resp_queue.head].data, resp_queue.buf[resp_queue.head].len);
    out->len = resp_queue.buf[resp_queue.head].len;
    resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
    return true;
}

/* Try to send all queued packets until the endpoint is busy or queue is empty */
void USB_SendFromQueues(void) {
    while (kb_queue.head != kb_queue.tail && USBFS_Endp_Busy[DEF_UEP1] == 0) {
        KB_Report_t *pkt = &kb_queue.buf[kb_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP1, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY)
            kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
        else break;
    }

    while (ms_rel_queue.head != ms_rel_queue.tail && USBFS_Endp_Busy[DEF_UEP2] == 0) {
        MS_Rel_Report_t *pkt = &ms_rel_queue.buf[ms_rel_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP2, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY)
            ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
        else break;
    }

    while (ms_abs_queue.head != ms_abs_queue.tail && USBFS_Endp_Busy[DEF_UEP3] == 0) {
        MS_Abs_Report_t *pkt = &ms_abs_queue.buf[ms_abs_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP3, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY)
            ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
        else break;
    }

    /* Attempt to send response packets (USBD endpoint). Keep head if endpoint busy. */
    while (resp_queue.head != resp_queue.tail) {
        Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
        if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0)
            resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
        else break;
    }
}
