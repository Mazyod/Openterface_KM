#ifndef __GPIO_INIT_H
#define __GPIO_INIT_H

#include "debug.h"
/*********************************************************************
 * @fn      GPIO_Toggle_INIT
 *
 * @brief   Initializes GPIOA.0
 *
 * @return  none
 */
extern void GPIO_Toggle_INIT(void);
extern void SD_SW(uint8_t *prev_state_p);

#endif