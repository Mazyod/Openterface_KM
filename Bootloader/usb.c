/* USB FS Device Driver for Bootloader
 *
 * Adapted from WCH IAP reference implementation.
 * Provides USB bulk transport on EP2 for firmware update.
 */
#include "ch32v20x.h"
#include "ch32v20x_usb.h"
#include "debug.h"
#include "usb.h"
#include "protocol.h"
#include <string.h>

/* USB PID definitions */
#define PID_OUT                 0
#define PID_SOF                 1
#define PID_IN                  2
#define PID_SETUP               3

/* USB buffer addresses (from WCH IAP) */
#define ENDP0_RXADDR            0x40
#define ENDP0_TXADDR            0x80
#define ENDP2_TXADDR            0xD0
#define ENDP2_RXADDR            0xE0

/* DMA buffers - must be 4-byte aligned */
__attribute__((aligned(4))) static uint8_t EP0_DatabufHD[64];
__attribute__((aligned(4))) static uint8_t EP2_DatabufHD[128];  /* 64 OUT + 64 IN */

/* Global state */
static volatile uint16_t SetupReqLen = 0;
static volatile uint8_t SetupReqCode = 0;
static volatile uint8_t DevConfig = 0;
static volatile uint8_t DevAddress = 0;
static const uint8_t *pDescr;  /* Descriptor pointer for GET_DESCRIPTOR across SETUP/IN tokens */

/* EP2 data buffers */
static uint8_t EP2_Rx_Buffer[64];
static uint8_t EP2_Tx_Buffer[64];
static volatile uint8_t EP2_OUT_Flag = 0;
static volatile uint16_t EP2_Rx_Cnt = 0;

/* Pointer aliases */
static uint8_t *pEP0_RAM_Addr = EP0_DatabufHD;
static uint8_t *pEP2_RAM_Addr = EP2_DatabufHD;

#define pSetupReqPakHD      ((USB_SETUP_REQ*)pEP0_RAM_Addr)
#define pEP0_DataBuf        (pEP0_RAM_Addr)
#define pEP2_OUT_DataBuf    (pEP2_RAM_Addr)
#define pEP2_IN_DataBuf     (pEP2_RAM_Addr + 64)

/* Device descriptor */
static const uint8_t MyDevDescr[] = {
    0x12,       /* bLength */
    0x01,       /* bDescriptorType: Device */
    0x10, 0x01, /* bcdUSB: USB 1.1 */
    0xFF,       /* bDeviceClass: Vendor Specific */
    0x80,       /* bDeviceSubClass */
    0x55,       /* bDeviceProtocol */
    EP0_SIZE,   /* bMaxPacketSize0 */
    USB_VENDOR_ID_LO, USB_VENDOR_ID_HI,   /* idVendor */
    USB_PRODUCT_ID_LO, USB_PRODUCT_ID_HI, /* idProduct */
    0x02, 0x00, /* bcdDevice: v2.0 — force fresh descriptor read */
    0x00,       /* iManufacturer */
    0x00,       /* iProduct */
    0x00,       /* iSerialNumber */
    0x01        /* bNumConfigurations */
};

/* Configuration descriptor */
static const uint8_t MyCfgDescr[] = {
    /* Configuration descriptor */
    0x09,       /* bLength */
    0x02,       /* bDescriptorType: Configuration */
    0x20, 0x00, /* wTotalLength: 32 bytes */
    0x01,       /* bNumInterfaces: 1 */
    0x01,       /* bConfigurationValue */
    0x00,       /* iConfiguration */
    0x80,       /* bmAttributes: Bus powered */
    0x32,       /* MaxPower: 100mA */

    /* Interface descriptor */
    0x09,       /* bLength */
    0x04,       /* bDescriptorType: Interface */
    0x00,       /* bInterfaceNumber */
    0x00,       /* bAlternateSetting */
    0x02,       /* bNumEndpoints: 2 */
    0xFF,       /* bInterfaceClass: Vendor Specific */
    0x80,       /* bInterfaceSubClass */
    0x55,       /* bInterfaceProtocol */
    0x00,       /* iInterface */

    /* EP2 IN descriptor */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType: Endpoint */
    0x82,       /* bEndpointAddress: EP2 IN */
    0x02,       /* bmAttributes: Bulk */
    EP2_SIZE, 0x00, /* wMaxPacketSize */
    0x00,       /* bInterval */

    /* EP2 OUT descriptor */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType: Endpoint */
    0x02,       /* bEndpointAddress: EP2 OUT */
    0x02,       /* bmAttributes: Bulk */
    EP2_SIZE, 0x00, /* wMaxPacketSize */
    0x00        /* bInterval */
};

