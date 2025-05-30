
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

// -------------------- 协议定义区 --------------------
#define CH9329_FRAME_HEAD1      0x57
#define CH9329_FRAME_HEAD2      0xAB

#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_MS_REL_DATA     0x05
#define CMD_SEND_MS_ABS_DATA     0x04  // 绝对鼠标数据命令码

// 修饰键位定义
#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

// -------------------- 数据缓冲区 --------------------
uint8_t KB_Data_Pack[8];
uint8_t MS_Data_Pack[4];

uint8_t mod_keys = 0;
uint8_t normal_keys[6] = {0};

// -------------------- 应答函数 --------------------
void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status) {
    uint8_t ack_packet[7] = {CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, addr, cmd_code, 0x01, status, 0};
    for (int i = 0; i < 6; i++) {
        ack_packet[6] += ack_packet[i];
    }
    USBD_ENDPx_DataUp(ENDP3, ack_packet, sizeof(ack_packet));
}

// -------------------- 数据解析处理 --------------------
void CH9329_DataParser(uint8_t* buf, uint16_t len) {
    uint16_t index = 0;

    while (index + 6 <= len) {
        // 查找帧头
        if (buf[index] != CH9329_FRAME_HEAD1 || buf[index + 1] != CH9329_FRAME_HEAD2) {
            index++;
            continue;
        }

        uint8_t addr = buf[index + 2];
        uint8_t cmd_code = buf[index + 3];
        uint8_t data_len = buf[index + 4];

        if ((index + 5 + data_len + 1) > len) break;  // 数据不足则退出

        uint8_t checksum = 0;
        for (uint8_t i = 0; i < 5 + data_len; i++) {
            checksum += buf[index + i];
        }

        if (checksum != buf[index + 5 + data_len]) {
            index++;  // 校验失败跳过此帧
            continue;
        }

        uint8_t* data = &buf[index + 5];

        // ---------- 键盘数据 ----------
        if (cmd_code == CMD_SEND_KB_GENERAL_DATA && data_len == 8) {
            mod_keys = data[0];
            memcpy(normal_keys, &data[2], 6);

            memset(KB_Data_Pack, 0x00, sizeof(KB_Data_Pack));
            KB_Data_Pack[0] = mod_keys;
            memcpy(&KB_Data_Pack[2], normal_keys, 6);

            USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, sizeof(KB_Data_Pack), DEF_UEP_CPY_LOAD);
            CH9329_SendAck(addr, cmd_code, 0x00);

        // ---------- 绝对鼠标数据 ----------
        } else if (cmd_code == CMD_SEND_MS_ABS_DATA && data_len == 7 && data[0] == 0x02) {
            uint8_t buttons = data[1];
            uint16_t x = data[2] | (data[3] << 8);
            uint16_t y = data[4] | (data[5] << 8);
            int8_t wheel = (int8_t)data[6];

            // 构建绝对坐标鼠标包
            uint8_t abs_mouse[5] = {
                buttons,
                (uint8_t)(x & 0xFF),
                (uint8_t)((x >> 8) & 0xFF),
                (uint8_t)(y & 0xFF),
                (uint8_t)((y >> 8) & 0xFF),
            };

            USBFS_Endp_DataUp(DEF_UEP2, abs_mouse, sizeof(abs_mouse), DEF_UEP_CPY_LOAD);

            // 滚轮单独发送
            if (wheel != 0) {
                uint8_t wheel_packet[4] = {0x00, 0x00, 0x00, (uint8_t)wheel};
                USBFS_Endp_DataUp(DEF_UEP2, wheel_packet, sizeof(wheel_packet), DEF_UEP_CPY_LOAD);
            }

            CH9329_SendAck(addr, cmd_code, 0x00);
        }

        index += 5 + data_len + 1;  // 跳到下一帧
    }
}

// -------------------- 主接收处理函数 --------------------
void USB_DataRx_To_KMHandle(void) {
    // ----------- 来自 RingMemBLE ----------
    while (RingMemBLE.CurrentLen > 0) {
        uint8_t buf[64];
        uint8_t read_len = (RingMemBLE.CurrentLen > sizeof(buf)) ? sizeof(buf) : RingMemBLE.CurrentLen;
        if (RingMemRead(&RingMemBLE, buf, read_len) == SUCCESS) {
            CH9329_DataParser(buf, read_len);
        } else {
            break;
        }
    }

    // ----------- 来自 UART2_Tx_Buf ----------
    while (Uart.Tx_RemainNum) {
        if (Uart.Tx_CurPackLen == 0) {
            Uart.Tx_CurPackLen = Uart.Tx_PackLen[Uart.Tx_DealNum];
            Uart.Tx_CurPackPtr = Uart.Tx_DealNum * DEF_USB_FS_PACK_LEN;
        }

        uint8_t uart_buf[64];
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
