/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32v20x_it.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/06/16
 * Description        : Main Interrupt Service Routines.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "ch32v20x_it.h"
#ifdef ENABLE_BLE
#include "config.h"
#endif

/*********************************************************************
 * LOCAL FUNCTIONS
 */
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#ifdef ENABLE_BLE
void BB_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#endif

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  None
 */
void NMI_Handler(void)
{
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  None
 */
void HardFault_Handler(void)
{
#ifdef ENABLE_ETH
    /* Debug: rapid-blink PC6 (red LED) so the operator can tell
     * a HardFault occurred rather than a silent reset loop.       */
    RCC->APB2PCENR |= (1 << 4);              /* RCC_APB2Periph_GPIOC */
    GPIOC->CFGLR = (GPIOC->CFGLR & ~(0xFu << 24)) | (0x1u << 24); /* PC6 out 10 MHz PP */
    while (1) {
        GPIOC->BSHR = (1 << 6);              /* PC6 high */
        for (volatile uint32_t i = 0; i < 80000; i++);
        GPIOC->BCR  = (1 << 6);              /* PC6 low  */
        for (volatile uint32_t i = 0; i < 80000; i++);
    }
#else
    NVIC_SystemReset();
    while(1)
    {
    }
#endif
}

#ifdef ENABLE_BLE
void BB_IRQHandler(void)
{
    BB_IRQLibHandler();
}
#endif /* ENABLE_BLE */
