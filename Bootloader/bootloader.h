#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

/* Entry point from main.c — never returns */
void bootloader_main(void);

/* Jump to application */
void bootloader_jump(void);

#endif /* BOOTLOADER_H */