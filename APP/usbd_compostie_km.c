
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"

/*******************************************************************************/
/* Global Variable Definition */

/* Mouse */
volatile uint8_t  MS_Scan_Done = 0x00;                                          // Mouse Movement Scan Done
volatile uint16_t MS_Scan_Result = 0x00F0;                                      // Mouse Movement Scan Result


/* Keyboard */
volatile uint8_t  KB_Scan_Done = 0x00;                                          // Keyboard Keys Scan Done
volatile uint16_t KB_Scan_Result = 0xF000;                                      // Keyboard Keys Current Scan Result
volatile uint16_t KB_Scan_Last_Result = 0xF000;                                 // Keyboard Keys Last Scan Resul                                       // Keyboard IN Data Packet
volatile uint8_t  KB_LED_Last_Status = 0x00;                                    // Keyboard LED Last Result
volatile uint8_t  KB_LED_Cur_Status = 0x00;                                     // Keyboard LED Current Result

/*********************************************************************
 * @fn      USB_Sleep_Wakeup_CFG
 *
 * @brief   Configure USB wake up mode
 *
 * @return  none
 */
void USB_Sleep_Wakeup_CFG( void )
{
    EXTI_InitTypeDef EXTI_InitStructure = { 0 };

    EXTI_InitStructure.EXTI_Line = EXTI_Line20;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Event;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init( &EXTI_InitStructure );
}

/*********************************************************************
 * @fn      MCU_Sleep_Wakeup_Operate
 *
 * @brief   Perform sleep operation
 *
 * @return  none
 */
void MCU_Sleep_Wakeup_Operate( void )
{
    __disable_irq();
    EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
    EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );

    PWR_EnterSTOPMode(PWR_Regulator_LowPower,PWR_STOPEntry_WFE);
    SystemInit();
    SystemCoreClockUpdate();
    USBFS_RCC_Init();

    if( EXTI_GetFlagStatus( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 ) != RESET  )
    {
        EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
        USBFS_Send_Resume( );
    }
    else if( EXTI_GetFlagStatus( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 ) != RESET )
    {
        EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );
        USBFS_Send_Resume( );
    }
    __enable_irq( );
}
#include <string.h>
#include <stdint.h>

// -------------------- Protocol Definitions --------------------
#define CH9329_FRAME_HEAD1      0x57
#define CH9329_FRAME_HEAD2      0xAB

#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_KB_GAME_DATA    0x12
#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05

// ACK Status Codes
#define DEF_CMD_SUCCESS         0x00    // Command execution successful
#define DEF_CMD_ERR_TIMEOUT     0xE1    // Serial receiving timeout
#define DEF_CMD_ERR_HEAD        0xE2    // Serial receiving header error
#define DEF_CMD_ERR_CMD         0xE3    // Serial receiving command error
#define DEF_CMD_ERR_SUM         0xE4    // Checksum mismatch
#define DEF_CMD_ERR_PARA        0xE5    // Parameter error
#define DEF_CMD_ERR_OPERATE     0xE6    // Operation error/execution failed

#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

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

// -------------------- Data Buffers --------------------
uint8_t KB_Data_Pack[8];
uint8_t MS_Data_Pack[4];
uint8_t ABS_MS_Data_Pack[6];

uint8_t mod_keys = 0;
uint8_t normal_keys[6] = {0};

// -------------------- ACK Packet Sender --------------------
void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status) {
    uint8_t ack_cmd = cmd_code | 0x80;  // Set bit 7 to indicate ACK response
    uint8_t ack_packet[7] = {CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, addr, ack_cmd, 0x01, status, 0};
    
    // Calculate checksum for first 6 bytes
    for (int i = 0; i < 6; i++) {
        ack_packet[6] += ack_packet[i];
    }
    
    // Send ACK packet via UART (this should be implemented based on your UART driver)
    UART2_Tx(ack_packet, sizeof(ack_packet));
    
    // For debugging, you might want to log the ACK being sent
    // printf("ACK sent: CMD=0x%02X, Status=0x%02X\n", ack_cmd, status);
}

