#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
#include "usb_pwr.h"
#include "peripheral.h"
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"
#include "sd_switch.h"
#include "CH32_TEMP.h"
#include "km_ring.h"
#include "Version_selection.h"
#include "include/usb_mode_config.h"

/* External variable from ch32v20x_usbfs_device.c - needs to be reset after STOP mode wakeup */
extern volatile uint8_t USBFS_DevSleepStatus;
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

    /* Fully re-initialize USBFS (all registers lost during STOP mode) */
    USBFS_Device_Init(ENABLE);

    /* Reset sleep status to prevent immediate re-entry into STOP mode.
     * Without this, USBFS_DevSleepStatus remains 0x03 after wakeup,
     * causing the next SUSPEND interrupt to trigger STOP mode again,
     * leading to enumeration failures. */
    USBFS_DevSleepStatus = 0;

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

/* CH9329_SendResponse enqueues the response packet instead of sending directly.
 * When BLE CH9329 response routing is enabled (via Peripheral_SetBleCh9329Response),
 * the response is sent via BLE notification instead of USB EP3 queue. */
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

    USB_DEBUG("SendResp: cmd=%02X plen=%d\n", cmd_code, index);

#ifdef ENABLE_BLE
    /* If response is for a BLE CH9329 command, send via BLE notification */
    if (Peripheral_GetBleCh9329Response())
    {
        app_usb_notify(packet, index);
        return;
    }
#endif

    Queue_Push_Response(packet, index);
}
//      Dedicated "Get Info" Command Response
void CH9329_Cmd_GetInfo_Reply(uint8_t addr)
{
    uint8_t data[8] = {0};

    USB_DEBUG("GetInfo: addr=%d\n", addr);

    /* Protocol requires a fixed 8-byte payload */

    /* Hardware selector: mirrors the board version (1 = KVM-GO,
     * 2 = MiniKVM, 3 = KeyMod). */
    data[0] = version;
    /* Device connection / enumeration status */
    if (USBFS_Connect == 1) {
        data[1] = 0x01;   // Connected (enumerated)
    } else {
        data[1] = 0x00;   // Not connected
    }
    data[2] = Keyboard_GetLEDStatus() & 0x07; // bit2-bit0: LED status
    data[3] = 0x06; // firmware version number
    data[4] = 0x00; // reserved
    data[5] = 0x00; // reserved
    data[6] = 0x00; // reserved
    data[7] = 0x00; // reserved
    CH9329_SendResponse(addr, CMD_GET_INFO , data, 8, 1);
    USB_DEBUG("GetInfo: response queued\n");
}

static void CH9329_Cmd_USBWorkMode(uint8_t addr, uint8_t cmd_code, uint8_t *pdata, uint8_t data_len)
{
    uint8_t response[12];
    uint16_t flags;
    uint32_t capability_mask;
    uint8_t status = STATUS_SUCCESS;

    if (data_len > 0) {
        switch (pdata[0]) {
        case 0x00:
            break;
        case 0x01:
            if (data_len < 2) {
                status = STATUS_ERR_PARAM;
            }
            else {
                uint16_t requested_flags = 0;
                if (data_len >= 4) {
                    requested_flags = (uint16_t)pdata[2] | ((uint16_t)pdata[3] << 8);
                }
                if (!USB_ModeConfig_Write(pdata[1], requested_flags)) {
                    status = STATUS_ERR_PARAM;
                }
            }
            break;
        case 0x02:
            if (!USB_ModeConfig_Clear()) {
                status = STATUS_ERR_FRAME;
            }
            break;
        case 0x03:
            CH9329_SendResponse(addr, cmd_code, &status, 1, 1);
            USB_ModeConfig_Reboot();
            return;
        default:
            status = STATUS_ERR_PARAM;
            break;
        }
    }

    flags = USB_ModeConfig_GetFlags();
    capability_mask = USB_ModeConfig_GetCapabilityMask();
    response[0] = status;
    response[1] = USB_ModeConfig_GetCompiledMode();
    response[2] = USB_ModeConfig_GetActiveMode();
    response[3] = USB_ModeConfig_GetStoredMode();
    response[4] = USB_ModeConfig_GetStatus();
    response[5] = (uint8_t)flags;
    response[6] = (uint8_t)(flags >> 8);
    response[7] = (uint8_t)capability_mask;
    response[8] = (uint8_t)(capability_mask >> 8);
    response[9] = (uint8_t)(capability_mask >> 16);
    response[10] = (uint8_t)(capability_mask >> 24);
    response[11] = 0;
    CH9329_SendResponse(addr, cmd_code, response, sizeof(response), 1);
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
    USB_DEBUG("Dispatch: cmd=%02X addr=%02X dlen=%d\n", cmd_code, addr, data_len);
    switch (cmd_code)
    {   
        //cmd=01,Obtain information such as chip version
        case CMD_GET_INFO:
            CH9329_Cmd_GetInfo_Reply(addr);
            break;
        //cmd=02,Send USB keyboard regular data
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
        //cmd=03,Send USB media keyboard data
        case CMD_SEND_KB_MEDIA_DATA:
        {
            uint8_t st;
            if (data_len < 2) {
                st = STATUS_ERR_PARAM;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 0);
            } else {
                Keyboard_HandleMediaData(addr, cmd_code, pdata, data_len);
                st = STATUS_SUCCESS;
                CH9329_SendResponse(addr, cmd_code, &st, 1, 1);
            }
        }
        break;
        //cmd=04,Send USB absolute mouse data
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
        //cmd=05,Send USB relative mouse data
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
        //cmd=17,Switch and return SD card status
        case CMD_SD_SWITCH:
            SD_USB_Switch(addr, cmd_code, pdata, data_len);
            break;
        //cmd=18,Return temperture
        case CMD_DS18B20_GET_TEMP:
            // Always pass length = 5 to DS18B20 command handler
            Temp_Command(addr, cmd_code, pdata, 5);
            break;
        case CMD_USB_WORK_MODE:
            CH9329_Cmd_USBWorkMode(addr, cmd_code, pdata, data_len);
            break;
        //cmd=22,BLE security control (pairing window / bond management)
        case CMD_BLE_SECURITY:
#ifdef ENABLE_BLE
            Peripheral_SecurityCommand(addr, cmd_code, pdata, data_len);
#else
        {
            uint8_t err = STATUS_ERR_CMD;
            CH9329_SendResponse(addr, cmd_code, &err, 1, 0);
        }
#endif
            break;
        //cmd=08,Get parameter configuration(Only used for matching with ch9329)
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
    /* Check that buffer is at least long enough for header + minimal fields */
    if (len < 6) return 0;

    USB_DEBUG("DataParser: len=%d buf[0]=%02X buf[1]=%02X\n", len, buf[0], buf[1]);

    /* Verify frame header */
    if (buf[0] != CH9329_FRAME_HEAD1 || buf[1] != CH9329_FRAME_HEAD2) {
        /* Not a valid frame header -- skip one byte and try to resync */
        USB_DEBUG("DataParser: bad header\n");
        return 1;
    }

    uint8_t addr     = buf[2];
    uint8_t cmd_code = buf[3];
    uint8_t data_len = buf[4];

    USB_DEBUG("DataParser: addr=%02X cmd=%02X len=%d\n", addr, cmd_code, data_len);

    /* Bounds check: protocol max payload is CH9329_MAX_DATA_LEN (100).
     * Reject oversized payloads early to prevent out-of-bounds reads
     * in the checksum loop and buf[5 + data_len] access below. */
    if (data_len > CH9329_MAX_DATA_LEN) {
        return 1;  /* invalid payload length -- skip header byte and resync */
    }

    uint16_t frame_total_len = 2 + 1 + 1 + 1 + (uint16_t)data_len + 1;

    /* Sanity: frame must fit in the receive buffer */
    if (frame_total_len > RX_BUF_SIZE) {
        return 1;  /* corrupted length field -- skip header byte and resync */
    }

    /* Wait until the full frame is received */
    if (len < frame_total_len) return 0;

    uint8_t *pdata = &buf[5];
    uint8_t recv_sum = buf[5 + data_len];

    // Calculate checksum
    uint8_t sum = 0;
    for (uint16_t i = 0; i < frame_total_len - 1; i++) {
        sum += buf[i];
    }

    if (sum != recv_sum) {
        // Checksum mismatch �� treat as invalid frame, skip full length
        USB_DEBUG("DataParser: bad checksum sum=%02X recv=%02X\n", sum, recv_sum);
        return frame_total_len;
    }

    // Valid frame �� dispatch command
    USB_DEBUG("DataParser: dispatching cmd=%02X\n", cmd_code);
    CH9329_DispatchCommand(addr, cmd_code, pdata, data_len);
    return frame_total_len;
}

