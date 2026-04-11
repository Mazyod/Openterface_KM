/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2019/10/15
 * Description        : USB Descriptors optimized for Android compatibility.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
*******************************************************************************/
#include "usb_lib.h"
#include "usb_desc.h"
#include "../../include/uac_desc.h"

/* USB Device Descriptors */
const uint8_t  USBD_DeviceDescriptor[] = { 
    USBD_SIZE_DEVICE_DESC,           // bLength
    0x01,                           // bDescriptorType
    0x00, 0x02,                     // bcdUSB 2.00
#ifdef ENABLE_ETH
    0x02,                           // bDeviceClass: Communications Device Class (CDC-ECM, no IAD)
    0x00,                           // bDeviceSubClass
    0x00,                           // bDeviceProtocol
#else
    0xEF,                           // bDeviceClass: Miscellaneous (required for IAD)
    0x02,                           // bDeviceSubClass: Common Class
    0x01,                           // bDeviceProtocol: Interface Association Descriptor
#endif
    DEF_USBD_UEP0_SIZE,             // bMaxPacketSize0
    0x86, 0x1A,                     // idVendor
    0x0C, 0xFE,                     // idProduct
    0x00, 0x01,                     // bcdDevice
    0x01,                           // iManufacturer
    0x02,                           // iProduct
    0x00,                           // iSerialNumber
    0x01,                           // bNumConfigurations
};

