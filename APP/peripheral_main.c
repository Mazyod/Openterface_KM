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
#include "km_ring.h"
#include "Version_selection.h"

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
    // SD card switching status reading
    if (version == 1) {
        selector_prev_state = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8);
    }
    else if (version == 2) {
        selector_prev_state = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_4);
    }
    uint32_t t0 = systick_ms;
    CH9329_Cmd_GetInfo_Reply(00);
    while(1)
    {   
        //rgb_state
        RGB_Update();
        //SDcard_state
        Update_SDcard();
        //Enable Bluetooth broadcasting
        if (version == 1) {
        TMOS_SystemProcess();
        }
        //Instruction code receiving protocol
        USB_DataRx_To_KMHandle();
        //Keyboard and mouse control code sending loop
        USB_SendFromQueues();
        //Different devices have different control pin
        //version == 1
        if (version == 1) {
        if(systick_ms - t0 > 20000)
        {
            SD_Switch_StateMachine();
        }
        else {
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
        }
        }
        //version == 2
        else
        {SD_Switch_StateMachine();}
        //watchdog
        IWDG_Auto_Handler();
    }
}
int main(void) {

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    //usbd_init
    USART_Printf_Init(115200);
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
    //usbfs_init
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    USB_Sleep_Wakeup_CFG();
    Keyboard_Init();
    Mouse_Init();
    //ble_init(Must be placed after USB initialization)
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    //minikvm havent ble & ds18b20
    if (version == 1)
    {    
    WCHBLE_Init();
    DS18B20_Init();
    }
    //km_ring
    Queue_Init();
    //SDcontrol_init
    SD_Switch_Init();
    //temp_init
    ADC_Function_Init();
    //rgb_init
    RGB_Init(); 
    Delay_Ms(1000);
    IWDG_Auto_Init(IWDG_Prescaler_32, 4000);
    Main_Circulation();
}


