#include "include/ecm_bridge.h"
#include "include/Version_selection.h"

#if USB_WORK_MODE_COMPILES_BRIDGE_ECM

#include "ch32v20x_usbfs_device.h"
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "usb_pwr.h"
#include "include/usbd_composite_km.h"
#include "include/km_ring.h"
#include "include/ble_eth_protocol.h"
#include <string.h>

#define ECM_BRIDGE_MTU_FRAME_SIZE       1514u
#define ECM_BRIDGE_QUEUE_DEPTH          2u
#define ECM_BRIDGE_USBFS_PACKET_SIZE    64u
#define ECM_BRIDGE_USBD_PACKET_SIZE     DEF_USBD_MAX_PACK_SIZE
#define ECM_BRIDGE_TARGET_COMM_IF       4u
#define ECM_BRIDGE_HOST_COMM_IF         0u
#define ECM_BRIDGE_LINK_SPEED_BPS       12000000u
#define ECM_ETH_TYPE_IPV4               0x0800u
#define ECM_ETH_TYPE_VLAN               0x8100u
#define ECM_ETH_TYPE_QINQ               0x88A8u
#define ECM_IPV4_PROTO_TCP              0x06u
#define ECM_ETH_HEADER_LEN              14u
#define ECM_IPV4_HEADER_LEN_OFFSET      16u  /* IP total_length offset within IP header */
#define ECM_IPV4_MIN_HEADER_LEN         20u
#define ECM_TCP_MIN_HEADER_LEN          20u
#define ECM_CH9329_MIN_FRAME_LEN        6u
#define ECM_ARP_FRAME_LEN               42u  /* 14 eth + 28 arp */

/* Firmware-side peer identity on the target CDC ECM link. The descriptor MAC
    02:00:00:00:00:30 becomes macOS's en20 MAC, so ARP replies must use a
    distinct peer MAC or macOS ignores them as self-MAC replies. */
static const uint8_t ECM_DEV_MAC[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x31 };
static const uint8_t ECM_DEV_IP[4]  = { 192, 168, 11, 3 };

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
    uint16_t expected_len;
    uint8_t drop;
    volatile uint32_t overflow;
} ECM_RxFrame_t;

typedef uint8_t (*ECM_FrameHandler_t)(const uint8_t *data, uint16_t len);

/* Forward declaration */
static uint8_t handle_target_frame(const uint8_t *data, uint16_t len);

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

/* Track whether the USBFS CDC ECM data interface (IF5, alt=1) has been
   activated by the host. Set in ch32v20x_usbfs_device.c SET_INTERFACE handler.
   Prevents sending through EP7 before the host is ready. */
volatile uint8_t g_ecm_data_if_active = 0;
volatile uint16_t g_ecm_tx_count = 0;       /* successful EP7 TX count */
/* LED debug overrides (set by RX path, read by main loop).
   0 = no override (normal pattern)
   1 = HostRxDone called (data arrived on EP6)
   2 = handle_arp_frame detected ARP (EtherType 0x0806)
   3 = ARP target IP matched device (reply queued)
   4 = ARP request but wrong IP
   5 = frame received but not ARP */
volatile uint8_t g_ecm_led_override = 0;
/* GARP queued flag — reset to 0 in SET_INTERFACE handler on every activation
   so GARP is re-queued each time the Mac enables the data interface. */
volatile uint8_t g_garp_sent = 0;
/* Diagnostic: 0=idle, 1=GARP queued, 2=DataUp called & SUCCESS, 3=EP7 busy, 4=DataUp FAILED */
volatile uint8_t g_ecm_tx_state = 0;

extern uint8_t USBD_Endp1_Busy;
extern uint8_t USBD_Endp3_Busy;

/* Callback invoked when target frame data is pushed to BLE.
   Set by peripheral.c to trigger BLE TX event. */
static void (*g_ble_tx_event_cb)(void);

