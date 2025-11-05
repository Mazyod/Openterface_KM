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
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"
#include "SD_SWITCH.h"
#include "RGB.h"
#include "DS18B20.h"
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
    uint8_t selector_prev_state;
        // 上电三色闪烁
    selector_prev_state = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);
    RGB_FlashStartupSequence();
    // 3秒后切换呼吸模式
    uint32_t t0 = systick_ms;
    TARGET_SD_Switch();
    while(1)
    {   
        RGB_Update();
        TMOS_SystemProcess();
        
        if (USBFS_DevEnumStatus) {
            USB_DataRx_To_KMHandle();
        }
        if(systick_ms - t0 > 3000)
        {
            RGB_SetBreathMode(0.02f); // 开始呼吸模式
            SD_Switch_StateMachine(&selector_prev_state);
        }
    }
}
int main(void) {
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);


    SystemCoreClockUpdate();
    Delay_Init();
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

    
    // Initialize keyboard handler
    Keyboard_Init();
    RGB_Init();
    // Initialize mouse handler
    Mouse_Init();
    SD_Switch_Init();
    DS18B20_Init();
    Delay_Ms(200);
    
    // RGB_BreathLoop();
    Main_Circulation();
}


