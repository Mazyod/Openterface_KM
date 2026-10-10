#include "include/usb_work_mode.h"
#if USB_WORK_MODE_COMPILES_BRIDGE_ECM
int bridge_ecm_true = 1;
#else
int bridge_ecm_false = 0;
#endif
