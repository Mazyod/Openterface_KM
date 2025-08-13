#include "include/mouse_handler.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "CONFIG.h"
#include <string.h>

/*******************************************************************************/
/* Global Variables */

volatile uint8_t  MS_Scan_Done = 0x00;
volatile uint16_t MS_Scan_Result = 0x00F0;

uint8_t MS_Data_Pack[4];
uint8_t ABS_MS_Data_Pack[6];

/*******************************************************************************/
/* External Functions */

extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);

// ACK Status Codes (from main file)
#define DEF_CMD_SUCCESS         0x00
#define DEF_CMD_ERR_PARA        0xE5
#define DEF_CMD_ERR_OPERATE     0xE6

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
    // Initialize mouse data structures
    memset(MS_Data_Pack, 0, sizeof(MS_Data_Pack));
    memset(ABS_MS_Data_Pack, 0, sizeof(ABS_MS_Data_Pack));
    
    // Initialize scan variables
    MS_Scan_Done = 0x00;
    MS_Scan_Result = 0x00F0;
}

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
uint16_t Mouse_ProcessCoordinateMapping(uint16_t ch9329_coord, uint8_t scale_factor) {
    return ch9329_coord << scale_factor;  // Bit shift for efficient scaling
}

/*********************************************************************
 * @fn      Mouse_SendRelativeDataToUSB
 *
 * @brief   Send relative mouse data to USB endpoint
 *
 * @param   data - Mouse data to send
 *
 * @return  none
 */
void Mouse_SendRelativeDataToUSB(uint8_t* data) {
    memset(MS_Data_Pack, 0x00, sizeof(MS_Data_Pack));
    memcpy(MS_Data_Pack, data, sizeof(MS_Data_Pack));
    
    USBFS_Endp_DataUp(DEF_UEP2, MS_Data_Pack, sizeof(MS_Data_Pack), DEF_UEP_CPY_LOAD);
}

/*********************************************************************
 * @fn      Mouse_SendAbsoluteDataToUSB
 *
 * @brief   Send absolute mouse data to USB endpoint
 *
 * @param   data - Mouse data to send
 *
 * @return  none
 */
void Mouse_SendAbsoluteDataToUSB(uint8_t* data) {
    memset(ABS_MS_Data_Pack, 0x00, sizeof(ABS_MS_Data_Pack));
    memcpy(ABS_MS_Data_Pack, data, sizeof(ABS_MS_Data_Pack));
    
    // Check if endpoint is not busy before sending
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        USBFS_Endp_DataUp(DEF_UEP3, ABS_MS_Data_Pack, sizeof(ABS_MS_Data_Pack), DEF_UEP_CPY_LOAD);
    }
}

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
void Mouse_HandleRelativeData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    // Validate data length
    if (data_len < 5) {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    uint8_t mouse_data[4];
    mouse_data[0] = data[1];  // Mouse buttons
    mouse_data[1] = (data[2] & 0x80) ? (int8_t)(data[2] - 256) : data[2];  // X movement
    mouse_data[2] = (data[3] & 0x80) ? (int8_t)(data[3] - 256) : data[3];  // Y movement
    mouse_data[3] = (data[4] & 0x80) ? (int8_t)(data[4] - 256) : data[4];  // Wheel

    Mouse_SendRelativeDataToUSB(mouse_data);
    CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);
}

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
void Mouse_HandleAbsoluteData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    // Validate data length
    if (data_len < 7) {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    uint8_t mouse_data[6];
    mouse_data[0] = data[1];  // Mouse buttons
    
    // More efficient coordinate mapping using bit shifting
    // Map X coordinate from CH9329 4096 range to HID 32768 range (scale by 8)
    uint16_t x_ch9329 = (data[3] << 8) | data[2];  // Combine high and low bytes
    uint16_t x_hid = Mouse_ProcessCoordinateMapping(x_ch9329, 3);  // Scale by 8 (2^3)
    mouse_data[1] = x_hid & 0xFF;        // X low byte
    mouse_data[2] = (x_hid >> 8) & 0xFF; // X high byte
    
    // Map Y coordinate from CH9329 4096 range to HID 32768 range (scale by 8)
    uint16_t y_ch9329 = (data[5] << 8) | data[4];  // Combine high and low bytes
    uint16_t y_hid = Mouse_ProcessCoordinateMapping(y_ch9329, 3);  // Scale by 8 (2^3)
    mouse_data[3] = y_hid & 0xFF;        // Y low byte
    mouse_data[4] = (y_hid >> 8) & 0xFF; // Y high byte
    
    // Wheel (8-bit signed relative)
    mouse_data[5] = (int8_t)data[6];  // Simpler cast for wheel

    // Send data immediately without additional processing delay
    // Check if endpoint is not busy before sending
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        Mouse_SendAbsoluteDataToUSB(mouse_data);
        CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);
    } else {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_OPERATE);
    }
}

/*
 * Example of extending mouse functionality:
 * 
 * 1. Add mouse acceleration/deceleration:
 *    void Mouse_SetAcceleration(float accel_factor);
 * 
 * 2. Add mouse sensitivity control:
 *    void Mouse_SetSensitivity(uint8_t sensitivity);
 * 
 * 3. Add coordinate transformation:
 *    void Mouse_SetCoordinateTransform(transform_matrix_t matrix);
 * 
 * 4. Add mouse button mapping:
 *    uint8_t Mouse_RemapButtons(uint8_t original_buttons);
 * 
 * 5. Add gesture recognition:
 *    gesture_t Mouse_DetectGesture(mouse_movement_t* movements, uint8_t count);
 */
