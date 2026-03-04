/******************************************************************************
 * @file    uac_handler.h
 * @brief   USB Audio Class 1.0 handler for CH32V208 dual-port audio bridge.
 *
 * Architecture:
 *   USBFS side (Target PC):  EP5 ISO IN  (Target mic → Host PC spk)
 *                            EP6 ISO OUT (Host PC mic → Target spk)
 *   USBD side (Host PC):     EP4 ISO IN  (Host PC mic → Target spk)
 *                            EP5 ISO OUT (Target PC mic → Host spk)
 *
 * Data flow:
 *   Host PC mic → Target spk:  USBFS EP6 OUT → g_uac_h2t_buf → USBD EP4 IN
 *   Target mic → Host PC spk:  USBD EP5 OUT  → g_uac_t2h_buf → USBFS EP5 IN
 *****************************************************************************/
#ifndef __UAC_HANDLER_H
#define __UAC_HANDLER_H

#include "uac_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise UAC state and clear all audio buffers.
 *         Call once at startup, after USB peripheral init.
 */
void UAC_Init(void);

/* ── USBFS-side callbacks (called from USBFS_IRQHandler) ─────────────────── */

/**
 * @brief  Called when USBFS EP5 ISO IN transfer completes.
 *         Loads the next Target→Host audio frame into the hardware buffer.
 */
void UAC_USBFS_EP5_TxDone(void);

/**
 * @brief  Called when USBFS EP6 ISO OUT transfer completes.
 * @param  len  Number of bytes received from the Host PC.
 */
void UAC_USBFS_EP6_RxDone(uint16_t len);

/* ── USBD-side callbacks (called from usb_endp.c CTR handlers) ───────────── */

/**
 * @brief  Called when USBD EP4 ISO IN transfer completes.
 *         Loads the next Host→Target audio frame for the Target PC.
 */
void UAC_USBD_EP3_TxDone(void);

/**
 * @brief  Called when USBD EP5 ISO OUT transfer completes.
 * @param  len  Number of bytes received from the Target PC.
 */
void UAC_USBD_EP4_RxDone(uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __UAC_HANDLER_H */
