#ifndef __RGB_H__
#define __RGB_H__

#include "ch32v20x.h"

/* RGB 颜色枚举 */
typedef enum {
    RGB_OFF,
    RGB_RED,
    RGB_GREEN,
    RGB_BLUE,
    RGB_YELLOW,
    RGB_CYAN,
    RGB_MAGENTA,
    RGB_WHITE
} RGB_Color_t;

/* RGB 模式 */
typedef enum {
    RGB_MODE_SOLID,
    RGB_MODE_BREATH,
    RGB_MODE_FLASH,
    RGB_MODE_STARTUP
} RGB_Mode_t;

/* 初始化 RGB 引脚 + PWM */
void RGB_Init(void);

/* 非阻塞模式刷新 */
void RGB_Update(void);

/* 设置颜色 */
void RGB_SetColor(float r, float g, float b);

/* 设置指定颜色常亮 */
void RGB_SetColorSolid(RGB_Color_t color, float brightness);

/* 启动呼吸模式 */
void RGB_SetBreathMode(float speed);

/* 启动单色闪烁 */
void RGB_SetFlashMode(uint16_t interval_ms, RGB_Color_t color);

/* 启动开机三色闪烁 */
void RGB_FlashStartupSequence(void);

/* 全局毫秒计数 */
extern volatile uint32_t systick_ms;

#endif