// -------------------- Keyboard Handler --------------------
void CH9329_HandleKeyboard(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    static uint8_t kb_last_data[8] = {0};
    static uint8_t kb_repeat_count = 0;
    static bool kb_long_press = 0;

    if (data_len != 8) {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    bool kb_same_as_last = (memcmp(data, kb_last_data, 8) == 0);

    if (kb_same_as_last) {
        kb_repeat_count++;
    } else {
        kb_repeat_count = 1;
        memcpy(kb_last_data, data, 8);
        kb_long_press = 0;
    }

    mod_keys = data[0];
    memcpy(normal_keys, &data[2], 6);

    memset(KB_Data_Pack, 0x00, sizeof(KB_Data_Pack));
    KB_Data_Pack[0] = mod_keys;
    memcpy(&KB_Data_Pack[2], normal_keys, 6);

    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, sizeof(KB_Data_Pack), DEF_UEP_CPY_LOAD);
    CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);

    bool has_non_modifier = 0;
    for (int i = 0; i < 6; i++) {
        uint8_t key = normal_keys[i];
        if (key != 0 && key != KEY_TAB &&
            key != KEY_LSHIFT && key != KEY_RSHIFT &&
            key != KEY_LCTRL  && key != KEY_RCTRL &&
            key != KEY_LALT   && key != KEY_RALT &&
            key != KEY_LWIN   && key != KEY_RWIN) {
            has_non_modifier = 1;
            break;
        }
    }

    bool is_kb_release = (mod_keys == 0 && memcmp(normal_keys, "\0\0\0\0\0\0", 6) == 0);

    if (has_non_modifier && !kb_long_press && cmd_code != CMD_SEND_KB_GAME_DATA) {
        if (kb_repeat_count >= 3) {
            kb_long_press = 1;
        } else {
            Delay_Ms(10);
            uint8_t release_pack[8] = {0};
            release_pack[0] = mod_keys;  // Keep modifier keys
            USBFS_Endp_DataUp(DEF_UEP1, release_pack, sizeof(release_pack), DEF_UEP_CPY_LOAD);
        }
    }

    if (is_kb_release) {
        kb_long_press = 0;
        kb_repeat_count = 0;
        memset(kb_last_data, 0, sizeof(kb_last_data));
    }
}

// -------------------- Relative Mouse Handler --------------------
void CH9329_HandleRelativeMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 5) {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    memset(MS_Data_Pack, 0x00, sizeof(MS_Data_Pack));
    MS_Data_Pack[0] = data[1];  // Mouse buttons
    MS_Data_Pack[1] = (data[2] & 0x80) ? (int8_t)(data[2] - 256) : data[2];  // X movement
    MS_Data_Pack[2] = (data[3] & 0x80) ? (int8_t)(data[3] - 256) : data[3];  // Y movement
    MS_Data_Pack[3] = (data[4] & 0x80) ? (int8_t)(data[4] - 256) : data[4];  // Wheel

    USBFS_Endp_DataUp(DEF_UEP2, MS_Data_Pack, sizeof(MS_Data_Pack), DEF_UEP_CPY_LOAD);
    CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);
}

// -------------------- Absolute Mouse Handler --------------------
void CH9329_HandleAbsoluteMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 7) {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_PARA);
        return;
    }

    // Clear data pack
    memset(ABS_MS_Data_Pack, 0x00, sizeof(ABS_MS_Data_Pack));
    ABS_MS_Data_Pack[0] = data[1];  // Mouse buttons
    
    // More efficient coordinate mapping using bit shifting
    // Map X coordinate from CH9329 4096 range to HID 32768 range (scale by 8)
    uint16_t x_ch9329 = (data[3] << 8) | data[2];  // Combine high and low bytes
    uint16_t x_hid = x_ch9329 << 3;  // Bit shift is faster than multiplication by 8
    ABS_MS_Data_Pack[1] = x_hid & 0xFF;        // X low byte
    ABS_MS_Data_Pack[2] = (x_hid >> 8) & 0xFF; // X high byte
    
    // Map Y coordinate from CH9329 4096 range to HID 32768 range (scale by 8)
    uint16_t y_ch9329 = (data[5] << 8) | data[4];  // Combine high and low bytes
    uint16_t y_hid = y_ch9329 << 3;  // Bit shift is faster than multiplication by 8
    ABS_MS_Data_Pack[3] = y_hid & 0xFF;        // Y low byte
    ABS_MS_Data_Pack[4] = (y_hid >> 8) & 0xFF; // Y high byte
    
    // Wheel (8-bit signed relative)
    ABS_MS_Data_Pack[5] = (int8_t)data[6];  // Simpler cast for wheel

    // Send data immediately without additional processing delay
    // Check if endpoint is not busy before sending
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        USBFS_Endp_DataUp(DEF_UEP3, ABS_MS_Data_Pack, sizeof(ABS_MS_Data_Pack), DEF_UEP_CPY_LOAD);
        CH9329_SendAck(addr, cmd_code, DEF_CMD_SUCCESS);
    } else {
        CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_OPERATE);
    }
}


