#ifndef ACM_BRIDGE_H
#define ACM_BRIDGE_H

#include <stdint.h>
#include "usb_work_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USB_WORK_MODE_COMPILES_BRIDGE_ACM
void ACM_Bridge_Init(void);
void ACM_Bridge_HostRxDone(const uint8_t *data, uint16_t len);
void ACM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len);
void ACM_Bridge_Poll(void);
#else
#define ACM_Bridge_Init()                 ((void)0)
#define ACM_Bridge_HostRxDone(data, len)  ((void)0)
#define ACM_Bridge_TargetRxDone(data, len) ((void)0)
#define ACM_Bridge_Poll()                 ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ACM_BRIDGE_H */
