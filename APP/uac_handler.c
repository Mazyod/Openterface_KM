/******************************************************************************
 * @file    uac_handler.c
 * @brief   USB Audio Class 1.0 – bidirectional audio bridge, CH32V208.
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Signal flow (from USB-Host perspective)                                │
 * │                                                                         │
 * │  Host PC mic  ──USBD EP5 OUT──► h2t_buf ──USBFS EP5 IN──► Target spk   │
 * │  Target mic   ─USBFS EP6 OUT──► t2h_buf ──USBD EP4 IN──►  Host PC spk  │
 * │                                                                         │
 * │  USBFS (Target PC side):  EP5 ISO IN   64 B/ms  16 kHz stereo 16-bit   │
 * │                           EP6 ISO OUT  64 B/ms  16 kHz stereo 16-bit   │
 * │  USBD  (Host PC side):    EP4 ISO IN   64 B/ms  16 kHz stereo 16-bit   │
 * │                           EP5 ISO OUT  64 B/ms  16 kHz stereo 16-bit   │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Bridge mode:
 *   Aligned-rate test (16k -> 16k): direct frame copy.
 *   Mixed-rate mode (16k <-> 48k): 3x up/down sample conversion.
 *
 * Small FIFO jitter buffers are used in both directions so short callback
 * timing jitter does not immediately become audible silence/click noise.
 *****************************************************************************/
#include "include/uac_handler.h"
#include "include/Version_selection.h"

#ifdef ENABLE_UAC

#include "ch32v20x_usbfs_device.h"   /* USBFS_EP5_Buf, USBFS_EP6_Buf, USBFSD */
#include "usb_lib.h"                  /* USBD SetEP helpers                    */
#include "usb_conf.h"                 /* ENDP4_ISO_TXADDR, ENDP5_ISO_RXADDR    */
#include <string.h>

/* ── global UAC state ─────────────────────────────────────────────────────── */
UAC_State_t g_uac_state;

/* Diagnostic mode:
 *   1 = loop Target-side USBFS EP6 OUT -> EP5 IN directly at 48 kHz
 *       (bypasses USBD<->USBFS sample-rate bridge on playback path)
 *   0 = normal bridge mode */
#define UAC_TEST_TARGET_LOOPBACK 0u

#define UAC_H2T_FIFO_FRAMES 8u
#define UAC_T2H_FIFO_FRAMES 16u
#define UAC_H2T_START_LEVEL 3u
#define UAC_T2H_START_LEVEL 6u
#define UAC_H2T_HIGH_WATER  (UAC_H2T_FIFO_FRAMES - 2u)
#define UAC_T2H_HIGH_WATER  (UAC_T2H_FIFO_FRAMES - 2u)

/* ── Host→Target bridge  (USBD EP5 OUT → USBFS EP5 IN) ────────────────────── */
static uint8_t          g_h2t_buf[UAC_H2T_FIFO_FRAMES * UAC_ISO_PACKET_SIZE];
static volatile uint8_t g_h2t_wr = 0;
static volatile uint8_t g_h2t_rd = 0;
static volatile uint8_t g_h2t_started = 0;
static uint8_t          g_h2t_last[UAC_ISO_PACKET_SIZE];
static volatile uint32_t g_h2t_underrun_cnt = 0;
static volatile uint32_t g_h2t_overflow_cnt = 0;
static volatile uint32_t g_h2t_hold_cnt = 0;
static volatile uint32_t g_h2t_drop_cnt = 0;

/* ── Target→Host bridge  (USBFS EP6 OUT → USBD EP4 IN) ───────────────────── */
static uint8_t          g_t2h_buf[UAC_T2H_FIFO_FRAMES * UAC_USBD_ISO_PACKET_SIZE];
static volatile uint8_t g_t2h_wr = 0;
static volatile uint8_t g_t2h_rd = 0;
static volatile uint8_t g_t2h_started = 0;
static uint8_t          g_t2h_last[UAC_USBD_ISO_PACKET_SIZE];
static volatile uint32_t g_t2h_underrun_cnt = 0;
static volatile uint32_t g_t2h_overflow_cnt = 0;
static volatile uint32_t g_t2h_hold_cnt = 0;
static volatile uint32_t g_t2h_drop_cnt = 0;
static volatile uint8_t  g_t2h_miss_streak = 0;

