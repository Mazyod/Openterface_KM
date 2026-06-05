#include "km_ring.h"
#include "ch32v20x_usbfs_device.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "usb_mode_config.h"

#include <string.h>
#include <stdarg.h>


/* Global queues */
KB_Queue_t kb_queue;
MS_Rel_Queue_t ms_rel_queue;
MS_Abs_Queue_t ms_abs_queue;
Resp_Queue_t resp_queue;
Media_Queue_t media_queue;

/* Initialize queues */
void Queue_Init(void) {
    kb_queue.head = kb_queue.tail = 0;
    ms_rel_queue.head = ms_rel_queue.tail = 0;
    ms_abs_queue.head = ms_abs_queue.tail = 0;
    resp_queue.head = resp_queue.tail = 0;
    media_queue.head = media_queue.tail = 0;
}
//Keyboard message queue
void Queue_Push_Keyboard(uint8_t *data) {
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[kb_queue.tail].data));
    kb_queue.tail = next;
}
//MouseRel message queue
void Queue_Push_MouseRel(uint8_t *data) {
    int next = (ms_rel_queue.tail + 1) % MS_REL_QUEUE_SIZE;
    if (next == ms_rel_queue.head) ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
    memcpy(ms_rel_queue.buf[ms_rel_queue.tail].data, data, sizeof(ms_rel_queue.buf[ms_rel_queue.tail].data));
    ms_rel_queue.tail = next;
}
//MouseAbs message queue
void Queue_Push_MouseAbs(uint8_t *data) {
    int next = (ms_abs_queue.tail + 1) % MS_ABS_QUEUE_SIZE;
    if (next == ms_abs_queue.head) ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
    memcpy(ms_abs_queue.buf[ms_abs_queue.tail].data, data, sizeof(ms_abs_queue.buf[ms_abs_queue.tail].data));
    ms_abs_queue.tail = next;
}
//Response message queue (Message length varies)
void Queue_Push_Response(uint8_t *data, uint8_t len) {
    int next = (resp_queue.tail + 1) % RESP_QUEUE_SIZE;
    if (next == resp_queue.head) resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
    memcpy(resp_queue.buf[resp_queue.tail].data, data, len);
    resp_queue.buf[resp_queue.tail].len = len;
    resp_queue.tail = next;
}
//Media key message queue
void Queue_Push_Media(uint8_t *data) {
    int next = (media_queue.tail + 1) % MEDIA_QUEUE_SIZE;
    if (next == media_queue.head) media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
    memcpy(media_queue.buf[media_queue.tail].data, data, sizeof(media_queue.buf[media_queue.tail].data));
    media_queue.tail = next;
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

bool Queue_Pop_Media(Media_Report_t *out) {
    if (media_queue.head == media_queue.tail) return false;
    memcpy(out->data, media_queue.buf[media_queue.head].data, sizeof(out->data));
    media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
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

    while (media_queue.head != media_queue.tail && USBFS_Endp_Busy[DEF_UEP4] == 0) {
        Media_Report_t *pkt = &media_queue.buf[media_queue.head];
        /* Report ID 0x01 = ACPI: 2 bytes (ID + 1 data)
         * Report ID 0x02 = Consumer: 4 bytes (ID + 3 data) */
        uint16_t send_len = (pkt->data[0] == 0x01) ? 2 : 4;
        if (USBFS_Endp_DataUp(DEF_UEP4, pkt->data, send_len, DEF_UEP_CPY_LOAD) == READY)
            media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
        else break;
    }

    if (USB_ModeConfig_ActiveModeHasHostCdcEcm()) {
        resp_queue.head = resp_queue.tail;
        return;
    }

    while (resp_queue.head != resp_queue.tail) {
        Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
        if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0)
            resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
        else break;
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

/* Call USB_DEBUG(fmt, ...) to send debug strings over USBFS EP2.
 * Each call pushes a null-terminated string into a ring buffer.
 * USB_DEBUG_Poll() in the main loop drains 2 chars at a time via EP2. */
void USB_DEBUG(const char *fmt, ...) {
    va_list ap;
    char numbuf[32];

    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            debug_ring_push_str(fmt);
            fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': debug_ring_push_str(va_arg(ap, const char *)); break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { debug_ring_push_str("-"); v = -v; }
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
            debug_ring_push_str(p);
            break;
        }
        case 'u': {
            unsigned int v = va_arg(ap, unsigned int);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = (char)('0' + (v % 10)); v /= 10; } while (v > 0);
            debug_ring_push_str(p);
            break;
        }
        case 'x': case 'X': {
            unsigned int v = va_arg(ap, unsigned int);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
            break;
        }
        case 'l': fmt++; if (*fmt == 'x' || *fmt == 'X') {
            unsigned long v = va_arg(ap, unsigned long);
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
        } break;
        case 'p': {
            unsigned long v = va_arg(ap, unsigned long);
            debug_ring_push_str("0x");
            char *p = numbuf + sizeof(numbuf) - 1; *p = 0;
            do { p--; *p = "0123456789abcdef"[v & 0xf]; v >>= 4; } while (v > 0);
            debug_ring_push_str(p);
            break;
        }
        case '%': debug_ring_push_str("%"); break;
        default: break;
        }
        if (*fmt) fmt++;
    }
    va_end(ap);
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
