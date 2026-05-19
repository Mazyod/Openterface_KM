/******************************************************************************
 * @file    uac_handler.h
 * @brief   USB Audio Class 1.0 – bidirectional audio bridge, CH32V208.
 *
 * Signal flow (from USB-Host perspective):
 *
 *   Host PC mic  ──USBD EP2 OUT──► g_h2t_buf ──USBFS EP5 IN──► Target spk
 *   Target mic   ─USBFS EP6 OUT──► g_t2h_buf ──USBD EP4 IN──►  Host PC spk
 *
 * Sample-rate bridge:
 *   USBD side: 16 kHz stereo 16-bit → 64 B/ms
 *   USBFS side: 16 kHz stereo 16-bit → 64 B/ms
 *   h2t (USBD→USBFS): direct copy when packet sizes match
 *   t2h (USBFS→USBD): direct copy when packet sizes match
 *****************************************************************************/
#ifndef __UAC_HANDLER_H
#define __UAC_HANDLER_H

#ifdef ENABLE_UAC

#include "uac_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise UAC state and clear all bridge buffers.
 *         Call once at startup, after USB peripheral init.
 */
void UAC_Init(void);

/**
 * @brief  Reset only the host→target FIFO state (wr/rd/started).
 *         Call when USBD AS-OUT interface alt-setting changes.
 */
void UAC_H2T_Reset(void);

/**
 * @brief  Reset only the target→host FIFO state (wr/rd/started).
 *         Call when USBFS AS-IN interface alt-setting changes.
 */
void UAC_T2H_Reset(void);

/* ── USBFS-side callbacks (called from USBFS_IRQHandler) ─────────────────── */

/**
 * @brief  Pre-fills both ping-pong halves of USBFS EP5 DMA buffer with silence.
 *         Call once when IF5 alt-setting 1 is activated, before T_RES_ACK.
 */
void UAC_USBFS_EP5_Preload(void);

/**
 * @brief  Called when USBFS EP5 ISO IN transfer completes.
 *         Copies next h2t frame (Host mic → Target spk) into the DMA buffer
 *         and re-arms T_RES_ACK for the next SOF.
 */
void UAC_USBFS_EP5_TxDone(void);

/**
 * @brief  Called when USBFS EP6 ISO OUT transfer completes.
 * @param  len  Number of bytes received from the Target PC mic.
 *         Converts and stores one frame in t2h bridge buffer.
 */
void UAC_USBFS_EP6_RxDone(uint16_t len);

/* ── USBD-side callbacks (called from usb_endp.c CTR handlers) ───────────── */

/**
 * @brief  Called when USBD EP4 ISO IN transfer completes (EP4_IN_Callback).
 *         Copies next t2h frame (Target mic → Host PC spk) into PMA.
 */
void UAC_USBD_EP3_TxDone(void);

/**
 * @brief  Called when USBD EP2 ISO OUT transfer completes (EP2_OUT_Callback).
 * @param  len  Number of bytes received from the Host PC mic.
 *         Converts and stores one frame in h2t bridge buffer.
 */
void UAC_USBD_EP4_RxDone(uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_UAC */

#endif /* __UAC_HANDLER_H */
