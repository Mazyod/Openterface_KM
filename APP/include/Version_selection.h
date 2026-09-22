#ifndef VERSION_SELECTION_H
#define VERSION_SELECTION_H

#include <stdint.h>
#include "usb_work_mode.h"

/* Values:
 * 1 = kvm go
 * 2 = mini kvm (version 1.2)
 * 3 = keymod v1.0
 */
#ifndef version
#define version 3
#endif
/* EP3_USE_STYLUS is now controlled by the build system.
 * Use:  make ENABLE_STYLUS=1   (stylus / touchscreen mode)
 *       make                   (absolute mouse mode, default)
 * Legacy: uncommenting #define EP3_USE_STYLUS here still works for IDE builds. */
/* USB Audio Class (UAC) support is derived from USB_WORK_MODE. */
#if defined(ENABLE_UAC) && !USB_WORK_MODE_COMPILES_TARGET_UAC
#error ENABLE_UAC conflicts with the selected USB_WORK_MODE.
#endif
#if USB_WORK_MODE_COMPILES_TARGET_UAC
#define ENABLE_UAC
#endif

/* TEMP: Disable UAC on both USB interfaces across all firmware versions.
 * Override the above ENABLE_UAC and all UAC-related compile-time macros.
 * Restore original behavior by removing the override block below. */
#undef ENABLE_UAC
#undef USB_WORK_MODE_HAS_TARGET_UAC
#undef USB_WORK_MODE_HAS_HOST_UAC
#undef USB_WORK_MODE_COMPILES_TARGET_UAC
#undef USB_WORK_MODE_COMPILES_HOST_UAC
#undef USB_WORK_MODE_COMPILES_BRIDGE_UAC
#define USB_WORK_MODE_HAS_TARGET_UAC        0
#define USB_WORK_MODE_HAS_HOST_UAC          0
#define USB_WORK_MODE_COMPILES_TARGET_UAC   0
#define USB_WORK_MODE_COMPILES_HOST_UAC     0
#define USB_WORK_MODE_COMPILES_BRIDGE_UAC   0

/* BLE support — controlled by the build system.
 * Default: enabled (ENABLE_BLE is defined via CFLAGS when ENABLE_BLE=1).
 * To build without BLE (frees ~14 KB RAM: 10 KB heap + 4 KB ring buffers):
 *   make ENABLE_BLE=0
 * In MounRiver Studio: add -DENABLE_BLE to project compiler flags. */
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif

/* BLE Name Config — enabled for all boards with BLE hardware.
 * All current products (KVM-GO, MiniKVM, KeyMod) have BLE support. */
#ifdef ENABLE_BLE
    #define ENABLE_BLE_NAME_CONFIG  1
#else
    #define ENABLE_BLE_NAME_CONFIG  0
#endif

#endif /* VERSION_SELECTION_H */
