#include "sd_switch.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "usbd_composite_km.h"

uint8_t sd_card_channel_state = 0;   // 0 = TARGET, 1 = HOST

void SD_Switch_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA7 output (selector) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA4 detection input, pull-down */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;   // Input pull-down mode
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_RESET); // 默认状态
}

void TARGET_SD_Switch(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_SET); // 指向 TARGET
    sd_card_channel_state = 0;                 // TARGET
}

void HOST_SD_Switch(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_RESET); // 指向 HOST
    sd_card_channel_state = 1;                   // HOST
}

void SD_Switch_StateMachine(void)
{
    uint8_t pa4_state = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_4);

    if(pa4_state == Bit_SET)  // PA4 高 → TARGET
    {
        if(sd_card_channel_state != 0)
            TARGET_SD_Switch();
    }
    else  // PA4 低 → HOST
    {
        if(sd_card_channel_state != 1)
            HOST_SD_Switch();
    }
}

void SD_USB_Switch(uint8_t addr, uint8_t cmd_code, uint8_t *pdata, uint8_t data_len)
{
    if (data_len < 5)
    {
        uint8_t st = STATUS_ERR_FRAME;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
        return;
    }

    uint8_t op = pdata[4];  // Control bit
    uint8_t st;

    switch (op)
    {
        case 0x00:  // Switch to HOST
            HOST_SD_Switch();
            st = 0x00;
            CH9329_SendResponse(addr, cmd_code, &st, 1);
            break;

        case 0x01:  // Switch to TARGET
            TARGET_SD_Switch();
            st = 0x01;
            CH9329_SendResponse(addr, cmd_code, &st, 1);
            break;

        case 0x03:  // Query current status
            st = (sd_card_channel_state == 0) ? 0x01 : 0x00;
            CH9329_SendResponse(addr, cmd_code, &st, 1);
            break;

        default:
            st = STATUS_ERR_PARAM;
            CH9329_SendResponse(addr, cmd_code,&st, 0);
            break;
    }
}
