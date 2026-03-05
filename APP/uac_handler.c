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
 * │  USBFS (Target PC side):  EP5 ISO IN  192 B/ms  48 kHz stereo 16-bit   │
 * │                           EP6 ISO OUT 192 B/ms  48 kHz stereo 16-bit   │
 * │  USBD  (Host PC side):    EP4 ISO IN   64 B/ms  16 kHz stereo 16-bit   │
 * │                           EP5 ISO OUT  64 B/ms  16 kHz stereo 16-bit   │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Sample-rate bridge:
 *   USBD → USBFS (h2t):  64-byte (16 samples @ 16 kHz) received on USBD EP5.
 *     Each sample is repeated 3× to produce 48 samples @ 48 kHz → 192 bytes.
 *   USBFS → USBD (t2h):  192-byte (48 samples @ 48 kHz) received on USBFS EP6.
 *     Every 3rd sample is picked to decimate to 16 samples → 64 bytes.
 *
 * Ping-pong buffers: writer fills slot (wr ^ 1), publishes by setting wr to
 * the new slot and ready=1.  Reader always consumes slot wr (latest frame).
 * If no fresh frame is available, silence (zero) is sent instead.
 *****************************************************************************/
#include "include/uac_handler.h"
#include "ch32v20x_usbfs_device.h"   /* USBFS_EP5_Buf, USBFS_EP6_Buf, USBFSD */
#include "usb_lib.h"                  /* USBD SetEP helpers                    */
#include "usb_conf.h"                 /* ENDP4_ISO_TXADDR, ENDP5_ISO_RXADDR    */
#include <string.h>

/* ── global UAC state ─────────────────────────────────────────────────────── */
UAC_State_t g_uac_state;

/* ── Host→Target bridge  (USBD EP5 OUT → USBFS EP5 IN) ──────────────────────
 * Writer (UAC_USBD_EP4_RxDone): stores 64-byte 16kHz frame into one slot.
 * Reader (UAC_USBFS_EP5_TxDone): reads 64-byte slot, up-samples to 192 B.   */
static uint8_t          g_h2t_buf[UAC_USBD_ISO_PACKET_SIZE * 2]; /* 2 × 64 B */
static volatile uint8_t g_h2t_wr  = 0;   /* slot last written by USBD RxDone */
static volatile uint8_t g_h2t_rdy = 0;   /* 1 = at least one fresh frame     */

/* ── Target→Host bridge  (USBFS EP6 OUT → USBD EP4 IN) ─────────────────────
 * Audio received at 48 kHz / 192 B is down-sampled to 16 kHz / 64 B.        */
static uint8_t          g_t2h_buf[UAC_USBD_ISO_PACKET_SIZE * 2]; /* 2 × 64 B */
static volatile uint8_t g_t2h_wr  = 0;
static volatile uint8_t g_t2h_rdy = 0;

/* ── Silence frames ────────────────────────────────────────────────────────── */
static const uint8_t g_silence_192[UAC_ISO_PACKET_SIZE];         /* BSS zero  */
static const uint8_t g_silence_64 [UAC_USBD_ISO_PACKET_SIZE];    /* BSS zero  */

/*============================================================================
 * up_sample_3x  — 16 kHz → 48 kHz linear interpolation
 *
 * Input:  src  16 stereo samples @ 16 kHz  (64 bytes, L16LE/R16LE interleaved)
 * Output: dst  48 stereo samples @ 48 kHz  (192 bytes)
 *
 * For each pair of consecutive input samples A and B, generate three output
 * samples by linear interpolation:
 *   out[0] = A
 *   out[1] = (2A + B) / 3
 *   out[2] = (A + 2B) / 3
 *
 * For the last input sample, the next sample is assumed equal (ZOH boundary).
 * Division by 3 uses the integer approximation (x * 21845) >> 16 which is
 * exact for |x| <= 65535 (i.e. sum of two 16-bit values).
 *===========================================================================*/
