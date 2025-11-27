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
#include "SD_SWITCH.h"
#include "DS18B20.h"
#include "km_ring.h"
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

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

 extern uint8_t sd_card_channel_state; 

/* CH9329_SendResponse enqueues the response packet instead of sending directly */
void CH9329_SendResponse(uint8_t addr, uint8_t cmd_code, uint8_t* pdata,
                         uint8_t len, uint8_t resp_mode)
{
    uint8_t packet[64];
    uint8_t index = 0;

    if (len > 58) len = 58; // max 58 bytes payload (64 - 6 header+checksum)
    packet[index++] = 0x57; // FRAME_HEAD1
    packet[index++] = 0xAB; // FRAME_HEAD2
    packet[index++] = addr;
    packet[index++] = cmd_code | (resp_mode ? 0x80 : 0xC0);
    packet[index++] = len;

    if (pdata && len > 0) {
        memcpy(&packet[index], pdata, len);
        index += len;
    }

    uint8_t checksum = 0;
    for (uint8_t i = 0; i < index; i++) checksum += packet[i];
    packet[index++] = checksum;

    Queue_Push_Response(packet, index);
}


// ------------------------------------------------------------
//      Dedicated "Get Info" Command Response
// ------------------------------------------------------------
void CH9329_Cmd_GetInfo_Reply(uint8_t addr)
{
    uint8_t data[8] = {0};

    /* Protocol requires a fixed 8-byte payload */

    data[0] = 0x00; // bit7 placeholder (status unused)
    data[1] = 0x00; // bit6 placeholder
    data[2] = 0x00; // bit5 placeholder
    data[3] = 0x00; // bit4 placeholder
    data[4] = 0x00; // bit3 placeholder
    data[5] = Keyboard_GetLEDStatus() & 0x07; // bit2-bit0: LED status

    /* Device connection / enumeration status */
    if (USBFS_DevConfig != 0 || USBFS_DevEnumStatus == 1) {
        data[6] = 0x01;   // Connected (enumerated)
    } else {
        data[6] = 0x00;   // Not connected
    }

    /* Firmware Version:
       - High nibble: product type
           0x00 = KVM Go
           0x40 = MiniKVM v2
       - Low nibble: KM firmware version number
    */
    data[7] = 0x40 | 0x02;    // Product type 0x00, version 0x02

    CH9329_SendResponse(addr, CMD_GET_INFO , data, 8, 1);
}


// -------------------- CH9329 Command Dispatcher --------------------
/*********************************************************************
 * @fn      CH9329_DispatchCommand
 *
 * @brief   Dispatch and handle CH9329 commands
 *
 * @param   addr - Device address
 * @param   cmd_code - Command code
 * @param   pdata - Pointer to command data
 * @param   data_len - Length of command data
 *
 * @return  none
 */
void CH9329_DispatchCommand(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t data_len)
{
    switch (cmd_code)
    {
        case CMD_GET_INFO:
            CH9329_Cmd_GetInfo_Reply(addr);
            break;

        case CMD_SEND_KB_GENERAL_DATA:
        {
            uint8_t st;
            if (data_len != 8) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 0);
            } else {
                Keyboard_HandleData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 1);
            }
        }
        break;

        case CMD_SEND_MS_ABS_DATA:
        {
            uint8_t st;
            if (data_len < 7) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 0);
            } else {
                Mouse_HandleAbsoluteData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 1);
            }
        }
        break;

        case CMD_SEND_MS_REL_DATA:
        {
            uint8_t st;
            if (data_len < 5) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 0);
            } else {
                Mouse_HandleRelativeData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 1);
            }
        }
        break;

        case CMD_SD_SWITCH:
            SD_USB_Switch(addr, cmd_code, pdata, data_len);
            break;

        case CMD_DS18B20_GET_TEMP:
            // Always pass length = 5 to DS18B20 command handler
            DS18B20_Command(addr, cmd_code, pdata, 5);
            break;

        case CMD_GET_PARA_CFG:
        {
            uint8_t response[] = {
                0x80, 0x80, 0x00, 0x00, 0x01, 0xC2, 
                0x00, 0x08, 0x00, 0x00, 0x03, 0x86, 
                0x1A, 0x29, 0xE1, 0x00, 0x00, 0x00, 
                0x01, 0x00, 0x0D, 0x00, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00
            };

            // Send parameter configuration response
            CH9329_SendResponse(addr, cmd_code, response, sizeof(response), 1);
        }
        break;

        default:
        {
            uint8_t st = STATUS_ERR_CMD;
            CH9329_SendResponse(addr, cmd_code, &st, 1, 0);
        }
        break;
    }
}

