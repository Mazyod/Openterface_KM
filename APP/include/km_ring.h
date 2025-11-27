#ifndef KM_RING_H
#define KM_RING_H

#include <stdint.h>
#include <stdbool.h>

/* Queue size definitions */
#define KB_QUEUE_SIZE      2048   // Keyboard report queue size
#define MS_REL_QUEUE_SIZE  28     // Relative mouse report queue size
#define MS_ABS_QUEUE_SIZE  28     // Absolute mouse report queue size
#define RESP_QUEUE_SIZE    28     // Response packet queue size

/* Keyboard report structure */
typedef struct {
    uint8_t data[8];
} KB_Report_t;

/* Mouse relative movement report */
typedef struct {
    uint8_t data[4];
} MS_Rel_Report_t;

/* Mouse absolute movement report */
typedef struct {
    uint8_t data[6];
} MS_Abs_Report_t;

/* Response packet structure */
typedef struct {
    uint8_t data[64];   // Maximum 64 bytes per response frame
    uint8_t len;        // Actual valid length
} Resp_Report_t;

/* Keyboard queue structure */
typedef struct {
    KB_Report_t buf[KB_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} KB_Queue_t;

/* Mouse relative queue structure */
typedef struct {
    MS_Rel_Report_t buf[MS_REL_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} MS_Rel_Queue_t;

/* Mouse absolute queue structure */
typedef struct {
    MS_Abs_Report_t buf[MS_ABS_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} MS_Abs_Queue_t;

/* Response packet queue structure */
typedef struct {
    Resp_Report_t buf[RESP_QUEUE_SIZE];
    volatile int head;
    volatile int tail;
} Resp_Queue_t;

/* Global queue declarations */
extern KB_Queue_t kb_queue;
extern MS_Rel_Queue_t ms_rel_queue;
extern MS_Abs_Queue_t ms_abs_queue;
extern Resp_Queue_t resp_queue;

/* Queue initialization */
void Queue_Init(void);

/* Push functions */
void Queue_Push_Keyboard(uint8_t *data);
void Queue_Push_MouseRel(uint8_t *data);
void Queue_Push_MouseAbs(uint8_t *data);
void Queue_Push_Response(uint8_t *data, uint8_t len);

/* Pop functions */
bool Queue_Pop_Keyboard(KB_Report_t *out);
bool Queue_Pop_MouseRel(MS_Rel_Report_t *out);
bool Queue_Pop_MouseAbs(MS_Abs_Report_t *out);
bool Queue_Pop_Response(Resp_Report_t *out);

/* Send functions */
void USB_SendFromQueues(void);        // Send keyboard/mouse reports via USBFS
void USBD_SendFromRespQueue(void);    // Send response packets via USBD (endpoint)

#endif /* KM_RING_H */
