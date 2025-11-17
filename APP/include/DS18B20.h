#ifndef __DS18B20_H
#define __DS18B20_H

#include <stdint.h>

#include "ch32v20x.h"
#include "usbd_composite_km.h"
#include "usb_lib.h"

#define ONEWIRE_PORT    GPIOA
#define ONEWIRE_PIN     GPIO_Pin_2
#define USB_TX_ENDP     ENDP3
#define DS18B20_OK            0x00
#define DS18B20_ERROR_BUS     0xE0  // Or define other error codes
#define DS18B20_ERROR_VALUE   0xE1

extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);
extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);
static void OW_PinOutput(void);
static void OW_PinInput(void);
static void OW_WriteLow(void);
static void OW_Release(void);
static uint8_t OW_Reset(void);
static void OW_WriteBit(uint8_t bit);

static uint8_t OW_ReadBit(void);
static uint8_t OW_WriteByte(uint8_t byte);
static uint8_t OW_ReadByte(uint8_t *byte);
#define DS18B20_CMD_SKIP_ROM       0xCC  // Skip ROM
#define DS18B20_CMD_CONVERT_T      0x44  // Convert T
#define DS18B20_CMD_READ_SCRATCH   0xBE  // Read Scratchpad

uint8_t DS18B20_Init(void);
uint8_t DS18B20_StartConversion(void);
uint8_t DS18B20_ReadRaw(int16_t *rawValue);
uint8_t DS18B20_Command(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len);

#endif /* __DS18B20_H */
