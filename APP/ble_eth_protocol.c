/********************************** (C) COPYRIGHT *******************************
 * File Name          : ble_eth_protocol.c
 * Author             : Openterface Team
 * Version            : V1.0.0
 * Date               : 2026/05/21
 * Description        : BLE-to-Ethernet tunneling protocol implementation.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#include "include/ble_eth_protocol.h"
#include <string.h>

/* ======================== Internal State ================================= */

static ble_eth_parser_t     g_parser;
static ble_eth_conn_t       g_connections[BLE_ETH_MAX_CONNECTIONS];
static ble_eth_event_cb_t   g_event_cb;

/* TX queue: holds pre-built frames ready for BLE notification */
#define BLE_ETH_TX_QUEUE_DEPTH  2U
#define BLE_ETH_TX_CHUNK_SIZE   255U  /* fits in ble notify (uint8_t len param, max 255) */

typedef struct {
    uint8_t  data[BLE_ETH_TX_CHUNK_SIZE];
    uint16_t len;
} ble_eth_tx_chunk_t;

typedef struct {
    ble_eth_tx_chunk_t chunks[BLE_ETH_TX_QUEUE_DEPTH];
    volatile uint8_t   rd;
    volatile uint8_t   wr;
    volatile uint8_t   count;
} ble_eth_tx_queue_t;

static ble_eth_tx_queue_t  g_tx_queue;

/* Reassembly buffer (one at a time, since BLE is single-connection) */
static ble_eth_reassembly_t g_reassembly;

/* Callback for sending complete Ethernet frames to ECM bridge.
   Set by peripheral.c via BLE_Eth_SetEcmtxCb(). */
typedef void (*ble_eth_ecm_tx_cb_t)(const uint8_t *data, uint16_t len);
static ble_eth_ecm_tx_cb_t g_ecm_tx_cb;

/* Device-side MAC and IP for ECM frames (must be on same subnet as Mac) */
static const uint8_t g_dev_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x03 };
static const uint8_t g_host_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
static const uint8_t g_dev_ip[4] = { 192, 168, 11, 3 };

void BLE_Eth_SetEcmtxCb(ble_eth_ecm_tx_cb_t cb)
{
    g_ecm_tx_cb = cb;
}

/* ======================== Helpers ======================================== */

static uint8_t calc_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

static uint16_t build_frame(uint8_t *buf, uint8_t addr, uint8_t cmd,
                            const uint8_t *payload, uint16_t plen)
{
    uint16_t off = 0;

    buf[off++] = BLE_ETH_FRAME_HEAD1;
    buf[off++] = BLE_ETH_FRAME_HEAD2;
    buf[off++] = addr;
    buf[off++] = cmd;
    buf[off++] = (uint8_t)plen;

    if (payload && plen > 0) {
        memcpy(&buf[off], payload, plen);
        off += plen;
    }

    buf[off++] = calc_checksum(buf, off);
    return off;
}

static void tx_queue_clear(void)
{
    g_tx_queue.rd = 0;
    g_tx_queue.wr = 0;
    g_tx_queue.count = 0;
}

static bool tx_queue_push(const uint8_t *data, uint16_t len)
{
    if (len == 0) return true;
    if (g_tx_queue.count >= BLE_ETH_TX_QUEUE_DEPTH) return false;

    uint16_t copy_len = len;
    if (copy_len > BLE_ETH_TX_CHUNK_SIZE) copy_len = BLE_ETH_TX_CHUNK_SIZE;

    memcpy(g_tx_queue.chunks[g_tx_queue.wr].data, data, copy_len);
    g_tx_queue.chunks[g_tx_queue.wr].len = copy_len;

    g_tx_queue.wr++;
    if (g_tx_queue.wr >= BLE_ETH_TX_QUEUE_DEPTH) g_tx_queue.wr = 0;
    g_tx_queue.count++;

    /* If data was larger than one chunk, push remainder recursively */
    if (len > BLE_ETH_TX_CHUNK_SIZE) {
        tx_queue_push(data + BLE_ETH_TX_CHUNK_SIZE, len - BLE_ETH_TX_CHUNK_SIZE);
    }

    return true;
}