/* ── Silence frames ────────────────────────────────────────────────────────── */
static const uint8_t g_silence_192[UAC_ISO_PACKET_SIZE];
static const uint8_t g_silence_64 [UAC_USBD_ISO_PACKET_SIZE];

#if UAC_TEST_TARGET_LOOPBACK
static uint8_t          g_usbfs_loop_48k[UAC_ISO_PACKET_SIZE];
static volatile uint8_t g_usbfs_loop_valid = 0;
#endif

static uint8_t h2t_level(void)
{
    uint8_t wr = g_h2t_wr;
    uint8_t rd = g_h2t_rd;
    return (uint8_t)((wr + UAC_H2T_FIFO_FRAMES - rd) % UAC_H2T_FIFO_FRAMES);
}

static uint8_t t2h_level(void)
{
    uint8_t wr = g_t2h_wr;
    uint8_t rd = g_t2h_rd;
    return (uint8_t)((wr + UAC_T2H_FIFO_FRAMES - rd) % UAC_T2H_FIFO_FRAMES);
}

static void copy_pad_frame(const uint8_t *src, uint16_t src_len, uint8_t *dst, uint16_t dst_len)
{
    uint16_t n = (src_len < dst_len) ? src_len : dst_len;
    memcpy(dst, src, n);
    if (n < dst_len)
        memset(dst + n, 0, dst_len - n);
}

static void h2t_convert_frame(const uint8_t *src, uint8_t *dst)
{
#if UAC_ISO_PACKET_SIZE == UAC_USBD_ISO_PACKET_SIZE
    memcpy(dst, src, UAC_ISO_PACKET_SIZE);
#else
    uint8_t i;
    for (i = 0; i < (UAC_USBD_ISO_PACKET_SIZE / 4u); i++) {
        const uint8_t *s = src + (i * 4u);
        uint8_t *d = dst + (i * 12u);
        d[ 0] = s[0]; d[ 1] = s[1]; d[ 2] = s[2]; d[ 3] = s[3];
        d[ 4] = s[0]; d[ 5] = s[1]; d[ 6] = s[2]; d[ 7] = s[3];
        d[ 8] = s[0]; d[ 9] = s[1]; d[10] = s[2]; d[11] = s[3];
    }
#endif
}

static void t2h_convert_frame(const uint8_t *src, uint8_t *dst)
{
#if UAC_ISO_PACKET_SIZE == UAC_USBD_ISO_PACKET_SIZE
    memcpy(dst, src, UAC_USBD_ISO_PACKET_SIZE);
#else
    uint8_t i;
    for (i = 0; i < (UAC_USBD_ISO_PACKET_SIZE / 4u); i++) {
        const uint8_t *s = src + (i * 12u);
        uint8_t *d = dst + (i * 4u);

        int16_t l0 = (int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8));
        int16_t r0 = (int16_t)((uint16_t)s[2] | ((uint16_t)s[3] << 8));
        int16_t l1 = (int16_t)((uint16_t)s[4] | ((uint16_t)s[5] << 8));
        int16_t r1 = (int16_t)((uint16_t)s[6] | ((uint16_t)s[7] << 8));
        int16_t l2 = (int16_t)((uint16_t)s[8] | ((uint16_t)s[9] << 8));
        int16_t r2 = (int16_t)((uint16_t)s[10] | ((uint16_t)s[11] << 8));

        int32_t lsum = (int32_t)l0 + (int32_t)l1 + (int32_t)l2;
        int32_t rsum = (int32_t)r0 + (int32_t)r1 + (int32_t)r2;
        int16_t lout = (int16_t)(lsum / 3);
        int16_t rout = (int16_t)(rsum / 3);

        d[0] = (uint8_t)(lout & 0xFF);
        d[1] = (uint8_t)((uint16_t)lout >> 8);
        d[2] = (uint8_t)(rout & 0xFF);
        d[3] = (uint8_t)((uint16_t)rout >> 8);
    }
#endif
}

/*============================================================================
 * UAC_Init
 *===========================================================================*/
