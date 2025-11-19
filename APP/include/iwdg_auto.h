#ifndef __IWDG_AUTO_H
#define __IWDG_AUTO_H

#include "ch32v20x_iwdg.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

// Initialize IWDG and the watchdog GPIO pin
void IWDG_Auto_Init(uint16_t prescaler, uint16_t reload);

// Call in the main loop to automatically toggle the watchdog pin
void IWDG_Auto_Handler(void);

#endif