static void reassembly_clear(void)
{
    g_reassembly.total_len = 0;
    g_reassembly.received_len = 0;
    g_reassembly.conn_id = BLE_ETH_INVALID_CONN_ID;
    g_reassembly.total_frags = 0;
    g_reassembly.next_seq = 0;
    g_reassembly.in_progress = false;
}

static void evt_frame_rx(const ble_eth_frame_t *frame)
{
    if (g_event_cb) {
        ble_eth_evt_t evt;
        evt.type = BLE_ETH_EVT_FRAME_RX;
        memcpy(&evt.info.frame, frame, sizeof(ble_eth_frame_t));
        g_event_cb(&evt);
    }
}

static void evt_data_ready(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    if (g_event_cb) {
        ble_eth_evt_t evt;
        evt.type = BLE_ETH_EVT_DATA_READY;
        evt.info.data.conn_id = conn_id;
        evt.info.data.data = (uint8_t *)data;
        evt.info.data.len = len;
        g_event_cb(&evt);
    }
}

static void evt_conn_opened(uint8_t conn_id, uint8_t status)
{
    if (g_event_cb) {
        ble_eth_evt_t evt;
        evt.type = BLE_ETH_EVT_CONN_OPENED;
        evt.info.conn.conn_id = conn_id;
        evt.info.conn.status = status;
        g_event_cb(&evt);
    }
}

static void evt_conn_closed(uint8_t conn_id, uint8_t status)
{
    if (g_event_cb) {
        ble_eth_evt_t evt;
        evt.type = BLE_ETH_EVT_CONN_CLOSED;
        evt.info.conn.conn_id = conn_id;
        evt.info.conn.status = status;
        g_event_cb(&evt);
    }
}

/* ======================== Frame Dispatch ================================ */

static uint8_t send_tcp_frame(uint8_t conn_id, uint16_t tcp_flags,
                              const uint8_t *payload, uint16_t payload_len);

