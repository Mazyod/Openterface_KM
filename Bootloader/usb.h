/* USB FS Device Driver for Bootloader
 *
 * Minimal USB FS device driver adapted from WCH IAP reference.
 * EP0 (control) + EP2 (bulk IN/OUT, 64-byte max packet).
 * VID=0x1A86, PID=0xFE10 (bootloader; app uses 0xFE00/0xFE0C).
 */
#ifndef USB_H
#define USB_H

#include <stdint.h>

/* USB descriptors */
#define USB_VID             0x1A86
#define USB_PID             0xFE10
#define USB_VENDOR_ID_LO    (USB_VID & 0xFF)
#define USB_VENDOR_ID_HI    ((USB_VID >> 8) & 0xFF)
#define USB_PRODUCT_ID_LO   (USB_PID & 0xFF)
#define USB_PRODUCT_ID_HI   ((USB_PID >> 8) & 0xFF)

/* Endpoint configuration */
#define EP0_SIZE            64
#define EP2_SIZE            64

/* USB API */
void usb_init(void);
void usb_poll(void);
uint8_t usb_data_available(void);
uint16_t usb_read(uint8_t *buf, uint16_t max_len);
void usb_send(const uint8_t *data, uint16_t len);

/* Callback for protocol processing */
void protocol_process(const uint8_t *data, uint16_t len);

#endif /* USB_H */
