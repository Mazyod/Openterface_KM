/*******************************************************************************/
/* Header Files */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "UART.h"
#include "debug.h"
#include "usb_lib.h"

// main enter
int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    Delay_Init();

    SystemCoreClockUpdate();;
    USART_Printf_Init(115200);
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    USB_Sleep_Wakeup_CFG();
    Set_USBConfig();
    USB_Init();
    USB_Interrupts_Config();

    while (1) {
        if (USBFS_DevEnumStatus) {
            USB_DataRx_To_KMHandle();
        }
    }
}