static void dispatch_frame(const ble_eth_frame_t *frame)
{
    uint8_t addr = frame->addr;
    uint8_t cmd = frame->cmd;

    /* Check if this is a response from device (cmd >= 0x80) -
     * these are normally handled by the host side, but on device
     * we may receive them for loopback testing. For now, handle
     * only host->device commands. */

    switch (cmd) {
    case BLE_ETH_CMD_CONNECT: {
        /* Parse connect payload */
        if (frame->payload_len < BLE_ETH_CONNECT_PAYLOAD_LEN) {
            uint8_t status = BLE_ETH_STATUS_PARAM_ERR;
            uint8_t rsp[2] = { BLE_ETH_INVALID_CONN_ID, status };
            uint8_t buf[16];
            uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_CONNECT_RESP, rsp, 2);
            tx_queue_push(buf, len);
            evt_conn_opened(BLE_ETH_INVALID_CONN_ID, status);
            return;
        }

        const uint8_t *ip = frame->payload;
        uint16_t port = ((uint16_t)frame->payload[4] << 8) | frame->payload[5];

        uint8_t conn_id = BLE_Eth_AllocConnection(ip, port);
        uint8_t status = (conn_id != BLE_ETH_INVALID_CONN_ID)
                         ? BLE_ETH_STATUS_SUCCESS
                         : BLE_ETH_STATUS_CONN_FULL;

        /* If connection succeeded, send TCP SYN to target via ECM */
        if (status == BLE_ETH_STATUS_SUCCESS && g_ecm_tx_cb) {
            send_tcp_frame(conn_id, 0x02, NULL, 0);  /* SYN only */
        }

        uint8_t rsp[2] = { conn_id, status };
        uint8_t buf[16];
        uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_CONNECT_RESP, rsp, 2);
        tx_queue_push(buf, len);
        evt_conn_opened(conn_id, status);
        break;
    }

    case BLE_ETH_CMD_DISCONNECT: {
        if (frame->payload_len < BLE_ETH_DISCONNECT_PAYLOAD_LEN) {
            uint8_t buf[16];
            uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_DISCONN_RESP,
                                       NULL, 0);
            tx_queue_push(buf, len);
            return;
        }

        uint8_t conn_id = frame->payload[0];
        BLE_Eth_FreeConnection(conn_id);

        uint8_t rsp[2] = { conn_id, BLE_ETH_STATUS_SUCCESS };
        uint8_t buf[16];
        uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_DISCONN_RESP, rsp, 2);
        tx_queue_push(buf, len);
        evt_conn_closed(conn_id, BLE_ETH_STATUS_SUCCESS);
        break;
    }

    case BLE_ETH_CMD_DATA: {
        /* Parse fragment header */
        if (frame->payload_len < BLE_ETH_FRAG_HEADER_LEN) {
            uint8_t buf[16];
            uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_ERROR, NULL, 0);
            tx_queue_push(buf, len);
            return;
        }

        uint8_t frag_flags = frame->payload[0];
        uint8_t seq        = frame->payload[1];
        uint8_t conn_id    = frame->payload[2];
        const uint8_t *frag_data = &frame->payload[3];
        uint16_t frag_data_len = frame->payload_len - BLE_ETH_FRAG_HEADER_LEN;
        uint8_t total_frags = frag_flags & BLE_ETH_FRAG_COUNT_MASK;

        if (total_frags == 0) total_frags = 1;

        /* Start new reassembly on first fragment */
        if (frag_flags & BLE_ETH_FRAG_FIRST) {
            reassembly_clear();
            g_reassembly.conn_id = conn_id;
            g_reassembly.total_frags = total_frags;
            g_reassembly.next_seq = 0;
            g_reassembly.in_progress = true;

            /* Pre-allocate buffer size estimate */
            g_reassembly.total_len = 0; /* will be set as we go */
        }

        if (!g_reassembly.in_progress) {
            return; /* no reassembly in progress, drop */
        }

        if (seq != g_reassembly.next_seq) {
            /* Out of order, abort */
            reassembly_clear();
            return;
        }

        /* Copy fragment data */
        if (g_reassembly.received_len + frag_data_len <= sizeof(g_reassembly.data)) {
            memcpy(&g_reassembly.data[g_reassembly.received_len],
                   frag_data, frag_data_len);
            g_reassembly.received_len += frag_data_len;
            g_reassembly.next_seq++;
        }

        /* Check if this is the last fragment */
        if (!(frag_flags & BLE_ETH_FRAG_MORE)) {
            /* Reassembly complete */
            g_reassembly.in_progress = false;
            evt_data_ready(conn_id, g_reassembly.data, g_reassembly.received_len);
            reassembly_clear();

            /* Send ACK */
            uint8_t rsp[1] = { BLE_ETH_STATUS_SUCCESS };
            uint8_t buf[16];
            uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_DATA_RESP, rsp, 1);
            tx_queue_push(buf, len);
        }
        break;
    }

    case BLE_ETH_CMD_INFO: {
        uint8_t rsp[BLE_ETH_INFO_PAYLOAD_LEN];
        memset(rsp, 0, sizeof(rsp));
        rsp[0] = BLE_ETH_PROTO_VERSION;
        rsp[1] = BLE_ETH_MAX_CONNECTIONS;
        rsp[2] = 0x00;  /* high byte of max single-frag payload */
        rsp[3] = BLE_ETH_MAX_PAYLOAD_LEN - BLE_ETH_FRAG_HEADER_LEN;  /* low byte */
        rsp[4] = BLE_ETH_FEAT_TCP_TUNNEL | BLE_ETH_FEAT_SSH_OPT;

        uint8_t buf[24];
        uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_INFO_RESP, rsp, BLE_ETH_INFO_PAYLOAD_LEN);
        tx_queue_push(buf, len);
        break;
    }

    case BLE_ETH_CMD_PING: {
        /* Echo payload back as PONG — tests BLE data path without network */
        uint8_t buf[8 + BLE_ETH_MAX_PAYLOAD_LEN];
        uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_PONG,
                                   frame->payload, frame->payload_len);
        tx_queue_push(buf, len);
        break;
    }

    default:
        /* Unknown command - send error */
        {
            uint8_t buf[16];
            uint16_t len = build_frame(buf, addr, BLE_ETH_CMD_ERROR, NULL, 0);
            tx_queue_push(buf, len);
        }
        break;
    }
}

