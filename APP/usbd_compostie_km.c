
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
#define CMD_SEND_MS_REL_DATA    0x05

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

uint8_t mod_keys = 0;
uint8_t normal_keys[6] = {0};

// -------------------- ACK Packet Sender --------------------
void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status) {
    uint8_t ack_packet[7] = {CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, addr, cmd_code, 0x01, status, 0};
    for (int i = 0; i < 6; i++) {
        ack_packet[6] += ack_packet[i];
    }
    USBD_ENDPx_DataUp(ENDP3, ack_packet, sizeof(ack_packet));
}

// -------------------- Keyboard Handler --------------------
void CH9329_HandleKeyboard(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    static uint8_t kb_last_data[8] = {0};
    static uint8_t kb_repeat_count = 0;
    static bool kb_long_press = 0;

    if (data_len != 8) return;

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
    CH9329_SendAck(addr, cmd_code, 0x00);

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

// -------------------- Mouse Handler --------------------
void CH9329_HandleMouse(uint8_t addr, uint8_t cmd_code, uint8_t* data, uint8_t data_len) {
    if (data_len < 5) return;

    memset(MS_Data_Pack, 0x00, sizeof(MS_Data_Pack));
    MS_Data_Pack[0] = data[1];  // Mouse buttons
    MS_Data_Pack[1] = (data[2] & 0x80) ? (int8_t)(data[2] - 256) : data[2];  // X movement
    MS_Data_Pack[2] = (data[3] & 0x80) ? (int8_t)(data[3] - 256) : data[3];  // Y movement
    MS_Data_Pack[3] = (data[4] & 0x80) ? (int8_t)(data[4] - 256) : data[4];  // Wheel

    USBFS_Endp_DataUp(DEF_UEP2, MS_Data_Pack, sizeof(MS_Data_Pack), DEF_UEP_CPY_LOAD);
    CH9329_SendAck(addr, cmd_code, 0x00);
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
                continue;  // Checksum error
            }

            // Handle Keyboard Data
            if (cmd_code == CMD_SEND_KB_GENERAL_DATA || cmd_code == CMD_SEND_KB_GAME_DATA) {
                CH9329_HandleKeyboard(addr, cmd_code, data, data_len);
            }
            // Handle Mouse Data
            else if (cmd_code == CMD_SEND_MS_REL_DATA) {
                CH9329_HandleMouse(addr, cmd_code, data, data_len);
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
