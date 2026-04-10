/********************************** (C) COPYRIGHT *******************************
 * File Name          : eth_handler.h
 * Description        : CDC-ECM USB Ethernet transport layer.
 *                      Active when ENABLE_ETH=1.
 *
 * Runs on the CH32V208 legacy USB Device controller (0x40005C00, IRQ 36).
 * That controller is on a SEPARATE physical USB port from the HID USBFS OTG
 * controller -- so BLE and UAC remain fully independent; no forced overrides.
 *
 *  Endpoint mapping (USBD legacy controller, ETH build):
 *    EP1 IN  interrupt (0x81) -- CDC NETWORK_CONNECTION notifications  (8 B)
 *    EP2 OUT bulk      (0x02) -- Ethernet frames: host -> device        (32 B)
 *    EP3 IN  bulk      (0x83) -- Ethernet frames: device -> host        (32 B)
 *******************************************************************************/

#ifndef ETH_HANDLER_H
#define ETH_HANDLER_H

#ifdef ENABLE_ETH

#include <stdint.h>

/* Maximum Ethernet frame size (14-byte header + 1500-byte payload, no FCS) */
#define ETH_MAX_FRAME_SIZE   1514

/* USBD legacy controller bulk max packet size (constrained by PMA layout) */
#define ETH_USB_BULK_PKT     32

/* Current alternate-setting of CDC-ECM Data interface IF1 (0=idle, 1=active).
 * Updated by SET_INTERFACE handler; read by GET_INTERFACE / usb_prop.c. */
extern uint8_t g_eth_data_alt;

/* -- Lifecycle ------------------------------------------------------------ */
/* Initialise handler state. Call once after USB init in peripheral_main.c.  */
void ETH_Init(void);

/* Send a CDC NETWORK_CONNECTION notification on EP1.
 * connected=1 => link-up notification; connected=0 => link-down. */
void ETH_SendNotification(uint8_t connected);

/* -- Called from usb_endp.c USB callbacks (ISR context) ------------------- */
void ETH_USBD_EP1_TxDone(void);           /* EP1 interrupt IN: notification sent   */
void ETH_USBD_EP2_RxDone(uint16_t len);  /* EP2 bulk OUT:     data from host       */
void ETH_USBD_EP3_TxDone(void);          /* EP3 bulk IN:      chunk sent to host   */

/* -- Transmit ------------------------------------------------------------- */
/* Queue an Ethernet frame for transmission over EP3.
 * Returns 1 if accepted, 0 if EP3 busy or interface not active (alt=0). */
uint8_t ETH_TxFrame(const uint8_t *buf, uint16_t len);

/* -- Receive callback (override in application) --------------------------- */
/* Called from ISR when a complete Ethernet frame arrives on EP2.
 * Override this weak function to process received frames. */
__attribute__((weak)) void ETH_OnRxFrame(const uint8_t *buf, uint16_t len);

#endif /* ENABLE_ETH */

#endif /* ETH_HANDLER_H */