void UAC_Init(void)
{
    memset(&g_uac_state, 0, sizeof(g_uac_state));
    /* Zero all bridge buffers so stale data from a previous enumeration
     * cannot be heard as noise before the first real frame arrives.     */
    memset(g_h2t_buf, 0, sizeof(g_h2t_buf));
    memset(g_t2h_buf, 0, sizeof(g_t2h_buf));
    memset(g_h2t_last, 0, sizeof(g_h2t_last));
    memset(g_t2h_last, 0, sizeof(g_t2h_last));
#if UAC_TEST_TARGET_LOOPBACK
    memset(g_usbfs_loop_48k, 0, sizeof(g_usbfs_loop_48k));
    g_usbfs_loop_valid = 0;
#endif
    g_h2t_wr = 0; g_h2t_rd = 0;
    g_t2h_wr = 0; g_t2h_rd = 0;
    g_h2t_started = 0;
    g_t2h_started = 0;
    g_h2t_underrun_cnt = 0;
    g_h2t_overflow_cnt = 0;
    g_h2t_hold_cnt = 0;
    g_h2t_drop_cnt = 0;
    g_t2h_underrun_cnt = 0;
    g_t2h_overflow_cnt = 0;
    g_t2h_hold_cnt = 0;
    g_t2h_drop_cnt = 0;
    g_t2h_miss_streak = 0;
}

/*============================================================================
 * UAC_USBFS_EP5_Preload
 *
 * Called once when IF5 alt-setting 1 is activated, before T_RES_NONE is set.
 * Fills the single USBFS EP5 DMA buffer with silence so the very first SOF
 * sends valid (silent) data instead of garbage.
 *===========================================================================*/
void UAC_USBFS_EP5_Preload(void)
{
    memset(USBFS_EP5_Buf, 0, UAC_ISO_PACKET_SIZE);
}

/*============================================================================
 * UAC_USBFS_EP5_TxDone   (USBFS EP5 ISO IN complete)
 *
 * Hardware just sent one ISO frame to the Target PC (mic IN direction).
 * Source: g_h2t_buf filled by UAC_USBD_EP4_RxDone() from Host PC mic.
 *
 * Single-buffer mode (BUF_MOD=0): DMA register always points at USBFS_EP5_Buf.
 * We prepare the NEXT frame into USBFS_EP5_Buf NOW so it is ready for the next SOF.
 * No ping-pong half tracking needed; no guard needed (one ISO IN callback per SOF).
 *===========================================================================*/
void UAC_USBFS_EP5_TxDone(void)
{
#if UAC_TEST_TARGET_LOOPBACK
    if (g_usbfs_loop_valid) {
        memcpy(USBFS_EP5_Buf, g_usbfs_loop_48k, UAC_ISO_PACKET_SIZE);
    } else {
        memcpy(USBFS_EP5_Buf, g_silence_192, UAC_ISO_PACKET_SIZE);
    }

    USBFSD->UEP5_TX_LEN  = UAC_ISO_PACKET_SIZE;
    USBFSD->UEP5_TX_CTRL = (USBFSD->UEP5_TX_CTRL & ~USBFS_UEP_T_RES_MASK)
                           | USBFS_UEP_T_RES_NONE;
    return;
#endif

    if (!g_h2t_started) {
        if (h2t_level() >= UAC_H2T_START_LEVEL)
            g_h2t_started = 1;
    }

    if (g_h2t_started) {
        uint8_t level = h2t_level();

        /* Too much queued data means producer clock is slightly faster.
         * Drop one old frame occasionally to keep latency bounded. */
        if (level >= UAC_H2T_HIGH_WATER && g_h2t_rd != g_h2t_wr) {
            g_h2t_rd = (uint8_t)((g_h2t_rd + 1u) % UAC_H2T_FIFO_FRAMES);
            g_h2t_drop_cnt++;
            level = h2t_level();
        }

        if (level == 0u) {
            g_h2t_underrun_cnt++;
            memcpy(USBFS_EP5_Buf, g_silence_192, UAC_ISO_PACKET_SIZE);
        } else {
            memcpy(USBFS_EP5_Buf,
                   g_h2t_buf + ((uint16_t)g_h2t_rd * UAC_ISO_PACKET_SIZE),
                   UAC_ISO_PACKET_SIZE);
            memcpy(g_h2t_last, USBFS_EP5_Buf, UAC_ISO_PACKET_SIZE);
            g_h2t_rd = (uint8_t)((g_h2t_rd + 1u) % UAC_H2T_FIFO_FRAMES);
        }
    } else {
        memcpy(USBFS_EP5_Buf, g_silence_192, UAC_ISO_PACKET_SIZE);
    }

    USBFSD->UEP5_TX_LEN  = UAC_ISO_PACKET_SIZE;
    /* Re-arm: ISO IN must use T_RES_NONE (no ACK handshake expected from host). */
    USBFSD->UEP5_TX_CTRL = (USBFSD->UEP5_TX_CTRL & ~USBFS_UEP_T_RES_MASK)
                           | USBFS_UEP_T_RES_NONE;
}

