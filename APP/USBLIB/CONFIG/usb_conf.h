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

/* USBD PMA layout — bidirectional UAC with double-buffered audio (512 B total):
 *   BTABLE:  0x000~0x03F  (64B)
 *   EP0 RX:  0x040~0x07F  (64B)
 *   EP0 TX:  0x080~0x0BF  (64B)
 *   EP1 TX:  0x0C0~0x0CF  (16B)  CDC interrupt IN
 *   EP5 RX:  0x0D0~0x0DF  (16B)  CDC bulk OUT  [max packet 16B]
 *   EP3 TX:  0x0E0~0x0EF  (16B)  CDC bulk IN   [max packet 16B]
 *   EP4 TX0: 0x100~0x13F  (64B)  UAC ISO IN buffer 0 (MCU→Host, mic path)
 *   EP4 TX1: 0x140~0x17F  (64B)  UAC ISO IN buffer 1
 *   EP2 RX0: 0x180~0x1BF  (64B)  UAC ISO OUT (Host→MCU, spk path)
 *   EP2 RX1: 0x1C0~0x1FF  (64B)  UAC ISO OUT double-buffer
 *   Total:   0x200 = 512B */
#define ENDP1_TXADDR        (0xC0)                          /* 16B, CDC int   */
#define ENDP5_BULK_RXADDR   (0xD0)                          /* 16B, CDC bulk OUT */
#define ENDP3_TXADDR        (0xE0)                          /* 16B, ACM bulk IN  */
#define ENDP4_ISO_TXADDR    (0x100)                         /* 64B, UAC ISO IN buffer 0 */
#define ENDP4_ISO_TXADDR1   (0x140)                         /* 64B, UAC ISO IN buffer 1 */
#define ENDP2_ISO_RXADDR    (0x180)                         /* 64B, UAC ISO OUT buffer 0 */
#define ENDP2_ISO_RXADDR1   (0x1C0)                         /* 64B, UAC ISO OUT buffer 1 */

/* Serial data endpoint for CDC-ACM. */
#define USBD_SERIAL_ENDP       ENDP3
#define USBD_SERIAL_BUSY       USBD_Endp3_Busy


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
#define  EP6_IN_Callback    NOP_Process
#define  EP7_IN_Callback    NOP_Process

#define  EP1_OUT_Callback   NOP_Process
// #define  EP2_OUT_Callback   NOP_Process
#define  EP3_OUT_Callback   NOP_Process
// #define  EP4_OUT_Callback   NOP_Process  /* EP4 ISO OUT handled in usb_endp.c */
// #define  EP5_OUT_Callback   NOP_Process  /* CDC bulk OUT relocated to EP5 */
#define  EP6_OUT_Callback   NOP_Process
#define  EP7_OUT_Callback   NOP_Process

#endif /* __USB_CONF_H */