/* ======================== Parser ========================================= */

void BLE_Eth_Parse(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        switch (g_parser.state) {
        case BLE_ETH_PARSER_WAIT_HEAD1:
            if (byte == BLE_ETH_FRAME_HEAD1) {
                g_parser.frame.payload_len = 0;
                g_parser.checksum = byte;
                g_parser.rx_index = 1;
                g_parser.state = BLE_ETH_PARSER_WAIT_HEAD2;
            }
            break;

        case BLE_ETH_PARSER_WAIT_HEAD2:
            if (byte == BLE_ETH_FRAME_HEAD2) {
                g_parser.checksum += byte;
                g_parser.rx_index++;
                g_parser.state = BLE_ETH_PARSER_WAIT_ADDR;
            } else {
                /* Not head2, restart search */
                g_parser.state = BLE_ETH_PARSER_WAIT_HEAD1;
                if (byte == BLE_ETH_FRAME_HEAD1) {
                    g_parser.checksum = byte;
                    g_parser.rx_index = 1;
                    g_parser.state = BLE_ETH_PARSER_WAIT_HEAD2;
                }
            }
            break;

        case BLE_ETH_PARSER_WAIT_ADDR:
            g_parser.frame.addr = byte;
            g_parser.checksum += byte;
            g_parser.rx_index++;
            g_parser.state = BLE_ETH_PARSER_WAIT_CMD;
            break;

        case BLE_ETH_PARSER_WAIT_CMD:
            g_parser.frame.cmd = byte;
            g_parser.checksum += byte;
            g_parser.rx_index++;
            g_parser.state = BLE_ETH_PARSER_WAIT_LEN;
            break;

        case BLE_ETH_PARSER_WAIT_LEN:
            g_parser.frame.payload_len = byte;
            g_parser.checksum += byte;
            g_parser.rx_index++;
            if (byte == 0) {
                /* No payload, go straight to checksum */
                g_parser.state = BLE_ETH_PARSER_WAIT_CSUM;
            } else if (byte > BLE_ETH_MAX_PAYLOAD_LEN) {
                /* Payload too long, skip this frame */
                g_parser.state = BLE_ETH_PARSER_WAIT_HEAD1;
            } else {
                g_parser.rx_index = 0;
                g_parser.state = BLE_ETH_PARSER_WAIT_PAYLOAD;
            }
            break;

        case BLE_ETH_PARSER_WAIT_PAYLOAD:
            g_parser.frame.payload[g_parser.rx_index] = byte;
            g_parser.checksum += byte;
            g_parser.rx_index++;
            if (g_parser.rx_index >= g_parser.frame.payload_len) {
                g_parser.state = BLE_ETH_PARSER_WAIT_CSUM;
            }
            break;

        case BLE_ETH_PARSER_WAIT_CSUM:
            if (byte == g_parser.checksum) {
                /* Valid frame, dispatch */
                dispatch_frame(&g_parser.frame);
            }
            /* Either way, restart */
            g_parser.state = BLE_ETH_PARSER_WAIT_HEAD1;
            break;
        }
    }
}

