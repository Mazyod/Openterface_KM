/********************************** (C) COPYRIGHT *******************************
 * File Name          : composite_km_desc.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : All descriptors for the keyboard and mouse composite device.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header File */
#include "usbd_desc.h"

/*******************************************************************************/
/* Device Descriptor */
const uint8_t MyDevDescr[ ] =
{
    0x12,                                                   // bLength
    0x01,                                                   // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                    // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,                                     // bMaxPacketSize0
    (uint8_t)DEF_USB_VID, (uint8_t)( DEF_USB_VID >> 8 ),    // idVendor
    (uint8_t)DEF_USB_PID, (uint8_t)( DEF_USB_PID >> 8 ),    // idProduct
    0x00, DEF_IC_PRG_VER,                                   // bcdDevice
    0x01,                                                   // iManufacturer
    0x02,                                                   // iProduct
    0x03,                                                   // iSerialNumber
    0x01,                                                   // bNumConfigurations
};

/* Configuration Descriptor Set */
const uint8_t MyCfgDescr[ ] =
{
    /* Configuration Descriptor */
    0x09,                                                   // bLength
    0x02,                                                   // bDescriptorType
    0x54, 0x00,                                             // wTotalLength (84 bytes total)
    0x03,                                                   // bNumInterfaces (changed from 2 to 3)
    0x01,                                                   // bConfigurationValue
    0x00,                                                   // iConfiguration
    0xA0,                                                   // bmAttributes: Bus Powered; Remote Wakeup
    0x32,                                                   // MaxPower: 100mA

    /* Interface Descriptor (Keyboard) */
    0x09,                                                   // bLength
    0x04,                                                   // bDescriptorType
    0x00,                                                   // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    0x03,                                                   // bInterfaceClass
    0x01,                                                   // bInterfaceSubClass
    0x01,                                                   // bInterfaceProtocol: Keyboard
    0x00,                                                   // iInterface

    /* HID Descriptor (Keyboard) */
    0x09,                                                   // bLength
    0x21,                                                   // bDescriptorType
    0x11, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    0x22,                                                   // bDescriptorType
    0x3E, 0x00,                                             // wDescriptorLength

    /* Endpoint Descriptor (Keyboard) */
    0x07,                                                   // bLength
    0x05,                                                   // bDescriptorType
    0x81,                                                   // bEndpointAddress: IN Endpoint 1
    0x03,                                                   // bmAttributes
    0x08, 0x00,                                             // wMaxPacketSize
    0x0A,                                                   // bInterval: 10mS

    /* Interface Descriptor (Mouse) */
    0x09,                                                   // bLength
    0x04,                                                   // bDescriptorType
    0x01,                                                   // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    0x03,                                                   // bInterfaceClass
    0x00,                                                   // bInterfaceSubClass
    0x00,                                                   // bInterfaceProtocol: Mouse
    0x00,                                                   // iInterface
  


    /* HID Descriptor (Mouse) */
    0x09,                                                   // bLength
    0x21,                                                   // bDescriptorType
    0x10, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    0x22,                                                   // bDescriptorType
    0x34, 0x00,                                             // wDescriptorLength

    /* Endpoint Descriptor (Mouse) */
    0x07,                                                   // bLength
    0x05,                                                   // bDescriptorType
    0x82,                                                   // bEndpointAddress: IN Endpoint 2
    0x03,                                                   // bmAttributes
    0x08, 0x00,                                             // wMaxPacketSize
    0x01,                                                   // bInterval: 1mS

    /* Interface Descriptor (Absolute Mouse) */
    0x09,                                                   // bLength
    0x04,                                                   // bDescriptorType
    0x02,                                                   // bInterfaceNumber
    0x00,                                                   // bAlternateSetting
    0x01,                                                   // bNumEndpoints
    0x03,                                                   // bInterfaceClass
    0x00,                                                   // bInterfaceSubClass
    0x00,                                                   // bInterfaceProtocol: None (custom)
    0x00,                                                   // iInterface

    /* HID Descriptor (Absolute Mouse) */
    0x09,                                                   // bLength
    0x21,                                                   // bDescriptorType
    0x10, 0x01,                                             // bcdHID
    0x00,                                                   // bCountryCode
    0x01,                                                   // bNumDescriptors
    0x22,                                                   // bDescriptorType
    0x3F, 0x00,                                             // wDescriptorLength (63 bytes)

    /* Endpoint Descriptor (Absolute Mouse) */
    0x07,                                                   // bLength
    0x05,                                                   // bDescriptorType
    0x83,                                                   // bEndpointAddress: IN Endpoint 3
    0x03,                                                   // bmAttributes
    0x08, 0x00,                                             // wMaxPacketSize
    0x01                                                    // bInterval: 1mS (125µs at HS, 1ms at FS)
};

