#include "include/mouse_handler.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "CONFIG.h"
#include "km_ring.h"
#include <string.h>

/*******************************************************************************/
/* Global Variables */

volatile uint8_t  MS_Scan_Done = 0x00;
volatile uint16_t MS_Scan_Result = 0x00F0;

uint8_t MS_Data_Pack[4];                   // Relative mouse data
uint8_t ABS_MS_Data_Pack[ABS_MS_REPORT_LEN]; // Absolute mouse data buffer

/*******************************************************************************/
/* External Functions */

extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);

/*******************************************************************************/
/* Function Implementations */

/*********************************************************************
 * @fn      Mouse_Init
 *
 * @brief   Initialize mouse handler
 *
 * @return  none
 */
void Mouse_Init(void) {
    memset(MS_Data_Pack, 0, sizeof(MS_Data_Pack));
    memset(ABS_MS_Data_Pack, 0, sizeof(ABS_MS_Data_Pack));
    MS_Scan_Done = 0x00;
    MS_Scan_Result = 0x00F0;
}

/*********************************************************************
 * @fn      Mouse_ProcessCoordinateMapping
 *
 * @brief   Map CH9329 coordinate to HID coordinate range
 *
 * @param   ch9329_coord - Input coordinate from CH9329
 * @param   scale_factor - Scaling factor for mapping (bit shift)
 *
 * @return  Mapped HID coordinate
 */
static uint16_t Mouse_ProcessCoordinateMapping(uint16_t ch9329_coord, uint8_t scale_factor) {
    return ch9329_coord << scale_factor;  // Shift for efficient scaling
}

/*********************************************************************
 * @fn      Mouse_SendRelativeDataToUSB
 *
 * @brief   Send relative mouse report to USB
 *
 * @param   data - 4-byte mouse report [buttons, x, y, wheel]
 *
 * @return  none
 */
void Mouse_SendRelativeDataToUSB(uint8_t* data) {
    Queue_Push_MouseRel(data);
}

/*********************************************************************
 * @fn      Mouse_SendAbsoluteDataToUSB
 *********************************************************************/
void Mouse_SendAbsoluteDataToUSB(uint8_t* data) {
    Queue_Push_MouseAbs(data);
}
/*********************************************************************
 * @fn      Mouse_HandleRelativeData
 *
 * @brief   Handle relative mouse input from CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Raw mouse data
 * @param   data_len - Length of data (should be >=5)
 *
 * @return  none
 */
void Mouse_HandleRelativeData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 5) return; // Invalid data length

    uint8_t report[4];
    report[0] = data[1];  // Buttons
    report[1] = (int8_t)data[2];  // X movement
    report[2] = (int8_t)data[3];  // Y movement
    report[3] = (int8_t)data[4];  // Wheel

    Mouse_SendRelativeDataToUSB(report);
}
/*********************************************************************
 * @fn      Mouse_HandleAbsoluteData
 *
 * @brief   Builds an EP3 HID report and queues it for USB transmission.
 *          The report format is selected at compile time via EP3_USE_STYLUS:
 *
 *   Without EP3_USE_STYLUS  →  6-byte Generic Desktop Mouse (absolute):
 *     Byte 0: buttons (bit0=L, bit1=R, bit2=M)
 *     Byte 1-2: X uint16 LE  (0~32767)
 *     Byte 3-4: Y uint16 LE  (0~32767)
 *     Byte 5:   wheel int8
 *
 *   With EP3_USE_STYLUS  →  5-byte Digitizer Pen / Stylus:
 *     Byte 0: bit0=Tip Switch (left btn→click), bit1=In Range (always 1)
 *     Byte 1-2: X uint16 LE  (0~32767)
 *     Byte 3-4: Y uint16 LE  (0~32767)
 *
 * @param   data - Raw CH9329 data: [reserved, buttons, x_low, x_high, y_low, y_high, wheel]
 *********************************************************************/
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 7) return;

    uint8_t report[ABS_MS_REPORT_LEN];

    uint16_t x_hid = Mouse_ProcessCoordinateMapping((data[3] << 8) | data[2], 3);
    uint16_t y_hid = Mouse_ProcessCoordinateMapping((data[5] << 8) | data[4], 3);

#ifdef EP3_USE_STYLUS
    /* 5-byte Digitizer Pen/Stylus report */
    report[0] = 0x02;                        // In Range = 1, Tip = 0 (hover)
    if (data[1] & 0x01) report[0] |= 0x01;  // left button → Tip = 1 (click)
    report[1] = x_hid & 0xFF;
    report[2] = (x_hid >> 8) & 0xFF;
    report[3] = y_hid & 0xFF;
    report[4] = (y_hid >> 8) & 0xFF;
#else
    /* 6-byte Generic Desktop Mouse absolute report */
    report[0] = data[1] & 0x07;             // buttons: L/R/M only
    report[1] = x_hid & 0xFF;
    report[2] = (x_hid >> 8) & 0xFF;
    report[3] = y_hid & 0xFF;
    report[4] = (y_hid >> 8) & 0xFF;
    report[5] = (int8_t)data[6];            // wheel
#endif

    Mouse_SendAbsoluteDataToUSB(report);
}
