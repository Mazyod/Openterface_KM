/******************************************************************************
 * @file    uac_handler.c
 * @brief   USB Audio Class 1.0 handler for CH32V208 dual-port audio bridge.
 *
 * Audio data flow:
 *
 *  Host PC mic  ──USBFS EP6 OUT──► g_uac_h2t_buf ──USBD EP4 IN──► Target PC spk
 *  Target PC mic ─USBD EP5 OUT──► g_uac_t2h_buf  ──USBFS EP5 IN──► Host PC spk
 *
 * Each bridge buffer has two halves (ping-pong):
 *   half 0 → bytes [0 .. SIZE-1]
 *   half 1 → bytes [SIZE .. 2*SIZE-1]
 *
 * Synchronisation: "latest frame" model.
 *   - Writer always fills  (wr_idx ^ 1) = the OTHER half, then flips wr_idx
 *     to point to the half it just filled, and sets the ready flag.
 *   - Reader always reads  wr_idx (the half the writer last finished).
 *
 * So after a write: wr_idx = just-filled half.
 *                   wr_idx ^ 1 = the half the writer will fill NEXT.
 *****************************************************************************/
#include "include/uac_handler.h"
#include "ch32v20x_usbfs_device.h"   /* USBFS_EP5_Buf, USBFS_EP6_Buf, USBFSD */
#include "usb_lib.h"                  /* USBD SetEP helpers                    */
#include "usb_conf.h"                 /* ENDP4_ISO_TXADDR, ENDP5_ISO_RXADDR    */
#include <string.h>

/* ── global state ──────────────────────────────────────────────────────────── */
UAC_State_t g_uac_state;

/* ── Host→Target bridge  (USBFS EP6 RX  →  USBD EP4 TX) ────────────────────
 * g_uac_h2t_wr_idx: the half that was LAST written (0 or 1).
 *   After each write, wr_idx = just-filled slot.
 *   Reader always reads wr_idx.  Writer always writes wr_idx ^ 1.
 * g_uac_h2t_ready : non-zero when at least one fresh frame is available.    */
static uint8_t  g_uac_h2t_buf[UAC_ISO_PACKET_SIZE * 2];
static volatile uint8_t  g_uac_h2t_wr_idx = 0;
static volatile uint8_t  g_uac_h2t_ready  = 0;

/* ── Target→Host bridge  (USBD EP5 RX  →  USBFS EP5 TX) ────────────────────
 * Same indexing convention: wr_idx = last-written half, ready = data available. */
static uint8_t  g_uac_t2h_buf[UAC_ISO_PACKET_SIZE * 2];
static volatile uint8_t  g_uac_t2h_wr_idx = 0;
static volatile uint8_t  g_uac_t2h_ready  = 0;

/*============================================================================
 * UAC_Init
 *===========================================================================*/
void UAC_Init(void)
{
    memset(&g_uac_state,   0, sizeof(g_uac_state));
    memset(g_uac_h2t_buf,  0, sizeof(g_uac_h2t_buf));
    memset(g_uac_t2h_buf,  0, sizeof(g_uac_t2h_buf));
    g_uac_h2t_wr_idx = 0;
    g_uac_h2t_ready  = 0;
    g_uac_t2h_wr_idx = 0;
    g_uac_t2h_ready  = 0;
}

/*============================================================================
 * USBFS EP5 ISO IN complete  (Target PC mic → Host PC spk)
 *
 * Called from USBFS_IRQHandler after the hardware sent a frame to Host PC.
 * We now fill the NEXT half (T_TOG already flipped → points to next half)
 * and update TX_LEN so hardware picks it up on the next SOF.
 *
 * WCH USBFS double TX buffer:
 *   T_TOG=0 → hardware will send from half 0  (offset 0)
 *   T_TOG=1 → hardware will send from half 1  (offset UAC_ISO_PACKET_SIZE)
 *===========================================================================*/
void UAC_USBFS_EP5_TxDone(void)
{
    /* T_TOG has already been flipped by hardware; it now points to the half
     * that will be sent on the NEXT token.  Write into that half.           */
    uint8_t next_half = (USBFSD->UEP5_TX_CTRL & USBFS_UEP_T_TOG) ? 1u : 0u;
    uint8_t *dst = USBFS_EP5_Buf + ((uint16_t)next_half * UAC_ISO_PACKET_SIZE);

    if (g_uac_t2h_ready)
    {
        /* Read the half that the writer just finished (= current wr_idx) */
        memcpy(dst,
               g_uac_t2h_buf + ((uint16_t)g_uac_t2h_wr_idx * UAC_ISO_PACKET_SIZE),
               UAC_ISO_PACKET_SIZE);
        g_uac_t2h_ready = 0;
    }
    else
    {
        /* Underrun — send silence */
        memset(dst, 0, UAC_ISO_PACKET_SIZE);
    }

    USBFSD->UEP5_TX_LEN = UAC_ISO_PACKET_SIZE;
}