/* USB Configuration Descriptors */
const uint8_t  USBD_ConfigDescriptor[] = { 
    /* Configuration Descriptor */
    0x09,                           // bLength
    0x02,                           // bDescriptorType
    USBD_SIZE_CONFIG_DESC & 0xFF, USBD_SIZE_CONFIG_DESC >> 8, // wTotalLength
#ifdef ENABLE_ETH
    0x02,                           // bNumInterfaces: ECM comm (IF0) + ECM data (IF1)
#else
    0x05,                           // bNumInterfaces: ACM(2) + UAC(3) = 5
#endif
    0x01,                           // bConfigurationValue
    0x00,                           // iConfiguration
    0x80,                           // bmAttributes: Bus Powered; Remote Wakeup
    0x32,                           // MaxPower: 100mA

#ifdef ENABLE_ETH
    /* ===== Pure CDC-ECM — USB Ethernet on the legacy USBD controller =========
     * bDeviceClass=0x02 (CDC) so NO IAD needed — single-function device.
     * EP1 IN interrupt (0x81): CDC NETWORK_CONNECTION notifications (8 bytes)
     * EP2 OUT bulk    (0x02): Ethernet frames host->device  (32 bytes/packet)
     * EP3 IN bulk     (0x83): Ethernet frames device->host  (32 bytes/packet)
     * ========================================================================= */

    /* Interface 0: CDC Communication (ECM) */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x00,                           // bInterfaceNumber 0
    0x00,                           // bAlternateSetting
    0x01,                           // bNumEndpoints 1 (EP1 interrupt IN)
    0x02,                           // bInterfaceClass (Communications)
    0x06,                           // bInterfaceSubClass (ECM)
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface

    /* CDC Header Functional Descriptor */
    0x05, 0x24, 0x00, 0x10, 0x01,   // bcdCDC 1.10

    /* CDC Union Functional Descriptor */
    0x05, 0x24, 0x06, 0x00, 0x01,   // masterIF=0, slaveIF=1

    /* CDC Ethernet Networking Functional Descriptor */
    0x0D,                           // bFunctionLength 13
    0x24,                           // bDescriptorType CS_INTERFACE
    0x0F,                           // bDescriptorSubtype Ethernet Networking
    0x04,                           // iMACAddress: string index 4 ("020000000001")
    0x00, 0x00, 0x00, 0x00,         // bmEthernetStatistics (none)
    0xEA, 0x05,                     // wMaxSegmentSize 1514
    0x00, 0x00,                     // wNumberMCFilters
    0x00,                           // bNumberPowerFilters

    /* EP1 IN Interrupt — CDC notifications to host */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x81,                           // bEndpointAddress (EP1 IN)
    0x03,                           // bmAttributes (Interrupt)
    0x08, 0x00,                     // wMaxPacketSize 8
    0x0A,                           // bInterval 10 ms

    /* Interface 1 alt0: CDC Data — zero-bandwidth (required by CDC-ECM spec) */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x01,                           // bInterfaceNumber 1
    0x00,                           // bAlternateSetting 0
    0x00,                           // bNumEndpoints 0
    0x0A,                           // bInterfaceClass (Data)
    0x00, 0x00, 0x00,               // SubClass, Protocol, iInterface

    /* Interface 1 alt1: CDC Data — active */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x01,                           // bInterfaceNumber 1
    0x01,                           // bAlternateSetting 1
    0x02,                           // bNumEndpoints 2
    0x0A,                           // bInterfaceClass (Data)
    0x00, 0x00, 0x00,               // SubClass, Protocol, iInterface

    /* EP2 OUT Bulk — Ethernet frames host->device (32B PMA buffer) */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x02,                           // bEndpointAddress (EP2 OUT)
    0x02,                           // bmAttributes (Bulk)
    0x20, 0x00,                     // wMaxPacketSize 32
    0x00,                           // bInterval

    /* EP3 IN Bulk — Ethernet frames device->host (32B PMA buffer) */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x83,                           // bEndpointAddress (EP3 IN)
    0x02,                           // bmAttributes (Bulk)
    0x20, 0x00,                     // wMaxPacketSize 32
    0x00,                           // bInterval

#else /* CDC-ACM (default) */

    /* IAD: CDC (IF0=comm + IF1=data) */
    0x08,                           // bLength
    0x0B,                           // bDescriptorType (IAD)
    0x00,                           // bFirstInterface: 0
    0x02,                           // bInterfaceCount: 2 (IF0+IF1)
    0x02,                           // bFunctionClass: Communications
    0x02,                           // bFunctionSubClass: Abstract Control Model
    0x01,                           // bFunctionProtocol: AT Commands
    0x00,                           // iFunction

    /* Interface 0 (CDC) descriptor */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x00,                           // bInterfaceNumber 0
    0x00,                           // bAlternateSetting
    0x01,                           // bNumEndpoints 1
    0x02,                           // bInterfaceClass (Communications)
    0x02,                           // bInterfaceSubClass (Abstract Control Model)
    0x01,                           // bInterfaceProtocol (AT Commands)
    0x00,                           // iInterface (String Index)

    /* Functional Descriptors */
    0x05,0x24,0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* Interrupt upload endpoint descriptor */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x81,                           // bEndpointAddress (IN/D2H)
    0x03,                           // bmAttributes (Interrupt)
    0x40, 0x00,                     // wMaxPacketSize 64
    0x01,                           // bInterval 1 (unit depends on device speed)

    /* Interface 1 (data interface) descriptor */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x01,                           // bInterfaceNumber 1
    0x00,                           // bAlternateSetting
    0x02,                           // bNumEndpoints 2
    0x0A,                           // bInterfaceClass (Data Interface)
    0x00,                           // bInterfaceSubClass
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface (String Index)

    /* Endpoint descriptor — CDC bulk OUT */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x02,                           // bEndpointAddress (OUT/H2D)
    0x02,                           // bmAttributes (Bulk)
    0x20, 0x00,                     // wMaxPacketSize 32 (reduced to free PMA for UAC EP5)
    0x00,                           // bInterval 0

    /* Endpoint descriptor — CDC bulk IN */
    0x07,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    0x83,                           // bEndpointAddress (IN/D2H)
    0x02,                           // bmAttributes (Bulk)
    0x20, 0x00,                     // wMaxPacketSize 32 (reduced to free PMA for UAC EP5)
    0x00,                           // bInterval 0

#endif /* ENABLE_ETH */

    /* ====== UAC 1.0 — USBD (Target) side ================================
     * IF2: AudioControl (1 streaming interface: IF3 only)
     * IF3: AudioStreaming IN — EP4 ISO IN (Host audio → Target, 64 B/ms)
     *   Single-buffer ISO, 64 bytes = max packet at FS, ~2ch@16kHz or 1ch@32kHz
     *   Constrained by USBD PMA: 512 B total, 448 B already used by EP0-3
     * =================================================================== */
#ifndef ENABLE_ETH

    /* IAD: UAC (IF2=AC + IF3=AS_IN + IF4=AS_OUT) */
    0x08,                           // bLength
    0x0B,                           // bDescriptorType (IAD)
    0x02,                           // bFirstInterface: 2
    0x03,                           // bInterfaceCount: 3 (IF2+IF3+IF4)
    0x01,                           // bFunctionClass: Audio
    0x00,                           // bFunctionSubClass
    0x00,                           // bFunctionProtocol
    0x00,                           // iFunction

    /* --- Interface Descriptor: AudioControl (IF2) --- */
    0x09,                           // bLength
    0x04,                           // bDescriptorType (Interface)
    0x02,                           // bInterfaceNumber 2
    0x00,                           // bAlternateSetting 0
    0x00,                           // bNumEndpoints 0
    0x01,                           // bInterfaceClass (Audio)
    0x01,                           // bInterfaceSubClass (AudioControl)
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface

    /* CS Interface: AC Header — 2 streaming interfaces (IF3 IN, IF4 OUT) */
    0x0A,                           // bLength (8 fixed + 2 baInterfaceNr = 10)
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_HEADER,                  // bDescriptorSubtype
    0x00, 0x01,                     // bcdADC 1.00
    0x34, 0x00,                     // wTotalLength: 10+12+9+12+9 = 52 = 0x34
    0x02,                           // bInCollection: 2 streaming interfaces
    0x03,                           // baInterfaceNr(1): IF3 (AudioStreaming IN)
    0x04,                           // baInterfaceNr(2): IF4 (AudioStreaming OUT)

    /* CS Interface: Input Terminal 1 — Microphone (physical input on Target side) */
    0x0C,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_INPUT_TERMINAL,          // bDescriptorSubtype
    0x01,                           // bTerminalID 1
    UAC_SR_B0(UAC_TERMINAL_MICROPHONE), UAC_SR_B1(UAC_TERMINAL_MICROPHONE),
    0x00,                           // bAssocTerminal
    UAC_CHANNELS,                   // bNrChannels
    0x03, 0x00,                     // wChannelConfig: L+R
    0x00,                           // iChannelNames
    0x00,                           // iTerminal

    /* CS Interface: Output Terminal 2 — USB Streaming OUT (mic → Host PC capture)
     * This terminal drives EP4 ISO IN (device→host).
     * bTerminalLink in IF3 CS AS General points here (0x02). */
    0x09,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_OUTPUT_TERMINAL,         // bDescriptorSubtype
    0x02,                           // bTerminalID 2
    UAC_SR_B0(UAC_TERMINAL_USB_STREAMING), UAC_SR_B1(UAC_TERMINAL_USB_STREAMING),
    0x00,                           // bAssocTerminal
    0x01,                           // bSourceID (IT1 — Microphone)
    0x00,                           // iTerminal

    /* CS Interface: Input Terminal 3 — USB Streaming IN (Host PC playback → Target spk)
     * This terminal feeds EP5 ISO OUT (host→device).
     * bTerminalLink in IF4 CS AS General points here (0x03). */
    0x0C,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_INPUT_TERMINAL,          // bDescriptorSubtype
    0x03,                           // bTerminalID 3
    UAC_SR_B0(UAC_TERMINAL_USB_STREAMING), UAC_SR_B1(UAC_TERMINAL_USB_STREAMING),
    0x00,                           // bAssocTerminal
    UAC_CHANNELS,                   // bNrChannels
    0x03, 0x00,                     // wChannelConfig: L+R
    0x00,                           // iChannelNames
    0x00,                           // iTerminal

    /* CS Interface: Output Terminal 4 — Speaker (physical output on Target side) */
    0x09,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_OUTPUT_TERMINAL,         // bDescriptorSubtype
    0x04,                           // bTerminalID 4
    UAC_SR_B0(UAC_TERMINAL_SPEAKER), UAC_SR_B1(UAC_TERMINAL_SPEAKER),
    0x00,                           // bAssocTerminal
    0x03,                           // bSourceID (IT3 — USB Streaming IN)
    0x00,                           // iTerminal

    /* --- IF3 AudioStreaming IN — alt0, zero-bandwidth --- */
    0x09, 0x04, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,

    /* --- IF3 AudioStreaming IN — alt1, active (EP4, 64 bytes/ms) --- */
    0x09, 0x04, 0x03, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,

    /* CS Interface: AS General — linked to OT2 (USB Streaming OUT = Capture path)
     * EP4 ISO IN sends mic audio from Target to Host PC. */
    0x07, USB_DESC_TYPE_CS_INTERFACE, UAC_AS_GENERAL,
    0x02,                           // bTerminalLink (OT2 — USB Streaming OUT)
    0x00,
    UAC_SR_B0(UAC_FORMAT_PCM), UAC_SR_B1(UAC_FORMAT_PCM),

    /* CS Interface: Format Type I — 64 B/ms = stereo 16kHz or mono 32kHz */
    0x0B, USB_DESC_TYPE_CS_INTERFACE, UAC_AS_FORMAT_TYPE,
    0x01,                           // bFormatType: Type I
    UAC_CHANNELS,                   // bNrChannels: 2
    UAC_BYTES_PER_SAMPLE,           // bSubFrameSize: 2
    UAC_BIT_RESOLUTION,             // bBitResolution: 16
    0x01,                           // bSamFreqType: 1 frequency
    UAC_SR_B0(UAC_USBD_SAMPLE_RATE), UAC_SR_B1(UAC_USBD_SAMPLE_RATE), UAC_SR_B2(UAC_USBD_SAMPLE_RATE),

    /* Endpoint: EP4 ISO IN — single buffer, 64 bytes */
    0x09, 0x05, UAC_USBD_EP_IN_ADDR, 0x01,
    0x40, 0x00,                     // wMaxPacketSize: 64
    0x01, 0x00, 0x00,               // bInterval=1ms, bRefresh=0, bSynchAddr=0

    /* CS Endpoint */
    0x07, USB_DESC_TYPE_CS_ENDPOINT, UAC_EP_GENERAL, 0x00, 0x00, 0x00, 0x00,

    /* ================================================================
     * IF4: AudioStreaming OUT — Target→Host (microphone capture)
     *   alt0 = zero-bandwidth (no EP)
     *   alt1 = active, EP5 ISO OUT 64 bytes/ms (16kHz stereo 16-bit)
     * ================================================================ */

    /* IF4 alt0 — zero-bandwidth */
    0x09, 0x04,                     // bLength, bDescriptorType (Interface)
    0x04,                           // bInterfaceNumber: 4
    0x00,                           // bAlternateSetting: 0
    0x00,                           // bNumEndpoints: 0
    0x01,                           // bInterfaceClass: Audio
    0x02,                           // bInterfaceSubClass: AudioStreaming
    0x00, 0x00,                     // bInterfaceProtocol, iInterface

    /* IF4 alt1 — active streaming */
    0x09, 0x04,                     // bLength, bDescriptorType (Interface)
    0x04,                           // bInterfaceNumber: 4
    0x01,                           // bAlternateSetting: 1
    0x01,                           // bNumEndpoints: 1
    0x01,                           // bInterfaceClass: Audio
    0x02,                           // bInterfaceSubClass: AudioStreaming
    0x00, 0x00,                     // bInterfaceProtocol, iInterface

    /* CS Interface: AS General — linked to IT3 (USB Streaming IN = Render path)
     * EP5 ISO OUT receives playback audio from Host PC to play on Target. */
    0x07,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AS_GENERAL,                 // bDescriptorSubtype
    0x03,                           // bTerminalLink: IT3 (USB Streaming IN → speaker)
    0x00,                           // bDelay
    UAC_SR_B0(UAC_FORMAT_PCM), UAC_SR_B1(UAC_FORMAT_PCM), // wFormatTag: PCM

    /* CS Interface: Format Type I — 16kHz stereo 16-bit */
    0x0B,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AS_FORMAT_TYPE,             // bDescriptorSubtype
    0x01,                           // bFormatType: Type I
    UAC_CHANNELS,                   // bNrChannels
    UAC_BYTES_PER_SAMPLE,           // bSubFrameSize
    UAC_BIT_RESOLUTION,             // bBitResolution
    0x01,                           // bSamFreqType: 1 frequency
    UAC_SR_B0(UAC_USBD_SAMPLE_RATE), UAC_SR_B1(UAC_USBD_SAMPLE_RATE), UAC_SR_B2(UAC_USBD_SAMPLE_RATE),

    /* Endpoint: EP5 ISO OUT — single buffer, 64 bytes/ms */
    0x09,                           // bLength
    0x05,                           // bDescriptorType (Endpoint)
    UAC_USBD_EP_OUT_ADDR,           // bEndpointAddress: EP5 OUT (0x05)
    0x01,                           // bmAttributes: Isochronous
    0x40, 0x00,                     // wMaxPacketSize: 64
    0x01, 0x00, 0x00,               // bInterval=1ms, bRefresh=0, bSynchAddr=0

    /* CS Endpoint */
    0x07, USB_DESC_TYPE_CS_ENDPOINT, UAC_EP_GENERAL, 0x00, 0x00, 0x00, 0x00,

#endif /* !ENABLE_ETH — UAC section ends here */

};


