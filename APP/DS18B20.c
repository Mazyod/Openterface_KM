#include "ds18b20.h"
#include "ch32v20x.h"        // 根据你的 MCU 头文件
#include <stdio.h>
#include "usbd_composite_km.h"
#include "usb_lib.h"
#define ONEWIRE_PORT    GPIOA
#define ONEWIRE_PIN     GPIO_Pin_2

extern uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len);
#define USB_TX_ENDP     ENDP3

// 初始化 GPIO 等
void DS18B20_Init(void)
{
    // 开启 GPIOA 时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // 配置引脚为输入上拉（初态）
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = ONEWIRE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ONEWIRE_PORT, &GPIO_InitStructure);
}

// 以下为 OneWire 驱动函数（基于你之前代码，改为适配 BRR/BCR、INDR）
static void OW_PinOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = ONEWIRE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;  // 开漏输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ONEWIRE_PORT, &GPIO_InitStructure);
}

static void OW_PinInput(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = ONEWIRE_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;  // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ONEWIRE_PORT, &GPIO_InitStructure);
}

static void OW_WriteLow(void)
{
    OW_PinOutput();
    ONEWIRE_PORT->BCR = ONEWIRE_PIN;  // 清位＝拉低
}

static void OW_Release(void)
{
    OW_PinInput();  // 切换至输入，由上拉电阻拉高
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

static void OW_WriteBit(uint8_t bit)
{
    if(bit)
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

static uint8_t OW_ReadByte(uint8_t *byte)
{
    uint8_t i;
    uint8_t result = 0;
    for(i = 0; i < 8; i++)
    {
        if(OW_ReadBit())
        {
            result |= (1 << i);
        }
    }
    *byte = result;
    return 0;
}

static uint8_t OW_WriteByte(uint8_t byte)
{
    uint8_t i;
    for(i = 0; i < 8; i++)
    {
        OW_WriteBit((byte >> i) & 0x01);
    }
    return 0;
}

// 启动转换
uint8_t DS18B20_StartConversion(void)
{
    if(!OW_Reset()) return DS18B20_ERROR_BUS;
    OW_WriteByte(0xCC);  // Skip ROM
    OW_WriteByte(0x44);  // Convert T
    return DS18B20_OK;
}

// 读取 raw 值
uint8_t DS18B20_ReadRaw(int16_t *rawValue)
{
    uint8_t low, high;
    if(!OW_Reset()) return DS18B20_ERROR_BUS;
    OW_WriteByte(0xCC);  // Skip ROM
    OW_WriteByte(0xBE);  // Read Scratchpad

    OW_ReadByte(&low);
    OW_ReadByte(&high);

    *rawValue = ((int16_t)high << 8) | low;

    // 常见错误值：全 1 或全 0
    if((*rawValue == (int16_t)0xFFFF) || (*rawValue == 0))
    {
        return DS18B20_ERROR_VALUE;
    }
    return DS18B20_OK;
}

// 读取温度（°C）
uint8_t DS18B20_ReadTemperature(float *tempC)
{
    int16_t raw;
    uint8_t err = DS18B20_ReadRaw(&raw);
    if(err != DS18B20_OK) return err;

    *tempC = raw * 0.0625f;  // 12 位分辨率时 LSB=0.0625°C
    return DS18B20_OK;
}
int16_t DS18B20_TempToHex(float tempC)
{
    int16_t raw = (int16_t)(tempC * 16.0f);
    return raw;
}

/**
 * @brief 打印温度与对应 16 进制值（调试用）
 * @param tempC 浮点温度
 */
void DS18B20_PrintHex(float tempC)
{
    int16_t raw = DS18B20_TempToHex(tempC);
    printf("Temperature: %.4f °C -> 0x%04X\r\n", tempC, (uint16_t)raw);
}

void DS18B20_TempToBytes(float tempC, uint8_t *out)
{
    int16_t raw = DS18B20_TempToHex(tempC);
    out[0] = (uint8_t)(raw & 0xFF);        // 低字节
    out[1] = (uint8_t)((raw >> 8) & 0xFF); // 高字节
}
/* ===================== DS18B20 命令处理 ===================== */
void DS18B20_Command(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len)
{
    float tempC;
    int16_t raw;
    uint8_t txbuf[11];  // 11 字节完整帧

    /* 读取温度（内部会启动转换并等待） */
    if (DS18B20_ReadTemperature(&tempC) != DS18B20_OK)
    {
        // 读取失败 —— 不发数据（或你也可以改为发送错误包）
        return;
    }

    /* 转为 DS18B20 原始 16-bit 格式（×16） */
    raw = (int16_t)(tempC * 16.0f);

    /* 构造完整帧（前 10 字节） */
    txbuf[0] = CH9329_FRAME_HEAD1;   // 0x57
    txbuf[1] = CH9329_FRAME_HEAD2;   // 0xAB
    txbuf[2] = addr;                 // 一般 0x00
    txbuf[3] = cmd_code;             // 0x18
    txbuf[4] = 0x05;                 // data length = 5
    txbuf[5] = 0x00;                 // data[0]
    txbuf[6] = 0x00;                 // data[1]
    txbuf[7] = 0x00;                 // data[2]
    txbuf[8] = (uint8_t)(raw & 0xFF);   // 温度低字节（data[3]）
    txbuf[9] = (uint8_t)((raw >> 8) & 0xFF); // 温度高字节（data[4]）

    /* 计算校验和（前 10 字节累加的低 8 位） */
    {
        uint8_t sum = 0;
        for (uint8_t i = 0; i < 10; i++)
            sum += txbuf[i];
        txbuf[10] = sum;
    }

    /* 通过已有 USB 发送接口发出（与 SD 逻辑不同，这里直接上报完整帧） */
    USBD_ENDPx_DataUp(ENDP3, txbuf, 11);
}