void ECM_Bridge_SetBleTxCallback(void (*cb)(void))
{
    g_ble_tx_event_cb = cb;
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint8_t ch9329_frame_valid(const uint8_t *data, uint16_t len, uint16_t *frame_len)
{
    uint16_t total_len;
    uint16_t index;
    uint8_t checksum = 0;

    if (data == 0 || len < ECM_CH9329_MIN_FRAME_LEN) {
        return 0;
    }
    if (data[0] != CH9329_FRAME_HEAD1 || data[1] != CH9329_FRAME_HEAD2) {
        return 0;
    }

    total_len = (uint16_t)(ECM_CH9329_MIN_FRAME_LEN + data[4]);
    if (len < total_len) {
        return 0;
    }

    for (index = 0; index < (uint16_t)(total_len - 1u); index++) {
        checksum = (uint8_t)(checksum + data[index]);
    }
    if (checksum != data[total_len - 1u]) {
        return 0;
    }

    if (frame_len != 0) {
        *frame_len = total_len;
    }
    return 1;
}

static uint8_t ch9329_payload_valid(const uint8_t *payload, uint16_t payload_len)
{
    uint16_t offset = 0;
    uint16_t frame_len;

    if (payload_len < ECM_CH9329_MIN_FRAME_LEN) {
        return 0;
    }

    while (offset < payload_len) {
        if (!ch9329_frame_valid(&payload[offset], (uint16_t)(payload_len - offset), &frame_len)) {
            return 0;
        }
        offset = (uint16_t)(offset + frame_len);
    }

    return 1;
}

static uint8_t queue_push(ECM_FrameQueue_t *queue, const uint8_t *data, uint16_t len);

/* ARP handler: respond to ARP requests for the device IP so the Mac
   can resolve our MAC address and route packets back to us. */
static uint8_t handle_arp_frame(const uint8_t *frame, uint16_t frame_len)
{
    if (frame_len < 28) {
        return 0;
    }

    uint16_t eth_type = (uint16_t)(frame[12] << 8) | frame[13];
    if (eth_type != 0x0806) {
        g_ecm_led_override = 5;  /* not ARP */
        return 0;
    }
    g_ecm_led_override = 2;  /* ARP detected */

    uint16_t arp_op = (uint16_t)(frame[20] << 8) | frame[21];

    /* Handle ARP requests for our device IP */
    if (arp_op == 1) {  /* ARP request */
        /* Check if target IP matches our device IP.
         * ARP frame offsets: bytes 22-27 = sender MAC, 28-31 = sender IP,
         *                    32-37 = target MAC, 38-41 = target IP */
        if (frame[38] != ECM_DEV_IP[0] || frame[39] != ECM_DEV_IP[1] ||
            frame[40] != ECM_DEV_IP[2] || frame[41] != ECM_DEV_IP[3]) {
            g_ecm_led_override = 4;  /* wrong IP */
            USB_DEBUG("ARP req but wrong IP %d.%d.%d.%d\r\n",
                       frame[38], frame[39], frame[40], frame[41]);
            return 0;
        }

        g_ecm_led_override = 3;  /* ARP reply queued */

        USB_DEBUG("ARP req matched, queuing reply\r\n");

        /* Build ARP reply: sender=us, target=requestor */
        uint8_t reply[42];
        memcpy(&reply[0], &frame[6], 6);   /* dst MAC = requestor MAC */
        memcpy(&reply[6], ECM_DEV_MAC, 6); /* src MAC = device MAC */
        reply[12] = 0x08; reply[13] = 0x06; /* EtherType = ARP */
        reply[14] = 0x00; reply[15] = 0x01; /* hardware type = Ethernet */
        reply[16] = 0x08; reply[17] = 0x00; /* protocol type = IPv4 */
        reply[18] = 6;  reply[19] = 4;     /* MAC len=6, IP len=4 */
        reply[20] = 0x00; reply[21] = 0x02; /* op = ARP reply */
        memcpy(&reply[22], ECM_DEV_MAC, 6); /* sender MAC */
        memcpy(&reply[28], ECM_DEV_IP, 4);  /* sender IP */
        memcpy(&reply[32], &frame[22], 6);  /* target MAC (from request) */
        memcpy(&reply[38], &frame[28], 4);  /* target IP = requestor IP */

        queue_push(&g_target_to_host, reply, sizeof(reply));
        return 1;
    }

    /* Handle ARP replies - pass to BLE-Eth for connection resolution */
    if (arp_op == 2) {  /* ARP reply */
        uint8_t sender_ip[4], sender_mac[6];
        memcpy(sender_mac, &frame[22], 6);
        memcpy(sender_ip, &frame[28], 4);
        if (BLE_Eth_HandleArpReply(sender_ip, sender_mac)) {
            USB_DEBUG("ARP reply processed\r\n");
        }
        /* Don't consume the frame - let it also go to Mac's queue */
    }

    return 0;
}

static uint8_t tcp_payload_from_ipv4_frame(const uint8_t *frame,
                                           uint16_t frame_len,
                                           const uint8_t **payload,
                                           uint16_t *payload_len)
{
    uint16_t eth_offset;
    uint16_t eth_type;
    const uint8_t *ip;
    const uint8_t *tcp;
    uint16_t ip_total_len;
    uint16_t ip_header_len;
    uint16_t tcp_segment_len;
    uint16_t tcp_header_len;
    uint16_t fragment;

    if (frame == 0 || payload == 0 || payload_len == 0 || frame_len < ECM_ETH_HEADER_LEN) {
        return 0;
    }

    eth_offset = ECM_ETH_HEADER_LEN;
    eth_type = get_be16(&frame[12]);
    while ((eth_type == ECM_ETH_TYPE_VLAN || eth_type == ECM_ETH_TYPE_QINQ) &&
           frame_len >= (uint16_t)(eth_offset + 4u)) {
        eth_type = get_be16(&frame[eth_offset + 2u]);
        eth_offset = (uint16_t)(eth_offset + 4u);
    }

    if (eth_type != ECM_ETH_TYPE_IPV4 ||
        frame_len < (uint16_t)(eth_offset + ECM_IPV4_MIN_HEADER_LEN)) {
        return 0;
    }

    ip = &frame[eth_offset];
    if ((ip[0] >> 4) != 4u) {
        return 0;
    }

    ip_header_len = (uint16_t)((ip[0] & 0x0Fu) * 4u);
    if (ip_header_len < ECM_IPV4_MIN_HEADER_LEN ||
        frame_len < (uint16_t)(eth_offset + ip_header_len)) {
        return 0;
    }

    ip_total_len = get_be16(&ip[2]);
    if (ip_total_len < (uint16_t)(ip_header_len + ECM_TCP_MIN_HEADER_LEN) ||
        frame_len < (uint16_t)(eth_offset + ip_total_len)) {
        return 0;
    }

    fragment = get_be16(&ip[6]);
    if ((fragment & 0x3FFFu) != 0u || ip[9] != ECM_IPV4_PROTO_TCP) {
        return 0;
    }

    tcp = &ip[ip_header_len];
    tcp_segment_len = (uint16_t)(ip_total_len - ip_header_len);
    tcp_header_len = (uint16_t)((tcp[12] >> 4) * 4u);
    if (tcp_header_len < ECM_TCP_MIN_HEADER_LEN || tcp_header_len > tcp_segment_len) {
        return 0;
    }

    *payload = &tcp[tcp_header_len];
    *payload_len = (uint16_t)(tcp_segment_len - tcp_header_len);
    return (*payload_len > 0u) ? 1u : 0u;
}

static uint8_t queue_push(ECM_FrameQueue_t *queue, const uint8_t *data, uint16_t len);

static uint8_t handle_host_control_frame(const uint8_t *frame, uint16_t frame_len)
{
    const uint8_t *payload;
    uint16_t payload_len;

    /* Handle ARP requests first — the Mac needs to resolve our MAC address
       before it can send packets back to us. */
    if (handle_arp_frame(frame, frame_len)) {
        return 1;  /* ARP reply queued, done */
    }

    /* First, check if this frame is a TCP response for an active BLE-Eth tunnel.
       The Mac's TCP stack may respond (e.g. SYN+ACK) to frames injected by
       BLE-Eth, and these need to be routed back through the tunnel. */
    if (BLE_Eth_HandleTargetFrame(frame, frame_len) && g_ble_tx_event_cb) {
        g_ble_tx_event_cb();
        /* Don't drop the frame — let it also go to the Mac's queue below */
    }

    if (!tcp_payload_from_ipv4_frame(frame, frame_len, &payload, &payload_len)) {
        return 0;
    }
    if (!ch9329_payload_valid(payload, payload_len)) {
        return 0;
    }

    CH9329_RxBuffer_Add((uint8_t *)payload, payload_len);

    /* Pop the CH9329 response (generated synchronously by the parser) and
     * wrap it in Ethernet/IPv4/TCP so it comes back to the host through
     * the ECM bridge's bulk IN endpoint. */
    Resp_Report_t resp;
    if (Queue_Pop_Response(&resp)) {
        uint8_t out_frame[ECM_BRIDGE_MTU_FRAME_SIZE];
        uint16_t off = 0;

        /* Ethernet header: src←dst, dst←src (reverse direction) */
        memcpy(&out_frame[off], &frame[6], 6);  /* dst = original src */
        off += 6;
        memcpy(&out_frame[off], &frame[0], 6);  /* src = original dst */
        off += 6;
        out_frame[off++] = 0x08;
        out_frame[off++] = 0x00;  /* EtherType = IPv4 */

        /* IPv4 header (20 bytes) */
        uint16_t total_ip_len = 20 + 20 + resp.len;
        memset(&out_frame[off], 0, 20);
        out_frame[off + 0] = 0x45;  /* version=4, IHL=5 */
        out_frame[off + 1] = 0x00;  /* DSCP */
        out_frame[off + 2] = (total_ip_len >> 8) & 0xFF;
        out_frame[off + 3] = total_ip_len & 0xFF;
        out_frame[off + 4] = 0x00;  /* identification */
        out_frame[off + 5] = 0x01;
        out_frame[off + 6] = 0x40;  /* DF */
        out_frame[off + 7] = 0x00;  /* fragment offset */
        out_frame[off + 8] = 64;    /* TTL */
        out_frame[off + 9] = 6;     /* protocol = TCP */
        /* src IP = original dst IP */
        memcpy(&out_frame[off + 12], &frame[0x1A], 4);
        /* dst IP = original src IP */
        memcpy(&out_frame[off + 16], &frame[0x1E], 4);

        /* IPv4 checksum */
        uint32_t ip_sum = 0;
        for (int i = 0; i < 20; i += 2) {
            ip_sum += (uint16_t)(out_frame[off + i] << 8) | out_frame[off + i + 1];
        }
        while (ip_sum >> 16) ip_sum = (ip_sum & 0xFFFF) + (ip_sum >> 16);
        uint16_t ip_chk = (~ip_sum) & 0xFFFF;
        out_frame[off + 10] = (ip_chk >> 8) & 0xFF;
        out_frame[off + 11] = ip_chk & 0xFF;
        off += 20;

        /* TCP header (20 bytes) */
        memset(&out_frame[off], 0, 20);
        /* src port = original dst port, dst port = original src port */
        memcpy(&out_frame[off + 0], &frame[0x22], 2);
        memcpy(&out_frame[off + 2], &frame[0x20], 2);
        out_frame[off + 4] = 0x00;
        out_frame[off + 5] = 0x00;
        out_frame[off + 6] = 0x00;
        out_frame[off + 7] = 0x01;  /* seq */
        out_frame[off + 8] = 0x00;
        out_frame[off + 9] = 0x00;
        out_frame[off + 10] = 0x00;
        out_frame[off + 11] = 0x00;  /* ack */
        out_frame[off + 12] = 0x50;  /* data offset = 5 (20 bytes) */
        out_frame[off + 13] = 0x02;  /* flags = SYN */
        out_frame[off + 14] = 0xFF;
        out_frame[off + 15] = 0xFF;  /* window */
        off += 20;

        /* TCP payload (CH9329 response) */
        memcpy(&out_frame[off], resp.data, resp.len);
        off += resp.len;

        /* Push to target→host queue so it comes back to the host */
        queue_push(&g_target_to_host, out_frame, off);
    }

    return 1;
}

static uint8_t queue_push(ECM_FrameQueue_t *queue, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return 1;
    }
    if (queue->count >= ECM_BRIDGE_QUEUE_DEPTH) {
        queue->overflow++;
        /* Queue is full — likely previous TX failed. Flush old data
           and push the new frame so it gets a chance to be sent. */
        queue->rd = queue->wr;
        queue->count = 0;
    }

    memcpy(queue->frames[queue->wr].data, data, len);
    queue->frames[queue->wr].len = len;
    USB_DEBUG("Q: wr=%d len=%d cnt=%d\r\n", queue->wr, len, queue->count + 1);
    queue->wr++;
    if (queue->wr >= ECM_BRIDGE_QUEUE_DEPTH) {
        queue->wr = 0;
    }
    queue->count++;
    return 1;
}

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
    frame->expected_len = 0;
    frame->drop = 0;
    frame->overflow = 0;
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