/* USB String Descriptors */
const uint8_t USBD_StringLangID[USBD_SIZE_STRING_LANGID] = {
	USBD_SIZE_STRING_LANGID,
	USB_STRING_DESCRIPTOR_TYPE,
	0x09,
	0x04 
};

/* USB Device String Vendor */
const uint8_t USBD_StringVendor[USBD_SIZE_STRING_VENDOR] = {
	USBD_SIZE_STRING_VENDOR,    
	USB_STRING_DESCRIPTOR_TYPE,           
	'w',0,'c',0,'h',0,'.',0,'c',0,'n',0
};

/* USB Device String Product */
const uint8_t USBD_StringProduct[USBD_SIZE_STRING_PRODUCT] = {
    USBD_SIZE_STRING_PRODUCT,
    USB_STRING_DESCRIPTOR_TYPE,
    'U', 0, 'S', 0, 'B', 0, ' ', 0, 'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0};

/* USB Device String Serial */
uint8_t USBD_StringSerial[USBD_SIZE_STRING_SERIAL] = {
	USBD_SIZE_STRING_SERIAL,          
	USB_STRING_DESCRIPTOR_TYPE,                   
	'0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0 , '6', 0, '7', 0, '8', 0, '9', 0
};

#ifdef ENABLE_ETH
/* CDC-ECM MAC address string (string index 4).
 * bLength=26, bDescriptorType=0x03, "020000000001" as UTF-16LE.
 * MAC 02:00:00:00:00:01 — locally administered, unicast. */
const uint8_t USBD_StringMAC[USBD_SIZE_STRING_MAC] = {
    26, USB_STRING_DESCRIPTOR_TYPE,
    '0',0, '2',0, '0',0, '0',0, '0',0, '0',0,     /* "020000" */
    '0',0, '0',0, '0',0, '0',0, '0',0, '1',0      /* "000001" */
};
#endif /* ENABLE_ETH */


