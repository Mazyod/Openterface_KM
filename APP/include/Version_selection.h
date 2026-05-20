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
#define version  1
#endif
// Use this definition to set endpoint 3 as a touchscreen device,or an absolute mouse device
#define EP3_USE_STYLUS
/* USB Audio Class (UAC) support is derived from USB_WORK_MODE.
 * Mode 1 omits UAC. Mode 2 keeps the existing target/host UAC bridge. */
#if defined(ENABLE_UAC) && !USB_WORK_MODE_HAS_TARGET_UAC
#error ENABLE_UAC conflicts with the selected USB_WORK_MODE.
#endif
#if USB_WORK_MODE_HAS_TARGET_UAC
#define ENABLE_UAC
#endif

/* BLE support — controlled by the build system.
 * Default: enabled (ENABLE_BLE is defined via CFLAGS when ENABLE_BLE=1).
 * To build without BLE (frees ~14 KB RAM: 10 KB heap + 4 KB ring buffers):
 *   make ENABLE_BLE=0
 * In MounRiver Studio: add -DENABLE_BLE to project compiler flags. */
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif

#endif /* VERSION_SELECTION_H */
