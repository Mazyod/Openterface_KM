/******************************************************************************
 * @file    uac_handler.c
 * @brief   USB Audio Class 1.0 – bidirectional audio bridge, CH32V208.
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Signal flow (from USB-Host perspective)                                │
 * │                                                                         │
 * │  Host PC mic  ──USBD EP2 OUT──► h2t_buf ──USBFS EP5 IN──► Target spk   │
 * │  Target mic   ─USBFS EP6 OUT──► t2h_buf ──USBD EP4 IN──►  Host PC spk  │
 * │                                                                         │
 * │  USBFS (Target PC side):  EP5 ISO IN   64 B/ms  16 kHz stereo 16-bit   │
 * │                           EP6 ISO OUT  64 B/ms  16 kHz stereo 16-bit   │
 * │  USBD  (Host PC side):    EP4 ISO IN   64 B/ms  16 kHz stereo 16-bit   │
 * │                           EP2 ISO OUT  64 B/ms  16 kHz stereo 16-bit   │
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
#include "usb_conf.h"                 /* ENDP4_ISO_TXADDR, ENDP2_ISO_RXADDR    */
#include <string.h>

/* ── global UAC state ─────────────────────────────────────────────────────── */
UAC_State_t g_uac_state;

/* Diagnostic mode:
 *   1 = loop Target-side USBFS EP6 OUT -> EP5 IN directly at 48 kHz
 *       (bypasses USBD<->USBFS sample-rate bridge on playback path)
 *   0 = normal bridge mode */
#define UAC_TEST_TARGET_LOOPBACK 0u

/* Diagnostic: while no USBD EP2 OUT playback frames are arriving, transmit a
 * fallback tone on USBFS EP5 IN. Once H2T frames arrive, transmit the actual
 * bridge stream. Test by playing 1 kHz into USB Serial speaker and recording
 * KeyMod mic:
 *   500 Hz = USBD IF4 alt=1 was not selected.
 *   625 Hz = IF4 is active, but EP2 OUT callback has not fired.
 *   750 Hz = EP2 OUT callback fired, but only zero-length packets arrived.
 *   875 Hz = EP2 OUT callback delivered data but FIFO has not started.
 *   1 kHz  = EP2 OUT frames are arriving and the bridge producer is alive. */
#define UAC_TEST_H2T_RX_FALLBACK_TONE 0u

#define UAC_H2T_FIFO_FRAMES 32u
#define UAC_T2H_FIFO_FRAMES 16u
#define UAC_H2T_START_LEVEL 4u
#define UAC_T2H_START_LEVEL 4u
#define UAC_H2T_HIGH_WATER  (UAC_H2T_FIFO_FRAMES - 1u)
#define UAC_T2H_HIGH_WATER  (UAC_T2H_FIFO_FRAMES - 1u)

/* ── Host→Target bridge  (USBD EP2 OUT → USBFS EP5 IN) ────────────────────── */
static uint8_t          g_h2t_buf[UAC_H2T_FIFO_FRAMES * UAC_ISO_PACKET_SIZE];
static volatile uint8_t g_h2t_wr = 0;
static volatile uint8_t g_h2t_rd = 0;
static volatile uint8_t g_h2t_started = 0;
static uint8_t          g_h2t_last[UAC_ISO_PACKET_SIZE];
static volatile uint32_t g_h2t_underrun_cnt = 0;
static volatile uint32_t g_h2t_overflow_cnt = 0;
static volatile uint32_t g_h2t_drop_cnt = 0;
static volatile uint8_t  g_h2t_ep5_out_seen = 0;
static volatile uint8_t  g_h2t_ep5_zero_seen = 0;
static volatile uint8_t  g_h2t_ep5_data_seen = 0;
static volatile uint8_t  g_h2t_ep2_pong = 0;

/* ── Target→Host bridge  (USBFS EP6 OUT → USBD EP4 IN) ───────────────────── */
/* Buffer size = UAC_T2H_FIFO_FRAMES × UAC_USBD_ISO_PACKET_SIZE (scales with define) */
static uint8_t          g_t2h_buf[UAC_T2H_FIFO_FRAMES * UAC_USBD_ISO_PACKET_SIZE];
static volatile uint8_t g_t2h_wr = 0;
static volatile uint8_t g_t2h_rd = 0;
static volatile uint8_t g_t2h_started = 0;
static uint8_t          g_t2h_last[UAC_USBD_ISO_PACKET_SIZE];
static volatile uint32_t g_t2h_underrun_cnt = 0;
static volatile uint32_t g_t2h_overflow_cnt = 0;
static volatile uint32_t g_t2h_drop_cnt = 0;
static volatile uint8_t  g_t2h_ep4_pong = 0;

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