/* ======================== Public API ===================================== */

void BLE_Eth_Init(ble_eth_event_cb_t event_cb)
{
    g_event_cb = event_cb;
    memset(&g_parser, 0, sizeof(g_parser));
    g_parser.state = BLE_ETH_PARSER_WAIT_HEAD1;
    memset(g_connections, 0, sizeof(g_connections));
    tx_queue_clear();
    reassembly_clear();
}

uint16_t BLE_Eth_BuildConnect(uint8_t *buf, const uint8_t remote_ip[4], uint16_t remote_port)
{
    uint8_t payload[BLE_ETH_CONNECT_PAYLOAD_LEN];
    memcpy(payload, remote_ip, 4);
    payload[4] = (remote_port >> 8) & 0xFF;
    payload[5] = remote_port & 0xFF;
    return build_frame(buf, 0x00, BLE_ETH_CMD_CONNECT, payload, BLE_ETH_CONNECT_PAYLOAD_LEN);
}

uint16_t BLE_Eth_BuildDisconnect(uint8_t *buf, uint8_t conn_id)
{
    return build_frame(buf, 0x00, BLE_ETH_CMD_DISCONNECT, &conn_id, BLE_ETH_DISCONNECT_PAYLOAD_LEN);
}

uint16_t BLE_Eth_BuildData(uint8_t *buf, uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    if (len > BLE_ETH_MAX_PAYLOAD_LEN - BLE_ETH_FRAG_HEADER_LEN) {
        return 0; /* Needs fragmentation, use PushTargetData instead */
    }

    uint8_t payload[BLE_ETH_MAX_PAYLOAD_LEN];
    payload[0] = BLE_ETH_FRAG_FIRST;  /* FIRST, no MORE, count=1 */
    payload[1] = 0x00;                 /* seq 0 */
    payload[2] = conn_id;

    if (data && len > 0) {
        memcpy(&payload[3], data, len);
    }

    return build_frame(buf, 0x00, BLE_ETH_CMD_DATA, payload,
                       BLE_ETH_FRAG_HEADER_LEN + len);
}

uint16_t BLE_Eth_BuildInfoRequest(uint8_t *buf)
{
    return build_frame(buf, 0x00, BLE_ETH_CMD_INFO, NULL, 0);
}

uint16_t BLE_Eth_BuildResponse(uint8_t *buf, uint8_t cmd, uint8_t addr,
                               uint8_t status, const uint8_t *payload, uint8_t plen)
{
    uint8_t rsp[1 + 252];
    rsp[0] = status;
    if (payload && plen > 0) {
        memcpy(&rsp[1], payload, plen);
    }
    return build_frame(buf, addr, cmd, rsp, plen + 1);
}

const ble_eth_conn_t *BLE_Eth_GetConnection(uint8_t conn_id)
{
    if (conn_id >= BLE_ETH_MAX_CONNECTIONS) return NULL;
    if (!g_connections[conn_id].active) return NULL;
    return &g_connections[conn_id];
}

uint8_t BLE_Eth_AllocConnection(const uint8_t remote_ip[4], uint16_t remote_port)
{
    for (uint8_t i = 0; i < BLE_ETH_MAX_CONNECTIONS; i++) {
        if (!g_connections[i].active) {
            g_connections[i].active = true;
            g_connections[i].conn_id = i;
            memcpy(g_connections[i].remote_ip, remote_ip, 4);
            g_connections[i].remote_port = remote_port;
            g_connections[i].tcp_state = BLE_ETH_TCP_SYN_SENT;
            g_connections[i].local_seq = 0x12345678 + i;  /* unique per conn */
            g_connections[i].local_ack = 0;
            g_connections[i].remote_seq = 0;
            g_connections[i].remote_ack = 0;
            g_connections[i].local_port = 50000 + i * 10;
            return i;
        }
    }
    return BLE_ETH_INVALID_CONN_ID;
}

