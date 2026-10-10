#include "include/usb_work_mode.h"
#if USB_WORK_MODE_COMPILES_BRIDGE_ECM
const int bridge_ecm_compiles = 1;
#else
const int bridge_ecm_compiles = 0;
#endif

#if USB_WORK_MODE_BRIDGE_ECM
const int bridge_ecm = 1;
#else
const int bridge_ecm = 0;
#endif
