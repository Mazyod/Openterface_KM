#include "include/keyboard_handler.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "CONFIG.h"
#include "HAL.h"
#include "km_ring.h"
#include <string.h>
#include <stdbool.h>

/*******************************************************************************/
/* Global Variables */

volatile uint8_t  KB_Scan_Done = 0x00;
volatile uint16_t KB_Scan_Result = 0xF000;
volatile uint16_t KB_Scan_Last_Result = 0xF000;
volatile uint8_t  KB_LED_Last_Status = 0x00;
volatile uint8_t  KB_LED_Cur_Status = 0x00;

uint8_t KB_Data_Pack[8];
uint8_t mod_keys = 0;
uint8_t normal_keys[6] = {0};

/*******************************************************************************/
/* Private Variables */

static uint8_t kb_last_data[8] = {0};

/*******************************************************************************/
/* External Functions */

extern void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status);

/*******************************************************************************/
/* Function Implementations */

/*********************************************************************
 * @fn      Keyboard_Init
 *
 * @brief   Initialize keyboard handler and internal states
 *
 * @return  none
 */
void Keyboard_Init(void) {
    memset(KB_Data_Pack, 0, sizeof(KB_Data_Pack));
    memset(normal_keys, 0, sizeof(normal_keys));
    memset(kb_last_data, 0, sizeof(kb_last_data));
    
    mod_keys = 0;
    
    KB_Scan_Done = 0x00;
    KB_Scan_Result = 0xF000;
    KB_Scan_Last_Result = 0xF000;
    KB_LED_Last_Status = 0x00;
    KB_LED_Cur_Status = 0x00;
}

/*********************************************************************
 * @fn      Keyboard_SendDataToUSB
 *
 * @brief   Send keyboard report to USB endpoint
 *
 * @param   data - 8-byte keyboard HID report
 *
 * @return  none
 */
void Keyboard_SendDataToUSB(uint8_t* data) {
    Queue_Push_Keyboard(data);
}

/*********************************************************************
 * @fn      Keyboard_HandleData
 *
 * @brief   Handle keyboard input from CH9329
 *          Only release keys if host explicitly sends release report
 *
 * @param   addr     - Device address
 * @param   cmd_code - Command code
 * @param   data     - 8-byte keyboard data
 * @param   data_len - Length of data (should be 8)
 *
 * @return  none
 */
void Keyboard_HandleData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len)
{
    uint8_t mod = 0;
    uint8_t keys[6] = {0};
    int key_count = 0;

    // CH9329 的普通键区在 data[2]~data[7]
    for(int i = 2; i < 8; i++)
    {
        uint8_t key = data[i];

        if(key >= 0xE0 && key <= 0xE7)
        {
            // 将特殊键转换为 HID 修饰键 bit 位
            mod |= (1 << (key - 0xE0));
        }
        else if(key != 0 && key_count < 6)
        {
            // 普通键
            keys[key_count++] = key;
        }
    }

    uint8_t hid_report[8] = {0};

    hid_report[0] = mod;          // modifier byte
    hid_report[1] = 0x00;         // reserved
    memcpy(&hid_report[2], keys, 6);

    // 发送 HID 报告到 USB
    Keyboard_SendDataToUSB(hid_report);
}


/*********************************************************************
 * @fn      Keyboard_SetLEDStatus
 *
 * @brief   Set keyboard LED status (Caps Lock, Num Lock, Scroll Lock)
 *
 * @param   led_status - LED status byte from HID SET_REPORT
 *
 * @return  none
 */
void Keyboard_SetLEDStatus(uint8_t led_status) {
    KB_LED_Last_Status = KB_LED_Cur_Status;
    KB_LED_Cur_Status = led_status;
}

/*********************************************************************
 * @fn      Keyboard_GetLEDStatus
 *
 * @brief   Get current keyboard LED status
 *
 * @return  Current LED status byte
 */
uint8_t Keyboard_GetLEDStatus(void) {
    return KB_LED_Cur_Status;
}

/*********************************************************************
 * @fn      Keyboard_GetLastLEDStatus
 *
 * @brief   Get last keyboard LED status
 *
 * @return  Last LED status byte
 */
uint8_t Keyboard_GetLastLEDStatus(void) {
    return KB_LED_Last_Status;
}