static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_start = 0;  /* offset of the first unparsed byte */
static uint16_t rx_len = 0;    /* number of valid bytes from rx_start */

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    /* Callers span USB ISR (EP5_OUT_Callback), a BLE callback, the ECM frame
     * handler and the main-loop UART2 path — protect the shared static buffer.
     * The parser below may Queue_Push_Response; that nested ENTER_CRITICAL is
     * safe because the macros save/restore MSTATUS. */
    ENTER_CRITICAL();

    /* Reset the cursor whenever the buffer empties so rx_start cannot drift to
     * the end of the array across many small frames. */
    if (rx_len == 0) rx_start = 0;

    /* Compact only when the tail cannot hold the new bytes:
     * at most one memmove per Add (amortized O(1)). */
    if ((uint32_t)rx_start + rx_len + len > RX_BUF_SIZE) {
        if ((uint32_t)rx_len + len > RX_BUF_SIZE) {
            /* Still too big even after compaction: drop the oldest bytes so the
             * newest data survives. */
            uint16_t drop = (uint16_t)((uint32_t)rx_len + len - RX_BUF_SIZE);
            rx_start += drop;
            rx_len -= drop;
        }
        if (rx_len > 0) memmove(rx_buf, rx_buf + rx_start, rx_len);
        rx_start = 0;
    }

    /* Append new data */
    memcpy(rx_buf + rx_start + rx_len, data, len);
    rx_len += len;

    /* Parse frame by frame: a consumed frame only advances rx_start — no
     * shuffling. CH9329_DataParser runs on contiguous memory and is unchanged. */
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf + rx_start, rx_len);
        if (consumed < 0) {
            /* Parsing error — clear buffer */
            rx_start = 0;
            rx_len = 0;
            break;
        } else if (consumed == 0) {
            /* Incomplete frame — wait for more data */
            break;
        } else {
            rx_start += (uint16_t)consumed;
            rx_len -= (uint16_t)consumed;
        }
    }

    EXIT_CRITICAL();
}

void USB_DataRx_To_KMHandle(void) {
    // Step 1: Handle data from BLE ring buffer
#ifdef ENABLE_BLE
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t temp[64];
        uint8_t len = (RingMemBLE.CurrentLen > sizeof(temp)) ? sizeof(temp) : (uint8_t)RingMemBLE.CurrentLen;
        if (RingMemRead(&RingMemBLE, temp, len) == SUCCESS) {
            CH9329_RxBuffer_Add(temp, len);
        } else {
            break;
        }
    }
#endif /* ENABLE_BLE */

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

        USB_DEBUG("DataRx: UART2 len=%d b0=%02X b1=%02X\n", copy_len, temp[0], temp[1]);
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