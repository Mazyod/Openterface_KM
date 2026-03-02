#ifndef __MOUSE_HANDLER_H
#define __MOUSE_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "Version_selection.h"

/*******************************************************************************/
/* EP3 Mode — compile-time selection via EP3_USE_STYLUS in Version_selection.h
 *
 *   EP3_USE_STYLUS not defined  →  Generic Desktop Mouse + Absolute
 *     Report: 6 bytes  [buttons | X_L|X_H | Y_L|Y_H | wheel]
 *     Works on: Windows, iOS
 *
 *   EP3_USE_STYLUS defined      →  Digitizer Pen / Stylus
 *     Report: 5 bytes  [InRange+Tip | X_L|X_H | Y_L|Y_H]
 *     Works on: Windows, iOS, Android
 *
 * Mouse_HandleAbsoluteData() automatically builds the correct report format.
 */
#ifdef EP3_USE_STYLUS
  #define ABS_MS_REPORT_LEN  5
#else
  #define ABS_MS_REPORT_LEN  6
#endif

/*******************************************************************************/
/* Global Variables */

extern volatile uint8_t  MS_Scan_Done;
extern volatile uint16_t MS_Scan_Result;

extern uint8_t MS_Data_Pack[4];                  /* Relative mouse data [buttons, x, y, wheel] */
extern uint8_t ABS_MS_Data_Pack[ABS_MS_REPORT_LEN]; /* Absolute mouse data buffer */

#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05

/*******************************************************************************/
/* Function Prototypes */

void Mouse_Init(void);
void Mouse_HandleRelativeData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

/*********************************************************************
 * Mouse_HandleAbsoluteData
 *
 * Builds and queues the EP3 report in the format selected by EP3_USE_STYLUS:
 *
 *   Without EP3_USE_STYLUS — 6-byte abs mouse report:
 *     Byte 0: buttons (bit0=L, bit1=R, bit2=M)
 *     Byte 1-2: X uint16 LE  (0~32767)
 *     Byte 3-4: Y uint16 LE  (0~32767)
 *     Byte 5:   wheel int8
 *
 *   With EP3_USE_STYLUS — 5-byte stylus report:
 *     Byte 0: bit0=Tip Switch (left btn→click), bit1=In Range (always 1)
 *     Byte 1-2: X uint16 LE  (0~32767)
 *     Byte 3-4: Y uint16 LE  (0~32767)
 *
 * @param data  Raw CH9329 packet: [reserved, buttons, x_low, x_high, y_low, y_high, wheel]
 *********************************************************************/
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

#endif // __MOUSE_HANDLER_H
