#include "include/mouse_handler.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "CONFIG.h"
#include <string.h>

/*******************************************************************************/
/* Global Variables */

volatile uint8_t  MS_Scan_Done = 0x00;
volatile uint16_t MS_Scan_Result = 0x00F0;

uint8_t MS_Data_Pack[4];    // Relative mouse data
uint8_t ABS_MS_Data_Pack[6]; // Absolute mouse data

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
static void Mouse_SendRelativeDataToUSB(uint8_t* data) {
    memcpy(MS_Data_Pack, data, sizeof(MS_Data_Pack));
    USBFS_Endp_DataUp(DEF_UEP2, MS_Data_Pack, sizeof(MS_Data_Pack), DEF_UEP_CPY_LOAD);
}

/*********************************************************************
 * @fn      Mouse_SendAbsoluteDataToUSB
 *
 * @brief   Send absolute mouse report to USB
 *
 * @param   data - 6-byte mouse report [buttons, x_low, x_high, y_low, y_high, wheel]
 *
 * @return  none
 */
static void Mouse_SendAbsoluteDataToUSB(uint8_t* data) {
    memcpy(ABS_MS_Data_Pack, data, sizeof(ABS_MS_Data_Pack));
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        USBFS_Endp_DataUp(DEF_UEP3, ABS_MS_Data_Pack, sizeof(ABS_MS_Data_Pack), DEF_UEP_CPY_LOAD);
    }
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
 * @brief   Handle absolute mouse input from CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Raw mouse data
 * @param   data_len - Length of data (should be >=7)
 *
 * @return  none
 */
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 7) return; // Invalid data length

    uint8_t report[6];
    report[0] = data[1]; // Buttons

    // Convert 16-bit coordinates from CH9329 to HID
    uint16_t x = (data[3] << 8) | data[2];
    uint16_t y = (data[5] << 8) | data[4];
    uint16_t x_hid = Mouse_ProcessCoordinateMapping(x, 3); // Scale by 8
    uint16_t y_hid = Mouse_ProcessCoordinateMapping(y, 3);

    report[1] = x_hid & 0xFF;
    report[2] = (x_hid >> 8) & 0xFF;
    report[3] = y_hid & 0xFF;
    report[4] = (y_hid >> 8) & 0xFF;

    report[5] = (int8_t)data[6]; // Wheel

    Mouse_SendAbsoluteDataToUSB(report);
}
