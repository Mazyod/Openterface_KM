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
#include "include/ble_eth_protocol.h"
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
#include "include/acm_bridge.h"
#include "include/ecm_bridge.h"
#include "include/usb_mode_config.h"
#include "include/pwm_led.h"

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
    uint32_t led_last = 0;
    uint16_t last_tx_count = 0;
    uint8_t led_state = 0;     /* 0=off, 1=on */
    uint32_t last_tx_time = 0; /* timestamp of last TX activity */
    uint32_t last_ep6_count = 0; /* track EP6 counter changes */
    CH9329_Cmd_GetInfo_Reply(00);


    while(1)
    {
        /* LED debug patterns (PC7, active-low):
         *   OFF          — booting (< 3s)
         *   Solid ON     — EP6 OUT interrupt fired, TOG_OK=1 (data processed)
         *   Rapid flash  — EP6 OUT interrupt fired, TOG_OK=0 (TOG mismatch!)
         *   Fast blink   — TX happening (data flowing via EP7)
         *   Slow blink   — idle, no EP6 activity
         *   Triple flash — DataUp called (state=2)
         *   Double flash — EP7 busy blocked TX */
        uint32_t now = systick_ms;
        uint32_t elapsed = now - t0;
        uint16_t tx_now = g_ecm_tx_count;
        if (tx_now != last_tx_count) {
            last_tx_time = now;
            last_tx_count = tx_now;
        }

        /* Poll EP6 OUT interrupt diagnostic flag */
        uint8_t ep6_fired = g_usbfs_ep6_int_flag;
        uint8_t ep6_tog = g_usbfs_ep6_tog_ok;
        uint16_t ep6_len = g_usbfs_ep6_rx_len;
        if (ep6_fired) {
            g_usbfs_ep6_int_flag = 0;  /* clear for next cycle */
        }

        if (elapsed < 3000) {
            /* Boot phase — LED off */
            PWM_LED_SetBrightness(0);
        }
        else if (g_ecm_led_override >= 1 && g_ecm_led_override <= 5) {
            /* RX path override: show ARP handler state */
            if (g_ecm_led_override == 3) {
                PWM_LED_SetBrightness(100);  /* IP matched — solid ON */
            }
            else {
                /* Other codes: blink pattern */
                if (now - led_last >= 400) { led_last = now; }
                uint32_t t = now - led_last;
                uint8_t flashes = g_ecm_led_override;  /* 1-5 blinks */
                uint8_t want = 0;
                for (int i = 0; i < flashes; i++) {
                    uint32_t start = (uint32_t)i * 80;
                    if (t >= start && t < start + 40) want = 1;
                }
                PWM_LED_SetBrightness(want ? 100 : 0);
            }
        }
        else if (ep6_fired && ep6_tog) {
            /* EP6 OUT: interrupt fired, TOG OK — data processed */
            PWM_LED_SetBrightness(100);  /* solid ON */
        }
        else if (ep6_fired && !ep6_tog) {
            /* EP6 OUT: interrupt fired, TOG mismatch! */
            if (now - led_last >= 100) {
                led_state ^= 1;
                led_last = now;
                PWM_LED_SetBrightness(led_state ? 100 : 0);
            }
        }
        else if ((now - last_tx_time) < 5000 && last_tx_time > 0) {
            /* TX recently — fast blink (500ms on, 500ms off) */
            if (now - led_last >= 500) {
                led_state ^= 1;
                led_last = now;
                PWM_LED_SetBrightness(led_state ? 100 : 0);
            }
        }
        else if (g_usbfs_ep6_rx_count > last_ep6_count) {
            /* EP6 interrupts arriving but TX not active — distinct pattern.
             * 4 quick flashes every 1.5s = interrupts received, but TOG may be wrong. */
            last_ep6_count = g_usbfs_ep6_rx_count;
            if (now - led_last >= 1500) { led_last = now; }
            uint32_t t = now - led_last;
            uint8_t want = 0;
            for (int i = 0; i < 4; i++) {
                uint32_t start = (uint32_t)i * 100;
                if (t >= start && t < start + 50) want = 1;
            }
            PWM_LED_SetBrightness(want ? 100 : 0);
        }
        else {
            /* Idle — no TX, no EP6 interrupts. Slow blink (2s on/off).
             * Mac is NOT sending OUT tokens to EP6. */
            if (now - led_last >= 2000) {
                led_state ^= 1;
                led_last = now;
                PWM_LED_SetBrightness(led_state ? 100 : 0);
            }
        }
        //rgb_state
        RGB_Update();
        USB_ModeConfig_Poll();
        //SDcard_state
        Update_SDcard();
        //Enable Bluetooth broadcasting
#ifdef ENABLE_BLE
        if (version == 1) {
        TMOS_SystemProcess();
        }
#endif
        //Instruction code receiving protocol
        USB_DataRx_To_KMHandle();
        //Keyboard and mouse control code sending loop
        USB_SendFromQueues();
        if (USB_ModeConfig_ActiveModeBridgeAcm()) {
            ACM_Bridge_Poll();
        }
        else if (USB_ModeConfig_ActiveModeBridgeEcm()) {
            ECM_Bridge_Poll();
        }
        /* Also poll ECM bridge when BLE-Eth has active tunnels or ARP-pending connections */
        else if (BLE_Eth_HasActiveOrPendingConnection()) {
            ECM_Bridge_Poll();
        }


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
    USB_ModeConfig_Init();
    //usbd_init
    USART_Printf_Init(115200);
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();
    if (version == 1)
    {
#ifdef ENABLE_BLE
    WCHBLE_Init();
#endif
    DS18B20_Init();
    }
    //usbfs_init
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    USB_Sleep_Wakeup_CFG();
#ifdef USB_BRINGUP_DIAG
    while (1)
    {
    }
#endif
#ifdef ENABLE_UAC
    if (USB_ModeConfig_ActiveModeHasTargetUac()) {
        UAC_Init();
    }
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

    /* Initialize PC7 LED - must be after RGB_Init since RGB configures GPIOC/AFIO */
    PWM_LED_Init();

    Delay_Ms(1000);
    IWDG_Auto_Init(IWDG_Prescaler_32, 4000);
    Main_Circulation();
}