static void usbd_ep4_write_frame(const uint8_t *src)
{
    uint16_t pma_addr = g_t2h_ep4_pong ? ENDP4_ISO_TXADDR1 : ENDP4_ISO_TXADDR;

    UserToPMABufferCopy((uint8_t *)src, pma_addr, UAC_USBD_ISO_PACKET_SIZE);
    SetEPDblBuffCount(ENDP4, EP_DBUF_IN, UAC_USBD_ISO_PACKET_SIZE);
    if (g_uac_state.usbd_as_in_alt) {
        FreeUserBuffer(ENDP4, EP_DBUF_IN);
        SetEPTxStatus(ENDP4, EP_TX_VALID);
        g_t2h_ep4_pong ^= 1u;
    } else {
        SetEPTxStatus(ENDP4, EP_TX_DIS);
        g_t2h_ep4_pong = 0;
    }
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

#if UAC_TEST_H2T_RX_FALLBACK_TONE
static uint8_t h2t_diag_mode(void)
{
    if (!g_uac_state.usbd_as_out_alt)
        return 0u;
    if (!g_h2t_ep5_out_seen)
        return 1u;
    if (!g_h2t_ep5_data_seen && g_h2t_ep5_zero_seen)
        return 2u;
    return 3u;
}

static void fill_h2t_fallback_tone(uint8_t *dst, uint8_t mode)
{
    static uint8_t phase = 0;
    static const int16_t sine32[32] = {
        0, 6393, 12539, 18204, 23170, 27245, 30273, 32137,
        32767, 32137, 30273, 27245, 23170, 18204, 12539, 6393,
        0, -6393, -12539, -18204, -23170, -27245, -30273, -32137,
        -32767, -32137, -30273, -27245, -23170, -18204, -12539, -6393
    };

    for (uint8_t frame = 0; frame < (UAC_ISO_PACKET_SIZE / 4u); frame++) {
        uint8_t index = phase & 0x1Fu;
        int16_t sample = sine32[index];
        dst[(frame * 4u) + 0u] = (uint8_t)((uint16_t)sample & 0xFFu);
        dst[(frame * 4u) + 1u] = (uint8_t)(((uint16_t)sample >> 8) & 0xFFu);
        dst[(frame * 4u) + 2u] = dst[(frame * 4u) + 0u];
        dst[(frame * 4u) + 3u] = dst[(frame * 4u) + 1u];

        phase = (uint8_t)(phase + 1u);
        if (mode >= 1u && ((frame & 0x03u) == 0u))
            phase = (uint8_t)(phase + 1u);
        if (mode >= 2u && ((frame & 0x01u) == 0u))
            phase = (uint8_t)(phase + 1u);
        if (mode >= 3u && ((frame & 0x03u) != 0u))
            phase = (uint8_t)(phase + 1u);
    }
}
#endif

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
    g_h2t_drop_cnt = 0;
    g_h2t_ep5_out_seen = 0;
    g_h2t_ep5_zero_seen = 0;
    g_h2t_ep5_data_seen = 0;
    g_h2t_ep2_pong = 0;
    g_t2h_underrun_cnt = 0;
    g_t2h_overflow_cnt = 0;
    g_t2h_drop_cnt = 0;
    g_t2h_ep4_pong = 0;
}

/*============================================================================
 * UAC_H2T_Reset  — reset host→target FIFO (call on USBD AS-OUT alt change)
 *===========================================================================*/
void UAC_H2T_Reset(void)
{
    g_h2t_wr = 0; g_h2t_rd = 0;
    g_h2t_started = 0;
    memset(g_h2t_last, 0, sizeof(g_h2t_last));
    g_h2t_underrun_cnt = 0;
    g_h2t_overflow_cnt = 0;
    g_h2t_drop_cnt = 0;
    g_h2t_ep5_out_seen = 0;
    g_h2t_ep5_zero_seen = 0;
    g_h2t_ep5_data_seen = 0;
    g_h2t_ep2_pong = 0;
}

/*============================================================================
 * UAC_T2H_Reset  — reset target→host FIFO (call on USBFS AS-IN alt change)
 *===========================================================================*/
void UAC_T2H_Reset(void)
{
    g_t2h_wr = 0; g_t2h_rd = 0;
    g_t2h_started = 0;
    memset(g_t2h_last, 0, sizeof(g_t2h_last));
    g_t2h_underrun_cnt = 0;
    g_t2h_overflow_cnt = 0;
    g_t2h_drop_cnt = 0;
    g_t2h_ep4_pong = 0;
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
    USBFSD->UEP5_DMA = (uint32_t)USBFS_EP5_Buf;
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

        if (level == 0u) {
            g_h2t_underrun_cnt++;
            memcpy(USBFS_EP5_Buf, g_h2t_last, UAC_ISO_PACKET_SIZE);
        } else {
            memcpy(USBFS_EP5_Buf,
                   g_h2t_buf + ((uint16_t)g_h2t_rd * UAC_ISO_PACKET_SIZE),
                   UAC_ISO_PACKET_SIZE);
            memcpy(g_h2t_last, USBFS_EP5_Buf, UAC_ISO_PACKET_SIZE);
            g_h2t_rd = (uint8_t)((g_h2t_rd + 1u) % UAC_H2T_FIFO_FRAMES);
        }
    } else {
#if UAC_TEST_H2T_RX_FALLBACK_TONE
    fill_h2t_fallback_tone(USBFS_EP5_Buf, h2t_diag_mode());
#else
        memcpy(USBFS_EP5_Buf, g_silence_192, UAC_ISO_PACKET_SIZE);
#endif
    }

    USBFSD->UEP5_TX_LEN  = UAC_ISO_PACKET_SIZE;
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
 * Single-buffer mode: data is always at USBFS_EP6_Buf.
 * No ping-pong half tracking is needed.
 *===========================================================================*/