void BLE_Eth_FreeConnection(uint8_t conn_id)
{
    if (conn_id >= BLE_ETH_MAX_CONNECTIONS) return;
    memset(&g_connections[conn_id], 0, sizeof(ble_eth_conn_t));
}

bool BLE_Eth_PushTargetData(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) return false;

    /* Calculate fragmentation */
    uint16_t max_frag_data = BLE_ETH_MAX_PAYLOAD_LEN - BLE_ETH_FRAG_HEADER_LEN;
    uint16_t total_frags = (len + max_frag_data - 1) / max_frag_data;
    if (total_frags > 6) return false; /* Too large (limited by reassembly buffer) */

    uint16_t offset = 0;
    for (uint8_t seq = 0; seq < total_frags; seq++) {
        uint16_t chunk_len = len - offset;
        if (chunk_len > max_frag_data) chunk_len = max_frag_data;

        uint8_t frag_header[3];
        frag_header[0] = BLE_ETH_FRAG_COUNT_MASK & total_frags;  /* count */
        if (seq == 0) frag_header[0] |= BLE_ETH_FRAG_FIRST;
        if (seq < total_frags - 1) frag_header[0] |= BLE_ETH_FRAG_MORE;
        frag_header[1] = seq;
        frag_header[2] = conn_id;

        /* Build complete frame: header + frag_header + data + checksum */
        uint16_t total_payload = BLE_ETH_FRAG_HEADER_LEN + chunk_len;
        uint8_t full_buf[8 + BLE_ETH_MAX_PAYLOAD_LEN];
        uint16_t off = 0;

        full_buf[off++] = BLE_ETH_FRAME_HEAD1;
        full_buf[off++] = BLE_ETH_FRAME_HEAD2;
        full_buf[off++] = 0x00;  /* addr */
        full_buf[off++] = BLE_ETH_CMD_DATA_PUSH;
        full_buf[off++] = (uint8_t)total_payload;
        memcpy(&full_buf[off], frag_header, 3);
        off += 3;
        memcpy(&full_buf[off], &data[offset], chunk_len);
        off += chunk_len;
        full_buf[off++] = calc_checksum(full_buf, off);

        tx_queue_push(full_buf, off);

        offset += chunk_len;
    }

    return true;
}

uint16_t BLE_Eth_GetNextTxChunk(uint8_t *buf)
{
    if (g_tx_queue.count == 0) return 0;

    ble_eth_tx_chunk_t *chunk = &g_tx_queue.chunks[g_tx_queue.rd];
    memcpy(buf, chunk->data, chunk->len);
    uint16_t len = chunk->len;

    g_tx_queue.rd++;
    if (g_tx_queue.rd >= BLE_ETH_TX_QUEUE_DEPTH) g_tx_queue.rd = 0;
    g_tx_queue.count--;

    return len;
}

/* ======================== ECM TCP frame generation ======================= */

static uint16_t ecm_get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t ecm_get_be32(const uint8_t *p)
{
    return (uint32_t)(p[0]) << 24 | (uint32_t)(p[1]) << 16 |
           (uint32_t)(p[2]) << 8  | (uint32_t)(p[3]);
}

static void ecm_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static void ecm_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static uint16_t ip_checksum(const uint8_t *hdr, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i += 2) {
        sum += (uint16_t)(hdr[i] << 8) | hdr[i + 1];
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (~sum) & 0xFFFF;
}

/* Build Ethernet + IPv4 + TCP frame and send via ECM callback.
   Returns 1 on success, 0 if no callback registered. */
