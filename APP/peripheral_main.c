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
#include "gpio_init.h"
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
    // GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
    // GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_SET);
    u8 prev_state = 0; 
    while(1)
    {
        
        TMOS_SystemProcess();
        SD_SW(&prev_state);
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
    GPIO_Toggle_INIT();
    
    // Initialize keyboard handler
    Keyboard_Init();
    
    // Initialize mouse handler
    Mouse_Init();
    
    Main_Circulation();
}