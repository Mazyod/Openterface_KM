#ifndef SD_SWITCH_H
#define SD_SWITCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** Current SD card channel status: 0 = TARGET_SD, 1 = HOST_SD */
extern uint8_t sd_card_channel_state;

/**
 * Initialize SD card switch related GPIO (PA1/PA7 output; PA6, PB8 input pull-down)
 */
void SD_Switch_Init(void);

/** Switch to USB1 (TARGET_SD) channel */
void TARGET_SD_Switch(void);

/** Switch to USB2 (HOST_SD) channel */
void HOST_SD_Switch(void);

/**
 * State machine detection function:
 * - Detect PA6 on first call;
 *   If PA6 = high → TARGET; PA6 = low → HOST.
 * - After that, no longer detect PA6, only detect switching via rising edge of PB8.
 * @param prev_selector_state_p Pointer to PB8 previous state variable
 */
void SD_Switch_StateMachine(uint8_t *prev_selector_state_p);
void SD_USB_Switch(uint8_t addr, uint8_t cmd_code, uint8_t *pdata, uint8_t data_len);
#ifdef __cplusplus
}
#endif

#endif /* SD_SWITCH_H */
