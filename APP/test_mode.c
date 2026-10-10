#include "include/usb_work_mode.h"
#if USB_WORK_MODE_HAS_HOST_CDC_ACM
int has_acm = 1;
#else
int has_acm = 0;
#endif
#if USB_WORK_MODE_HAS_HOST_CDC_ECM
int has_ecm = 1;
#else
int has_ecm = 0;
#endif
#if USB_WORK_MODE_HAS_HOST_ACM_AND_ECM
int has_both = 1;
#else
int has_both = 0;
#endif
