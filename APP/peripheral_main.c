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

    /* ---- Square movement test ----
     * Each corner: move → click-down (50ms) → click-up → wait → next corner
     *   Step 0: Q1 top-right    (24575, 8191)
     *   Step 1: Q2 top-left     (8191,  8191)
     *   Step 2: Q3 bottom-left  (8191,  24575)
     *   Step 3: Q4 bottom-right (24575, 24575)
     *
     * Sub-step timing per corner (total ~1200ms):
     *   phase 0 (0ms):    move to corner   (In Range=1, Tip=0)
     *   phase 1 (+100ms): press            (In Range=1, Tip=1)
     *   phase 2 (+150ms): release          (In Range=1, Tip=0)
     *   phase 3 (+1200ms): advance to next corner
     */
    static const uint16_t square_x[4] = { 24575, 8191,  8191,  24575 };
    static const uint16_t square_y[4] = { 8191,  8191,  24575, 24575 };
    uint8_t  square_step     = 0;
    uint8_t  square_phase    = 0;   // 0=move 1=press 2=release
    uint32_t square_last_ms  = 0;
    /* ---- End test variables ---- */

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