// -------------------- CH9329 Data Parser --------------------
void CH9329_DataParser(uint8_t* buf, uint8_t len) {
    uint8_t index = 0;

    while (index + 6 <= len) {
        if (buf[index] == CH9329_FRAME_HEAD1 && buf[index + 1] == CH9329_FRAME_HEAD2) {
            uint8_t frame_start = index;
            index += 2;

            uint8_t addr = buf[index++];
            uint8_t cmd_code = buf[index++];
            uint8_t data_len = buf[index++];

            if ((index + data_len + 1) > len) {
                break;  // Incomplete frame
            }

            uint8_t checksum = CH9329_FRAME_HEAD1 + CH9329_FRAME_HEAD2 + addr + cmd_code + data_len;
            uint8_t data[8] = {0};
            for (uint8_t i = 0; i < data_len && i < sizeof(data); i++) {
                data[i] = buf[index++];
                checksum += data[i];
            }

            uint8_t recv_sum = buf[index++];

            if (checksum != recv_sum) {
                CH9329_SendAck(addr, cmd_code, DEF_CMD_ERR_SUM);
                continue;  // Checksum error
            }

            // Handle Keyboard Data
            if (cmd_code == CMD_SEND_KB_GENERAL_DATA || cmd_code == CMD_SEND_KB_GAME_DATA) {
                CH9329_HandleKeyboard(addr, cmd_code, data, data_len);
            }
            // Handle Relative Mouse Data
            else if (cmd_code == CMD_SEND_MS_REL_DATA) {
                CH9329_HandleRelativeMouse(addr, cmd_code, data, data_len);
            } // Handle Absolute Mouse Data
            else if (cmd_code == CMD_SEND_MS_ABS_DATA) {
                CH9329_HandleAbsoluteMouse(addr, cmd_code, data, data_len);
            }

        } else {
            index++;  // Search for next frame header
        }
    }
}

// -------------------- Main Data Receive Handler --------------------
void USB_DataRx_To_KMHandle(void) {
    // Step 1: Read and parse from BLE ring buffer
    // while (RingMemBLE.CurrentLen > 0) {
    //     uint8_t resv[64];
    //     uint8_t len = (RingMemBLE.CurrentLen > sizeof(resv)) ? sizeof(resv) : RingMemBLE.CurrentLen;
    //     if (RingMemRead(&RingMemBLE, resv, len) == SUCCESS) {
    //         CH9329_DataParser(resv, len);
    //     } else {
    //         break;
    //     }
    // }

    // Step 2: Read and parse from UART2 transmission buffer
    while (Uart.Tx_RemainNum) {
        if (Uart.Tx_CurPackLen == 0x00) {
            Uart.Tx_CurPackLen = Uart.Tx_PackLen[Uart.Tx_DealNum];
            Uart.Tx_CurPackPtr = Uart.Tx_DealNum * DEF_USB_FS_PACK_LEN;
        }

        uint8_t uart_buf[32];
        uint8_t copy_len = (Uart.Tx_CurPackLen > sizeof(uart_buf)) ? sizeof(uart_buf) : Uart.Tx_CurPackLen;

        for (uint8_t i = 0; i < copy_len; i++) {
            uart_buf[i] = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
        }

        CH9329_DataParser(uart_buf, copy_len);

        Uart.Tx_CurPackLen -= copy_len;
        if (Uart.Tx_CurPackLen == 0) {
            Uart.Tx_PackLen[Uart.Tx_DealNum] = 0;
            Uart.Tx_DealNum = (Uart.Tx_DealNum + 1) % DEF_UARTx_TX_BUF_NUM_MAX;
            Uart.Tx_RemainNum--;
        }
    }
}
