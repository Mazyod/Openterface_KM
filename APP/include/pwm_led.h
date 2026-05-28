#ifndef PWM_LED_H
#define PWM_LED_H

#include <stdint.h>

#define PWM_LED_PERIOD 1000u

void PWM_LED_Init(void);
void PWM_LED_SetBrightness(uint8_t brightness); /* 0 (off) .. 100 (full on) */
uint8_t PWM_LED_GetBrightness(void);

#endif /* PWM_LED_H */
