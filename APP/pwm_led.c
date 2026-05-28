#include "ch32v20x.h"
#include "include/pwm_led.h"

/* PWM period ticks */
#define PWM_PERIOD   1000u

static volatile uint8_t g_brightness = 0;

void PWM_LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    /* Clocks (RGB_Init already enabled GPIOC and TIM3 clocks) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);

    /* Full remap: TIM3_CH2 -> PC7 (same as RGB_Init does) */
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, ENABLE);

    /* PC7: alternate function push-pull for TIM3_CH2 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* TIM3 CH2: PWM mode 1, polarity HIGH, 100% brightness
     * RGB_Init already configured TIM3 base (PSC=71, ARR=999, PWM1)
     * We just need to configure CH2 output.
     *
     * With PWM1+High polarity:
     *   CCR = 0: pin always LOW  (counter >= CCR always) -> LED ON
     *   CCR = 1000: pin always HIGH (counter < CCR always) -> LED OFF
     */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;            /* 100% brightness */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC2Init(TIM3, &TIM_OCInitStructure);

    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);

    g_brightness = 100;
}

void PWM_LED_SetBrightness(uint8_t brightness)
{
    if (brightness > 100u) brightness = 100u;
    g_brightness = brightness;

    /* Active-low LED with PWM1+High polarity:
     *   brightness 100% -> CCR = 0       (pin always LOW  = LED ON)
     *   brightness   0% -> CCR = 1000   (pin always HIGH = LED OFF)
     */
    uint16_t ccr = (uint16_t)(PWM_PERIOD - (uint32_t)brightness * PWM_PERIOD / 100u);
    TIM_SetCompare2(TIM3, ccr);
}

uint8_t PWM_LED_GetBrightness(void)
{
    return g_brightness;
}
