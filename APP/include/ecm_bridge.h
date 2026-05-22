#ifndef ECM_BRIDGE_H
#define ECM_BRIDGE_H

#include <stdint.h>
#include "usb_work_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

#if USB_WORK_MODE_COMPILES_BRIDGE_ECM
void ECM_Bridge_Init(void);
void ECM_Bridge_HostRxDone(const uint8_t *data, uint16_t len);
void ECM_Bridge_TargetRxDone(const uint8_t *data, uint16_t len);
void ECM_Bridge_Poll(void);
void ECM_Bridge_SetBleTxCallback(void (*cb)(void));
void ECM_Bridge_SendRawFrame(const uint8_t *data, uint16_t len);
#else
#define ECM_Bridge_Init()                  ((void)0)
#define ECM_Bridge_HostRxDone(data, len)   ((void)0)
#define ECM_Bridge_TargetRxDone(data, len) ((void)0)
#define ECM_Bridge_Poll()                  ((void)0)
#define ECM_Bridge_SetBleTxCallback(cb)    ((void)0)
#define ECM_Bridge_SendRawFrame(data, len) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ECM_BRIDGE_H */