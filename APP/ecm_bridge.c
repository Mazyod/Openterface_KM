#include "include/ecm_bridge.h"
#include "include/Version_selection.h"

#if USB_WORK_MODE_BRIDGE_ECM

#include "ch32v20x_usbfs_device.h"
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include <string.h>

#define ECM_BRIDGE_MTU_FRAME_SIZE       1514u
#define ECM_BRIDGE_QUEUE_DEPTH          2u
#define ECM_BRIDGE_USBFS_PACKET_SIZE    64u
#define ECM_BRIDGE_USBD_PACKET_SIZE     DEF_USBD_MAX_PACK_SIZE
#define ECM_BRIDGE_TARGET_COMM_IF       4u
#define ECM_BRIDGE_HOST_COMM_IF         0u
#define ECM_BRIDGE_LINK_SPEED_BPS       12000000u

typedef struct {
    uint8_t data[ECM_BRIDGE_MTU_FRAME_SIZE];
    uint16_t len;
} ECM_Frame_t;

typedef struct {
    ECM_Frame_t frames[ECM_BRIDGE_QUEUE_DEPTH];
    volatile uint8_t rd;
    volatile uint8_t wr;
    volatile uint8_t count;
    volatile uint32_t overflow;
} ECM_FrameQueue_t;

typedef struct {
    uint8_t data[ECM_BRIDGE_MTU_FRAME_SIZE];
    uint16_t len;
    uint8_t drop;
    volatile uint32_t overflow;
} ECM_RxFrame_t;

static ECM_FrameQueue_t g_host_to_target;
static ECM_FrameQueue_t g_target_to_host;
static ECM_RxFrame_t g_host_rx_frame;
static ECM_RxFrame_t g_target_rx_frame;
static uint16_t g_target_tx_offset;
static uint16_t g_host_tx_offset;
static uint8_t g_target_zlp_pending;
static uint8_t g_host_zlp_pending;
static uint8_t g_target_notify_state;
static uint8_t g_host_notify_state;

extern uint8_t USBD_Endp1_Busy;
extern uint8_t USBD_Endp3_Busy;

static void queue_clear(ECM_FrameQueue_t *queue)
{
    queue->rd = 0;
    queue->wr = 0;
    queue->count = 0;
    queue->overflow = 0;
}

static void rx_frame_clear(ECM_RxFrame_t *frame)
{
    frame->len = 0;
    frame->drop = 0;
    frame->overflow = 0;
}

static uint8_t queue_push(ECM_FrameQueue_t *queue, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return 1;
    }
    if (queue->count >= ECM_BRIDGE_QUEUE_DEPTH) {
        queue->overflow++;
        return 0;
    }

    memcpy(queue->frames[queue->wr].data, data, len);
    queue->frames[queue->wr].len = len;
    queue->wr++;
    if (queue->wr >= ECM_BRIDGE_QUEUE_DEPTH) {
        queue->wr = 0;
    }
    queue->count++;
    return 1;
}

static ECM_Frame_t *queue_peek(ECM_FrameQueue_t *queue)
{
    if (queue->count == 0) {
        return 0;
    }
    return &queue->frames[queue->rd];
}

static void queue_pop(ECM_FrameQueue_t *queue)
{
    if (queue->count == 0) {
        return;
    }
    queue->rd++;
    if (queue->rd >= ECM_BRIDGE_QUEUE_DEPTH) {
        queue->rd = 0;
    }
    queue->count--;
}

static void rx_frame_append(ECM_RxFrame_t *frame,
                            ECM_FrameQueue_t *queue,
                            const uint8_t *data,
                            uint16_t len,
                            uint16_t max_packet)
{
    uint8_t end_of_frame = (len < max_packet) ? 1u : 0u;

    if (!frame->drop && len > 0) {
        if ((uint32_t)frame->len + len <= ECM_BRIDGE_MTU_FRAME_SIZE) {
            memcpy(&frame->data[frame->len], data, len);
            frame->len = (uint16_t)(frame->len + len);
        }
        else {
            frame->drop = 1;
            frame->overflow++;
        }
    }

    if (end_of_frame) {
        if (!frame->drop && frame->len > 0) {
            (void)queue_push(queue, frame->data, frame->len);
        }
        frame->len = 0;
        frame->drop = 0;
    }
}

static void put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint8_t make_notification(uint8_t state, uint8_t interface, uint8_t *packet)
{
    packet[0] = 0xA1;
    if (state == 1) {
        packet[1] = 0x00;
        put_u16(&packet[2], 1);
        put_u16(&packet[4], interface);
        put_u16(&packet[6], 0);
        return 8;
    }

    packet[1] = 0x2A;
    put_u16(&packet[2], 0);
    put_u16(&packet[4], interface);
    put_u16(&packet[6], 8);
    put_u32(&packet[8], ECM_BRIDGE_LINK_SPEED_BPS);
    put_u32(&packet[12], ECM_BRIDGE_LINK_SPEED_BPS);
    return 16;
}

