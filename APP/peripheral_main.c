/*******************************************************************************/
/* Header Files */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "iwdg_auto.h"
#include "UART.h"
#include "debug.h"
#include "usb_lib.h"
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"
#include "sd_switch.h"
#include "rgb.h"
#include "ds18B20.h"
#include "ch32_temp.h"
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
        // Power-on three-color flashing
    selector_prev_state = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);
    RGB_FlashStartupSequence();
    // Switch to breathing mode after 3 seconds
    uint32_t t0 = systick_ms;
    CH9329_Cmd_GetInfo_Reply(00);
    while(1)
    {   
       RGB_Update();
        TMOS_SystemProcess();
        USB_DataRx_To_KMHandle();

        if(systick_ms - t0 > 3000)
        {

            RGB_SetBreathMode(0.02f);
            SD_Switch_StateMachine(&selector_prev_state);
            
        }

        

        IWDG_Auto_Handler();
    }
}
int main(void) {
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);


    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    IWDG_Auto_Init(IWDG_Prescaler_32, 8000);
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
    ADC_Function_Init();
    Delay_Ms(1000);
    
    // RGB_BreathLoop();
    Main_Circulation();
}


