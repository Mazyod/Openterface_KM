#include "km_ring.h"
#include "ch32v20x_usbfs_device.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "usb_mode_config.h"
#include "include/ecm_bridge.h"

#include <string.h>
#include <stdarg.h>


/* Global queues */
KB_Queue_t kb_queue;
MS_Rel_Queue_t ms_rel_queue;
MS_Abs_Mailbox_t ms_abs_mbox;
Resp_Queue_t resp_queue;
Media_Queue_t media_queue;

/* Extern variables */
extern uint8_t USBD_Endp3_Busy;

/* Initialize queues */
void Queue_Init(void) {
    ENTER_CRITICAL();
    kb_queue.head = kb_queue.tail = 0;
    ms_rel_queue.head = ms_rel_queue.tail = 0;
    ms_abs_mbox.valid = 0;
    resp_queue.head = resp_queue.tail = 0;
    media_queue.head = media_queue.tail = 0;
    EXIT_CRITICAL();
}
//Keyboard message queue
void Queue_Push_Keyboard(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[kb_queue.tail].data));
    kb_queue.tail = next;
    EXIT_CRITICAL();
}
//MouseRel message queue
void Queue_Push_MouseRel(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (ms_rel_queue.tail + 1) % MS_REL_QUEUE_SIZE;
    if (next == ms_rel_queue.head) ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
    memcpy(ms_rel_queue.buf[ms_rel_queue.tail].data, data, sizeof(ms_rel_queue.buf[ms_rel_queue.tail].data));
    ms_rel_queue.tail = next;
    EXIT_CRITICAL();
}
//MouseAbs message queue — mailbox: unconditionally overwrite with the newest state
void Queue_Push_MouseAbs(uint8_t *data) {
    ENTER_CRITICAL();
    memcpy(ms_abs_mbox.slot.data, data, sizeof(ms_abs_mbox.slot.data));
    ms_abs_mbox.valid = 1;
    EXIT_CRITICAL();
}
//Response message queue (Message length varies)
void Queue_Push_Response(uint8_t *data, uint8_t len) {
    USB_DEBUG("QueueResp: len=%d head=%d tail=%d\n", len, resp_queue.head, resp_queue.tail);
    ENTER_CRITICAL();
    int next = (resp_queue.tail + 1) % RESP_QUEUE_SIZE;
    if (next == resp_queue.head) resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
    memcpy(resp_queue.buf[resp_queue.tail].data, data, len);
    resp_queue.buf[resp_queue.tail].len = len;
    resp_queue.tail = next;
    EXIT_CRITICAL();
}
//Media key message queue
void Queue_Push_Media(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (media_queue.tail + 1) % MEDIA_QUEUE_SIZE;
    if (next == media_queue.head) media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
    memcpy(media_queue.buf[media_queue.tail].data, data, sizeof(media_queue.buf[media_queue.tail].data));
    media_queue.tail = next;
    EXIT_CRITICAL();
}

