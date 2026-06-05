#ifndef ECM_BRIDGE_H
#define ECM_BRIDGE_H

#include <stdint.h>
#include "usb_work_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USB_WORK_MODE_COMPILES_BRIDGE_ECM
void ECM_Bridge_Init(void);
void ECM_Bridge_HostRxDone(const uint8_t *data, uint16_t len);
void ECM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len);
void ECM_Bridge_Poll(void);
void ECM_Bridge_SetBleTxCallback(void (*cb)(void));
void ECM_Bridge_SendRawFrame(const uint8_t *data, uint16_t len);
/* Set when USBFS CDC ECM data interface (IF5, alt=1) is activated. */
extern volatile uint8_t g_ecm_data_if_active;
/* Incremented on each successful EP7 TX. */
extern volatile uint16_t g_ecm_tx_count;
/* Reset in SET_INTERFACE handler so GARP is re-queued on every activation. */
extern volatile uint8_t g_garp_sent;
/* Diagnostic: 0=idle, 1=GARP queued, 2=DataUp called, 3=EP7 busy */
extern volatile uint8_t g_ecm_tx_state;
/* LED override: 0=normal, 1=LED solid ON, 2=LED solid OFF.
   Set by HostRxDone (1 on data received) and handle_arp_frame (1 on ARP detected). */
extern volatile uint8_t g_ecm_led_override;
/* EP6 OUT diagnostic — set in USBFS_IRQHandler, cleared by main loop. */
extern volatile uint8_t  g_usbfs_ep6_int_flag;  /* 1 = EP6 OUT interrupt fired */
extern volatile uint8_t  g_usbfs_ep6_tog_ok;    /* last TOG_OK result */
extern volatile uint16_t g_usbfs_ep6_rx_len;    /* last RX length */
extern volatile uint32_t g_usbfs_ep6_rx_count;  /* total EP6 OUT interrupts (any TOG) */
extern volatile uint16_t g_usbfs_ep7_in_count;  /* total EP7 IN completions */
#else
#define ECM_Bridge_Init()                  ((void)0)
#define ECM_Bridge_HostRxDone(data, len)   ((void)0)
#define ECM_Bridge_TargetRxDone(data, len) ((void)0)
#define ECM_Bridge_Poll()                  ((void)0)
#define ECM_Bridge_SetBleTxCallback(cb)    ((void)0)
#define ECM_Bridge_SendRawFrame(data, len) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ECM_BRIDGE_H */
