#include "RGB.h"
#include <math.h>

/* Millisecond counter */
volatile uint32_t systick_ms = 0;

/* RGB internal state */
static RGB_Mode_t rgb_mode = RGB_MODE_SOLID;
static float rgb_r=0, rgb_g=0, rgb_b=0;
static float breath_phase = 0;
static float breath_speed = 0.01f;

/* Monochrome flashing state */
static uint16_t flash_interval = 500;
static RGB_Color_t flash_color = RGB_RED;
static uint32_t flash_last_ms = 0;
static uint8_t flash_state = 0;

/* Startup flashing sequence */
typedef struct {
    RGB_Color_t color;
    uint16_t duration_ms;
} RGB_FlashStep_t;

static RGB_FlashStep_t startup_steps[] = {
    {RGB_RED,    2000},
    {RGB_GREEN,  2000},
    {RGB_BLUE,   2000},
    {RGB_OFF,     500}
};

static uint8_t startup_index = 0;
static uint32_t startup_last_ms = 0;
static uint8_t startup_active = 0;

/* ====================== TIM2 Initialization ====================== */
void RGB_TIM_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_TimeBaseStructure.TIM_Period = 999;          // 1000 ticks
    TIM_TimeBaseStructure.TIM_Prescaler = 35;        // 36MHz /36 = 1MHz
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure={0};
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* ====================== TIM2 Interrupt Handler ====================== */
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        
        systick_ms++;
    }
}

/* ====================== RGB Initialization ====================== */
void RGB_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};

    /* GPIO initialization */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* TIM3 PWM */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    TIM_TimeBaseStructure.TIM_Period = 999;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OCInitStructure.TIM_Pulse = 0;

    TIM_OC1Init(TIM3, &TIM_OCInitStructure);
    TIM_OC3Init(TIM3, &TIM_OCInitStructure);
    TIM_OC4Init(TIM3, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_Cmd(TIM3, ENABLE);

    /* Initialize TIM2 millisecond counter */
    RGB_TIM_Init();
}

/* ====================== Set Color ====================== */
void RGB_SetColor(float r, float g, float b)
{
    if(r>1) r=1; if(g>1) g=1; if(b>1) b=1;
    if(r<0) r=0; if(g<0) g=0; if(b<0) b=0;

    uint16_t pwm_r = (uint16_t)((1.0f - r) * 999);
    uint16_t pwm_g = (uint16_t)((1.0f - g) * 999);
    uint16_t pwm_b = (uint16_t)((1.0f - b) * 999);

    TIM_SetCompare1(TIM3, pwm_b);
    TIM_SetCompare3(TIM3, pwm_g);
    TIM_SetCompare4(TIM3, pwm_r);

    rgb_r = r; rgb_g = g; rgb_b = b;
}

/* ====================== Solid Color (Always On) ====================== */
void RGB_SetColorSolid(RGB_Color_t color, float brightness)
{
    rgb_mode = RGB_MODE_SOLID;
    float r=0,g=0,b=0;
    switch(color)
    {
        case RGB_RED: r=1; break;
        case RGB_GREEN: g=1; break;
        case RGB_BLUE: b=1; break;
        case RGB_YELLOW: r=1; g=1; break;
        case RGB_CYAN: g=1; b=1; break;
        case RGB_MAGENTA: r=1; b=1; break;
        case RGB_WHITE: r=1; g=1; b=1; break;
        case RGB_OFF: r=0; g=0; b=0; break;
    }
    RGB_SetColor(r*brightness, g*brightness, b*brightness);
}

/* ====================== Breathing Mode ====================== */
void RGB_SetBreathMode(float speed)
{
    rgb_mode = RGB_MODE_BREATH;
    breath_speed = speed;
}

/* ====================== Monochrome Flashing ====================== */
void RGB_SetFlashMode(uint16_t interval_ms, RGB_Color_t color)
{
    rgb_mode = RGB_MODE_FLASH;
    flash_interval = interval_ms;
    flash_color = color;
    flash_last_ms = systick_ms;
    flash_state = 0;
}

/* ====================== Power-on Three-Color Flashing ====================== */
void RGB_FlashStartupSequence(void)
{
    rgb_mode = RGB_MODE_STARTUP;
    startup_active = 1;
    startup_index = 0;
    startup_last_ms = systick_ms;
    RGB_SetColorSolid(startup_steps[0].color, 1.0f);
}

/* ====================== Non-blocking Refresh ====================== */
void RGB_Update(void)
{
    static uint32_t last_ms = 0;
    uint32_t now = systick_ms;

    /* Power-on flashing has priority */
    if(startup_active)
    {
        if(now - startup_last_ms >= startup_steps[startup_index].duration_ms)
        {
            startup_index++;
            if(startup_index >= sizeof(startup_steps)/sizeof(startup_steps[0]))
            {
                startup_active = 0;
            }
            else
            {
                RGB_SetColorSolid(startup_steps[startup_index].color, 1.0f);
                startup_last_ms = now;
            }
        }
        return;
    }

    switch(rgb_mode)
    {
        case RGB_MODE_SOLID:
            break;

        case RGB_MODE_BREATH:
            if(now - last_ms >= 25)
            {
                last_ms = now;
                breath_phase += breath_speed;
                if(breath_phase>6.283185f) breath_phase -= 6.283185f;

                float r = (sinf(breath_phase)+1)*0.5f;
                float g = (sinf(breath_phase+2.094395f)+1)*0.5f;
                float b = (sinf(breath_phase+4.18879f)+1)*0.5f;

                RGB_SetColor(r, g, b);
            }
            break;

        case RGB_MODE_FLASH:
            if(now - flash_last_ms >= flash_interval)
            {
                flash_last_ms = now;
                flash_state = !flash_state;
                if(flash_state) RGB_SetColorSolid(flash_color, 1.0f);
                else RGB_SetColorSolid(RGB_OFF, 0);
            }
            break;

        case RGB_MODE_STARTUP:
            break;
    }
}
