#include "include/keyboard_handler.h"
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "CONFIG.h"
#include "HAL.h"
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

static bool kb_sender_handles_release = FALSE;  // Flag to track if sender handles KB release
static uint8_t kb_last_data[8] = {0};
static uint8_t kb_repeat_count = 0;
static bool kb_long_press = FALSE;

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
 * @fn      Keyboard_Init
 *
 * @brief   Initialize keyboard handler
 *
 * @return  none
 */
void Keyboard_Init(void) {
    // Initialize keyboard data structures
    memset(KB_Data_Pack, 0, sizeof(KB_Data_Pack));
    memset(normal_keys, 0, sizeof(normal_keys));
    memset(kb_last_data, 0, sizeof(kb_last_data));
    
    mod_keys = 0;
    kb_sender_handles_release = FALSE;
    kb_repeat_count = 0;
    kb_long_press = FALSE;
    
    // Initialize scan variables
    KB_Scan_Done = 0x00;
    KB_Scan_Result = 0xF000;
    KB_Scan_Last_Result = 0xF000;
    KB_LED_Last_Status = 0x00;
    KB_LED_Cur_Status = 0x00;
}

/*********************************************************************
 * @fn      Keyboard_SetAutoReleaseMode
 *
 * @brief   Configure keyboard auto-release behavior
 *
 * @param   sender_handles_release - true if sender will handle releases, false for auto-release
 *
 * @return  none
 */
void Keyboard_SetAutoReleaseMode(bool sender_handles_release) {
    kb_sender_handles_release = sender_handles_release;
}

/*********************************************************************
 * @fn      Keyboard_GetAutoReleaseMode
 *
 * @brief   Get current keyboard auto-release mode
 *
 * @return  true if sender handles releases, false if auto-release is enabled
 */
bool Keyboard_GetAutoReleaseMode(void) {
    return kb_sender_handles_release;
}

/*********************************************************************
 * @fn      Keyboard_ResetAutoDetection
 *
 * @brief   Reset auto-detection and re-enable auto-release mode
 *          Use this to allow the system to re-detect sender behavior
 *
 * @return  none
 */
void Keyboard_ResetAutoDetection(void) {
    kb_sender_handles_release = FALSE;
    memset(kb_last_data, 0, sizeof(kb_last_data));
    kb_repeat_count = 0;
    kb_long_press = FALSE;
}

/*********************************************************************
 * @fn      Keyboard_SendDataToUSB
 *
 * @brief   Send keyboard data to USB endpoint
 *
 * @param   data - Keyboard data to send
 *
 * @return  none
 */
void Keyboard_SendDataToUSB(uint8_t* data) {
    memset(KB_Data_Pack, 0x00, sizeof(KB_Data_Pack));
    memcpy(KB_Data_Pack, data, 8);
    
    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, sizeof(KB_Data_Pack), DEF_UEP_CPY_LOAD);
}

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
void Keyboard_ProcessAutoRelease(uint8_t* data, uint8_t cmd_code, uint8_t repeat_count, bool* long_press) {
    // Check for non-modifier keys
    bool has_non_modifier = FALSE;
    for (int i = 0; i < 6; i++) {
        uint8_t key = normal_keys[i];
        if (key != 0 && key != KEY_TAB &&
            key != KEY_LSHIFT && key != KEY_RSHIFT &&
            key != KEY_LCTRL  && key != KEY_RCTRL &&
            key != KEY_LALT   && key != KEY_RALT &&
            key != KEY_LWIN   && key != KEY_RWIN) {
            has_non_modifier = TRUE;
            break;
        }
    }

    // Check if all 8 bytes are zero (complete release command)
    bool is_kb_release = (memcmp(data, "\0\0\0\0\0\0\0\0", 8) == 0);

    // Auto-detect: if sender sends explicit release commands, disable auto-release
    if (is_kb_release && !kb_sender_handles_release) {
        kb_sender_handles_release = TRUE;  // Sender is handling releases, disable auto-release
    }

    // Auto-release logic - only if sender doesn't handle releases AND it's not already a release command
    if (has_non_modifier && !*long_press && cmd_code != CMD_SEND_KB_GAME_DATA && !kb_sender_handles_release && !is_kb_release) {
        if (repeat_count >= 3) {
            *long_press = TRUE;
        } else {
            Delay_Ms(10);
            uint8_t release_pack[8] = {0};
            release_pack[0] = mod_keys;  // Keep modifier keys
            Keyboard_SendDataToUSB(release_pack);
        }
    }

    if (is_kb_release) {
        *long_press = FALSE;
        kb_repeat_count = 0;
        memset(kb_last_data, 0, sizeof(kb_last_data));
    }
}

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
void Keyboard_HandleData(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    // Validate data length
    if (data_len != 8) {
        // CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    // Check if data is same as last
    bool kb_same_as_last = (memcmp(data, kb_last_data, 8) == 0);

    if (kb_same_as_last) {
        kb_repeat_count++;
    } else {
        kb_repeat_count = 1;
        memcpy(kb_last_data, data, 8);
        kb_long_press = FALSE;
    }

    // Extract modifier and normal keys
    mod_keys = data[0];
    memcpy(normal_keys, &data[2], 6);

    // Send keyboard data to USB
    Keyboard_SendDataToUSB(data);
    
    // Send ACK response
    // CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);

    // Process auto-release logic
    Keyboard_ProcessAutoRelease(data, cmd_code, kb_repeat_count, &kb_long_press);
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
