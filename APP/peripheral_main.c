/*******************************************************************************/
/* Header Files */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "debug.h"
#include "usb_lib.h"
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif
/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   Main loop
 *
 * @return  none
 */
__attribute__((section(".highcode")))
__attribute__((noinline))
void Main_Circulation(void)
{
    while(1)
    {
        TMOS_SystemProcess();
        if (USBFS_DevEnumStatus) {
            USB_DataRx_To_KMHandle();
        }
    }
}
int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    Delay_Init();

    SystemCoreClockUpdate();
    USART_Printf_Init(115200);
    WCHBLE_Init();
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    USB_Sleep_Wakeup_CFG();
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
    
    // Main_Circulation();
    #define TOUCH_ENDPOINT DEF_UEP2
    #define STEP_DELAY_MS 20   // 每步延时，控制滑动速度
    #define MOVE_STEPS 100     // 总步数，数越大滑动越慢更平滑
    // while(1) {
        uint16_t maxX = 4095;
        uint16_t maxY = 4095;
        uint16_t x = 0, y = 0;
        uint16_t stepX = maxX / MOVE_STEPS;
        uint16_t stepY = maxY / MOVE_STEPS;

        // 按下起点（左上角）
        SendTouchPoint(TOUCH_ENDPOINT, 1, x, y);
        Delay_Ms(STEP_DELAY_MS);

        for (uint16_t i = 0; i < MOVE_STEPS; i++)
        {
            x += stepX;
            y += stepY;

            // 发送移动点
            SendTouchPoint(TOUCH_ENDPOINT, 1, x, y);
            Delay_Ms(STEP_DELAY_MS);
        }

        // 抬起手指，结束触摸
        // SendTouchPoint(TOUCH_ENDPOINT, 0, x, y);

    // }
}