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
#include <stdbool.h>
/*******************************************************************************/
/* Global Variable Declaration */

#define STATUS_SUCCESS        0x00
#define STATUS_ERR_TIMEOUT    0xE1
#define STATUS_ERR_HEADER     0xE2
#define STATUS_ERR_CMD        0xE3
#define STATUS_ERR_CHECKSUM   0xE4
#define STATUS_ERR_PARAM      0xE5
#define STATUS_ERR_FRAME      0xE6  // Custom frame format error, frame exception, execution failed
/*******************************************************************************/

// -------------------- Protocol Definitions --------------------
#define CH9329_MAX_DATA_LEN 100
#define CH9329_FRAME_HEAD1      0x57
#define CH9329_FRAME_HEAD2      0xAB
#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_KB_GAME_DATA    0x12
#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05
#define CMD_GET_PARA_CFG     0x08
#define CMD_SD_SWITCH  0x17

// ACK Status Codes
#define STATUS_SUCCESS        0x00
#define STATUS_ERR_TIMEOUT    0xE1
#define STATUS_ERR_HEADER     0xE2
#define STATUS_ERR_CMD        0xE3
#define STATUS_ERR_CHECKSUM   0xE4
#define STATUS_ERR_PARAM      0xE5
#define STATUS_ERR_FRAME      0xE6  // Custom frame format error, frame exception, execution failed
#define CMD_GET_INFO  0x01
#define STATUS_OK     0    

// Custom DS18B20 Command
#define CMD_DS18B20_GET_TEMP   0x18

#define RX_BUF_SIZE   256

/*******************************************************************************/
void USB_Sleep_Wakeup_CFG( void );
void MCU_Sleep_Wakeup_Operate( void );
void CH9329_SendResponse(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len, uint8_t resp_mode);
void CH9329_Cmd_GetInfo_Reply(uint8_t addr);
void CH9329_DataParser(uint8_t* buf, uint8_t len);
void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) ;
void USB_DataRx_To_KMHandle(void) ;
void CH9329_SetAutoReleaseMode(bool sender_handles_release) ;
bool CH9329_GetAutoReleaseMode(void) ;
void CH9329_ResetAutoDetection(void) ;
void KB_SetLEDStatus(uint8_t led_status) ;
uint8_t KB_GetLEDStatus(void) ;
void CH9329_HandleRelativeMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);
void CH9329_HandleAbsoluteMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) ;

#endif