static void poll_target_notification(void)
{
    uint8_t packet[16];
    uint8_t len;

    if (g_target_notify_state == 0 || USBFS_Endp_Busy[DEF_UEP5] != 0) {
        return;
    }

    len = make_notification(g_target_notify_state, ECM_BRIDGE_TARGET_COMM_IF, packet);
    if (USBFS_Endp_DataUp(DEF_UEP5, packet, len, DEF_UEP_CPY_LOAD) == READY) {
        g_target_notify_state++;
        if (g_target_notify_state > 2) {
            g_target_notify_state = 0;
        }
    }
}

static void poll_host_notification(void)
{
    uint8_t packet[16];
    uint8_t len;

    if (g_host_notify_state == 0 || USBD_Endp1_Busy != 0) {
        return;
    }

    len = make_notification(g_host_notify_state, ECM_BRIDGE_HOST_COMM_IF, packet);
    if (USBD_ENDPx_DataUp(ENDP1, packet, len) == USB_SUCCESS) {
        g_host_notify_state++;
        if (g_host_notify_state > 2) {
            g_host_notify_state = 0;
        }
    }
}

static void poll_target_tx(void)
{
    ECM_Frame_t *frame;
    uint16_t len;

    if (USBFS_Endp_Busy[DEF_UEP7] != 0) {
        return;
    }

    frame = queue_peek(&g_host_to_target);
    if (frame == 0) {
        g_target_tx_offset = 0;
        g_target_zlp_pending = 0;
        return;
    }

    if (g_target_zlp_pending) {
        if (USBFS_Endp_DataUp(DEF_UEP7, frame->data, 0, DEF_UEP_CPY_LOAD) == READY) {
            g_target_zlp_pending = 0;
            g_target_tx_offset = 0;
            queue_pop(&g_host_to_target);
        }
        return;
    }

    len = (uint16_t)(frame->len - g_target_tx_offset);
    if (len > ECM_BRIDGE_USBFS_PACKET_SIZE) {
        len = ECM_BRIDGE_USBFS_PACKET_SIZE;
    }

    if (USBFS_Endp_DataUp(DEF_UEP7, &frame->data[g_target_tx_offset], len, DEF_UEP_CPY_LOAD) == READY) {
        g_target_tx_offset = (uint16_t)(g_target_tx_offset + len);
        if (g_target_tx_offset >= frame->len) {
            if (len == ECM_BRIDGE_USBFS_PACKET_SIZE) {
                g_target_zlp_pending = 1;
            }
            else {
                g_target_tx_offset = 0;
                queue_pop(&g_host_to_target);
            }
        }
    }
}

static void poll_host_tx(void)
{
    ECM_Frame_t *frame;
    uint16_t len;

    if (USBD_Endp3_Busy != 0) {
        return;
    }

    frame = queue_peek(&g_target_to_host);
    if (frame == 0) {
        g_host_tx_offset = 0;
        g_host_zlp_pending = 0;
        return;
    }

    if (g_host_zlp_pending) {
        if (USBD_ENDPx_DataUp(ENDP3, frame->data, 0) == USB_SUCCESS) {
            g_host_zlp_pending = 0;
            g_host_tx_offset = 0;
            queue_pop(&g_target_to_host);
        }
        return;
    }

    len = (uint16_t)(frame->len - g_host_tx_offset);
    if (len > ECM_BRIDGE_USBD_PACKET_SIZE) {
        len = ECM_BRIDGE_USBD_PACKET_SIZE;
    }

    if (USBD_ENDPx_DataUp(ENDP3, &frame->data[g_host_tx_offset], len) == USB_SUCCESS) {
        g_host_tx_offset = (uint16_t)(g_host_tx_offset + len);
        if (g_host_tx_offset >= frame->len) {
            if (len == ECM_BRIDGE_USBD_PACKET_SIZE) {
                g_host_zlp_pending = 1;
            }
            else {
                g_host_tx_offset = 0;
                queue_pop(&g_target_to_host);
            }
        }
    }
}

void ECM_Bridge_Init(void)
{
    queue_clear(&g_host_to_target);
    queue_clear(&g_target_to_host);
    rx_frame_clear(&g_host_rx_frame);
    rx_frame_clear(&g_target_rx_frame);
    g_target_tx_offset = 0;
    g_host_tx_offset = 0;
    g_target_zlp_pending = 0;
    g_host_zlp_pending = 0;
    g_target_notify_state = 1;
    g_host_notify_state = 1;
}

void ECM_Bridge_HostRxDone(const uint8_t *data, uint16_t len)
{
    rx_frame_append(&g_host_rx_frame, &g_host_to_target, data, len, ECM_BRIDGE_USBD_PACKET_SIZE);
}

void ECM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len)
{
    rx_frame_append(&g_target_rx_frame, &g_target_to_host, data, len, ECM_BRIDGE_USBFS_PACKET_SIZE);
}

void ECM_Bridge_Poll(void)
{
    poll_target_notification();
    poll_host_notification();
    poll_target_tx();
    poll_host_tx();
}

#endif /* USB_WORK_MODE_BRIDGE_ECM */