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

#define STATUS_SUCCESS        0x00
#define STATUS_ERR_TIMEOUT    0xE1
#define STATUS_ERR_HEADER     0xE2
#define STATUS_ERR_CMD        0xE3
#define STATUS_ERR_CHECKSUM   0xE4
#define STATUS_ERR_PARAM      0xE5
#define STATUS_ERR_FRAME      0xE6  // 自定义，帧异常执行失败
/*******************************************************************************/
/* Function Declaration */
extern void USB_Sleep_Wakeup_CFG( void );
extern void MCU_Sleep_Wakeup_Operate( void );
extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);
extern void CH9329_DataParser(uint8_t* buf, uint8_t len);
extern void USB_DataRx_To_KMHandle(void);
extern void CH9329_Cmd_KB_General_Reply(uint8_t addr, uint8_t recv_cmd, uint8_t status);
/* Backward Compatibility Functions for Keyboard */
extern void CH9329_SetAutoReleaseMode(bool sender_handles_release);
extern bool CH9329_GetAutoReleaseMode(void);
extern void CH9329_ResetAutoDetection(void);
extern void KB_SetLEDStatus(uint8_t led_status);
extern uint8_t KB_GetLEDStatus(void);

/* Backward Compatibility Functions for Mouse */
extern void CH9329_HandleRelativeMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);
extern void CH9329_HandleAbsoluteMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

#endif
