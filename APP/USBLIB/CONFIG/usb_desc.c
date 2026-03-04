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
    0x10, 0x01,                     // bcdUSB
    0xEF,                           // bDeviceClass: Miscellaneous (required for IAD)
    0x02,                           // bDeviceSubClass: Common Class
    0x01,                           // bDeviceProtocol: Interface Association Descriptor
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
    0x05,                           // bNumInterfaces: 2(CDC) + 1(AC) + 2(AS) = 5
    0x01,                           // bConfigurationValue
    0x00,                           // iConfiguration
    0x80,                           // bmAttributes: Bus Powered; Remote Wakeup
    0x32,                           // MaxPower: 100mA

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

    /* ====== UAC 1.0 — USBD (Target) side ================================
     * IF2: AudioControl (1 streaming interface: IF3 only)
     * IF3: AudioStreaming IN — EP4 ISO IN (Host audio → Target, 64 B/ms)
     *   Single-buffer ISO, 64 bytes = max packet at FS, ~2ch@16kHz or 1ch@32kHz
     *   Constrained by USBD PMA: 512 B total, 448 B already used by EP0-3
     * =================================================================== */

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

    /* CS Interface: Input Terminal 1 — USB streaming (Host audio IN) */
    0x0C,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_INPUT_TERMINAL,          // bDescriptorSubtype
    0x01,                           // bTerminalID 1
    UAC_SR_B0(UAC_TERMINAL_USB_STREAMING), UAC_SR_B1(UAC_TERMINAL_USB_STREAMING),
    0x00,                           // bAssocTerminal
    UAC_CHANNELS,                   // bNrChannels
    0x03, 0x00,                     // wChannelConfig: L+R
    0x00,                           // iChannelNames
    0x00,                           // iTerminal

    /* CS Interface: Output Terminal 2 — Speaker */
    0x09,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_OUTPUT_TERMINAL,         // bDescriptorSubtype
    0x02,                           // bTerminalID 2
    UAC_SR_B0(UAC_TERMINAL_SPEAKER), UAC_SR_B1(UAC_TERMINAL_SPEAKER),
    0x00,                           // bAssocTerminal
    0x01,                           // bSourceID (IT1)
    0x00,                           // iTerminal

    /* CS Interface: Input Terminal 3 — Microphone (stub, required by UAC topology) */
    0x0C,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_INPUT_TERMINAL,          // bDescriptorSubtype
    0x03,                           // bTerminalID 3
    UAC_SR_B0(UAC_TERMINAL_MICROPHONE), UAC_SR_B1(UAC_TERMINAL_MICROPHONE),
    0x00,                           // bAssocTerminal
    UAC_CHANNELS,                   // bNrChannels
    0x03, 0x00,                     // wChannelConfig: L+R
    0x00,                           // iChannelNames
    0x00,                           // iTerminal

    /* CS Interface: Output Terminal 4 — USB streaming (stub) */
    0x09,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AC_OUTPUT_TERMINAL,         // bDescriptorSubtype
    0x04,                           // bTerminalID 4
    UAC_SR_B0(UAC_TERMINAL_USB_STREAMING), UAC_SR_B1(UAC_TERMINAL_USB_STREAMING),
    0x00,                           // bAssocTerminal
    0x03,                           // bSourceID (IT3)
    0x00,                           // iTerminal

    /* --- IF3 AudioStreaming IN — alt0, zero-bandwidth --- */
    0x09, 0x04, 0x03, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,

    /* --- IF3 AudioStreaming IN — alt1, active (EP4, 64 bytes/ms) --- */
    0x09, 0x04, 0x03, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00,

    /* CS Interface: AS General (linked to IT1 — USB streaming IN) */
    0x07, USB_DESC_TYPE_CS_INTERFACE, UAC_AS_GENERAL,
    0x01,                           // bTerminalLink (IT1)
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

    /* CS Interface: AS General (linked to OT4 — USB streaming output terminal, mic→Host) */
    0x07,                           // bLength
    USB_DESC_TYPE_CS_INTERFACE,     // bDescriptorType
    UAC_AS_GENERAL,                 // bDescriptorSubtype
    0x04,                           // bTerminalLink: OT4 (USB Streaming OUT = CAPTURE path)
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