int CH9329_DataParser(uint8_t* buf, uint16_t len)
{
    // Check that buffer is at least long enough for header + minimal fields
    if (len < 6) return 0;

    // Verify frame header
    if (buf[0] != CH9329_FRAME_HEAD1 || buf[1] != CH9329_FRAME_HEAD2) {
        // Not a valid frame header ！ skip one byte and try again
        return 1;
    }

    uint8_t addr     = buf[2];
    uint8_t cmd_code = buf[3];
    uint8_t data_len = buf[4];
    uint16_t frame_total_len = 2 + 1 + 1 + 1 + (uint16_t)data_len + 1;

    // Wait until the full frame is received
    if (len < frame_total_len) return 0;

    uint8_t *pdata = &buf[5];
    uint8_t recv_sum = buf[5 + data_len];

    // Calculate checksum
    uint8_t sum = 0;
    for (uint16_t i = 0; i < frame_total_len - 1; i++) {
        sum += buf[i];
    }

    if (sum != recv_sum) {
        // Checksum mismatch ！ treat as invalid frame, skip full length
        return frame_total_len;
    }

    // Valid frame ！ dispatch command
    CH9329_DispatchCommand(addr, cmd_code, pdata, data_len);
    return frame_total_len;
}

static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_len = 0;

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    // Prevent buffer overflow; if overflow risk, retain only the latter half
    if (rx_len + len > RX_BUF_SIZE) {
        uint16_t keep = RX_BUF_SIZE / 2;
        if (keep > rx_len) keep = rx_len;
        memmove(rx_buf, rx_buf + (rx_len - keep), keep);
        rx_len = keep;
    }

    // Append new data
    memcpy(rx_buf + rx_len, data, len);
    rx_len += len;

    // Try parsing frames as long as data exists
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf, rx_len);
        if (consumed < 0) {
            // Parsing error ！ clear buffer
            rx_len = 0;
            break;
        } else if (consumed == 0) {
            // Incomplete frame ！ wait for more data
            break;
        } else {
            // Remove consumed bytes from buffer
            if ((uint16_t)consumed < rx_len) {
                memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
                rx_len -= (uint16_t)consumed;
            } else {
                // Exactly consumed all data
                rx_len = 0;
                break;
            }
        }
    }
}

void USB_DataRx_To_KMHandle(void) {
    // Step 1: Handle data from BLE ring buffer
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t temp[64];
        uint8_t len = (RingMemBLE.CurrentLen > sizeof(temp)) ? sizeof(temp) : (uint8_t)RingMemBLE.CurrentLen;
        if (RingMemRead(&RingMemBLE, temp, len) == SUCCESS) {
            CH9329_RxBuffer_Add(temp, len);
        } else {
            break;
        }
    }

    // Step 2: Handle data from UART2 transmit buffer
    while (Uart.Tx_RemainNum) {
        if (Uart.Tx_CurPackLen == 0x00) {
            Uart.Tx_CurPackLen = Uart.Tx_PackLen[Uart.Tx_DealNum];
            Uart.Tx_CurPackPtr = Uart.Tx_DealNum * DEF_USB_FS_PACK_LEN;
        }

        uint8_t temp[255];
        uint8_t copy_len = (Uart.Tx_CurPackLen > sizeof(temp)) ? sizeof(temp) : (uint8_t)Uart.Tx_CurPackLen;

        for (uint16_t i = 0; i < copy_len; i++) {
            temp[i] = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
        }

        CH9329_RxBuffer_Add(temp, copy_len);

        Uart.Tx_CurPackLen -= copy_len;
        if (Uart.Tx_CurPackLen == 0) {
            Uart.Tx_PackLen[Uart.Tx_DealNum] = 0;
            Uart.Tx_DealNum = (Uart.Tx_DealNum + 1) % DEF_UARTx_TX_BUF_NUM_MAX;
            Uart.Tx_RemainNum--;
        }
    }
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