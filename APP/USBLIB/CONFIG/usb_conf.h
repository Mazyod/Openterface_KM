/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_conf.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : This file contains all the functions prototypes for the  
 *                      USB configration firmware library.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#ifndef __USB_CONF_H
#define __USB_CONF_H


#define EP_NUM                          (15)

/* Buffer Description Table */
/* buffer table base address */
/* buffer table base address */
#define BTABLE_ADDRESS      (0x00)

/* EP0  */
/* rx/tx buffer base address */
#define ENDP0_RXADDR        (0x40)
#define ENDP0_TXADDR        (0x80)

/* USBD PMA layout — bidirectional UAC (512 B total):
 *   BTABLE:  0x000~0x03F  (64B)
 *   EP0 RX:  0x040~0x07F  (64B)
 *   EP0 TX:  0x080~0x0BF  (64B)
 *   EP1 TX:  0x0C0~0x0DF  (32B)  CDC interrupt IN
 *   EP2 RX:  0x0E0~0x0FF  (32B)  CDC bulk OUT  [max packet 32B]
 *   EP3 TX:  0x100~0x11F  (32B)  CDC bulk IN   [max packet 32B]
 *   EP4 TX:  0x120~0x15F  (64B)  UAC ISO IN  (MCU→Host, mic path)
 *   EP5 RX:  0x160~0x19F  (64B)  UAC ISO OUT (Host→MCU, spk path)
 *   Total:   0x1A0 = 416B < 512B ✓
 * CDC bulk bandwidth: 32B/packet × 1000 pkt/s = 32 KB/s > 115200bps (11.5 KB/s) ✓ */
#define ENDP1_TXADDR        (0xC0)                          /* 32B, CDC int   */
#define ENDP2_RXADDR        (0xE0)                          /* 32B, CDC bulk  */
#define ENDP3_TXADDR        (0x100)                         /* 32B, CDC bulk  */
#define ENDP4_ISO_TXADDR    (0x120)                         /* 64B, UAC IN    */
#define ENDP5_ISO_RXADDR    (0x160)                         /* 64B, UAC OUT   */

/* When ENABLE_ETH=1, EP6/EP7 carry CDC-ACM serial (separate from ECM on EP1-3).
 *   EP6 TX:  0x1A0 (32B) -- ACM interrupt IN (notifications)
 *   EP7 RX:  0x1C0 (32B) -- ACM bulk OUT (host->device serial data)
 *   EP7 TX:  0x1E0 (32B) -- ACM bulk IN  (device->host serial data)
 *   Total PMA with EP6/EP7: 416+96 = 512B (exactly full)  */
#ifdef ENABLE_ETH
#define ENDP6_TXADDR        (0x1A0)                         /* 32B, ACM INT IN  */
#define ENDP7_RXADDR        (0x1C0)                         /* 32B, ACM bulk OUT */
#define ENDP7_TXADDR        (0x1E0)                         /* 32B, ACM bulk IN  */
#endif /* ENABLE_ETH */

/* Serial data endpoint abstraction:
 *   Without ETH: CDC-ACM occupies EP3 (as before)
 *   With    ETH: CDC-ECM occupies EP3; CDC-ACM serial moves to EP7 */
#ifdef ENABLE_ETH
#  define USBD_SERIAL_ENDP       ENDP7
#  define USBD_SERIAL_BUSY       USBD_Endp7_Busy
#else
#  define USBD_SERIAL_ENDP       ENDP3
#  define USBD_SERIAL_BUSY       USBD_Endp3_Busy
#endif


/* ISTR events */
/* IMR_MSK */
/* mask defining which events has to be handled */
/* by the device application software */
#define IMR_MSK (CNTR_CTRM  | CNTR_WKUPM | CNTR_SUSPM | CNTR_ERRM  | CNTR_SOFM \
                 | CNTR_ESOFM | CNTR_RESETM )

/* #define CTR_CALLBACK */
/* #define DOVR_CALLBACK */
/* #define ERR_CALLBACK */
/* #define WKUP_CALLBACK */
/* #define SUSP_CALLBACK */
/* #define RESET_CALLBAC K*/
/* #define SOF_CALLBACK */
/* #define ESOF_CALLBACK */


/* CTR service routines */
/* associated to defined endpoints */
// #define  EP1_IN_Callback   NOP_Process
#define  EP2_IN_Callback   NOP_Process
// #define  EP3_IN_Callback   NOP_Process
// #define  EP4_IN_Callback   NOP_Process  /* UAC ISO IN handled in uac_handler */
#define  EP5_IN_Callback   NOP_Process
#ifdef ENABLE_ETH
/* EP6 IN (ACM notification sent), EP7 IN (ACM bulk IN done),
 * EP7 OUT (ACM bulk OUT received) are real functions in usb_endp.c. */
#else
#define  EP6_IN_Callback    NOP_Process
#define  EP7_IN_Callback    NOP_Process
#endif /* ENABLE_ETH */

#define  EP1_OUT_Callback   NOP_Process
// #define  EP2_OUT_Callback   NOP_Process
#define  EP3_OUT_Callback   NOP_Process
#define  EP4_OUT_Callback   NOP_Process
// #define  EP5_OUT_Callback   NOP_Process  /* EP5 ISO OUT active -- handled in usb_endp.c */
#define  EP6_OUT_Callback   NOP_Process   /* EP6 has no bulk OUT direction */
#ifdef ENABLE_ETH
/* EP7_OUT handled in usb_endp.c for ACM bulk OUT (host->device) */
#else
#define  EP7_OUT_Callback   NOP_Process
#endif /* ENABLE_ETH */

#endif /* __USB_CONF_H */






