/*******************************************************************************
 * File Name          : uac_desc.h
 * Description        : UAC 1.0 descriptor constants for USB Full-Speed.
 *                      Two AudioStreaming interfaces (Playback + Record) are
 *                      added on both the USBFS side (Host-facing, EP5/EP6)
 *                      and the USBD/CDC side (Target-facing, EP4/EP5).
 *
 * Audio format:
 *   USBFS side: PCM, 48 kHz, Stereo, 16-bit  → 192 bytes/ms
 *   USBD side:  PCM, 16 kHz, Stereo, 16-bit  → 64 bytes/ms
 *
 * Interface layout on USBFS (Host side):
 *   IF4  AudioControl          (no EP)
 *   IF5  AudioStreaming IN   → EP5 ISO IN  (Host <-- MCU, i.e. Target audio)
 *   IF6  AudioStreaming OUT  → EP6 ISO OUT (Host --> MCU, i.e. Host playback)
 *
 * Interface layout on USBD (Target side):
 *   CDC occupies: EP1 INT IN, EP2 BULK OUT, EP3 BULK IN
 *   IF2  AudioControl          (no EP)
 *   IF3  AudioStreaming IN   → EP4 ISO IN  (Target <-- MCU, i.e. Host audio)
 *   IF4  AudioStreaming OUT  → EP5 ISO OUT (Target --> MCU, i.e. Target audio)
 *******************************************************************************/

#ifndef __UAC_DESC_H
#define __UAC_DESC_H

#include "stdint.h"

/* -----------------------------------------------------------------------
 * Audio format parameters
 * --------------------------------------------------------------------- */

/* USBFS side (Host-facing): 48 kHz stereo 16-bit = 192 B/ms */
#define UAC_SAMPLE_RATE         48000u
#define UAC_CHANNELS            2u
#define UAC_BIT_RESOLUTION      16u
#define UAC_BYTES_PER_SAMPLE    (UAC_BIT_RESOLUTION / 8u)
#define UAC_ISO_PACKET_SIZE     ((UAC_SAMPLE_RATE / 1000u) * UAC_CHANNELS * UAC_BYTES_PER_SAMPLE)
                                                /* = 192 */

/* USBD side (Target-facing): 16 kHz stereo 16-bit = 64 B/ms */
#define UAC_USBD_SAMPLE_RATE    16000u
#define UAC_USBD_ISO_PACKET_SIZE  ((UAC_USBD_SAMPLE_RATE / 1000u) * UAC_CHANNELS * UAC_BYTES_PER_SAMPLE)
                                                /* = 64 */

/* -----------------------------------------------------------------------
 * UAC 1.0 descriptor type/subtype codes
 * --------------------------------------------------------------------- */
#define USB_DESC_TYPE_CS_INTERFACE      0x24    /* CS_INTERFACE           */
#define USB_DESC_TYPE_CS_ENDPOINT       0x25    /* CS_ENDPOINT            */

/* AudioControl interface descriptor subtypes */
#define UAC_AC_HEADER                   0x01
#define UAC_AC_INPUT_TERMINAL           0x02
#define UAC_AC_OUTPUT_TERMINAL          0x03
#define UAC_AC_FEATURE_UNIT             0x06

/* AudioStreaming interface descriptor subtypes */
#define UAC_AS_GENERAL                  0x01
#define UAC_AS_FORMAT_TYPE              0x02

/* Endpoint subtypes */
#define UAC_EP_GENERAL                  0x01

/* Terminal types */
#define UAC_TERMINAL_USB_STREAMING      0x0101  /* USB streaming          */
#define UAC_TERMINAL_SPEAKER            0x0301  /* Generic Speaker        */
#define UAC_TERMINAL_MICROPHONE         0x0201  /* Generic Microphone     */

/* Audio data format: PCM */
#define UAC_FORMAT_PCM                  0x0001

