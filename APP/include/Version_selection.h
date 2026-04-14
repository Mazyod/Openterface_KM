#ifndef VERSION_SELECTION_H
#define VERSION_SELECTION_H

#include <stdint.h>

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
/* USB Audio Class (UAC) support — controlled by the build system.
 * Default: enabled (ENABLE_UAC is defined via CFLAGS when ENABLE_UAC=1).
 * To build without UAC (HID-only, frees EP5+EP6):  make ENABLE_UAC=0
 * In MounRiver Studio: add -DENABLE_UAC to project compiler flags. */

/* BLE support — controlled by the build system.
 * Default: enabled (ENABLE_BLE is defined via CFLAGS when ENABLE_BLE=1).
 * To build without BLE (frees ~14 KB RAM: 10 KB heap + 4 KB ring buffers):
 *   make ENABLE_BLE=0
 * In MounRiver Studio: add -DENABLE_BLE to project compiler flags. */

#endif /* VERSION_SELECTION_H */
