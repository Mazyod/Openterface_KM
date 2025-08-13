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
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"

/*******************************************************************************/
/* Global Variable Definition */

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

/*********************************************************************
 * @fn      CH9329_SendAck
 *
 * @brief   Send ACK packet to CH9329
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   status - Status byte
 *
 * @return  none
 */
void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status) {
    // // Check if there's space in the TX buffer for a new packet
    // if (Uart.Tx_RemainNum >= DEF_UARTx_TX_BUF_NUM_MAX) {
    //     return; // Buffer full, cannot send ACK
    // }
    
    // uint8_t ack_cmd = cmd_code | 0x80;  // Set bit 7 to indicate ACK response
    // uint8_t ack_packet[7] = {CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, addr, ack_cmd, 0x01, status, 0};
    
    // // Calculate checksum for first 6 bytes
    // for (int i = 0; i < 6; i++) {
    //     ack_packet[6] += ack_packet[i];
    // }
    
    // // Disable interrupts to prevent race conditions during buffer manipulation
    // __disable_irq();
    
    // // Double-check buffer availability after disabling interrupts
    // if (Uart.Tx_RemainNum >= DEF_UARTx_TX_BUF_NUM_MAX) {
    //     __enable_irq();
    //     return; // Buffer became full
    // }
    
    // // Calculate the buffer position for the new packet
    // uint16_t load_index = Uart.Tx_LoadNum % DEF_UARTx_TX_BUF_NUM_MAX;
    // uint16_t buffer_offset = load_index * DEF_USB_FS_PACK_LEN;
    
    // // Ensure we don't exceed buffer bounds
    // if (buffer_offset + 7 <= DEF_UARTx_TX_BUF_LEN) {
    //     // Copy ACK packet to UART TX buffer
    //     for (int i = 0; i < 7; i++) {
    //         UART2_Tx_Buf[buffer_offset + i] = ack_packet[i];
    //     }
        
    //     // Update packet length for this buffer slot
    //     Uart.Tx_PackLen[load_index] = 7;
        
    //     // Update control structure
    //     Uart.Tx_LoadNum++;
    //     Uart.Tx_RemainNum++;
    // }
    
    // __enable_irq();
    
    // // Trigger transmission if UART is not currently busy
    // if (Uart.Tx_Flag == 0) {
    //     UART2_DataTx_Deal();
    // }
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
                Keyboard_HandleData(addr, cmd_code, data, data_len);
            }
            // Handle Relative Mouse Data
            else if (cmd_code == CMD_SEND_MS_REL_DATA) {
                Mouse_HandleRelativeData(addr, cmd_code, data, data_len);
            } // Handle Absolute Mouse Data
            else if (cmd_code == CMD_SEND_MS_ABS_DATA) {
                Mouse_HandleAbsoluteData(addr, cmd_code, data, data_len);
            }

        } else {
            index++;  // Search for next frame header
        }
    }
}

// -------------------- Main Data Receive Handler --------------------
void USB_DataRx_To_KMHandle(void) {
    // Step 1: Read and parse from BLE ring buffer
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t resv[64];
        uint8_t len = (RingMemBLE.CurrentLen > sizeof(resv)) ? sizeof(resv) : RingMemBLE.CurrentLen;
        if (RingMemRead(&RingMemBLE, resv, len) == SUCCESS) {
            CH9329_DataParser(resv, len);
        } else {
            break;
        }
    }

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
#define TOUCH_ENDPOINT DEF_UEP2

void SendTouchPoint(uint8_t endp, uint8_t tip_switch, uint16_t x, uint16_t y) {
    uint8_t report[5];
    report[0] = tip_switch ? 0x01 : 0x00;
    report[1] = (uint8_t)(x & 0xFF);
    report[2] = (uint8_t)((x >> 8) & 0xFF);
    report[3] = (uint8_t)(y & 0xFF);
    report[4] = (uint8_t)((y >> 8) & 0xFF);

    USBFS_Endp_DataUp(endp, report, sizeof(report), DEF_UEP_CPY_LOAD);
}

// -------------------- Keyboard Compatibility Functions --------------------
// These functions provide backward compatibility for existing code

/*********************************************************************
 * @fn      CH9329_SetAutoReleaseMode
 *
 * @brief   Configure keyboard auto-release behavior (backward compatibility)
 *
 * @param   sender_handles_release - true if sender will handle releases, false for auto-release
 *
 * @return  none
 */
void CH9329_SetAutoReleaseMode(bool sender_handles_release) {
    Keyboard_SetAutoReleaseMode(sender_handles_release);
}

/*********************************************************************
 * @fn      CH9329_GetAutoReleaseMode
 *
 * @brief   Get current keyboard auto-release mode (backward compatibility)
 *
 * @return  true if sender handles releases, false if auto-release is enabled
 */
bool CH9329_GetAutoReleaseMode(void) {
    return Keyboard_GetAutoReleaseMode();
}

/*********************************************************************
 * @fn      CH9329_ResetAutoDetection
 *
 * @brief   Reset auto-detection and re-enable auto-release mode (backward compatibility)
 *
 * @return  none
 */
void CH9329_ResetAutoDetection(void) {
    Keyboard_ResetAutoDetection();
}

/*********************************************************************
 * @fn      Keyboard_SetLEDStatus (Backward Compatibility)
 *
 * @brief   Set keyboard LED status (backward compatibility wrapper)
 *
 * @param   led_status - LED status byte
 *
 * @return  none
 */
void KB_SetLEDStatus(uint8_t led_status) {
    Keyboard_SetLEDStatus(led_status);
}

/*********************************************************************
 * @fn      Keyboard_GetLEDStatus (Backward Compatibility)
 *
 * @brief   Get current keyboard LED status (backward compatibility wrapper)
 *
 * @return  Current LED status byte
 */
uint8_t KB_GetLEDStatus(void) {
    return Keyboard_GetLEDStatus();
}

// -------------------- Mouse Compatibility Functions --------------------
// These functions provide backward compatibility for existing code

/*********************************************************************
 * @fn      CH9329_HandleRelativeMouse (Backward Compatibility)
 *
 * @brief   Handle relative mouse data (backward compatibility wrapper)
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Mouse data
 * @param   data_len - Length of data
 *
 * @return  none
 */
void CH9329_HandleRelativeMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    Mouse_HandleRelativeData(addr, cmd_code, data, data_len);
}

/*********************************************************************
 * @fn      CH9329_HandleAbsoluteMouse (Backward Compatibility)
 *
 * @brief   Handle absolute mouse data (backward compatibility wrapper)
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   data - Mouse data
 * @param   data_len - Length of data
 *
 * @return  none
 */
void CH9329_HandleAbsoluteMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    Mouse_HandleAbsoluteData(addr, cmd_code, data, data_len);
}