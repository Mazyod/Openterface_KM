#ifndef __KEYBOARD_HANDLER_H
#define __KEYBOARD_HANDLER_H

#include <stdint.h>
#include "usb_type.h"  // For bool type definition
#include <stdbool.h>

/*******************************************************************************/
/* Definitions */

// Keyboard Commands
#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_KB_GAME_DATA    0x12

// Modifier Keys
#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

// Special Keys
#define KEY_PIPE      0x64
#define KEY_TAB       0x2B
#define KEY_LSHIFT    0xE1
#define KEY_RSHIFT    0xE5
#define KEY_LCTRL     0xE0
#define KEY_RCTRL     0xE4
#define KEY_LALT      0xE2
#define KEY_RALT      0xE6
#define KEY_LWIN      0xE3
#define KEY_RWIN      0xE7

/*******************************************************************************/
/* Global Variables */

extern volatile uint8_t  KB_Scan_Done;
extern volatile uint16_t KB_Scan_Result;
extern volatile uint16_t KB_Scan_Last_Result;
extern volatile uint8_t  KB_LED_Last_Status;
extern volatile uint8_t  KB_LED_Cur_Status;

extern uint8_t KB_Data_Pack[8];
extern uint8_t mod_keys;
extern uint8_t normal_keys[6];

/*******************************************************************************/
/* Function Prototypes */

/*********************************************************************
 * @fn      Keyboard_Init
 *
 * @brief   Initialize keyboard handler
 *
 * @return  none
 */
void Keyboard_Init(void);

/*********************************************************************
 * @fn      Keyboard_SetAutoReleaseMode
 *
 * @brief   Configure keyboard auto-release behavior
 *
 * @param   sender_handles_release - true if sender will handle releases, false for auto-release
 *
 * @return  none
 */
void Keyboard_SetAutoReleaseMode(bool sender_handles_release);

/*********************************************************************
 * @fn      Keyboard_GetAutoReleaseMode
 *
 * @brief   Get current keyboard auto-release mode
 *
 * @return  true if sender handles releases, false if auto-release is enabled
 */
bool Keyboard_GetAutoReleaseMode(void);

/*********************************************************************
 * @fn      Keyboard_ResetAutoDetection
 *
 * @brief   Reset auto-detection and re-enable auto-release mode
 *          Use this to allow the system to re-detect sender behavior
 *
 * @return  none
 */
void Keyboard_ResetAutoDetection(void);

/*********************************************************************
 * @fn      Keyboard_HandleData
 *
 * @brief   Handle keyboard data from CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Keyboard data (8 bytes)
 * @param   data_len - Length of data
 *
 * @return  none
 */
void Keyboard_HandleData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len);

/*********************************************************************
 * @fn      Keyboard_SendDataToUSB
 *
 * @brief   Send keyboard data to USB endpoint
 *
 * @param   data - Keyboard data to send
 *
 * @return  none
 */
void Keyboard_SendDataToUSB(uint8_t* data);

/*********************************************************************
 * @fn      Keyboard_ProcessAutoRelease
 *
 * @brief   Process auto-release logic for keyboard keys
 *
 * @param   data - Current keyboard data
 * @param   cmd_code - Command code
 * @param   repeat_count - Number of times this data has been repeated
 * @param   long_press - Pointer to long press flag
 *
 * @return  none
 */
void Keyboard_ProcessAutoRelease(uint8_t* data, uint8_t cmd_code, uint8_t repeat_count, bool* long_press);

/*********************************************************************
 * @fn      Keyboard_SetLEDStatus
 *
 * @brief   Set keyboard LED status (Caps Lock, Num Lock, Scroll Lock)
 *
 * @param   led_status - LED status byte from HID SET_REPORT
 *
 * @return  none
 */
void Keyboard_SetLEDStatus(uint8_t led_status);

/*********************************************************************
 * @fn      Keyboard_GetLEDStatus
 *
 * @brief   Get current keyboard LED status
 *
 * @return  Current LED status byte
 */
uint8_t Keyboard_GetLEDStatus(void);

/*********************************************************************
 * @fn      Keyboard_GetLastLEDStatus
 *
 * @brief   Get last keyboard LED status
 *
 * @return  Last LED status byte
 */
uint8_t Keyboard_GetLastLEDStatus(void);

#endif /* __KEYBOARD_HANDLER_H */
