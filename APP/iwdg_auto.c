#include "iwdg_auto.h"

#define IWDG_GPIO_PORT GPIOA
#define IWDG_GPIO_PIN  GPIO_Pin_0

static uint32_t last_toggle_time = 0;
extern volatile uint32_t systick_ms;  // Global millisecond tick counter

/**
 * @brief  Initialize IWDG and the watchdog pin
 * @param  prescaler: IWDG prescaler value
 * @param  reload: IWDG reload value
 */
void IWDG_Auto_Init(uint16_t prescaler, uint16_t reload)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    // Enable GPIOA clock
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    // Configure PA0 as push-pull output
    GPIO_InitStructure.GPIO_Pin = IWDG_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(IWDG_GPIO_PORT, &GPIO_InitStructure);

    // Set initial output low
    GPIO_ResetBits(IWDG_GPIO_PORT, IWDG_GPIO_PIN);

    // Initialize IWDG
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(prescaler);
    IWDG_SetReload(reload);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/**
 * @brief  Call this function in the main loop to toggle the watchdog pin
 *         and feed the IWDG automatically.
 *         The pin toggles every 500ms.
 */
void IWDG_Auto_Handler(void)
{
    if(systick_ms - last_toggle_time >= 500) // Toggle every 500ms
    {
        // Toggle PA0 output level
        if(GPIO_ReadOutputDataBit(IWDG_GPIO_PORT, IWDG_GPIO_PIN))
            GPIO_ResetBits(IWDG_GPIO_PORT, IWDG_GPIO_PIN);
        else
            GPIO_SetBits(IWDG_GPIO_PORT, IWDG_GPIO_PIN);

        // Reload the IWDG counter
        IWDG_ReloadCounter();

        last_toggle_time = systick_ms;
    }
}
