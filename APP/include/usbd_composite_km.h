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

/*******************************************************************************/
/* Global Variable Declaration */
extern volatile uint8_t  KB_LED_Last_Status;
extern volatile uint8_t  KB_LED_Cur_Status;


/*******************************************************************************/
/* Function Declaration */
extern void USB_Sleep_Wakeup_CFG( void );
extern void MCU_Sleep_Wakeup_Operate( void );




// º¯ÊýÉùÃ÷
extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);
extern void ProcessSpecialKeys(uint8_t *keys, uint8_t keyCount);
extern uint8_t MapSpecialKeyToModBit(uint8_t key);
extern void CH9329_DataParser(uint8_t* buf, uint16_t len);
extern void USB_DataRx_To_KMHandle(void);
#endif