/* -----------------------------------------------------------------------
 * USBFS-side (Host-facing) UAC descriptor block sizes
 *
 * AudioControl (IF4):
 *   AC Header         9 bytes
 *   Input  Terminal   12 bytes  (USB stream IN  → speaker path)
 *   Output Terminal   9  bytes
 *   Input  Terminal   12 bytes  (mic → USB stream OUT path)
 *   Output Terminal   9  bytes
 *   Total AC body     51 bytes
 *
 * Per AudioStreaming interface (alternate 0 = zero-bw + alternate 1 = active):
 *   IF descriptor     9  bytes  (alt 0, 0 EP)
 *   IF descriptor     9  bytes  (alt 1, 1 EP)
 *   AS General        7  bytes
 *   Format Type I     11 bytes
 *   Endpoint          9  bytes  (standard, ISO)
 *   CS Endpoint       7  bytes
 *   Total per AS      52 bytes  (9+9+7+11+9+7)
 *
 * Two AS interfaces: 52 × 2 = 104 bytes
 * Grand total added to USBFS config descriptor: 9(AC IF) + 52(AC CS) + 104 = 165 bytes
 * --------------------------------------------------------------------- */

/* AudioControl class-specific descriptor sizes */
/* AC Header bLength = 10 (8 fixed + 1 per baInterfaceNr; bInCollection=2 -> 8+2=10) */
#define UAC_AC_HEADER_LEN               10u
#define UAC_AC_INPUT_TERMINAL_LEN       12u
#define UAC_AC_OUTPUT_TERMINAL_LEN      9u
#define UAC_AC_INTERFACE_TOTAL_LEN      (UAC_AC_HEADER_LEN + \
                                         UAC_AC_INPUT_TERMINAL_LEN  + \
                                         UAC_AC_OUTPUT_TERMINAL_LEN + \
                                         UAC_AC_INPUT_TERMINAL_LEN  + \
                                         UAC_AC_OUTPUT_TERMINAL_LEN)
                                        /* 10+12+9+12+9 = 52 */

/* One AudioStreaming interface (alt0 + alt1 + CS descriptors + EP) */
#define UAC_AS_IFACE_LEN                (9u + 9u + 7u + 11u + 9u + 7u)
                                        /* = 52 */

/* Full UAC block appended to USBFS config descriptor */
#define UAC_USBFS_BLOCK_LEN             (9u + UAC_AC_INTERFACE_TOTAL_LEN + \
                                         UAC_AS_IFACE_LEN * 2u)
                                        /* 9(IF desc) + 52(AC CS) + 104(2×AS) = 165 */

/* Full UAC block appended to USBD config descriptor (same structure) */
#define UAC_USBD_BLOCK_LEN              UAC_USBFS_BLOCK_LEN

/* -----------------------------------------------------------------------
 * Endpoint numbers used for UAC
 * USBFS side: EP5 IN (ISO), EP6 OUT (ISO)
 * USBD  side: EP3 IN (ISO), EP4 OUT (ISO)
 * --------------------------------------------------------------------- */
#define UAC_USBFS_EP_IN_ADDR    0x85    /* EP5 IN  */
#define UAC_USBFS_EP_OUT_ADDR   0x06    /* EP6 OUT */
#define UAC_USBD_EP_IN_ADDR     0x84    /* EP4 IN  (EP3 reserved for CDC Bulk IN) */
#define UAC_USBD_EP_OUT_ADDR    0x05    /* EP5 OUT */

/* -----------------------------------------------------------------------
 * Helper macro: encode 3-byte little-endian sample rate
 * --------------------------------------------------------------------- */
#define UAC_SR_B0(sr)   ((uint8_t)((sr) & 0xFF))
#define UAC_SR_B1(sr)   ((uint8_t)(((sr) >> 8)  & 0xFF))
#define UAC_SR_B2(sr)   ((uint8_t)(((sr) >> 16) & 0xFF))

/* -----------------------------------------------------------------------
 * State flags (used by uac_handler.c)
 * --------------------------------------------------------------------- */
typedef struct {
    uint8_t usbfs_as_in_alt;    /* alternate setting for USBFS AudioStreaming IN  */
    uint8_t usbfs_as_out_alt;   /* alternate setting for USBFS AudioStreaming OUT */
    uint8_t usbd_as_in_alt;     /* alternate setting for USBD  AudioStreaming IN  */
    uint8_t usbd_as_out_alt;    /* alternate setting for USBD  AudioStreaming OUT */
} UAC_State_t;

extern UAC_State_t g_uac_state;

/* -----------------------------------------------------------------------
 * Public API (implemented in uac_handler.c)
 * --------------------------------------------------------------------- */
void UAC_Init(void);

#endif /* __UAC_DESC_H */
