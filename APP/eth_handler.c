/********************************** (C) COPYRIGHT *******************************
 * File Name          : eth_handler.c
 * Description        : CDC-ECM USB Ethernet transport layer.
 *
 * Runs on the CH32V208 legacy USB Device controller (USBD, IRQ 36).
 * EP1 IN interrupt: CDC notifications (8 bytes).
 * EP2 OUT bulk:     frame reassembly (host->device, 32B max packet).
 * EP3 IN bulk:      frame fragmentation (device->host, 32B max packet).
 *
 * Frame reassembly (host->device):
 *   EP2 packets are accumulated in s_rx_buf. A short packet (len < 32)
 *   or ZLP (len == 0) terminates the frame and calls ETH_OnRxFrame().
 *
 * Frame fragmentation (device->host):
 *   ETH_TxFrame() segments caller's buffer into 32-byte EP3 chunks.
 *   If the frame is an exact multiple of 32 bytes, a ZLP is appended.
 *
 * Note: all EP callbacks run in USB ISR context.
 *******************************************************************************/

#ifdef ENABLE_ETH

#include "include/eth_handler.h"
#include "usb_lib.h"          /* SetEPRxValid, SetEPTxCount, SetEPTxStatus       */
#include "usb_mem.h"          /* UserToPMABufferCopy, PMAToUserBufferCopy         */
#include "usb_conf.h"         /* ENDP1_TXADDR, ENDP2_RXADDR, ENDP3_TXADDR        */
                              /* ENDP1/ENDP2/ENDP3 endpoint number constants      */
#include <string.h>

/* USBD bulk EP max packet size (matches PMA buffer allocation in usb_conf.h) */
#define ETH_BULK_PKT  ETH_USB_BULK_PKT   /* 32 */

/* CDC NETWORK_CONNECTION notification (Table 11, USB CDC 1.2 spec).
 *   bmRequestType = 0xA1  (Device-to-Host | Class | Interface)
 *   bNotification = 0x00  NETWORK_CONNECTION
 *   wValue        = 1 (connected) or 0 (disconnected)
 *   wIndex        = 0x0000  (IF0 = CDC Comm interface)
 *   wLength       = 0 */
static const uint8_t s_notify_connected[8]    = {0xA1, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t s_notify_disconnected[8] = {0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Public state */
uint8_t g_eth_data_alt = 0;

/* RX reassembly */
static uint8_t  s_rx_buf[ETH_MAX_FRAME_SIZE];
static uint16_t s_rx_pos;

/* TX fragmentation */
static const uint8_t *s_tx_ptr;
static uint16_t        s_tx_remaining;
static uint8_t         s_tx_need_zlp;  /* 1 = must send ZLP after last full packet */

/* USBD_Endp3_Busy is shared with usb_endp.c */
extern uint8_t USBD_Endp3_Busy;

/* ---- Internal helpers --------------------------------------------------- */

static void eth_arm_ep2(void)
{
    SetEPRxValid(ENDP2);
}

static void eth_send_next_chunk(void)
{
    uint16_t chunk;

    if (s_tx_remaining == 0 && !s_tx_need_zlp) {
        USBD_Endp3_Busy = 0;
        return;
    }

    if (s_tx_need_zlp) {
        /* ZLP terminates a frame that was an exact multiple of ETH_BULK_PKT */
        chunk = 0;
        s_tx_need_zlp = 0;
    } else {
        chunk = (s_tx_remaining > ETH_BULK_PKT) ? ETH_BULK_PKT : s_tx_remaining;
        UserToPMABufferCopy((uint8_t*)s_tx_ptr, ENDP3_TXADDR, chunk);
        s_tx_ptr += chunk;
        s_tx_remaining -= chunk;
        if (s_tx_remaining == 0 && chunk == ETH_BULK_PKT) {
            s_tx_need_zlp = 1;
        }
    }

    SetEPTxCount(ENDP3, chunk);
    SetEPTxStatus(ENDP3, EP_TX_VALID);
}

/* ---- API ---------------------------------------------------------------- */

void ETH_Init(void)
{
    s_rx_pos        = 0;
    s_tx_ptr        = NULL;
    s_tx_remaining  = 0;
    s_tx_need_zlp   = 0;
    g_eth_data_alt  = 0;
    USBD_Endp3_Busy = 0;
}

void ETH_USBD_EP1_TxDone(void)
{
    /* Notification delivered -- nothing more to do here. */
    (void)0;
}

void ETH_USBD_EP2_RxDone(uint16_t len)
{
    /* Copy received chunk into reassembly buffer */
    if (len > 0 && s_rx_pos + len <= ETH_MAX_FRAME_SIZE) {
        PMAToUserBufferCopy(s_rx_buf + s_rx_pos, ENDP2_RXADDR, len);
        s_rx_pos += len;
    }

    /* A short packet (< ETH_BULK_PKT) or ZLP (len == 0) terminates the frame */
    if (len < ETH_BULK_PKT) {
        if (s_rx_pos > 0) {
            ETH_OnRxFrame(s_rx_buf, s_rx_pos);
        }
        s_rx_pos = 0;
    }

    /* Re-arm EP2 for next packet */
    eth_arm_ep2();
}

void ETH_USBD_EP3_TxDone(void)
{
    eth_send_next_chunk();
}

void ETH_SendNotification(uint8_t connected)
{
    const uint8_t *n = connected ? s_notify_connected : s_notify_disconnected;
    UserToPMABufferCopy((uint8_t*)n, ENDP1_TXADDR, 8);
    SetEPTxCount(ENDP1, 8);
    SetEPTxStatus(ENDP1, EP_TX_VALID);
}

uint8_t ETH_TxFrame(const uint8_t *buf, uint16_t len)
{
    if (USBD_Endp3_Busy)                    return 0;  /* EP3 already transmitting */
    if (g_eth_data_alt != 1)                return 0;  /* interface in zero-bandwidth alt */
    if (len == 0 || len > ETH_MAX_FRAME_SIZE) return 0;

    s_tx_ptr        = buf;
    s_tx_remaining  = len;
    s_tx_need_zlp   = ((len % ETH_BULK_PKT) == 0) ? 1 : 0;
    USBD_Endp3_Busy = 1;
    eth_send_next_chunk();
    return 1;
}

__attribute__((weak)) void ETH_OnRxFrame(const uint8_t *buf, uint16_t len)
{
    (void)buf; (void)len;
}

#endif /* ENABLE_ETH */
