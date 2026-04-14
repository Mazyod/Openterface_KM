/*******************************************************************************/
/* Header Files */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "iwdg_auto.h"
#include "UART.h"
#include "debug.h"
#include "usb_lib.h"
#ifdef ENABLE_BLE
#include "config.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#endif
#include "include/keyboard_handler.h"
#include "include/mouse_handler.h"
#include "sd_switch.h"
#include "RGB.h"
#include "DS18B20.h"
#include "CH32_TEMP.h"
#include "km_ring.h"
#include "Version_selection.h"
#include "include/uac_handler.h"
#ifdef ENABLE_ETH
#include "include/eth_handler.h"
#endif /* ENABLE_ETH */

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
#ifdef ENABLE_BLE
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
#endif

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
#ifdef ENABLE_BLE
        TMOS_SystemProcess();
#endif
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

#ifdef ENABLE_ETH
    /* Early diagnostic: blink PC6 (red LED) 3 times so the operator
     * can confirm the MCU is actually running after a flash update.
     * This executes BEFORE any USB initialisation. */
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
        GPIO_InitTypeDef dbg_gpio = {0};
        dbg_gpio.GPIO_Pin   = GPIO_Pin_6;
        dbg_gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
        dbg_gpio.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_Init(GPIOC, &dbg_gpio);
        for (int i = 0; i < 6; i++) {
            GPIO_WriteBit(GPIOC, GPIO_Pin_6, (i & 1) ? Bit_SET : Bit_RESET);
            Delay_Ms(200);
        }
        GPIO_WriteBit(GPIOC, GPIO_Pin_6, Bit_RESET);
    }
#endif /* ENABLE_ETH */

    //usbd_init
    USART_Printf_Init(115200);
    printf("FW start, ver=%d\r\n", version);
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
    printf("USBD init done\r\n");
#ifdef ENABLE_BLE
    WCHBLE_Init();
#endif
    //minikvm havent ble & ds18b20
    if (version == 1)
    {
    DS18B20_Init();
    }
    //usbfs_init
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    USB_Sleep_Wakeup_CFG();
    printf("USBFS init done\r\n");
#ifdef ENABLE_UAC
    UAC_Init();
#endif
#ifdef ENABLE_ETH
    ETH_Init();
#endif
    Keyboard_Init();
    Mouse_Init();
    //ble_init(Must be placed after USB initialization)
#ifdef ENABLE_BLE
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();
#endif
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
    printf("Init complete, entering main loop\r\n");
    Main_Circulation();
}


