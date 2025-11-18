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
#include "sd_switch.h"
#include "DS18B20.h"
#include <stdbool.h>
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
#define CH9329_MAX_DATA_LEN 8
#define CH9329_FRAME_HEAD1      0x57
#define CH9329_FRAME_HEAD2      0xAB

#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_KB_GAME_DATA    0x12
#define CMD_SEND_MS_ABS_DATA    0x04
#define CMD_SEND_MS_REL_DATA    0x05
#define CMD_SD_SWITCH  0x17
#define CMD_DS18B20_GET_TEMP   0x18
// ACK Status Codes
#define STATUS_SUCCESS        0x00
#define STATUS_ERR_TIMEOUT    0xE1
#define STATUS_ERR_HEADER     0xE2
#define STATUS_ERR_CMD        0xE3
#define STATUS_ERR_CHECKSUM   0xE4
#define STATUS_ERR_PARAM      0xE5
#define STATUS_ERR_FRAME      0xE6  // Custom frame format error, frame exception, execution failed

// Command code definitions
#define CMD_GET_INFO  0x01

// Status: Normal/Exception
#define STATUS_OK     0    // No dedicated status byte in this example, protocol uses command code high bits to distinguish exceptions
extern uint8_t sd_card_channel_state; 
void CH9329_SendResponse(uint8_t addr, uint8_t cmd_code, uint8_t* pdata, uint8_t len)
{
    uint8_t packet[32];
    uint8_t index = 0;

    packet[index++] = CH9329_FRAME_HEAD1;
    packet[index++] = CH9329_FRAME_HEAD2;
    packet[index++] = addr;

    uint8_t resp_cmd = cmd_code | 0x80;
    packet[index++] = resp_cmd;

    packet[index++] = len;

    if (pdata != NULL && len > 0) {
        memcpy(&packet[index], pdata, len);
        index += len;
    }

    // Calculate checksum
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < index; i++) {
        checksum += packet[i];
    }
    packet[index++] = checksum;

    USBD_ENDPx_DataUp(ENDP3, packet, index);
}



// Dedicated "Get Info" response function
void CH9329_Cmd_GetInfo_Reply(uint8_t addr)
{
    uint8_t data[8] = {0};

    // Protocol payload fixed 8 bytes
    data[0] = 0x00; // status (not used but placeholder)
    data[1] = 0x00;
    data[2] = 0x00;
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = Keyboard_GetLEDStatus() & 0x07;
    data[6] = 0x01;
    data[7] = 0x10;

    CH9329_SendResponse(addr, CMD_GET_INFO, data, 8);
}


void CH9329_Cmd_KB_General_Reply(uint8_t addr, uint8_t recv_cmd, uint8_t status)
{
    uint8_t data[1] = { status };
    CH9329_SendResponse(addr, recv_cmd, data, 1);
}

void CH9329_Cmd_MS_Abs_Reply(uint8_t addr, uint8_t recv_cmd, uint8_t status)
{
    uint8_t data[1] = { status };
    CH9329_SendResponse(addr, recv_cmd, data, 1);
}

void CH9329_Cmd_MS_Rel_Reply(uint8_t addr, uint8_t recv_cmd, uint8_t status)
{
    uint8_t data[1] = { status };
    CH9329_SendResponse(addr, recv_cmd, data, 1);
}


