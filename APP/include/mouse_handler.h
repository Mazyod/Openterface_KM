#ifndef __MOUSE_HANDLER_H
#define __MOUSE_HANDLER_H

#include <stdint.h>
#include "usb_type.h"  // For bool type definition

/*******************************************************************************/
/* Definitions */

// Mouse Commands
#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05

/*******************************************************************************/
/* Global Variables */

extern volatile uint8_t  MS_Scan_Done;
extern volatile uint16_t MS_Scan_Result;

extern uint8_t MS_Data_Pack[4];
extern uint8_t ABS_MS_Data_Pack[6];

/*******************************************************************************/
/* Function Prototypes */

/*********************************************************************
 * @fn      Mouse_Init
 *
 * @brief   Initialize mouse handler
 *
 * @return  none
 */
void Mouse_Init(void);

/*********************************************************************
 * @fn      Mouse_HandleRelativeData
 *
 * @brief   Handle relative mouse data from CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Mouse data
 * @param   data_len - Length of data
 *
 * @return  none
 */
void Mouse_HandleRelativeData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

/*********************************************************************
 * @fn      Mouse_HandleAbsoluteData
 *
 * @brief   Handle absolute mouse data from CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Mouse data
 * @param   data_len - Length of data
 *
 * @return  none
 */
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

/*********************************************************************
 * @fn      Mouse_SendRelativeDataToUSB
 *
 * @brief   Send relative mouse data to USB endpoint
 *
 * @param   data - Mouse data to send
 *
 * @return  none
 */
void Mouse_SendRelativeDataToUSB(uint8_t* data);

/*********************************************************************
 * @fn      Mouse_SendAbsoluteDataToUSB
 *
 * @brief   Send absolute mouse data to USB endpoint
 *
 * @param   data - Mouse data to send
 *
 * @return  none
 */
void Mouse_SendAbsoluteDataToUSB(uint8_t* data);

/*********************************************************************
 * @fn      Mouse_ProcessCoordinateMapping
 *
 * @brief   Map coordinates from CH9329 range to HID range
 *
 * @param   ch9329_coord - Input coordinate from CH9329
 * @param   scale_factor - Scaling factor for mapping
 *
 * @return  Mapped coordinate for HID
 */
uint16_t Mouse_ProcessCoordinateMapping(uint16_t ch9329_coord, uint8_t scale_factor);

#endif /* __MOUSE_HANDLER_H */
