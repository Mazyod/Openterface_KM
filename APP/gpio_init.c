/*
 *@Note
 *GPIO routine:
 *PA0 push-pull output.
 *
 */

#include "debug.h"
/*********************************************************************
 * @fn      GPIO_Toggle_INIT
 *
 * @brief   Initializes GPIOA.0
 *
 * @return  none
 */
void GPIO_Toggle_INIT(void)
{
    u8 prev_state = 1;
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 输入上拉模式
    GPIO_Init(GPIOB, &GPIO_InitStructure);

}


void SD_SW(uint8_t *prev_state_p)
{
    uint8_t curr_state;

    curr_state = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);

    if ((*prev_state_p == 0) && (curr_state == 1))
    {
        // 上升沿触发逻辑
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        Delay_Ms(30);
        GPIO_WriteBit(GPIOA, GPIO_Pin_7,
            (BitAction)(1u - GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_7)));
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
    }

    *prev_state_p = curr_state;
}