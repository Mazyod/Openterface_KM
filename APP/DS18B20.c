#include "DS18B20.h"
#include "ch32v20x.h"
#include "usbd_composite_km.h"
#include "usb_lib.h"


#define ONEWIRE_PORT    GPIOA
#define ONEWIRE_PIN     GPIO_Pin_2
#define USB_TX_ENDP     ENDP3

extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);

// === One-Wire driver functions ===
static void OW_PinOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = ONEWIRE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  // Open-drain output
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ONEWIRE_PORT, &GPIO_InitStructure);
}
static void OW_PinInput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = ONEWIRE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // Pull-up input
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ONEWIRE_PORT, &GPIO_InitStructure);
}

static void OW_WriteLow(void)
{
    OW_PinOutput();
    ONEWIRE_PORT->BCR = ONEWIRE_PIN;  // Pull low
}

static void OW_Release(void)
{
    OW_PinInput();  // Switch to input, pull-up resistor pulls high
}

static uint8_t OW_Reset(void)
{
    uint8_t presence;
    OW_WriteLow();
    Delay_Us(480);
    OW_Release();
    Delay_Us(70);
    presence = ((ONEWIRE_PORT->INDR & ONEWIRE_PIN) ? 0 : 1);
    Delay_Us(410);
    return presence;
}

static void OW_WriteBit(uint8_t bit)
{
    if (bit)
    {
        OW_WriteLow();
        Delay_Us(6);
        OW_Release();
        Delay_Us(64);
    }
    else
    {
        OW_WriteLow();
        Delay_Us(60);
        OW_Release();
        Delay_Us(10);
    }
}

static uint8_t OW_ReadBit(void)
{
    uint8_t bit;
    OW_WriteLow();
    Delay_Us(6);
    OW_Release();
    Delay_Us(9);
    bit = ((ONEWIRE_PORT->INDR & ONEWIRE_PIN) ? 1 : 0);
    Delay_Us(55);
    return bit;
}

static uint8_t OW_WriteByte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        OW_WriteBit((byte >> i) & 0x01);
    }
    return 0;
}

static uint8_t OW_ReadByte(uint8_t *byte)
{
    uint8_t result = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        if (OW_ReadBit())
        {
            result |= (1 << i);
        }
    }
    *byte = result;
    return 0;
}

// === DS18B20 specific commands ===

#define DS18B20_CMD_SKIP_ROM       0xCC  // Skip ROM
#define DS18B20_CMD_CONVERT_T      0x44  // Convert T
#define DS18B20_CMD_READ_SCRATCH   0xBE  // Read Scratchpad

uint8_t DS18B20_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    OW_PinInput();
    return DS18B20_OK;
}

uint8_t DS18B20_StartConversion(void)
{
    if (!OW_Reset()) return DS18B20_ERROR_BUS;
    OW_WriteByte(DS18B20_CMD_SKIP_ROM);
    OW_WriteByte(DS18B20_CMD_CONVERT_T);
    return DS18B20_OK;
}

uint8_t DS18B20_ReadRaw(int16_t *rawValue)
{
    uint8_t low, high;
    if (!OW_Reset()) return DS18B20_ERROR_BUS;
    OW_WriteByte(DS18B20_CMD_SKIP_ROM);
    OW_WriteByte(DS18B20_CMD_READ_SCRATCH);
    OW_ReadByte(&low);
    OW_ReadByte(&high);
    *rawValue = ((int16_t)high << 8) | low;
    if ((*rawValue == (int16_t)0xFFFF) || (*rawValue == 0)) return DS18B20_ERROR_VALUE;
    return DS18B20_OK;
}