/*============================================================================
 * USBFS EP6 ISO OUT complete  (Host PC mic → Target PC spk)
 *
 * Called from USBFS_IRQHandler after the hardware received a frame.
 * DMA already wrote the data into USBFS_EP6_Buf.
 *
 * WCH USBFS double RX buffer + BUF_MOD:
 *   R_TOG is auto-complemented by hardware AFTER filling a half.
 *   When the interrupt fires, R_TOG already points to the NEXT expected
 *   toggle, so the half that was JUST filled = (1 - current R_TOG bit),
 *   i.e.  R_TOG=1 → filled half 0,  R_TOG=0 → filled half 1.
 *===========================================================================*/
void UAC_USBFS_EP6_RxDone(uint16_t len)
{
    if (len == 0 || g_uac_state.usbfs_as_out_alt == 0)
        return;

    /* Identify which half the hardware JUST filled */
    uint8_t filled_half = (USBFSD->UEP6_RX_CTRL & USBFS_UEP_R_TOG) ? 0u : 1u;
    uint8_t *src = USBFS_EP6_Buf + ((uint16_t)filled_half * UAC_ISO_PACKET_SIZE);

    /* Fill the write slot (= 1 - wr_idx), then flip wr_idx atomically */
    uint8_t write_slot = g_uac_h2t_wr_idx ^ 1u;
    uint16_t copy_len  = (len < UAC_ISO_PACKET_SIZE) ? len : UAC_ISO_PACKET_SIZE;
    memcpy(g_uac_h2t_buf + ((uint16_t)write_slot * UAC_ISO_PACKET_SIZE),
           src, copy_len);

    g_uac_h2t_wr_idx = write_slot;   /* atomic flip: reader now reads (write_slot ^ 1 old) */
    g_uac_h2t_ready  = 1;
}

/*============================================================================
 * USBD EP4 ISO IN complete  (Host PC mic audio → Target PC spk)
 *
 * Called from EP4_IN_Callback().  The Target PC just consumed our frame.
 * We load the next 64-byte frame (16kHz, truncated from 192-byte 48kHz) into
 * the PMA buffer and re-arm EP4.
 *===========================================================================*/
void UAC_USBD_EP3_TxDone(void)
{
    static const uint8_t silence[UAC_USBD_ISO_PACKET_SIZE]; /* BSS zero-init */
    const uint8_t *src;

    if (g_uac_h2t_ready)
    {
        /* Read the half the writer just finished (= current wr_idx) */
        src = g_uac_h2t_buf + ((uint16_t)g_uac_h2t_wr_idx * UAC_ISO_PACKET_SIZE);
        g_uac_h2t_ready = 0;
    }
    else
    {
        src = silence;
    }

    /* Copy only the first UAC_USBD_ISO_PACKET_SIZE (64) bytes.
     * This passes the low-frequency content (16kHz) of the 48kHz stream. */
    UserToPMABufferCopy((uint8_t*)src, ENDP4_ISO_TXADDR, UAC_USBD_ISO_PACKET_SIZE);
    SetEPTxCount(ENDP4, UAC_USBD_ISO_PACKET_SIZE);
    if (g_uac_state.usbd_as_in_alt)
        SetEPTxStatus(ENDP4, EP_TX_VALID);
}

/*============================================================================
 * USBD EP5 ISO OUT complete  (Target PC mic → Host PC spk)
 *
 * Called from EP5_OUT_Callback().  The Host PC sent a 64-byte mic frame.
 * We expand it to 192 bytes (zero-pad) for the USBFS side and signal ready.
 *===========================================================================*/
void UAC_USBD_EP4_RxDone(uint16_t len)
{
    if (len == 0 || g_uac_state.usbd_as_out_alt == 0)
    {
        SetEPRxStatus(ENDP5, EP_RX_VALID);
        return;
    }

    uint16_t copy_len  = (len < UAC_USBD_ISO_PACKET_SIZE) ? len : UAC_USBD_ISO_PACKET_SIZE;

    /* Fill the write slot, zero-pad to full 192 B, then flip wr_idx */
    uint8_t write_slot = g_uac_t2h_wr_idx ^ 1u;
    uint8_t *dst = g_uac_t2h_buf + ((uint16_t)write_slot * UAC_ISO_PACKET_SIZE);
    memset(dst, 0, UAC_ISO_PACKET_SIZE);          /* zero-pad to 192 B */
    PMAToUserBufferCopy(dst, ENDP5_ISO_RXADDR, copy_len);

    g_uac_t2h_wr_idx = write_slot;               /* atomic flip */
    g_uac_t2h_ready  = 1;

    SetEPRxStatus(ENDP5, EP_RX_VALID);
}
