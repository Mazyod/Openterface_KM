#ifndef __RGB_H__
#define __RGB_H__

#include "ch32v20x.h"

/* RGB color enumeration */
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

/* RGB modes */
typedef enum {
    RGB_MODE_SOLID,
    RGB_MODE_BREATH,
    RGB_MODE_FLASH,
    RGB_MODE_STARTUP
} RGB_Mode_t;

/* Initialize RGB pins + PWM */
void RGB_Init(void);

/* Non-blocking mode refresh */
void RGB_Update(void);

/* Set color */
void RGB_SetColor(float r, float g, float b);

/* Set specified color always on */
void RGB_SetColorSolid(RGB_Color_t color, float brightness);

/* Start breathing mode */
void RGB_SetBreathMode(float speed);

/* Start monochrome flashing */
void RGB_SetFlashMode(uint16_t interval_ms, RGB_Color_t color);

/* Start power-on three-color flashing */
void RGB_FlashStartupSequence(void);

/* Global millisecond counter */
extern volatile uint32_t systick_ms;

#endif