static uint16_t eth_frame_length(const uint8_t *data, uint16_t accumulated)
{
    /* Once we have at least 14 bytes (Ethernet header), extract the total
       frame length. */
    if (accumulated < 14) {
        return 0;
    }
    uint16_t ether = get_be16(&data[12]);
    if (ether == 0x0806) {
        /* ARP over Ethernet: fixed 28-byte payload + 14-byte header = 42 */
        return 42;
    }
    if (ether == 0x0800 && accumulated >= 18) {
        /* IPv4: Total Length is at offset 16 (2 bytes into IPv4 header,
           which is 16 bytes into the Ethernet frame).
           Total Ethernet frame = 14 + IPv4 total length. */
        uint16_t ipv4_total = get_be16(&data[16]);
        return (uint16_t)(14 + ipv4_total);
    }
    if (ether == 0x86DD && accumulated >= 20) {
        /* IPv6 payload length is at bytes 4..5 of the IPv6 header. */
        uint16_t ipv6_payload = get_be16(&data[18]);
        return (uint16_t)(14 + 40 + ipv6_payload);
    }
    if (ether < 0x0600) {
        /* IEEE 802.3 length field = payload length after 14-byte header */
        return (uint16_t)(14 + ether);
    }
    /* Unknown EtherType — can't determine length from header */
    return 0;
}

