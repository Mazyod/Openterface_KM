#ifndef KM_RING_H
#define KM_RING_H

#include <stdint.h>
#include <stdbool.h>
#include "usbd_desc.h"
#include "usb_type.h"

/* 队列大小，可存储大批量数据包 */
#define KB_QUEUE_SIZE      256
#define MS_REL_QUEUE_SIZE  64
#define MS_ABS_QUEUE_SIZE  64

/* 报文类型 */
typedef struct { uint8_t data[8]; } KB_Report_t;
typedef struct { uint8_t data[4]; } MS_Rel_Report_t;
typedef struct { uint8_t data[6]; } MS_Abs_Report_t;

/* 队列结构 */
typedef struct {
    KB_Report_t buf[KB_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} KB_Queue_t;

typedef struct {
    MS_Rel_Report_t buf[MS_REL_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} MS_Rel_Queue_t;

typedef struct {
    MS_Abs_Report_t buf[MS_ABS_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} MS_Abs_Queue_t;

/* 全局队列变量 */
extern KB_Queue_t kb_queue;
extern MS_Rel_Queue_t ms_rel_queue;
extern MS_Abs_Queue_t ms_abs_queue;

/* 初始化队列 */
void Queue_Init(void);

/* 入队函数 — 如果满，则覆盖最旧 */
void Queue_Push_Keyboard(uint8_t *data);
void Queue_Push_MouseRel(uint8_t *data);
void Queue_Push_MouseAbs(uint8_t *data);

/* 发送队列中的数据包（发送失败会保留） */
void USB_SendFromQueues(void);

#endif /* KM_RING_H */