/*============================================================================
 * UAC_USBFS_EP6_RxDone   (USBFS EP6 ISO OUT complete)
 *
 * Hardware just received one frame from the Target PC mic.
 * DMA deposited data into USBFS_EP6_Buf (single buffer, BUF_MOD=0).
 * Convert and publish into g_t2h_buf for USBD EP4 IN.
 *
 * Single-buffer mode: data is always at USBFS_EP6_Buf[0].
 * No ping-pong half tracking is needed.
 *===========================================================================*/
void UAC_USBFS_EP6_RxDone(uint16_t len)
{
    if (len == 0)
        return;

    const uint8_t *src = USBFS_EP6_Buf;   /* single buffer, always offset 0 */

#if UAC_TEST_TARGET_LOOPBACK
    if (len >= UAC_ISO_PACKET_SIZE) {
        memcpy(g_usbfs_loop_48k, src, UAC_ISO_PACKET_SIZE);
    } else {
        memcpy(g_usbfs_loop_48k, src, len);
        memset(g_usbfs_loop_48k + len, 0, UAC_ISO_PACKET_SIZE - len);
    }
    g_usbfs_loop_valid = 1;
#endif

    uint8_t *dst = g_t2h_buf + ((uint16_t)g_t2h_wr * UAC_USBD_ISO_PACKET_SIZE);

    if (len >= UAC_ISO_PACKET_SIZE) {
        t2h_convert_frame(src, dst);
    } else {
        static uint8_t tmp[UAC_ISO_PACKET_SIZE];
        copy_pad_frame(src, len, tmp, UAC_ISO_PACKET_SIZE);
        t2h_convert_frame(tmp, dst);
    }

    {
        uint8_t next = (uint8_t)((g_t2h_wr + 1u) % UAC_T2H_FIFO_FRAMES);
        if (next == g_t2h_rd) {
            /* Full: drop oldest frame to keep stream live. */
            g_t2h_rd = (uint8_t)((g_t2h_rd + 1u) % UAC_T2H_FIFO_FRAMES);
            g_t2h_overflow_cnt++;
        }
        g_t2h_wr = next;
    }
}

/*============================================================================
 * UAC_USBD_EP3_TxDone   (USBD EP4 ISO IN complete)
 *
 * The Host PC just consumed one audio frame (spk IN direction).
 * Source: g_t2h_buf filled by UAC_USBFS_EP6_RxDone() from Target mic.
 * Send UAC_USBD_ISO_PACKET_SIZE (64 B) via PMA.
 *===========================================================================*/