static void rx_frame_append(ECM_RxFrame_t *frame,
                            ECM_FrameQueue_t *queue,
                            const uint8_t *data,
                            uint16_t len,
                            uint16_t max_packet,
                            ECM_FrameHandler_t frame_handler)
{
    /* Append the chunk first (if not dropping) so we can parse headers. */
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

    /* Try to determine expected frame length from accumulated Ethernet data.
       This lets us handle fragmented USB transfers (e.g., 16+16+10 byte
       chunks for a 42-byte ARP frame) instead of incorrectly treating each
       short chunk as a complete frame. */
    if (frame->expected_len == 0 && !frame->drop && frame->len > 0) {
        uint16_t elen = eth_frame_length(frame->data, frame->len);
        if (elen > 0) {
            frame->expected_len = elen;
        }
    }

    uint8_t end_of_frame = 0;
    if (frame->expected_len > 0) {
        /* We know the exact frame length from Ethernet/protocol header */
        if (frame->len >= frame->expected_len) {
            end_of_frame = 1;
        }
    } else {
        /* Fallback: short-packet detection. Only fire if we either have
           enough data to have parsed headers (>= 18 bytes for IPv4) or the
           packet is too small to contain a valid Ethernet header (< 14).
           This prevents prematurely firing on fragmented EtherType frames
           where the first chunk is < max_packet but more data follows. */
        if (len < max_packet) {
            if (frame->len < 14) {
                /* Tiny fragment — treat as end of frame (likely an error) */
                end_of_frame = 1;
            } else {
                /* We have >= 14 bytes accumulated. Check if this is a known
                   EtherType where we should wait for more data. */
                uint16_t ether = get_be16(&frame->data[12]);
                if (ether == 0x0800 && frame->len < 18) {
                    /* IPv4 but not enough for Total Length field yet */
                    end_of_frame = 0;
                } else if (ether >= 0x0600 && ether != 0x0806) {
                    end_of_frame = 1;
                } else {
                    end_of_frame = 1;
                }
            }
        }
    }

    if (end_of_frame) {
        if (!frame->drop && frame->len > 0) {
            if (frame_handler == 0 || !frame_handler(frame->data, frame->len)) {
                (void)queue_push(queue, frame->data, frame->len);
            }
        }
        frame->len = 0;
        frame->drop = 0;
        frame->expected_len = 0;
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

    if (g_target_notify_state == 0) {
        return;
    }

    USB_DEBUG("ECM: notif state=%d busy=%d\r\n",
              g_target_notify_state, USBFS_Endp_Busy[DEF_UEP5]);

    if (USBFS_Endp_Busy[DEF_UEP5] != 0) {
        return;
    }

    len = make_notification(g_target_notify_state, ECM_BRIDGE_TARGET_COMM_IF, packet);
    if (USBFS_Endp_DataUp(DEF_UEP5, packet, len, DEF_UEP_CPY_LOAD) == READY) {
        USB_DEBUG("ECM: notif sent state=%d len=%d\r\n", g_target_notify_state, len);
        g_target_notify_state++;
        if (g_target_notify_state > 2) {
            g_target_notify_state = 0;
        }
    }
    else {
        USB_DEBUG("ECM: notif FAILED to send\r\n");
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

    /* Guard against EP7 being in use — poll_host_tx() also drives EP7.
     * Without this busy check, poll_target_tx() and poll_host_tx() can
     * overwrite each other's data or send out-of-order fragments. */
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

    /* When the CDC ECM data interface first becomes active (Mac sent
       SET_INTERFACE), queue a gratuitous ARP so the Mac's ARP cache
       gets populated immediately. */
    if (g_ecm_data_if_active && !g_garp_sent) {
        USB_DEBUG("ECM: IF active, queuing GARP\r\n");
        uint8_t garp[42];
        memset(garp, 0xFF, 6);
        memcpy(&garp[6], ECM_DEV_MAC, 6);
        garp[12] = 0x08; garp[13] = 0x06;
        garp[14] = 0x00; garp[15] = 0x01;
        garp[16] = 0x08; garp[17] = 0x00;
        garp[18] = 6;  garp[19] = 4;
        garp[20] = 0x00; garp[21] = 0x02;
        memcpy(&garp[22], ECM_DEV_MAC, 6);
        memcpy(&garp[28], ECM_DEV_IP, 4);
        memcpy(&garp[32], ECM_DEV_MAC, 6);
        memcpy(&garp[38], ECM_DEV_IP, 4);
        queue_push(&g_target_to_host, garp, sizeof(garp));
        g_garp_sent = 1;
        g_ecm_tx_state = 1;
    }

    frame = queue_peek(&g_target_to_host);
    if (frame == 0) {
        g_host_tx_offset = 0;
        g_host_zlp_pending = 0;
        return;
    }

    /* USBFS EP7 path (target port, en20). Only attempt TX after the Mac
       has activated the CDC ECM data interface via SET_INTERFACE(IF5, alt=1).
       Without this, USBFS_Endp_DataUp returns READY but the endpoint never
       responds to host IN tokens. */
    if (!g_ecm_data_if_active) {
        return;
    }

    if (USBFS_Endp_Busy[DEF_UEP7] != 0) {
        g_ecm_tx_state = 3;
        return;
    }

    static uint8_t tx_count = 0;
    if ((++tx_count & 0x0F) == 0) {
        USB_DEBUG("EP7 TX len=%d busy=%d if_active=%d\r\n", frame->len,
                   USBFS_Endp_Busy[DEF_UEP7], g_ecm_data_if_active);
    }

    if (g_host_zlp_pending) {
        g_ecm_tx_state = 2;
        if (USBFS_Endp_DataUp(DEF_UEP7, frame->data, 0, DEF_UEP_CPY_LOAD) == READY) {
            g_ecm_tx_count++;
            g_ecm_tx_state = 2;  /* success */
            g_host_zlp_pending = 0;
            g_host_tx_offset = 0;
            queue_pop(&g_target_to_host);
        }
        else {
            g_ecm_tx_state = 4;  /* DataUp failed */
        }
        return;
    }

    len = (uint16_t)(frame->len - g_host_tx_offset);
    if (len > ECM_BRIDGE_USBFS_PACKET_SIZE) {
        len = ECM_BRIDGE_USBFS_PACKET_SIZE;
    }

    if (USBFS_Endp_DataUp(DEF_UEP7, &frame->data[g_host_tx_offset], len, DEF_UEP_CPY_LOAD) == READY) {
        g_ecm_tx_count++;
        g_ecm_tx_state = 2;  /* success */
        g_host_tx_offset = (uint16_t)(g_host_tx_offset + len);
        if (g_host_tx_offset >= frame->len) {
            if (len == ECM_BRIDGE_USBFS_PACKET_SIZE) {
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
    g_garp_sent = 0;

     g_ecm_data_if_active = 0;

    /* Force enable EP7 TX at the hardware level */
    USBFSD_UEP_MOD(3) |= USBFS_UEP7_TX_EN;

    /* Also configure EP6 RX to ACK so we can receive data from the Mac
       without SET_INTERFACE. Without this, EP6 stays in NAK state and
       the Mac's bulk OUT (ARP requests etc.) is silently dropped. */
    USBFSD->UEP6_RX_CTRL = USBFS_UEP_R_RES_ACK;

    USB_DEBUG("ECM init done\r\n");
}

void ECM_Bridge_SendRawFrame(const uint8_t *data, uint16_t len)
{
    /* Queue raw Ethernet frame directly to target_to_host, bypassing
       CH9329 protocol handler. Used by BLE-Eth TCP frame injection. */
    USB_DEBUG("ECM send: len=%d\r\n", len);
    if (len > 0 && len <= 54) {
        USB_DEBUG("  First 16: ");
        for (int i = 0; i < 16 && i < len; i++) {
            USB_DEBUG("%02x ", data[i]);
        }
        USB_DEBUG("\r\n");
    }
    uint8_t ok = queue_push(&g_target_to_host, data, len);
    USB_DEBUG("  q: %s\r\n", ok ? "OK" : "FAIL");
}

void ECM_Bridge_HostRxDone(const uint8_t *data, uint16_t len)
{
    /* LED: blink ON briefly when data arrives via EP6 */
    if (len > 0) g_ecm_led_override = 1;
    rx_frame_append(&g_host_rx_frame, &g_host_to_target, data, len,
                    ECM_BRIDGE_USBD_PACKET_SIZE, handle_host_control_frame);
}

void ECM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len)
{
    /* Force active state if we are receiving data (bypasses missed SET_INTERFACE) */
    g_ecm_data_if_active = 1;

    rx_frame_append(&g_target_rx_frame, &g_target_to_host, data, len,
                    ECM_BRIDGE_USBFS_PACKET_SIZE, handle_target_frame);
}

static uint8_t handle_target_frame(const uint8_t *frame, uint16_t frame_len)
{
    /* Learn host MAC from any incoming frame (point-to-point CDC ECM link) */
    if (frame_len >= 14) {
        BLE_Eth_LearnHostMac(&frame[6]);  /* source MAC is the host's MAC */
    }

    /* Handle ARP replies from target side (USBFS) for BLE-Eth connections */
    if (frame_len >= 42 && frame[12] == 0x08 && frame[13] == 0x06) {
        uint16_t arp_op = (uint16_t)(frame[20] << 8) | frame[21];
        if (arp_op == 2) {  /* ARP reply */
            uint8_t sender_ip[4], sender_mac[6];
            memcpy(sender_mac, &frame[22], 6);
            memcpy(sender_ip, &frame[28], 4);
            if (BLE_Eth_HandleArpReply(sender_ip, sender_mac)) {
                USB_DEBUG("ARP reply from target side\r\n");
            }
        }
    }

    /* Handle ARP requests from the Mac (connected via USBFS) */
    if (handle_arp_frame(frame, frame_len)) {
        return 1;  /* ARP reply queued, don't queue original */
    }

    /* Try to match frame to a BLE-Eth tunnel and push TCP payload back via BLE */
    if (BLE_Eth_HandleTargetFrame(frame, frame_len) && g_ble_tx_event_cb) {
        g_ble_tx_event_cb();
    }
    return 1;
}

void ECM_Bridge_Poll(void)
{
    poll_target_notification();
    poll_host_notification();
    poll_target_tx();
    poll_host_tx();
}

#endif /* USB_WORK_MODE_COMPILES_BRIDGE_ECM */