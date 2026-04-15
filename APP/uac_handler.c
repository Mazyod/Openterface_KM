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
 *     Each 3-sample group is averaged to decimate to 16 samples → 64 bytes.
 *
 * Ping-pong buffers: writer fills slot (wr ^ 1), publishes by setting wr to
 * the new slot and ready=1.  Reader always consumes slot wr (latest frame).
 * If no fresh frame is available, silence (zero) is sent instead.
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

/* ── Host→Target bridge  (USBD EP5 OUT → USBFS EP5 IN) ──────────────────────
 * Audio received at 16 kHz / 64 B is up-sampled to 48 kHz / 192 B.          */
static uint8_t          g_h2t_buf[UAC_ISO_PACKET_SIZE * 2];  /* 2 × 192 B    */
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
 * up_sample_3x
 *   Expand src (n_src stereo 16-bit samples, 4 bytes each) to dst by
 *   repeating every sample 3 times.
 *   n_src must equal UAC_USBD_ISO_PACKET_SIZE / 4 = 16.
 *   dst receives UAC_ISO_PACKET_SIZE = 192 bytes.
 *===========================================================================*/
static void up_sample_3x(const uint8_t *src, uint8_t *dst)
{
    uint8_t i;
    for (i = 0; i < (UAC_USBD_ISO_PACKET_SIZE / 4u); i++) {
        /* One stereo sample = 4 bytes (L_lo, L_hi, R_lo, R_hi) */
        const uint8_t *s = src + (i * 4u);
        uint8_t *d = dst + (i * 12u);       /* 3 × 4 bytes */
        /* repeat 3× */
        d[ 0] = s[0]; d[ 1] = s[1]; d[ 2] = s[2]; d[ 3] = s[3];
        d[ 4] = s[0]; d[ 5] = s[1]; d[ 6] = s[2]; d[ 7] = s[3];
        d[ 8] = s[0]; d[ 9] = s[1]; d[10] = s[2]; d[11] = s[3];
    }
}

/*============================================================================
 * down_sample_3x
 *   Decimate src (48 stereo 16-bit samples, 192 bytes) to dst by averaging
 *   each 3-sample group per channel, producing 16 stereo samples = 64 bytes.
 *
 *   Keeping only every 3rd sample causes strong aliasing when converting
 *   48 kHz -> 16 kHz and is heard as noise on the recording path.
 *===========================================================================*/
static void down_sample_3x(const uint8_t *src, uint8_t *dst)
{
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
        memcpy(USBFS_EP5_Buf,
               g_h2t_buf + ((uint16_t)g_h2t_wr * UAC_ISO_PACKET_SIZE),
               UAC_ISO_PACKET_SIZE);
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

    const uint8_t *src = USBFS_EP6_Buf;   /* single buffer, always offset 0 */

    uint8_t write_slot = g_t2h_wr ^ 1u;
    uint8_t *dst = g_t2h_buf + ((uint16_t)write_slot * UAC_USBD_ISO_PACKET_SIZE);

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
 *     ENDP5_ISO_RXADDR (DTOG_RX=0, cleared in USBD_Reset) every SOF.
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

    uint8_t write_slot = g_h2t_wr ^ 1u;
    uint8_t *dst = g_h2t_buf + ((uint16_t)write_slot * UAC_ISO_PACKET_SIZE);

    up_sample_3x(usbd_tmp, dst);

    g_h2t_wr  = write_slot;
    g_h2t_rdy = 1;

    /* Do NOT call SetEPRxStatus(ENDP5, EP_RX_VALID) here!
     * On EP_ISOCHRONOUS endpoints the STAT_RX field is a SW_BUF toggle,
     * not a NAK/VALID control.  The hardware re-arms ISO OUT automatically. */
}

#endif /* ENABLE_UAC */