/* Forward declarations */
static void USBFS_RCC_Init(void);
static void USBDeviceInit(void);
static void DevEP2_IN_Deal(uint8_t len);
static void DevEP2_OUT_Deal(uint8_t len);
static uint8_t RecData_Deal(void);

/* USB clock initialization */
static void USBFS_RCC_Init(void) {
    RCC_ClocksTypeDef RCC_ClocksStatus = {0};
    RCC_GetClocksFreq(&RCC_ClocksStatus);

    if (RCC_ClocksStatus.SYSCLK_Frequency == 144000000) {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
    } else if (RCC_ClocksStatus.SYSCLK_Frequency == 96000000) {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
    } else if (RCC_ClocksStatus.SYSCLK_Frequency == 48000000) {
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);
    }

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
}

/* USB device initialization */
static void USBDeviceInit(void) {
    USBFSD->BASE_CTRL = 0x00;

    /* Enable EP2 RX + TX */
    USBFSD->UEP2_3_MOD = USBFS_UEP2_RX_EN | USBFS_UEP2_TX_EN;
    USBFSD->UEP0_DMA = (uint32_t)pEP0_RAM_Addr;
    USBFSD->UEP2_DMA = (uint32_t)pEP2_RAM_Addr;

    /* Endpoint control */
    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP2_RX_CTRL = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP2_TX_LEN = 0;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP2_TX_CTRL = USBFS_UEP_T_RES_NAK | USBFS_UEP_T_AUTO_TOG;

    /* Clear interrupts and enable */
    USBFSD->INT_FG = 0xFF;
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
    USBFSD->DEV_ADDR = 0x00;

    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
}

/* USB FS initialization */
void usb_init(void) {
    USBFS_RCC_Init();
    Delay_Us(100);

    /* USB port reset sequence (from WCH IAP) */
    USBFSD->BASE_CTRL &= ~USBFS_UC_DEV_PU_EN;
    Delay_Ms(700);
    USBFSD->BASE_CTRL |= USBFS_UC_DEV_PU_EN;

    USBDeviceInit();
    NVIC_EnableIRQ(USBFS_IRQn);
}