// -------------------- CH9329 Data Parser --------------------
void CH9329_DataParser(uint8_t* buf, uint8_t len)
{
    uint8_t index = 0;

    while (index + 6 <= len)  // Minimum frame length check
    {
        if (buf[index] == CH9329_FRAME_HEAD1 && buf[index + 1] == CH9329_FRAME_HEAD2)
        {
            uint8_t addr     = buf[index + 2];
            uint8_t cmd_code = buf[index + 3];
            uint8_t data_len = buf[index + 4];
            uint16_t frame_total_len = 2 + 1 + 1 + 1 + data_len + 1;

            if (index + frame_total_len > len)
            {
                uint8_t st = STATUS_ERR_FRAME;
                CH9329_SendResponse(addr, cmd_code, &st, 1);
                break;
            }

            uint8_t *pdata = &buf[index + 5];
            uint8_t recv_sum = buf[index + 5 + data_len];

            // Checksum
            uint8_t sum = 0;
            for (uint8_t i = 0; i < frame_total_len - 1; i++)
                sum += buf[index + i];

            if (sum != recv_sum)
            {
                uint8_t st = STATUS_ERR_CHECKSUM;
                CH9329_SendResponse(addr, cmd_code, &st, 1 );
                index += frame_total_len;
                continue;
            }

            /* ======== Command dispatch ======== */
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
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    } else {
        Keyboard_HandleData(addr, cmd_code, pdata, data_len);
        st = STATUS_SUCCESS;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    }
}
break;

case CMD_SEND_MS_ABS_DATA:
{
    uint8_t st;
    if (data_len < 7) {
        st = STATUS_ERR_PARAM;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    } else {
        Mouse_HandleAbsoluteData(addr, cmd_code, pdata, data_len);
        st = STATUS_SUCCESS;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    }
}
break;

case CMD_SEND_MS_REL_DATA:
{
    uint8_t st;
    if (data_len < 5) {
        st = STATUS_ERR_PARAM;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    } else {
        Mouse_HandleRelativeData(addr, cmd_code, pdata, data_len);
        st = STATUS_SUCCESS;
        CH9329_SendResponse(addr, cmd_code, &st, 1);
    }
}
break;

case CMD_SD_SWITCH:
    SD_USB_Switch(addr, cmd_code, pdata, data_len);
    break;

case CMD_DS18B20_GET_TEMP:
    DS18B20_Command(addr, cmd_code, pdata, 5);
    break;

default:
{
    uint8_t st = STATUS_ERR_CMD;
    CH9329_SendResponse(addr, cmd_code, &st, 1);
}
break;

            }

            index += frame_total_len;
        }

        else
        {
            index++;
        }
    }
}


#define RX_BUF_SIZE   256

static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_len = 0;

// Add data to buffer and try to parse
void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    // If remaining space is insufficient, shift and clean
    if (rx_len + len > RX_BUF_SIZE) {
        // Move unprocessed data forward
        memmove(rx_buf, rx_buf + (rx_len - (RX_BUF_SIZE/2)), RX_BUF_SIZE/2);
        rx_len = RX_BUF_SIZE/2;
    }

    memcpy(rx_buf + rx_len, data, len);
    rx_len += len;

    // Try to parse
    CH9329_DataParser(rx_buf, rx_len);

    // Delete processed content (assume index advanced after parsing)
    // For simplicity, clear all here; can enhance to "remove N bytes processed" logic
    rx_len = 0;
}

// Replace the call in USB_DataRx_To_KMHandle with:
void USB_DataRx_To_KMHandle(void) {
    // Step 1: BLE
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t temp[64];
        uint8_t len = (RingMemBLE.CurrentLen > sizeof(temp)) ? sizeof(temp) : RingMemBLE.CurrentLen;
        if (RingMemRead(&RingMemBLE, temp, len) == SUCCESS) {
            CH9329_RxBuffer_Add(temp, len);
        } else {
            break;
        }
    }

    // Step 2: UART2
    while (Uart.Tx_RemainNum) {
        if (Uart.Tx_CurPackLen == 0x00) {
            Uart.Tx_CurPackLen = Uart.Tx_PackLen[Uart.Tx_DealNum];
            Uart.Tx_CurPackPtr = Uart.Tx_DealNum * DEF_USB_FS_PACK_LEN;
        }

        uint8_t temp[32];
        uint8_t copy_len = (Uart.Tx_CurPackLen > sizeof(temp)) ? sizeof(temp) : Uart.Tx_CurPackLen;

        for (uint8_t i = 0; i < copy_len; i++) {
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