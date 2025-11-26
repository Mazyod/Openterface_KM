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

// extern uint8_t sd_card_channel_state; 

// void CH9329_SendResponse(uint8_t addr, uint8_t cmd_code, uint8_t* pdata,
//                          uint8_t len, uint8_t resp_mode)
// {
//     uint8_t packet[100];
//     uint8_t index = 0;

//     /* Optional: protect against oversized payload */
//     if (len > CH9329_MAX_DATA_LEN) {
//         /* Truncate to the maximum allowed payload length */
//         len = CH9329_MAX_DATA_LEN;
//     }

//     packet[index++] = CH9329_FRAME_HEAD1;
//     packet[index++] = CH9329_FRAME_HEAD2;
//     packet[index++] = addr;

//     /* Select mask:
//        resp_mode == 1 → OR with 0x80
//        resp_mode == 0 → OR with 0x0C
//     */
//     uint8_t mask = (resp_mode ? 0x80 : 0xC0);

//     /* Optional:
//        Clear existing bits in cmd_code to avoid duplicate OR operations.
//        For example: cmd_code &= ~(0xC0u | 0x0Fu);
//        (Actual required masks depend on protocol definition.)
//        Not applied here based on your original intention.
//     */
//     uint8_t resp_cmd = (cmd_code | mask);
//     packet[index++] = resp_cmd;

//     packet[index++] = len;

//     if (pdata != NULL && len > 0) {
//         memcpy(&packet[index], pdata, len);
//         index += len;
//     }

//     /* Calculate checksum for all preceding bytes */
//     uint8_t checksum = 0;
//     for (uint8_t i = 0; i < index; i++) {
//         checksum += packet[i];
//     }
//     packet[index++] = checksum;

//     /* Send the packet (same interface as your original implementation) */
//     USBD_ENDPx_DataUp(ENDP3, packet, index);
// }



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
                CH9329_SendResponse(addr, cmd_code, &st, 1,0);
            } else {
                Keyboard_HandleData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1,1);
            }
        }
        break;

        case CMD_SEND_MS_ABS_DATA:
        {
            uint8_t st;
            if (data_len < 7) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1,0);
            } else {
                Mouse_HandleAbsoluteData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1,1);
            }
        }
        break;

        case CMD_SEND_MS_REL_DATA:
        {
            uint8_t st;
            if (data_len < 5) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1,0);
            } else {
                Mouse_HandleRelativeData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1,1);
            }
        }
        break;

        case CMD_SD_SWITCH:
            SD_USB_Switch(addr, cmd_code, pdata, data_len);
            break;

        case CMD_DS18B20_GET_TEMP:
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

            // 发送响应
            CH9329_SendResponse(addr, cmd_code, response, sizeof(response), 1);
        }
        break;
        default:
        {
            uint8_t st = STATUS_ERR_CMD;
            CH9329_SendResponse(addr, cmd_code, &st, 1,0);
        }
        break;
    }

}

int CH9329_DataParser(uint8_t* buf, uint16_t len)
{
    uint16_t index = 0;

    // 最小帧长： head(2) + addr(1) + cmd(1) + len(1) + checksum(1) = 6
    while (index + 6 <= len)
    {
        // 找到帧头
        if (buf[index] == CH9329_FRAME_HEAD1 && buf[index + 1] == CH9329_FRAME_HEAD2)
        {
            // 可能的帧起点
            if (index + 5 > len) {
                // 虽然找到了头，但连长度字段都还不完整，等待更多数据
                break;
            }

            uint8_t addr     = buf[index + 2];
            uint8_t cmd_code = buf[index + 3];
            uint8_t data_len = buf[index + 4];
            uint16_t frame_total_len = 2 + 1 + 1 + 1 + (uint16_t)data_len + 1; // head+addr+cmd+len+data+chk

            // 如果整个帧还没到齐 -> 等待更多数据（不要回复错误）
            if (index + frame_total_len > len) {
                break;
            }

            uint8_t *pdata = &buf[index + 5];
            uint8_t recv_sum = buf[index + 5 + data_len];

            // 计算校验和（对 frame_total_len - 1 字节求和）
            uint8_t sum = 0;
            for (uint16_t i = 0; i < frame_total_len - 1; i++) {
                sum += buf[index + i];
            }

            if (sum != recv_sum)
            {
                // 校验错误：回复错误并把该帧视为已消费（以便继续同步下一帧）
                uint8_t st = STATUS_ERR_CHECKSUM;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 0);

                // 跳过这个帧（认为已被处理），继续解析下一个可能的帧
                index += frame_total_len;
                continue;
            }

            // 校验通过 -> 分发
            CH9329_DispatchCommand(addr, cmd_code, pdata, data_len);

            // 标记已消费
            index += frame_total_len;
        }
        else
        {
            // 如果不是帧头，单字节跳过以寻找帧头（同步恢复）
            index++;
        }
    }

    // index 是已消费（解析或跳过）的字节数
    return (int)index;
}
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_len = 0;

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    // 如果可用空间不足，尝试腾出空间（保留最近的数据）
    if (rx_len + len > RX_BUF_SIZE) {
        // 尽量保留最近的未处理部分，丢弃最老的一半（保守策略）
        // 更好的办法是增大 RX_BUF_SIZE 或减少上游发送速率
        uint16_t keep = RX_BUF_SIZE / 2;
        if (keep > rx_len) keep = rx_len;
        memmove(rx_buf, rx_buf + (rx_len - keep), keep);
        rx_len = keep;
    }

    // 追加新数据
    memcpy(rx_buf + rx_len, data, len);
    rx_len += len;

    // 循环解析尽量多的完整帧，保留未完成的尾部
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf, rx_len);
        if (consumed < 0) {
            // 致命错误：清空缓冲避免死循环（可以改为更复杂的恢复策略）
            rx_len = 0;
            break;
        } else if (consumed == 0) {
            // 没有完整帧可处理（尾部不完整），等待后续数据
            break;
        } else {
            // 移除已处理 bytes
            if ((uint16_t)consumed < rx_len) {
                memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
                rx_len -= (uint16_t)consumed;
            } else {
                // 恰好全部处理
                rx_len = 0;
                break;
            }
        }
    }
}


void USB_DataRx_To_KMHandle(void) {
    // Step 1: BLE
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t temp[64];
        uint8_t len = (RingMemBLE.CurrentLen > sizeof(temp)) ? sizeof(temp) : (uint8_t)RingMemBLE.CurrentLen;
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