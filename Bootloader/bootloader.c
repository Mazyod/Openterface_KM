#include "ch32v20x.h"
#include "debug.h"
#include "bootloader.h"
#include "protocol.h"
#include "usb.h"
#include "flash.h"

/* ponytail: global lock, add per-sector locks if concurrent access matters */
static uint8_t g_bootloader_active = 0;

void bootloader_main(void) {
    uint8_t buf[64];
    uint16_t len;

    g_bootloader_active = 1;

    /* Enable IWDG: ~4s timeout at 32kHz LSI, prescaler 64, reload 4000 */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(4000);
    IWDG_Enable();

    /* Initialize delay functions (uses SysTick after SystemInit) */
    Delay_Init();

    /* Init USB on EP2 bulk */
    usb_init();

    /* Main loop: poll USB, feed watchdog */
    while (1) {
        IWDG_ReloadCounter();

        if (usb_data_available()) {
            len = usb_read(buf, sizeof(buf));
            if (len > 0) {
                protocol_process(buf, len);
            }
        }
    }
}

void bootloader_jump(void) {
    uint32_t app_entry = 0x00004000;

    /* Clear BKP flag one more time */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    BKP_WriteBackupRegister(BKP_DR1, 0);
    BKP_WriteBackupRegister(BKP_DR2, 0);

    /* Disable USBD and interrupts */
    *(volatile uint16_t *)(0x40005C00L + 0x40) = 0x0000;  /* CNTR: PDWN+FRES */
    *(volatile uint16_t *)(0x40005C00L + 0x44) = 0x0000;  /* ISTR: clear */
    NVIC_DisableIRQ(USBFS_IRQn);
    EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;  /* disable pull-up */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, DISABLE);
    __asm volatile("csrc mstatus, 0x8");

    /* Set mstatus: MPP=11 (Machine mode), MPIE=1, MIE=0 */
    __asm volatile("li t0, 0x1880");
    __asm volatile("csrw mstatus, t0");

    /* Set vector table to app */
    __asm volatile("csrw mtvec, %0" : : "r"(app_entry + 4 | 3));

    /* Jump via mret */
    __asm volatile("csrw mepc, %0" : : "r"(app_entry));
    __asm volatile("mret");

    while (1) { }
}