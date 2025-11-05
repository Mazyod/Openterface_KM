#ifndef SD_SWITCH_H
#define SD_SWITCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** 当前 SD 卡通道状态：0 = TARGET_SD, 1 = HOST_SD */
extern uint8_t sd_card_channel_state;

/**
 * 初始化 SD 卡切换相关 GPIO（PA1/PA7 输出；PA6、PB8 输入下拉）
 */
void SD_Switch_Init(void);

/** 切换至 USB1（TARGET_SD）通道 */
void TARGET_SD_Switch(void);

/** 切换至 USB2（HOST_SD）通道 */
void HOST_SD_Switch(void);

/**
 * 状态机检测函数：
 * - 第一次调用时检测 PA6；
 *   若 PA6 = 高电平 → TARGET；PA6 = 低电平 → HOST。
 * - 之后不再检测 PA6，只通过 PB8 的上升沿检测切换。
 * @param prev_selector_state_p 指向 PB8 上次状态变量
 */
void SD_Switch_StateMachine(uint8_t *prev_selector_state_p);

#ifdef __cplusplus
}
#endif

#endif /* SD_SWITCH_H */