static void up_sample_3x(const uint8_t *src, uint8_t *dst)
{
    uint8_t i;
    /* Load all 16 input stereo samples to avoid repeated unaligned reads */
    int16_t sL[16], sR[16];
    for (i = 0; i < 16u; i++) {
        sL[i] = (int16_t)((uint16_t)src[i*4u+0u] | ((uint16_t)src[i*4u+1u] << 8u));
        sR[i] = (int16_t)((uint16_t)src[i*4u+2u] | ((uint16_t)src[i*4u+3u] << 8u));
    }

    for (i = 0; i < 16u; i++) {
        int32_t aL = sL[i],  aR = sR[i];
        /* Look-ahead: last sample holds itself (zero-order hold at boundary) */
        int32_t bL = (i < 15u) ? (int32_t)sL[i+1u] : aL;
        int32_t bR = (i < 15u) ? (int32_t)sR[i+1u] : aR;

        /* out[0] = A */
        int16_t o0L = (int16_t)aL,  o0R = (int16_t)aR;
        /* out[1] = (2A + B) / 3 */
        int16_t o1L = (int16_t)(((2*aL + bL) * 21845L) >> 16);
        int16_t o1R = (int16_t)(((2*aR + bR) * 21845L) >> 16);
        /* out[2] = (A + 2B) / 3 */
        int16_t o2L = (int16_t)(((aL + 2*bL) * 21845L) >> 16);
        int16_t o2R = (int16_t)(((aR + 2*bR) * 21845L) >> 16);

        uint8_t *d = dst + i * 12u;
        d[ 0] = (uint8_t)((uint16_t)o0L);      d[ 1] = (uint8_t)((uint16_t)o0L >> 8u);
        d[ 2] = (uint8_t)((uint16_t)o0R);      d[ 3] = (uint8_t)((uint16_t)o0R >> 8u);
        d[ 4] = (uint8_t)((uint16_t)o1L);      d[ 5] = (uint8_t)((uint16_t)o1L >> 8u);
        d[ 6] = (uint8_t)((uint16_t)o1R);      d[ 7] = (uint8_t)((uint16_t)o1R >> 8u);
        d[ 8] = (uint8_t)((uint16_t)o2L);      d[ 9] = (uint8_t)((uint16_t)o2L >> 8u);
        d[10] = (uint8_t)((uint16_t)o2R);      d[11] = (uint8_t)((uint16_t)o2R >> 8u);
    }
}

/*============================================================================
 * down_sample_3x  — 48 kHz → 16 kHz with 3-tap box filter (anti-alias)
 *
 * Input:  src  48 stereo samples @ 48 kHz  (192 bytes)
 * Output: dst  16 stereo samples @ 16 kHz  (64 bytes)
 *
 * Each output sample = average of 3 consecutive input samples:
 *   out[i] = (in[3i] + in[3i+1] + in[3i+2]) / 3
 *
 * The 3-tap box filter attenuates frequencies above 8 kHz before decimation,
 * preventing high-frequency content from aliasing into the voice band.
 *===========================================================================*/
static void down_sample_3x(const uint8_t *src, uint8_t *dst)
{
    uint8_t i;
    for (i = 0; i < 16u; i++) {
        const uint8_t *s = src + i * 12u;   /* 3 consecutive stereo samples */

        /* Left channel: bytes [0,1], [4,5], [8,9] */
        int32_t a0L = (int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8u));
        int32_t a1L = (int16_t)((uint16_t)s[4] | ((uint16_t)s[5] << 8u));
        int32_t a2L = (int16_t)((uint16_t)s[8] | ((uint16_t)s[9] << 8u));
        /* Right channel: bytes [2,3], [6,7], [10,11] */
        int32_t a0R = (int16_t)((uint16_t)s[2]  | ((uint16_t)s[3]  << 8u));
        int32_t a1R = (int16_t)((uint16_t)s[6]  | ((uint16_t)s[7]  << 8u));
        int32_t a2R = (int16_t)((uint16_t)s[10] | ((uint16_t)s[11] << 8u));

        int16_t outL = (int16_t)(((a0L + a1L + a2L) * 21845L) >> 16);
        int16_t outR = (int16_t)(((a0R + a1R + a2R) * 21845L) >> 16);

        uint8_t *d = dst + i * 4u;
        d[0] = (uint8_t)((uint16_t)outL);      d[1] = (uint8_t)((uint16_t)outL >> 8u);
        d[2] = (uint8_t)((uint16_t)outR);      d[3] = (uint8_t)((uint16_t)outR >> 8u);
    }
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
    g_h2t_wr  = 0;  g_h2t_rdy = 0;
    g_t2h_wr  = 0;  g_t2h_rdy = 0;
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
 * Hardware just sent one 192-byte frame to the Target PC (mic IN direction).
 * Source: g_h2t_buf filled by UAC_USBD_EP4_RxDone() from Host PC mic.
 *
 * Single-buffer mode (BUF_MOD=0): DMA register always points at USBFS_EP5_Buf.
 * We prepare the NEXT frame into USBFS_EP5_Buf NOW so it is ready for the next SOF.
 * No ping-pong half tracking needed; no guard needed (one ISO IN callback per SOF).
 *===========================================================================*/