static uint8_t send_tcp_frame(uint8_t conn_id, uint16_t tcp_flags,
                              const uint8_t *payload, uint16_t payload_len)
{
    if (!g_ecm_tx_cb) return 0;

    ble_eth_conn_t *c = &g_connections[conn_id];
    if (!c->active) return 0;

    /* Build frame: 14 Eth + 20 IPv4 + 20 TCP + payload */
    uint16_t total = 14 + 20 + 20 + payload_len;
    if (total > 1514) return 0;

    uint8_t buf[1514];
    uint16_t off = 0;

    /* Ethernet header */
    memcpy(&buf[off], g_host_mac, 6); off += 6;  /* dst = Mac */
    memcpy(&buf[off], g_dev_mac, 6);  off += 6;  /* src = device */
    ecm_put_be16(&buf[off], 0x0800); off += 2;   /* EtherType = IPv4 */

    /* IPv4 header */
    uint16_t ip_total = 20 + 20 + payload_len;
    buf[off + 0] = 0x45;              /* v4, IHL=5 */
    buf[off + 1] = 0x00;              /* DSCP */
    ecm_put_be16(&buf[off + 2], ip_total);
    ecm_put_be16(&buf[off + 4], 0x0001); /* identification */
    ecm_put_be16(&buf[off + 6], 0x4000); /* DF */
    buf[off + 8] = 64;                /* TTL */
    buf[off + 9] = 6;                 /* protocol = TCP */
    buf[off + 10] = 0; buf[off + 11] = 0; /* checksum placeholder */
    memcpy(&buf[off + 12], g_dev_ip, 4);    /* src IP */
    memcpy(&buf[off + 16], c->remote_ip, 4); /* dst IP */
    uint16_t ip_chk = ip_checksum(&buf[off], 20);
    ecm_put_be16(&buf[off + 10], ip_chk);
    off += 20;

    /* TCP header */
    ecm_put_be16(&buf[off + 0], c->local_port);   /* src port */
    ecm_put_be16(&buf[off + 2], c->remote_port);  /* dst port */
    ecm_put_be32(&buf[off + 4], c->local_seq);    /* seq */
    ecm_put_be32(&buf[off + 8], c->local_ack);    /* ack */
    buf[off + 12] = 0x50;             /* data offset = 5 (20 bytes) */
    buf[off + 13] = tcp_flags;        /* flags */
    ecm_put_be16(&buf[off + 14], 0xFFFF); /* window */
    buf[off + 16] = 0; buf[off + 17] = 0; /* checksum = 0 (not verified by most stacks) */
    buf[off + 18] = 0; buf[off + 19] = 0; /* urgent pointer */
    off += 20;

    /* Payload */
    if (payload && payload_len > 0) {
        memcpy(&buf[off], payload, payload_len);
        off += payload_len;
    }

    /* Update TCP sequence numbers */
    if (tcp_flags & 0x02) c->local_seq++;  /* SYN increments seq */
    if (tcp_flags & 0x01) c->local_seq++;  /* FIN increments seq */
    if (payload_len > 0) c->local_seq += payload_len;

    g_ecm_tx_cb(buf, total);
    return 1;
}

/* Send TCP data through tunnel. Wraps in Ethernet/IPv4/TCP. */
void BLE_Eth_SendDataToEth(uint8_t conn_id, const uint8_t *data, uint16_t len)
{
    if (conn_id >= BLE_ETH_MAX_CONNECTIONS) return;
    ble_eth_conn_t *c = &g_connections[conn_id];
    if (!c->active || c->tcp_state != BLE_ETH_TCP_ESTABLISHED) return;
    send_tcp_frame(conn_id, 0x18, data, len);  /* PSH+ACK */
}

/* ======================== Target frame handler =========================== */

static uint16_t frame_get_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

