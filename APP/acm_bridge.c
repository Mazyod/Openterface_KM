#include "include/acm_bridge.h"
#include "include/Version_selection.h"

#if USB_WORK_MODE_BRIDGE_ACM

#include "ch32v20x_usbfs_device.h"
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include <string.h>

#define ACM_BRIDGE_RING_SIZE 2048u
#define ACM_BRIDGE_USBFS_PACKET_SIZE 64u

typedef struct {
    uint8_t data[ACM_BRIDGE_RING_SIZE];
    volatile uint16_t wr;
    volatile uint16_t rd;
    volatile uint32_t overflow;
} ACM_BridgeRing_t;

static ACM_BridgeRing_t g_host_to_target;
static ACM_BridgeRing_t g_target_to_host;
static volatile uint8_t g_usbfs_ep7_zlp_pending;
static volatile uint8_t g_usbd_ep3_zlp_pending;

extern uint8_t USBD_Endp3_Busy;

static void ring_clear(ACM_BridgeRing_t *ring)
{
    ring->wr = 0;
    ring->rd = 0;
    ring->overflow = 0;
}

static uint16_t ring_next(uint16_t index)
{
    index++;
    if (index >= ACM_BRIDGE_RING_SIZE) {
        index = 0;
    }
    return index;
}

static uint16_t ring_level(const ACM_BridgeRing_t *ring)
{
    uint16_t wr = ring->wr;
    uint16_t rd = ring->rd;
    if (wr >= rd) {
        return (uint16_t)(wr - rd);
    }
    return (uint16_t)(ACM_BRIDGE_RING_SIZE - rd + wr);
}

static void ring_write(ACM_BridgeRing_t *ring, const uint8_t *data, uint16_t len)
{
    uint16_t index;

    for (index = 0; index < len; index++) {
        uint16_t next = ring_next(ring->wr);
        if (next == ring->rd) {
            ring->overflow++;
            break;
        }
        ring->data[ring->wr] = data[index];
        ring->wr = next;
    }
}

static uint16_t ring_peek(const ACM_BridgeRing_t *ring, uint8_t *data, uint16_t max_len)
{
    uint16_t count = 0;
    uint16_t rd = ring->rd;
    uint16_t available = ring_level(ring);

    if (available > max_len) {
        available = max_len;
    }

    while (count < available) {
        data[count++] = ring->data[rd];
        rd = ring_next(rd);
    }

    return count;
}

static void ring_drop(ACM_BridgeRing_t *ring, uint16_t len)
{
    uint16_t count;
    for (count = 0; count < len; count++) {
        if (ring->rd == ring->wr) {
            break;
        }
        ring->rd = ring_next(ring->rd);
    }
}

void ACM_Bridge_Init(void)
{
    ring_clear(&g_host_to_target);
    ring_clear(&g_target_to_host);
    g_usbfs_ep7_zlp_pending = 0;
    g_usbd_ep3_zlp_pending = 0;
}

void ACM_Bridge_HostRxDone(const uint8_t *data, uint16_t len)
{
    ring_write(&g_host_to_target, data, len);
}

void ACM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len)
{
    ring_write(&g_target_to_host, data, len);
}

void ACM_Bridge_Poll(void)
{
    uint8_t packet[ACM_BRIDGE_USBFS_PACKET_SIZE];
    uint16_t len;

    if (USBFS_Endp_Busy[DEF_UEP7] == 0) {
        len = ring_peek(&g_host_to_target, packet, ACM_BRIDGE_USBFS_PACKET_SIZE);
        if (len > 0) {
            if (USBFS_Endp_DataUp(DEF_UEP7, packet, len, DEF_UEP_CPY_LOAD) == READY) {
                ring_drop(&g_host_to_target, len);
                g_usbfs_ep7_zlp_pending = (len == ACM_BRIDGE_USBFS_PACKET_SIZE) ? 1 : 0;
            }
        }
        else if (g_usbfs_ep7_zlp_pending) {
            if (USBFS_Endp_DataUp(DEF_UEP7, packet, 0, DEF_UEP_CPY_LOAD) == READY) {
                g_usbfs_ep7_zlp_pending = 0;
            }
        }
    }

    if (USBD_Endp3_Busy == 0) {
        len = ring_peek(&g_target_to_host, packet, DEF_USBD_MAX_PACK_SIZE);
        if (len > 0) {
            if (USBD_ENDPx_DataUp(ENDP3, packet, len) == USB_SUCCESS) {
                ring_drop(&g_target_to_host, len);
                g_usbd_ep3_zlp_pending = (len == DEF_USBD_MAX_PACK_SIZE) ? 1 : 0;
            }
        }
        else if (g_usbd_ep3_zlp_pending) {
            if (USBD_ENDPx_DataUp(ENDP3, packet, 0) == USB_SUCCESS) {
                g_usbd_ep3_zlp_pending = 0;
            }
        }
    }
}

#endif /* USB_WORK_MODE_BRIDGE_ACM */