void UAC_USBFS_EP6_RxDone(uint16_t len)
{
    if (len == 0)
        return;

    const uint8_t *src = USBFS_EP6_Buf;

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
            /* True underrun: repeat last good frame as concealment. */
            g_t2h_underrun_cnt++;
            src = g_t2h_last;
        } else {
            src = g_t2h_buf + ((uint16_t)g_t2h_rd * UAC_USBD_ISO_PACKET_SIZE);
            memcpy(g_t2h_last, src, UAC_USBD_ISO_PACKET_SIZE);
            g_t2h_rd = (uint8_t)((g_t2h_rd + 1u) % UAC_T2H_FIFO_FRAMES);
        }
    } else {
        src = g_silence_64;
    }

    /* Re-arm EP4 only while the host has selected alt-setting 1.
     * Without this guard, a stray EP4_IN_Callback at alt=0 (e.g. during
     * enumeration) would unconditionally re-arm EP4, causing a runaway loop
     * that pumps stale PMA data to the host as audible noise. */
    usbd_ep4_write_frame(src);
}

/*============================================================================
 * UAC_USBD_EP4_RxDone   (USBD EP2 ISO OUT complete)
 *
 * The Host PC just sent one mic frame (Host PC mic OUT direction).
 * PMA holds UAC_USBD_ISO_PACKET_SIZE (64 B) at ENDP2_ISO_RXADDR.
 * Convert and publish into g_h2t_buf for USBFS EP5 IN.
 *
 * For ISO OUT endpoints on CH32V20x USBD (STM32-compatible USB IP):
 *   - The hardware sets EP_CTR_RX after each received ISO packet.
 *   - CTR_LP clears EP_CTR_RX and calls this callback.
 * EP2 is configured as a hardware double-buffered ISO OUT endpoint.  The
 * callback copies whichever PMA slot completed, then FreeUserBuffer() releases
 * that slot back to USB hardware for the next frame. Do not call
 * SetEPRxStatus() here; double-buffered EP2 is re-armed by FreeUserBuffer().
 *===========================================================================*/
void UAC_USBD_EP4_RxDone(uint16_t len)
{
    uint8_t done = g_h2t_ep2_pong;
    uint16_t len0 = GetEPDblBuf0Count(ENDP2);
    uint16_t len1 = GetEPDblBuf1Count(ENDP2);
    uint16_t pma_addr;

    (void)len;
    g_h2t_ep5_out_seen = 1;

    if (done == 0u) {
        len = len0;
        pma_addr = ENDP2_ISO_RXADDR;
    } else {
        len = len1;
        pma_addr = ENDP2_ISO_RXADDR1;
    }

    if (len == 0u) {
        if (done == 0u && len1 != 0u) {
            done = 1u;
            len = len1;
            pma_addr = ENDP2_ISO_RXADDR1;
        } else if (done != 0u && len0 != 0u) {
            done = 0u;
            len = len0;
            pma_addr = ENDP2_ISO_RXADDR;
        }
        g_h2t_ep2_pong = done;
    }

    if (len == 0) {
        g_h2t_ep5_zero_seen = 1;
        if (g_uac_state.usbd_as_out_alt)
            FreeUserBuffer(ENDP2, EP_DBUF_OUT);
        g_h2t_ep2_pong = (uint8_t)(g_h2t_ep2_pong ^ 1u);
        return;
    }

    g_h2t_ep5_data_seen = 1;

    static uint8_t usbd_tmp[UAC_USBD_ISO_PACKET_SIZE];
    uint16_t copy_len = (len < UAC_USBD_ISO_PACKET_SIZE) ? len : UAC_USBD_ISO_PACKET_SIZE;
    memset(usbd_tmp, 0, UAC_USBD_ISO_PACKET_SIZE);
    PMAToUserBufferCopy(usbd_tmp, pma_addr, copy_len);

    if (g_uac_state.usbd_as_out_alt)
        FreeUserBuffer(ENDP2, EP_DBUF_OUT);
    g_h2t_ep2_pong = (uint8_t)(g_h2t_ep2_pong ^ 1u);

    uint8_t *dst = g_h2t_buf + ((uint16_t)g_h2t_wr * UAC_ISO_PACKET_SIZE);
    h2t_convert_frame(usbd_tmp, dst);

    {
        uint8_t next = (uint8_t)((g_h2t_wr + 1u) % UAC_H2T_FIFO_FRAMES);
        if (next == g_h2t_rd) {
            g_h2t_rd = (uint8_t)((g_h2t_rd + 1u) % UAC_H2T_FIFO_FRAMES);
            g_h2t_overflow_cnt++;
        }
        g_h2t_wr = next;
    }

    /* Double-buffered EP2 is re-armed by FreeUserBuffer(). */
}

#endif /* ENABLE_UAC */