void UAC_USBD_EP3_TxDone(void)
{
    const uint8_t *src;

    if (!g_t2h_started) {
        if (t2h_level() >= UAC_T2H_START_LEVEL)
            g_t2h_started = 1;
    }

    if (g_t2h_started) {
        uint8_t level = t2h_level();

        /* Too much queued data means producer clock is slightly faster.
         * Drop one old frame occasionally to keep latency bounded. */
        if (level >= UAC_T2H_HIGH_WATER && g_t2h_rd != g_t2h_wr) {
            g_t2h_rd = (uint8_t)((g_t2h_rd + 1u) % UAC_T2H_FIFO_FRAMES);
            g_t2h_drop_cnt++;
            level = t2h_level();
        }

        if (level == 0u) {
            g_t2h_underrun_cnt++;
            if (g_t2h_miss_streak == 0u) {
                /* Conceal a one-frame timing miss with the previous frame.
                 * If misses continue, fall back to silence to avoid pumping. */
                src = g_t2h_last;
                g_t2h_hold_cnt++;
            } else {
                src = g_silence_64;
            }
            g_t2h_miss_streak++;
        } else {
            src = g_t2h_buf + ((uint16_t)g_t2h_rd * UAC_USBD_ISO_PACKET_SIZE);
            memcpy(g_t2h_last, src, UAC_USBD_ISO_PACKET_SIZE);
            g_t2h_rd = (uint8_t)((g_t2h_rd + 1u) % UAC_T2H_FIFO_FRAMES);
            g_t2h_miss_streak = 0;
        }
    } else {
        src = g_silence_64;
        g_t2h_miss_streak = 0;
    }

    UserToPMABufferCopy((uint8_t *)src, ENDP4_ISO_TXADDR, UAC_USBD_ISO_PACKET_SIZE);
    SetEPTxCount(ENDP4, UAC_USBD_ISO_PACKET_SIZE);
    /* Re-arm EP4 only while the host has selected alt-setting 1.
     * Without this guard, a stray EP4_IN_Callback at alt=0 (e.g. during
     * enumeration) would unconditionally re-arm EP4, causing a runaway loop
     * that pumps stale PMA data to the host as audible noise. */
    if (g_uac_state.usbd_as_in_alt)
        SetEPTxStatus(ENDP4, EP_TX_VALID);
    else
        SetEPTxStatus(ENDP4, EP_TX_DIS);
}

/*============================================================================
 * UAC_USBD_EP4_RxDone   (USBD EP5 ISO OUT complete)
 *
 * The Host PC just sent one mic frame (Host PC mic OUT direction).
 * PMA holds UAC_USBD_ISO_PACKET_SIZE (64 B) at ENDP5_ISO_RXADDR.
 * Convert and publish into g_h2t_buf for USBFS EP5 IN.
 *
 * For ISO OUT endpoints on CH32V20x USBD (STM32-compatible USB IP):
 *   - The hardware sets EP_CTR_RX after each received ISO packet.
 *   - CTR_LP clears EP_CTR_RX and calls this callback.
 *   - For EP_ISOCHRONOUS type, STAT_RX bits are SW_BUF toggle bits, NOT
 *     NAK/VALID status.  Calling SetEPRxStatus(EP_RX_VALID) after each
 *     packet would TOGGLE the buffer-select bit, making every other packet
 *     land at an uninitialised PMA address → alternating silence/garbage.
 *   - Correct behaviour: do NOT call SetEPRxStatus here.
 *   - RX count is refreshed in EP5_OUT_Callback after each packet so the
 *     next ISO OUT frame always has PMA space, while RX status bits stay
 *     untouched to avoid SW_BUF toggle side effects.
 *===========================================================================*/
void UAC_USBD_EP4_RxDone(uint16_t len)
{
    if (len == 0) {
        /* Zero-length ISO packet: host pausing — don't touch EP status */
        return;
    }

    static uint8_t usbd_tmp[UAC_USBD_ISO_PACKET_SIZE];
    uint16_t copy_len = (len < UAC_USBD_ISO_PACKET_SIZE) ? len : UAC_USBD_ISO_PACKET_SIZE;
    memset(usbd_tmp, 0, UAC_USBD_ISO_PACKET_SIZE);
    PMAToUserBufferCopy(usbd_tmp, ENDP5_ISO_RXADDR, copy_len);

    uint8_t *dst = g_h2t_buf + ((uint16_t)g_h2t_wr * UAC_ISO_PACKET_SIZE);

    h2t_convert_frame(usbd_tmp, dst);

    {
        uint8_t next = (uint8_t)((g_h2t_wr + 1u) % UAC_H2T_FIFO_FRAMES);
        if (next == g_h2t_rd) {
            /* Full: drop oldest frame to keep stream live. */
            g_h2t_rd = (uint8_t)((g_h2t_rd + 1u) % UAC_H2T_FIFO_FRAMES);
            g_h2t_overflow_cnt++;
        }
        g_h2t_wr = next;
    }

    /* Do NOT call SetEPRxStatus(ENDP5, EP_RX_VALID) here!
     * On EP_ISOCHRONOUS endpoints the STAT_RX field is a SW_BUF toggle,
     * not a NAK/VALID control. */
}

#endif /* ENABLE_UAC */