bool BLE_Eth_HandleTargetFrame(const uint8_t *frame, uint16_t len)
{
    if (!frame || len < 14u) return false;

    uint16_t off = 0;

    /* Parse Ethernet header (handle VLAN tags) */
    uint16_t eth_type = frame_get_be16(&frame[12]);
    off = 14u;
    while ((eth_type == 0x8100u || eth_type == 0x88A8u) && off + 4u <= len) {
        eth_type = frame_get_be16(&frame[off + 2]);
        off += 4;
    }

    if (eth_type != 0x0800u || off + 20u > len) return false;  /* not IPv4 */

    const uint8_t *ip = &frame[off];
    if ((ip[0] >> 4) != 4u) return false;

    uint16_t ip_header_len = (uint16_t)((ip[0] & 0x0Fu) * 4u);
    if (ip_header_len < 20u || off + ip_header_len + 20u > len) return false;
    if (ip[9] != 6u) return false;  /* not TCP */

    const uint8_t *tcp = &frame[off + ip_header_len];
    uint16_t tcp_segment_len = frame_get_be16(&ip[2]) - ip_header_len;
    uint16_t tcp_header_len = (uint16_t)((tcp[12] >> 4) * 4u);
    if (tcp_header_len < 20u || tcp_header_len > tcp_segment_len) return false;

    uint8_t tcp_flags = tcp[13];
    uint32_t remote_seq = ecm_get_be32(&tcp[4]);
    uint32_t remote_ack = ecm_get_be32(&tcp[8]);
    uint16_t tcp_payload_len = tcp_segment_len - tcp_header_len;

    /* Extract source IP and port from the frame */
    uint8_t src_ip[4];
    memcpy(src_ip, &ip[12], 4);
    uint16_t src_port = frame_get_be16(&tcp[0]);

    /* Match against active connections (remote_ip:remote_port == src_ip:src_port) */
    int8_t conn_id = -1;
    for (uint8_t i = 0; i < BLE_ETH_MAX_CONNECTIONS; i++) {
        if (g_connections[i].active &&
            memcmp(g_connections[i].remote_ip, src_ip, 4) == 0 &&
            g_connections[i].remote_port == src_port) {
            conn_id = (int8_t)i;
            break;
        }
    }

    if (conn_id < 0) return false;

    ble_eth_conn_t *c = &g_connections[(uint8_t)conn_id];

    /* Handle SYN-ACK: complete TCP handshake */
    if ((tcp_flags & 0x12) == 0x12) {  /* SYN+ACK */
        c->tcp_state = BLE_ETH_TCP_ESTABLISHED;
        c->remote_seq = remote_seq;
        c->remote_ack = remote_ack;
        c->local_ack = remote_seq + 1;  /* ACK the SYN */
        send_tcp_frame((uint8_t)conn_id, 0x10, NULL, 0);  /* ACK */
        /* PRINT("BLE-Eth TCP connected: conn=%d\r\n", conn_id); */
        return true;
    }

    /* Handle RST: connection refused */
    if (tcp_flags & 0x04) {
        c->tcp_state = BLE_ETH_TCP_CLOSED;
        /* PRINT("BLE-Eth TCP RST: conn=%d\r\n", conn_id); */
        return false;
    }

    /* Handle data from target */
    if (tcp_payload_len == 0) return false;

    /* Update ack number from target's response */
    if (remote_ack > c->remote_ack) {
        c->remote_ack = remote_ack;
    }
    c->local_ack = remote_seq + tcp_payload_len;

    /* Push TCP payload back to BLE host */
    const uint8_t *tcp_payload = &tcp[tcp_header_len];
    return BLE_Eth_PushTargetData((uint8_t)conn_id, tcp_payload, tcp_payload_len);
}

/* ======================== Public API ===================================== */

void BLE_Eth_ResetParser(void)
{
    g_parser.state = BLE_ETH_PARSER_WAIT_HEAD1;
    g_parser.rx_index = 0;
    g_parser.checksum = 0;
    reassembly_clear();
}

bool BLE_Eth_HasActiveTunnel(void)
{
    for (uint8_t i = 0; i < BLE_ETH_MAX_CONNECTIONS; i++) {
        if (g_connections[i].active &&
            g_connections[i].tcp_state == BLE_ETH_TCP_ESTABLISHED) {
            return true;
        }
    }
    return false;
}
