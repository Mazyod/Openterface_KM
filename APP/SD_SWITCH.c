#include "sd_switch.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "usbd_composite_km.h"
uint8_t sd_card_channel_state = 0;   // Default 0 = TARGET

static bool firstDetect = true;

void SD_Switch_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* PA1 output */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA7 output */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA6 detection input, pull-down */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;   // Input pull-down mode
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PB8 selector input, pull-down */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPD;   // Input pull-down mode
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
}

void TARGET_SD_Switch(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
    Delay_Ms(30);
    GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_SET);
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);

    sd_card_channel_state = 0; // TARGET
}

void HOST_SD_Switch(void)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
    Delay_Ms(30);
    GPIO_WriteBit(GPIOA, GPIO_Pin_7, Bit_RESET);
    GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);

    sd_card_channel_state = 1; // HOST
}


void SD_Switch_StateMachine(uint8_t *prev_selector_state_p)
{
    if (firstDetect)
    {
        uint8_t pa6_state = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_6);
        if (pa6_state == Bit_SET)
        {
            TARGET_SD_Switch();
        }
        else
        {
            HOST_SD_Switch();
        }
        firstDetect = false;
    }
    else
    {
        uint8_t curr = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);
        if ((*prev_selector_state_p == 0) && (curr == 1))
        {
            // PB8 rising edge triggers switch
            if (sd_card_channel_state == 0)
            {
                HOST_SD_Switch();
            }
            else
            {
                TARGET_SD_Switch();
            }
        }
        *prev_selector_state_p = curr;
    }
}
void SD_USB_Switch(uint8_t addr, uint8_t cmd_code, uint8_t *pdata, uint8_t data_len)
{
    if (data_len < 5)
    {
        uint8_t st = STATUS_ERR_FRAME;
        CH9329_SendResponse(addr, cmd_code, &st, 1,0);
        return;
    }

    uint8_t op = pdata[4];  // Control bit
    uint8_t resp_data[1];

    switch (op)
    {
        case 0x00:  // Switch to HOST
            HOST_SD_Switch();
            uint8_t st = 0x00;  // Current status: HOST
            
            CH9329_SendResponse(addr, cmd_code, &st, 1 ,1);
            break;

        case 0x01:  // Switch to TARGET
            TARGET_SD_Switch();
            st = 0x01;  // Current status: TARGET
            CH9329_SendResponse(addr, cmd_code, &st, 1,1);
            break;

        case 0x03:  // Query current status
            st = (sd_card_channel_state == 0) ? 0x01 : 0x00;
            CH9329_SendResponse(addr, cmd_code, &st, 1,1);
            break;

        default:
            st = STATUS_ERR_PARAM;
            CH9329_SendResponse(addr, cmd_code,&st, 0,0);
            break;
    }
}
