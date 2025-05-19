/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_composite_km.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : USB keyboard and mouse processing.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header Files */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_prop.h"
/*******************************************************************************/
/* Global Variable Definition */

/* Mouse */
volatile uint8_t  MS_Scan_Done = 0x00;                                          // Mouse Movement Scan Done
volatile uint16_t MS_Scan_Result = 0x00F0;                                      // Mouse Movement Scan Result


/* Keyboard */
volatile uint8_t  KB_Scan_Done = 0x00;                                          // Keyboard Keys Scan Done
volatile uint16_t KB_Scan_Result = 0xF000;                                      // Keyboard Keys Current Scan Result
volatile uint16_t KB_Scan_Last_Result = 0xF000;                                 // Keyboard Keys Last Scan Result
uint8_t  KB_Data_Pack[ 8 ] = { 0x00 };                                          // Keyboard IN Data Packet
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
// CH9329 协议头字节定义
#define CH9329_FRAME_HEAD1      0x57
#define CH9329_FRAME_HEAD2      0xAB

// CH9329 键盘和鼠标命令码定义
#define CMD_SEND_KB_GENERAL_DATA 0x02
#define CMD_SEND_MS_REL_DATA    0x05

// 修饰键定义（左右区分，按 HID Usage ID）
#define MOD_LCTRL   0x01
#define MOD_LSHIFT  0x02
#define MOD_LALT    0x04
#define MOD_LGUI    0x08
#define MOD_RCTRL   0x10
#define MOD_RSHIFT  0x20
#define MOD_RALT    0x40
#define MOD_RGUI    0x80

// 特殊按键定义
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

// 键盘和鼠标数据包
uint8_t KB_Data_Pack[8];
uint8_t MS_Data_Pack[4];  // 标准4字节鼠标报告：按键 + X + Y + 滚轮

// 特殊按键状态
uint8_t mod_keys = 0;
uint8_t normal_keys[6] = {0};

// 应答函数
void CH9329_SendAck(uint8_t addr, uint8_t cmd_code, uint8_t status) {
    uint8_t ack_packet[7] = {CH9329_FRAME_HEAD1, CH9329_FRAME_HEAD2, addr, cmd_code, 0x01, status, 0};
    for (int i = 0; i < 6; i++) ack_packet[6] += ack_packet[i];
    USBD_ENDPx_DataUp(ENDP3, ack_packet, sizeof(ack_packet));
}

// USB 数据接收处理
void USB_DataRx_To_KMHandle(void) {
    while (Uart.Tx_RemainNum) {
        if (Uart.Tx_CurPackLen == 0x00) {
            Uart.Tx_CurPackLen = Uart.Tx_PackLen[Uart.Tx_DealNum];
            Uart.Tx_CurPackPtr = Uart.Tx_DealNum * DEF_USB_FS_PACK_LEN;
        }

        uint8_t recv = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
        if (recv == CH9329_FRAME_HEAD1 && UART2_Tx_Buf[Uart.Tx_CurPackPtr++] == CH9329_FRAME_HEAD2) {
            uint8_t addr = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
            uint8_t cmd_code = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
            uint8_t length = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
            uint8_t data[8] = {0};
            uint8_t checksum = CH9329_FRAME_HEAD1 + CH9329_FRAME_HEAD2 + addr + cmd_code + length;

            for (uint8_t i = 0; i < length; i++) {
                data[i] = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];
                checksum += data[i];
            }

            uint8_t received_checksum = UART2_Tx_Buf[Uart.Tx_CurPackPtr++];

            // 校验码检测
            if (checksum != received_checksum) {
                // 校验失败，丢弃该数据包
                continue;
            }

            // 键盘控制
            if (cmd_code == CMD_SEND_KB_GENERAL_DATA) {
                mod_keys = data[0];
                memcpy(normal_keys, &data[2], 6);
                memset(KB_Data_Pack, 0x00, sizeof(KB_Data_Pack));
                KB_Data_Pack[0] = mod_keys;
                memcpy(&KB_Data_Pack[2], normal_keys, 6);
                USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, sizeof(KB_Data_Pack), DEF_UEP_CPY_LOAD);
                CH9329_SendAck(addr, cmd_code, 0x00);
            }

            // 鼠标控制
            else if (cmd_code == CMD_SEND_MS_REL_DATA) {
                memset(MS_Data_Pack, 0x00, sizeof(MS_Data_Pack));
                MS_Data_Pack[0] = data[1];  // 鼠标按键状态
                MS_Data_Pack[1] = data[2];  // X轴移动
                MS_Data_Pack[2] = data[3];  // Y轴移动
                MS_Data_Pack[3] = data[4];  // 滚轮移动

                // 相对坐标转换（补码转换）
                MS_Data_Pack[1] = (MS_Data_Pack[1] & 0x80) ? (0xFF - MS_Data_Pack[1] + 1) * -1 : MS_Data_Pack[1];
                MS_Data_Pack[2] = (MS_Data_Pack[2] & 0x80) ? (0xFF - MS_Data_Pack[2] + 1) * -1 : MS_Data_Pack[2];
                MS_Data_Pack[3] = (MS_Data_Pack[3] & 0x80) ? (0xFF - MS_Data_Pack[3] + 1) * -1 : MS_Data_Pack[3];

                // 更新鼠标数据到端点2
                USBFS_Endp_DataUp(DEF_UEP2, MS_Data_Pack, sizeof(MS_Data_Pack), DEF_UEP_CPY_LOAD);
                CH9329_SendAck(addr, cmd_code, 0x00);
            }
        }

        if (--Uart.Tx_CurPackLen == 0) {
            Uart.Tx_PackLen[Uart.Tx_DealNum] = 0;
            Uart.Tx_DealNum = (Uart.Tx_DealNum + 1) % DEF_UARTx_TX_BUF_NUM_MAX;
            Uart.Tx_RemainNum--;
        }
    }
}