/* Keyboard Report Descriptor */
const uint8_t KeyRepDesc[ ] =
{
    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x06,                                             // Usage (Keyboard)
    0xA1, 0x01,                                             // Collection (Application)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0xE0,                                             // Usage Minimum (224)
    0x29, 0xE7,                                             // Usage Maximum (231)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x25, 0x01,                                             // Logical Maximum (1)
    0x75, 0x01,                                             // Report Size (1)
    0x95, 0x08,                                             // Report Count (8)
    0x81, 0x02,                                             // Input (Data,Variable,Absolute)
    0x95, 0x01,                                             // Report Count (1)
    0x75, 0x08,                                             // Report Size (8)
    0x81, 0x01,                                             // Input (Constant)
    0x95, 0x03,                                             // Report Count (3)
    0x75, 0x01,                                             // Report Size (1)
    0x05, 0x08,                                             // Usage Page (LEDs)
    0x19, 0x01,                                             // Usage Minimum (1)
    0x29, 0x03,                                             // Usage Maximum (3)
    0x91, 0x02,                                             // Output (Data,Variable,Absolute)
    0x95, 0x05,                                             // Report Count (5)
    0x75, 0x01,                                             // Report Size (1)
    0x91, 0x01,                                             // Output (Constant,Array,Absolute)
    0x95, 0x06,                                             // Report Count (6)
    0x75, 0x08,                                             // Report Size (8)
    0x26, 0xFF, 0x00,                                       // Logical Maximum (255)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0x00,                                             // Usage Minimum (0)
    0x29, 0x91,                                             // Usage Maximum (145)
    0x81, 0x00,                                             // Input(Data,Array,Absolute)
    0xC0                                                    // End Collection
};

const uint8_t TouchRepDesc[] =
{
    0x05, 0x0D,       // Usage Page (Digitizers)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)

    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x42,       //     Usage (Tip Switch)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x75, 0x01,       //     Report Size (1)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)

    0x95, 0x07,       //     Report Count (7)
    0x81, 0x03,       //     Input (Const,Var,Abs)

    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x16, 0x00, 0x00, //     Logical Min (0)
    0x26, 0xFF, 0x0F, //     Logical Max (4095)
    0x36, 0x00, 0x00, //     Physical Min (0)
    0x46, 0xFF, 0x0F, //     Physical Max (4095)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x02,       //     Report Count (2)
    0x81, 0x02,       //     Input (Data,Var,Abs)

    0xC0,             //   End Collection
    0xC0              // End Collection
};



/* Qualifier Descriptor */
const uint8_t  MyQuaDesc[ ] =
{
    0x0A,                                                   // bLength
    0x06,                                                   // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                   // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    0x40,                                                   // bMaxPacketSize0
    0x00,                                                   // bNumConfigurations
    0x00                                                    // bReserved
};

/* Language Descriptor */
const uint8_t MyLangDescr[ ] =
{
    0x04,       // Descriptor length (4 bytes)
    0x03,       // Descriptor type (STRING)
    0x09, 0x04  // LANGID: English (United States)
};

/* Manufacturer Descriptor */
const uint8_t MyManuInfo[ ] =
{
    0x1A, // Descriptor length (13 chars * 2 + 2)
    0x03, // Descriptor type (STRING)
    'T',0, // TechxArtisan
    'e',0,
    'c',0,
    'h',0,
    'x',0,
    'A',0,
    'r',0,
    't',0,
    'i',0,
    's',0,
    'a',0,
    'n',0
};

/* Product Information */
const uint8_t MyProdInfo[ ]  =
{
    0x0E, // Descriptor length (6 chars * 2 + 2)
    0x03, // Descriptor type (STRING)
    'K', 0,  // KeyMod
    'e', 0,
    'y', 0,
    'M', 0,
    'o', 0,
    'd', 0
};

/* Serial Number Information */
const uint8_t  MySerNumInfo[ ] =
{
    0x16,
    0x03,
    '0',
    0,
    '1',
    0,
    '2',
    0,
    '3',
    0,
    '4',
    0,
    '5',
    0,
    '6',
    0,
    '7',
    0,
    '8',
    0,
    '9',
    0
};
