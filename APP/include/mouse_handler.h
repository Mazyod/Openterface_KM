#ifndef __MOUSE_HANDLER_H
#define __MOUSE_HANDLER_H

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************/
/* Global Variables */

extern volatile uint8_t  MS_Scan_Done;    // Flag indicating a mouse scan is done
extern volatile uint16_t MS_Scan_Result;  // Latest mouse scan result

extern uint8_t MS_Data_Pack[4];           // Relative mouse data [buttons, x, y, wheel]
extern uint8_t ABS_MS_Data_Pack[6];       // Absolute mouse data [buttons, x_low, x_high, y_low, y_high, wheel]
#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05
/*******************************************************************************/
/* Function Prototypes */

/*********************************************************************
 * @fn      Mouse_Init
 *
 * @brief   Initialize mouse handler and internal states
 *
 * @return  none
 */
void Mouse_Init(void);

/*********************************************************************
 * @fn      Mouse_HandleRelativeData
 *
 * @brief   Handle relative mouse input from CH9329
 *
 * @param   addr     - Device address
 * @param   cmd_code - Command code
 * @param   data     - Raw mouse data from CH9329
 * @param   data_len - Length of data (should be >=5)
 *
 * @return  none
 */
void Mouse_HandleRelativeData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

/*********************************************************************
 * @fn      Mouse_HandleAbsoluteData
 *
 * @brief   Handle absolute mouse input from CH9329
 *
 * @param   addr     - Device address
 * @param   cmd_code - Command code
 * @param   data     - Raw mouse data from CH9329
 * @param   data_len - Length of data (should be >=7)
 *
 * @return  none
 */
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

#endif // __MOUSE_HANDLER_H