/* USB interrupt handler */
void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBFS_IRQHandler(void) {
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;

    intflag = USBFSD->INT_FG;

    if (intflag & USBFS_UIF_TRANSFER) {
        switch (USBFSD->INT_ST & USBFS_UIS_TOKEN_MASK) {
            case USBFS_UIS_TOKEN_SETUP:
                USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
                USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                SetupReqLen = pSetupReqPakHD->wLength;
                SetupReqCode = pSetupReqPakHD->bRequest;
                chtype = pSetupReqPakHD->bRequestType;
                len = 0;
                errflag = 0;

                switch (SetupReqCode) {
                    case USB_GET_DESCRIPTOR:
                        switch ((pSetupReqPakHD->wValue) >> 8) {
                            case 0x01:  /* Device descriptor */
                                pDescr = MyDevDescr;
                                len = MyDevDescr[0];
                                break;
                            case 0x02:  /* Configuration descriptor */
                                pDescr = MyCfgDescr;
                                len = MyCfgDescr[2];
                                break;
                            default:
                                errflag = 0xFF;
                                break;
                        }
                        if (SetupReqLen > len) SetupReqLen = len;
                        len = (SetupReqLen >= EP0_SIZE) ? EP0_SIZE : SetupReqLen;
                        memcpy(pEP0_DataBuf, pDescr, len);
                        pDescr += len;
                        break;

                    case USB_SET_ADDRESS:
                        SetupReqLen = (pSetupReqPakHD->wValue) & 0xFF;
                        break;

                    case USB_GET_CONFIGURATION:
                        pEP0_DataBuf[0] = DevConfig;
                        if (SetupReqLen > 1) SetupReqLen = 1;
                        break;

                    case USB_SET_CONFIGURATION:
                        DevConfig = (pSetupReqPakHD->wValue) & 0xFF;
                        break;

                    case USB_GET_STATUS:
                        pEP0_DataBuf[0] = 0x00;
                        pEP0_DataBuf[1] = 0x00;
                        if (SetupReqLen > 2) SetupReqLen = 2;
                        break;

                    default:
                        errflag = 0xFF;
                        break;
                }

                if (errflag == 0xFF) {
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
                } else {
                    if (chtype & 0x80) {
                        len = (SetupReqLen > EP0_SIZE) ? EP0_SIZE : SetupReqLen;
                        SetupReqLen -= len;
                    } else {
                        len = 0;
                    }
                    USBFSD->UEP0_TX_LEN = len;
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                }
                break;

            case USBFS_UIS_TOKEN_IN:
                switch (USBFSD->INT_ST & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK)) {
                    case USBFS_UIS_TOKEN_IN:
                        switch (SetupReqCode) {
                            case USB_GET_DESCRIPTOR:
                                len = SetupReqLen >= EP0_SIZE ? EP0_SIZE : SetupReqLen;
                                memcpy(pEP0_DataBuf, pDescr, len);
                                SetupReqLen -= len;
                                pDescr += len;
                                USBFSD->UEP0_TX_LEN = len;
                                USBFSD->UEP0_TX_CTRL ^= USBFS_UEP_T_TOG;
                                break;
                            case USB_SET_ADDRESS:
                                USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | SetupReqLen;
                                USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
                                USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
                                break;
                            default:
                                USBFSD->UEP0_TX_LEN = 0;
                                USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
                                USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
                                break;
                        }
                        break;
                    case USBFS_UIS_TOKEN_IN | 2:
                        USBFSD->UEP2_TX_CTRL ^= USBFS_UEP_T_TOG;
                        USBFSD->UEP2_TX_CTRL = (USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
                        break;
                    default:
                        break;
                }
                break;

            case USBFS_UIS_TOKEN_OUT:
                switch (USBFSD->INT_ST & (USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK)) {
                    case USBFS_UIS_TOKEN_OUT | 2:
                        if (USBFSD->INT_ST & USBFS_UIS_TOG_OK) {
                            USBFSD->UEP2_RX_CTRL ^= USBFS_UEP_R_TOG;
                            len = USBFSD->RX_LEN;
                            DevEP2_OUT_Deal(len);
                        }
                        break;
                    default:
                        break;
                }
                break;

            default:
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    } else if (intflag & USBFS_UIF_BUS_RST) {
        USBFSD->DEV_ADDR = 0;
        USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
        USBFSD->UEP2_RX_CTRL = USBFS_UEP_R_RES_ACK;
        USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
        USBFSD->UEP2_TX_CTRL = USBFS_UEP_T_RES_NAK;
        USBFSD->INT_FG |= USBFS_UIF_BUS_RST;
    } else if (intflag & USBFS_UIF_SUSPEND) {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
    } else {
        USBFSD->INT_FG = intflag;
    }
}

/* EP2 OUT data handler */
static void DevEP2_OUT_Deal(uint8_t len) {
    if (len > 0 && len <= 64) {
        memcpy(EP2_Rx_Buffer, pEP2_OUT_DataBuf, len);
        EP2_Rx_Cnt = len;
        EP2_OUT_Flag = 1;
    }
}

/* EP2 IN data handler */
static void DevEP2_IN_Deal(uint8_t len) {
    USBFSD->UEP2_TX_LEN = len;
    USBFSD->UEP2_TX_CTRL = (USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
}

/* Process received data */
static uint8_t RecData_Deal(void) {
    uint8_t status;

    /* Call protocol handler */
    protocol_process(EP2_Rx_Buffer, EP2_Rx_Cnt);

    /* For now, always return success */
    /* TODO: protocol_process should return status */
    status = 0x00;  /* success */

    /* Send 2-byte response */
    EP2_Tx_Buffer[0] = 0x00;
    EP2_Tx_Buffer[1] = status;
    memcpy(pEP2_IN_DataBuf, EP2_Tx_Buffer, 2);
    DevEP2_IN_Deal(2);

    return status;
}

/* Poll for USB data */
void usb_poll(void) {
    if (EP2_OUT_Flag) {
        EP2_OUT_Flag = 0;
        RecData_Deal();

        /* Re-enable EP2 RX */
        USBFSD->UEP2_RX_CTRL = (USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
    }
}

/* Check if data is available */
uint8_t usb_data_available(void) {
    return EP2_OUT_Flag;
}

/* Read data from USB */
uint16_t usb_read(uint8_t *buf, uint16_t max_len) {
    if (!EP2_OUT_Flag || max_len == 0) {
        return 0;
    }

    uint16_t len = (EP2_Rx_Cnt < max_len) ? EP2_Rx_Cnt : max_len;
    memcpy(buf, EP2_Rx_Buffer, len);
    EP2_OUT_Flag = 0;

    /* Re-enable EP2 RX for next packet */
    USBFSD->UEP2_RX_CTRL = (USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;

    return len;
}

/* Send data via USB */
void usb_send(const uint8_t *data, uint16_t len) {
    if (len > 64) len = 64;
    memcpy(pEP2_IN_DataBuf, data, len);
    DevEP2_IN_Deal(len);
}