void UAC_USBFS_EP5_TxDone(void)
{
    if (g_h2t_rdy) {
        up_sample_3x(g_h2t_buf + ((uint16_t)g_h2t_wr * UAC_USBD_ISO_PACKET_SIZE),
                     USBFS_EP5_Buf);
        g_h2t_rdy = 0;
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
 * Hardware just received one 192-byte frame from the Target PC mic.
 * DMA deposited data into USBFS_EP6_Buf (single buffer, BUF_MOD=0).
 * Down-sample 192 B → 64 B and publish into g_t2h_buf for USBD EP4 IN.
 *
 * Single-buffer mode: data is always at USBFS_EP6_Buf[0].
 * No ping-pong half tracking is needed.
 *===========================================================================*/
void UAC_USBFS_EP6_RxDone(uint16_t len)
{
    if (len == 0)
        return;

    uint8_t write_slot = g_t2h_wr ^ 1u;
    uint8_t *dst = g_t2h_buf + ((uint16_t)write_slot * UAC_USBD_ISO_PACKET_SIZE);

    const uint8_t *src = USBFS_EP6_Buf;   /* single buffer, always offset 0 */

    if (len >= UAC_ISO_PACKET_SIZE) {
        down_sample_3x(src, dst);
    } else {
        /* Partial frame: zero-pad before decimating */
        static uint8_t tmp[UAC_ISO_PACKET_SIZE];
        memcpy(tmp, src, len);
        memset(tmp + len, 0, UAC_ISO_PACKET_SIZE - len);
        down_sample_3x(tmp, dst);
    }

    g_t2h_wr  = write_slot;
    g_t2h_rdy = 1;
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

    if (g_t2h_rdy) {
        src = g_t2h_buf + ((uint16_t)g_t2h_wr * UAC_USBD_ISO_PACKET_SIZE);
        g_t2h_rdy = 0;
    } else {
        src = g_silence_64;
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
 * Up-sample 64 B → 192 B and publish into g_h2t_buf for USBFS EP5 IN.
 *
 * For ISO OUT endpoints on CH32V20x USBD (STM32-compatible USB IP):
 *   - The hardware sets EP_CTR_RX after each received ISO packet.
 *   - CTR_LP clears EP_CTR_RX and calls this callback.
 *   - For EP_ISOCHRONOUS type, STAT_RX bits are SW_BUF toggle bits, NOT
 *     NAK/VALID status.  Calling SetEPRxStatus(EP_RX_VALID) after each
 *     packet would TOGGLE the buffer-select bit, making every other packet
 *     land at an uninitialised PMA address → alternating silence/garbage.
 *   - Correct behaviour: do NOT call SetEPRxStatus here.  The ISO OUT
 *     endpoint re-arms automatically; hardware writes the next packet into
 *     ENDP5_ISO_RXADDR (DTOG_RX=0, cleared on each activation) every SOF.
 *   - EP5 is kept VALID at all times; the host only sends ISO OUT packets
 *     when IF4 alt-setting 1 is active, so stray packets at alt=0 are
 *     discarded by the guard below.
 *===========================================================================*/
void UAC_USBD_EP4_RxDone(uint16_t len)
{
    /* Guard: only process audio data while IF4 alt-setting 1 is active.
     * EP5 stays VALID at all times (ISO endpoints must not be NAK'd), so
     * the host may fire a spurious CTR_RX during enumeration; ignore it. */
    if (!g_uac_state.usbd_as_out_alt)
        return;

    if (len == 0) {
        /* Zero-length ISO packet: host pausing — don't touch EP status */
        return;
    }

    /* ISO OUT double-buffer: hardware toggles DTOG_RX after each packet.
     * The packet just received is in the buffer that was being written BEFORE
     * the toggle — i.e. the buffer NOT selected by the current DTOG_RX value.
     *   DTOG_RX now = 1  →  packet landed in buf0 (ENDP5_ISO_RXADDR0)
     *   DTOG_RX now = 0  →  packet landed in buf1 (ENDP5_ISO_RXADDR1)      */
    uint16_t ep_reg = _GetENDPOINT(ENDP5);
    uint16_t rx_addr = (ep_reg & EP_DTOG_RX) ? ENDP5_ISO_RXADDR0
                                              : ENDP5_ISO_RXADDR1;

    static uint8_t usbd_tmp[UAC_USBD_ISO_PACKET_SIZE];
    uint16_t copy_len = (len < UAC_USBD_ISO_PACKET_SIZE) ? len : UAC_USBD_ISO_PACKET_SIZE;
    memset(usbd_tmp, 0, UAC_USBD_ISO_PACKET_SIZE);
    PMAToUserBufferCopy(usbd_tmp, rx_addr, copy_len);

    uint8_t write_slot = g_h2t_wr ^ 1u;
    uint8_t *dst = g_h2t_buf + ((uint16_t)write_slot * UAC_USBD_ISO_PACKET_SIZE);

    up_sample_3x(usbd_tmp, dst);

    g_h2t_wr  = write_slot;
    g_h2t_rdy = 1;

    /* Do NOT call SetEPRxStatus(ENDP5, EP_RX_VALID) here!
     * On EP_ISOCHRONOUS endpoints the STAT_RX field is a SW_BUF toggle,
     * not a NAK/VALID control.  The hardware re-arms ISO OUT automatically. */
}

