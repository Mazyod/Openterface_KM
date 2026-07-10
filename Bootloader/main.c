/* Bootloader for Openterface KM (CH32V208) */
#include "ch32v20x.h"
#include "bootloader.h"
#include "protocol.h"
#include "usb.h"
#include "flash.h"

#define BOOTLOADER_MAGIC_LO   0x10AD
#define BOOTLOADER_MAGIC_HI   0xB007
#define APP_START_ADDR        0x00004000

extern uint32_t _eusrstack;
static void jump_to_app(void);
extern void SystemInit(void);

int main(void) {
    /* ponytail: TEST - infinite loop to verify bootloader runs */
    /* If device doesn't enumerate at all, bootloader is running but stuck here */
    /* If device enumerates as app (PID 0xFE00), bootloader is NOT running */
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    GPIOB->CFGLR &= ~(0xF << 28);
    GPIOB->CFGLR |= (0x3 << 28);  /* PB15 output push-pull */

    /* Infinite loop with GPIO toggle */
    while (1) {
        GPIOB->OUTDR ^= (1 << 15);
        for (volatile uint32_t j = 0; j < 1000000; j++);
    }

    /* Should never reach here */
    SystemInit();
    bootloader_main();
}

static void jump_to_app(void) {
    uint32_t app_entry = APP_START_ADDR;
    if (*(__IO uint32_t *)app_entry == 0xFFFFFFFF) {
        bootloader_main();
        return;
    }
    BKP_WriteBackupRegister(BKP_DR1, 0);
    BKP_WriteBackupRegister(BKP_DR2, 0);
    __asm volatile("li t0, 0x1880");
    __asm volatile("csrw mstatus, t0");
    __asm volatile("csrw mtvec, %0" : : "r"(app_entry + 4 | 3));
    __asm volatile("csrw mepc, %0" : : "r"(app_entry));
    __asm volatile("mret");
    while (1) { }
}