/* Pop functions */
bool Queue_Pop_Keyboard(KB_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (kb_queue.head != kb_queue.tail) {
        memcpy(out->data, kb_queue.buf[kb_queue.head].data, sizeof(out->data));
        kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}

bool Queue_Pop_MouseRel(MS_Rel_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (ms_rel_queue.head != ms_rel_queue.tail) {
        memcpy(out->data, ms_rel_queue.buf[ms_rel_queue.head].data, sizeof(out->data));
        ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}

bool Queue_Pop_MouseAbs(MS_Abs_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (ms_abs_mbox.valid) {
        memcpy(out->data, ms_abs_mbox.slot.data, sizeof(out->data));
        ms_abs_mbox.valid = 0;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}

bool Queue_Pop_Response(Resp_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (resp_queue.head != resp_queue.tail) {
        memcpy(out->data, resp_queue.buf[resp_queue.head].data, resp_queue.buf[resp_queue.head].len);
        out->len = resp_queue.buf[resp_queue.head].len;
        resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}

bool Queue_Pop_Media(Media_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (media_queue.head != media_queue.tail) {
        memcpy(out->data, media_queue.buf[media_queue.head].data, sizeof(out->data));
        media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}

/* Try to send all queued packets until the endpoint is busy or queue is empty */
void USB_SendFromQueues(void) {
    while (kb_queue.head != kb_queue.tail && USBFS_Endp_Busy[DEF_UEP1] == 0) {
        KB_Report_t *pkt = &kb_queue.buf[kb_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP1, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    while (ms_rel_queue.head != ms_rel_queue.tail && USBFS_Endp_Busy[DEF_UEP2] == 0) {
        MS_Rel_Report_t *pkt = &ms_rel_queue.buf[ms_rel_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP2, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    /* Absolute mouse: mailbox — send the newest pending report whenever the
     * endpoint is free. Copy under the critical section so a concurrent push
     * cannot tear the bytes the USB controller is about to read, and clear
     * valid before DataUp: a push during the copy simply marks valid again and
     * the newest state goes out next pass (no loss, at most one stale frame). */
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        uint8_t abs_report[MS_ABS_DATA_LEN];
        uint8_t have = 0;
        ENTER_CRITICAL();
        if (ms_abs_mbox.valid) {
            memcpy(abs_report, ms_abs_mbox.slot.data, sizeof(abs_report));
            ms_abs_mbox.valid = 0;
            have = 1;
        }
        EXIT_CRITICAL();
        if (have &&
            USBFS_Endp_DataUp(DEF_UEP3, abs_report, sizeof(abs_report), DEF_UEP_CPY_LOAD) != READY) {
            /* Endpoint became busy between check and arm (should not happen:
             * only this drain arms EP3). Restore so the report is not lost. */
            ENTER_CRITICAL();
            memcpy(ms_abs_mbox.slot.data, abs_report, sizeof(abs_report));
            ms_abs_mbox.valid = 1;
            EXIT_CRITICAL();
        }
    }

    while (media_queue.head != media_queue.tail && USBFS_Endp_Busy[DEF_UEP4] == 0) {
        Media_Report_t *pkt = &media_queue.buf[media_queue.head];
        /* Report ID 0x01 = ACPI: 2 bytes (ID + 1 data)
         * Report ID 0x02 = Consumer: 4 bytes (ID + 3 data) */
        uint16_t send_len = (pkt->data[0] == 0x01) ? 2 : 4;
        if (USBFS_Endp_DataUp(DEF_UEP4, pkt->data, send_len, DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    /* Response queue drain — send CH9329 responses via EP3 (ACM bulk IN).
     *
     * In all modes with host CDC ACM (including ACM+ECM combined mode),
     * responses must be sent via EP3. The ECM bridge has its own separate
     * response handling for TCP-based CH9329 commands, but USB serial
     * commands still need this path.
     *
     * IMPORTANT: CH9329 responses go through the USBD library (USB 1.1 FS
     * controller at 0x40005C00), NOT the USBFS library (USB 2.0 OTG at
     * 0x50000000). The USBD library handles CDC ACM, while USBFS handles
     * HID endpoints. Both controllers share the same physical USB port
     * via a multiplexer.
     */
    if (USB_ModeConfig_ActiveModeHasHostCdcAcm() ||
        (!USB_ModeConfig_ActiveModeBridgeEcm() && !USB_ModeConfig_ActiveModeHasHostCdcEcm())) {

        /* Check if EP3 IN endpoint is enabled */
        uint16_t ep3_status = _GetEPTxStatus(ENDP3);
        USB_DEBUG("EP3 status: 0x%x\n", ep3_status);
        if (ep3_status == EP_TX_DIS) {
            /* EP3 not enabled, try to enable it */
            USB_DEBUG("EP3 is disabled, enabling\n");
            if (USB_ModeConfig_ActiveModeHasHostCdcAcm()) {
                _ClearDTOG_TX(ENDP3);
                SetEPTxStatus(ENDP3, EP_TX_NAK);
                USB_DEBUG("EP3 enabled to NAK\n");
            }
            /* Don't return - continue to process the queue after enabling */
        }

        while (resp_queue.head != resp_queue.tail) {
            Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];

            /* Check if endpoint is busy */
            if (USBD_Endp3_Busy) {
                USB_DEBUG("EP3 busy, breaking\n");
                break;  /* endpoint busy, retry next pass */
            }

            USB_DEBUG("DrainQ: head=%d tail=%d len=%d\n", resp_queue.head, resp_queue.tail, pkt->len);
            int result = USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len);
            USB_DEBUG("USBD_ENDPx_DataUp result: %d\n", result);
            if (result == 0) {
                ENTER_CRITICAL();
                resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
                EXIT_CRITICAL();
                USB_DEBUG("DrainQ: sent\n");
            }
            else break;  /* endpoint busy, retry next pass */
        }
    }
}

/* ====== Debug ring buffer (sends via USBFS EP2 mouse endpoint) ====== */

static struct {
    char buf[USB_DEBUG_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} g_debug_ring;

/* Marker byte in data[1] distinguishes debug reports from real media keys.
 * Format: {0x02, 0xFE, char, char} — consumer HID report, 2 chars per packet. */
#define USB_DEBUG_MARKER  0xFE

/* Called from USB_DEBUG() — adds string to ring buffer (caller holds lock). */
static void debug_ring_push_str(const char *s) {
    while (*s && (uint16_t)(g_debug_ring.tail + 1) % USB_DEBUG_RING_SIZE != g_debug_ring.head) {
        g_debug_ring.buf[g_debug_ring.tail] = *s++;
        g_debug_ring.tail = (uint16_t)(g_debug_ring.tail + 1) % USB_DEBUG_RING_SIZE;
    }
}

/* Call USB_DEBUG(fmt, ...) to send debug strings over USBFS EP2 and USART1.
 * Each call pushes a null-terminated string into a ring buffer.
 * USB_DEBUG_Poll() in the main loop drains 2 chars at a time via EP2.
 * Also outputs via USART1 (PA9) for easy reading with USB-to-serial adapter. */
void USB_DEBUG(const char *fmt, ...) {
    va_list ap;
    char numbuf[32];
    char uart_buf[256];
    int uart_len = 0;

    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            debug_ring_push_str(fmt);
            /* Also add to UART buffer */
            if (uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *fmt;
            }
            fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            debug_ring_push_str(s);
            while (*s && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *s++;
            }
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { debug_ring_push_str("-"); if (uart_len < sizeof(uart_buf) - 1) uart_buf[uart_len++] = '-'; v = -v; }
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
            debug_ring_push_str(p);
            while (*p && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *p++;
            }
            break;
        }
        case 'u': {
            unsigned int v = va_arg(ap, unsigned int);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
            debug_ring_push_str(p);
            while (*p && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *p++;
            }
            break;
        }
        case 'x': case 'X': {
            unsigned int v = va_arg(ap, unsigned int);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
            while (*p && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *p++;
            }
            break;
        }
        case 'l': fmt++; if (*fmt == 'x' || *fmt == 'X') {
            unsigned long v = va_arg(ap, unsigned long);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
            while (*p && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *p++;
            }
        } break;
        case 'p': {
            unsigned long v = va_arg(ap, unsigned long);
            debug_ring_push_str("0x");
            if (uart_len < sizeof(uart_buf) - 2) {
                uart_buf[uart_len++] = '0';
                uart_buf[uart_len++] = 'x';
            }
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
            while (*p && uart_len < sizeof(uart_buf) - 1) {
                uart_buf[uart_len++] = *p++;
            }
            break;
        }
        case '%': debug_ring_push_str("%"); if (uart_len < sizeof(uart_buf) - 1) uart_buf[uart_len++] = '%'; break;
        default: break;
        }
        if (*fmt) fmt++;
    }
    va_end(ap);

    /* Output via USART1 serial port */
    if (uart_len > 0) {
        uart_buf[uart_len] = '\0';
        iprintf("%s", uart_buf);
    }
}

/* Drain the debug ring buffer — sends 2 chars per packet via EP2
 * (mouse relative endpoint, which macOS actively polls).
 * Format: {buttons=0xFF(debug_marker), x=char1, y=char2, wheel=0}.
 * The 0xFF buttons byte distinguishes debug reports from real mouse moves.
 * Real mouse reports have buttons in {0,1,2,3,4,5,6,7}. */
#define USB_DEBUG_MOUSE_MARKER  0xFF

void USB_DEBUG_Poll(void) {
    if (USBFS_Endp_Busy[DEF_UEP2] != 0) return;
    if (g_debug_ring.head == g_debug_ring.tail) return;

    MS_Rel_Report_t report;
    report.data[0] = USB_DEBUG_MOUSE_MARKER;  /* debug marker */
    report.data[1] = g_debug_ring.buf[g_debug_ring.head];
    g_debug_ring.head = (uint16_t)(g_debug_ring.head + 1) % USB_DEBUG_RING_SIZE;

    if (g_debug_ring.head != g_debug_ring.tail) {
        report.data[2] = g_debug_ring.buf[g_debug_ring.head];
        g_debug_ring.head = (uint16_t)(g_debug_ring.head + 1) % USB_DEBUG_RING_SIZE;
    } else {
        report.data[2] = 0x00;
    }
    report.data[3] = 0x00;  /* wheel */

    USBFS_Endp_DataUp(DEF_UEP2, report.data, sizeof(report.data), DEF_UEP_CPY_LOAD);
}
