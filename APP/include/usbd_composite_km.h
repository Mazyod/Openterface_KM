/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_composite_km.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : USB keyboard and mouse processing.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


#ifndef __USBD_COMPOSITE_KM_H
#define __USBD_COMPOSITE_KM_H

/*******************************************************************************/
/* Header Files */
#include "debug.h"
#include "string.h"
#include "usbd_desc.h"
#include "usb_type.h"  // For bool type definition

/*******************************************************************************/
/* Global Variable Declaration */


/*******************************************************************************/
/* Function Declaration */
extern void USB_Sleep_Wakeup_CFG( void );
extern void MCU_Sleep_Wakeup_Operate( void );
extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);
extern void CH9329_DataParser(uint8_t* buf, uint8_t len);
extern void USB_DataRx_To_KMHandle(void);
#endif
