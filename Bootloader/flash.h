#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PAGE_SIZE       4096
#define FLASH_PROTECT_START   0x00004000  /* First app address */
#define FLASH_PROTECT_END     0x0006E000  /* Before config pages */

/* Error codes */
#define ERR_OK               0x00
#define ERR_ADDR_PROTECTED   0x01
#define ERR_ADDR_RANGE       0x02
#define ERR_ALIGN            0x03

#endif /* FLASH_H */