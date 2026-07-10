/*
 * USBD Device Driver for Bootloader (CH32V20x)
 *
 * Uses the host-facing USBD peripheral at 0x40005C00 (PA11/PA12).
 * This is a PMA-based STM32-like USB peripheral, NOT the USBFS at 0x50000000.
 *
 * EP0: Control (setup/descriptors)
 * EP2: Bulk IN/OUT (bootloader protocol data)
 */
#include "ch32v20x.h"
#include "debug.h"
#include "usb.h"
#include "protocol.h"
#include <string.h>

/* ================================================================
 * USBD register definitions (PMA-based, at 0x40005C00)
 * ================================================================ */
#define USBD_BASE       0x40005C00UL
#define USBD_PMA        0x40006000UL

#define USBD_EP(n)      (*(volatile uint16_t *)(USBD_BASE + (n) * 4))
/* All USBD registers are 16-bit (CNTR, ISTR, DADDR, BTABLE) */
#define USBD_CNTLR      (*(volatile uint16_t *)(USBD_BASE + 0x40))
#define USBD_ISTR       (*(volatile uint16_t *)(USBD_BASE + 0x44))
#define USBD_FNR        (*(volatile uint16_t *)(USBD_BASE + 0x48))
#define USBD_DADDR      (*(volatile uint16_t *)(USBD_BASE + 0x4C))
#define USBD_BTABLE     (*(volatile uint16_t *)(USBD_BASE + 0x50))

/* PMA access — data buffers use half-word addresses (byte offset = addr * 2).
 * BTABLE entries use byte offsets from PMA start (BTABLE=0 → byte 0).
 * Use the helpers below, NOT raw PMA_BYTE. */

/* CNTR (CNTLR register) bits */
#define CNTR_FRES       0x0001
#define CNTR_PDWN       0x0002
#define CNTR_LP_MODE    0x0004
#define CNTR_FSUSP      0x0008
#define CNTR_RESUME     0x0010
#define CNTR_RESETM     0x0400
#define CNTR_SUSPM      0x0800
#define CNTR_CTRM       0x8000

/* ISTR bits */
#define ISTR_EP_ID      0x000F
#define ISTR_DIR        0x0010
#define ISTR_RESET      0x0400
#define ISTR_SUSP       0x0800
#define ISTR_CTR        0x8000

/* EP register bits */
#define EP_EADDR        0x000F
#define EP_TX_STAT      0x0030
#define EP_T_FIELD      0x0600
#define EP_KIND         0x0100
#define EP_CTR_TX       0x0080
#define EP_DTOG_TX     0x0040
#define EP_SETUP        0x0800
#define EP_CTR_RX       0x8000
#define EP_DTOG_RX     0x4000
#define EP_RX_STAT      0x3000
#define EPREG_MASK      (EP_CTR_RX | EP_SETUP | EP_T_FIELD | EP_KIND | EP_CTR_TX | EP_EADDR)

/* EP type values */
#define EP_TYPE_BULK    0x0000
#define EP_TYPE_CONTROL 0x0200

/* EP status values (for STAT_TX / STAT_RX fields) */
#define STAT_DIS        0x00
#define STAT_STALL      0x01
#define STAT_NAK        0x02
#define STAT_VALID      0x03

/* TX/RX status masks (within EP register) */
#define EPTX_STAT_MASK  0x0030
#define EPRX_STAT_MASK  0x3000

/* DADDR bits */
#define DADDR_EF        0x0080
#define DADDR_ADD       0x007F

/* ================================================================
 * PMA buffer layout (BTABLE at PMA offset 0)
 *
 * NOTE: PMA addresses in the USB peripheral are HALF-WORD offsets.
 * Actual byte offset in PMA = addr * 2 (same as STM32).
 * E.g. addr=0x40 → byte offset 0x80 in PMA memory.
 *
 * BTABLE entries: addr_tx, count_tx, addr_rx, count_rx (8 bytes each)
 * Stored at byte offsets (BTABLE_offset * 2) in PMA.
 *
 * BTABLE[0] at byte 0x00, BTABLE[1] at 0x08,
 * BTABLE[2] at byte 0x10, BTABLE[3] at 0x18
 *
 * Data buffers (byte offsets in PMA):
 * EP0 RX: 64B at addr 0x40 → byte 0x80
 * EP0 TX: 64B at addr 0x80 → byte 0x100
 * EP2 RX: 64B at addr 0xC0 → byte 0x180
 * EP2 TX: 64B at addr 0x100 → byte 0x200
 * Total PMA used: 0x240 = 576 bytes (fits in 1KB)
 * ================================================================ */
#define EP0_BUF_RX      0x40
#define EP0_BUF_TX      0x80
#define EP2_BUF_RX      0xC0
#define EP2_BUF_TX      0x100
#define BUF_SIZE        64

/* PMA helper: convert half-word PMA address to byte offset */
#define PMA_BYTES(halfoff)  ((halfoff) * 2)

/* BTABLE register access (byte offsets within BTABLE) */
#define BTABLE_TX_ADDR(ep)  (USBD_PMA + (ep) * 8 + 0)
#define BTABLE_TX_CNT(ep)   (USBD_PMA + (ep) * 8 + 2)
#define BTABLE_RX_ADDR(ep)  (USBD_PMA + (ep) * 8 + 4)
#define BTABLE_RX_CNT(ep)   (USBD_PMA + (ep) * 8 + 6)

/* Data buffer read/write in PMA (half-word address → byte offset) */
static void pma_write_buf(uint16_t pma_addr, const uint8_t *src, uint16_t len) {
    uint16_t off = PMA_BYTES(pma_addr);
    for (uint16_t i = 0; i < len; i++)
        *(volatile uint8_t *)(USBD_PMA + off + i) = src[i];
}

static uint8_t pma_read_byte(uint16_t pma_addr, uint16_t offset) {
    return *(volatile uint8_t *)(USBD_PMA + PMA_BYTES(pma_addr) + offset);
}

/* ================================================================
 * EP status toggle helper
 *
 * STAT_TX/STAT_RX are "write-1-to-toggle" bits. To set a specific
 * status, we compute which bits to XOR and write them back along
 * with 1s in CTR_TX/CTR_RX positions (to clear = preserve those flags).
 * ================================================================ */
static void set_ep_tx_stat(uint8_t ep, uint16_t target) {
    uint16_t reg = USBD_EP(ep);
    uint16_t val = reg & (EP_TX_STAT | EPREG_MASK | EP_RX_STAT | EP_DTOG_RX | EP_DTOG_TX);
    if ((target & 0x01) != ((val >> 4) & 0x01)) val ^= 0x0010;
    if ((target & 0x02) != ((val >> 5) & 0x01)) val ^= 0x0020;
    USBD_EP(ep) = val | EP_CTR_RX | EP_CTR_TX;
}

static void set_ep_rx_stat(uint8_t ep, uint16_t target) {
    uint16_t reg = USBD_EP(ep);
    uint16_t val = reg & (EP_RX_STAT | EPREG_MASK | EP_TX_STAT | EP_DTOG_RX | EP_DTOG_TX);
    if ((target & 0x01) != ((val >> 12) & 0x01)) val ^= 0x1000;
    if ((target & 0x02) != ((val >> 13) & 0x01)) val ^= 0x2000;
    USBD_EP(ep) = val | EP_CTR_RX | EP_CTR_TX;
}

/* ================================================================
 * Descriptors — CDC ACM for host compatibility
 * ================================================================ */

/* Device descriptor */
static const uint8_t DevDesc[18] = {
    18,         /* bLength */
    0x01,       /* bDescriptorType: Device */
    0x00, 0x02, /* bcdUSB: 2.0 */
    0x02,       /* bDeviceClass: CDC */
    0x02,       /* bDeviceSubClass: ACM */
    0x00,       /* bDeviceProtocol */
    64,         /* bMaxPacketSize0 */
    USB_VID & 0xFF, (USB_VID >> 8) & 0xFF,         /* idVendor */
    USB_PID & 0xFF, (USB_PID >> 8) & 0xFF,         /* idProduct */
    0x00, 0x01, /* bcdDevice: 1.0 */
    0x01,       /* iManufacturer */
    0x02,       /* iProduct */
    0x00,       /* iSerialNumber */
    0x01        /* bNumConfigurations */
};

/* Configuration descriptor (IAD + CDC ACM + 2 interfaces + 3 endpoints) */
static const uint8_t CfgDesc[75] = {
    /* Configuration */
    9, 0x02,
    75, 0x00,     /* wTotalLength = 75 bytes */
    2,            /* bNumInterfaces */
    1,            /* bConfigurationValue */
    0,            /* iConfiguration */
    0x80,         /* bmAttributes: bus powered */
    50,           /* bMaxPower: 100mA */

    /* IAD: bind interfaces 0+1 */
    8, 0x0B,
    0,            /* bFirstInterface */
    2,            /* bInterfaceCount */
    0x02,         /* bFunctionClass: CDC */
    0x02,         /* bFunctionSubClass: ACM */
    0x01,         /* bFunctionProtocol: AT commands */
    0,            /* iFunction */

    /* Interface 0: CDC Control */
    9, 0x04,
    0,            /* bInterfaceNumber */
    0,            /* bAlternateSetting */
    1,            /* bNumEndpoints */
    0x02,         /* bInterfaceClass: CDC */
    0x02,         /* bInterfaceSubClass: ACM */
    0x01,         /* bInterfaceProtocol */
    0,            /* iInterface */

    /* CDC Header */
    5, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management */
    5, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM */
    4, 0x24, 0x02, 0x02,
    /* CDC Union */
    5, 0x24, 0x06, 0x00, 0x01,

    /* EP1 IN: Notification (interrupt) */
    7, 0x05,
    0x81,         /* EP1 IN */
    0x03,         /* Interrupt */
    8, 0x00,      /* wMaxPacketSize */
    0xFF,         /* bInterval */

    /* Interface 1: CDC Data */
    9, 0x04,
    1,            /* bInterfaceNumber */
    0,            /* bAlternateSetting */
    2,            /* bNumEndpoints */
    0x0A,         /* bInterfaceClass: Data */
    0x00, 0x00,   /* SubClass, Protocol */
    0,            /* iInterface */

    /* EP2 OUT: Bulk data */
    7, 0x05,
    0x02,         /* EP2 OUT */
    0x02,         /* Bulk */
    64, 0x00,     /* wMaxPacketSize */
    0,            /* bInterval */

    /* EP2 IN: Bulk data */
    7, 0x05,
    0x82,         /* EP2 IN */
    0x02,         /* Bulk */
    64, 0x00,     /* wMaxPacketSize */
    0             /* bInterval */
};

/* String descriptors */
static const uint8_t StrDesc0[4] = {4, 0x03, 0x09, 0x04};
static const uint8_t StrDesc1[18] = {18, 0x03, 'W',0,'C',0,'H',0,' ',0,'T',0,'e',0,'c',0,'h',0};
static const uint8_t StrDesc2[24] = {24, 0x03, 'O',0,'p',0,'e',0,'n',0,'t',0,'e',0,'r',0,'f',0,'a',0,'c',0,'e',0};
static const uint8_t StrDesc3[16] = {16, 0x03, 'B',0,'L',0,'0',0,'0',0,'0',0,'1',0};

static const uint8_t * const StringDescs[] = {StrDesc0, StrDesc1, StrDesc2, StrDesc3};
#define NUM_STRING_DESCS 4

/* ================================================================
 * Control transfer state
 * ================================================================ */
static const uint8_t *descr_ptr;
static uint16_t descr_remaining;
static uint8_t  dev_address;
static uint8_t  dev_config;

enum { CTRL_IDLE, CTRL_DATA_IN, CTRL_STATUS_OUT };
static volatile uint8_t ctrl_state;

/* ================================================================
 * EP2 bulk data
 * ================================================================ */
static uint8_t ep2_rx_buf[64];
static volatile uint8_t ep2_rx_flag;
static volatile uint16_t ep2_rx_len;

/* ================================================================
 * USB clock and GPIO
 * ================================================================ */
static void usb_clk_init(void) {
    RCC_ClocksTypeDef clks = {0};
    RCC_GetClocksFreq(&clks);

    /* Disable USBFS peripheral (used by app) to avoid conflicts */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, DISABLE);

    if (clks.SYSCLK_Frequency == 144000000)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
    else if (clks.SYSCLK_Frequency == 96000000)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div2);
    else if (clks.SYSCLK_Frequency == 48000000)
        RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div1);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
}

static void usb_gpio_init(void) {
    /* PA11 (D-), PA12 (D+): Floating Input - USB peripheral drives these internally */
    /* This matches WCH IAP USB_Port_Set() configuration */
    uint32_t cfg = GPIOA->CFGHR;
    cfg &= ~(0xFF << 12);    /* clear PA11+PA12 config */
    cfg |=  (0x44 << 12);    /* 0x4 = FLOAT for both (not AF_PP!) */
    GPIOA->CFGHR = cfg;

    /* Ensure PA11/PA12 output is LOW */
    GPIOA->OUTDR &= ~(3 << 11);

    /* Enable internal USB pull-up via EXTEN register */
    EXTEN->EXTEN_CTR |= EXTEN_USBD_PU_EN;
}

/* ================================================================
 * PMA buffer setup
 * ================================================================ */
/* Write BTABLE entry for an endpoint: TX addr, TX count, RX addr, RX count.
 * BTABLE is at byte 0 of PMA (BTABLE register = 0), so byte offsets are ep*8+0/2/4/6. */
static void pma_set_btable(uint8_t ep, uint16_t tx_addr, uint16_t tx_count,
                           uint16_t rx_addr, uint16_t rx_count) {
    *(volatile uint16_t *)(BTABLE_TX_ADDR(ep)) = tx_addr;
    *(volatile uint16_t *)(BTABLE_TX_CNT(ep))  = tx_count;
    *(volatile uint16_t *)(BTABLE_RX_ADDR(ep)) = rx_addr;
    *(volatile uint16_t *)(BTABLE_RX_CNT(ep))  = rx_count;
}

/* RX count register encoding: blocks of 2 or 32 bytes */
static uint16_t pma_rx_count(uint16_t size) {
    if (size > 62) {
        uint16_t blocks = size / 32;
        if (size % 32 == 0) blocks--;
        return (blocks << 10) | 0x8000;
    } else {
        uint16_t blocks = size / 2;
        if (size % 2 != 0) blocks++;
        return blocks << 10;
    }
}

static void usb_setup_buffers(void) {
    USBD_BTABLE = 0;  /* BTABLE at byte 0 of PMA */

    /* EP0: TX buffer at EP0_BUF_TX, RX buffer at EP0_BUF_RX */
    pma_set_btable(0, EP0_BUF_TX, 0, EP0_BUF_RX, pma_rx_count(BUF_SIZE));

    /* EP2: TX buffer at EP2_BUF_TX, RX buffer at EP2_BUF_RX */
    pma_set_btable(2, EP2_BUF_TX, 0, EP2_BUF_RX, pma_rx_count(BUF_SIZE));
}

/* ================================================================
 * Endpoint setup
 * ================================================================ */
static void ep0_setup(void) {
    /* Write initial value: CONTROL type, addr 0, all flags zero */
    USBD_EP(0) = EP_TYPE_CONTROL | 0;
    /* Set TX=NAK (ready for IN), RX=VALID (ready for SETUP/OUT) */
    set_ep_tx_stat(0, STAT_NAK);
    set_ep_rx_stat(0, STAT_VALID);
}

static void ep2_setup(void) {
    /* BULK type, addr 2 */
    USBD_EP(2) = EP_TYPE_BULK | 2;
    /* Set TX=NAK, RX=VALID */
    set_ep_tx_stat(2, STAT_NAK);
    set_ep_rx_stat(2, STAT_VALID);
}

/* ================================================================
 * EP0 control transfer handlers
 * ================================================================ */

static void ep0_stall(void) {
    set_ep_tx_stat(0, STAT_STALL);
    set_ep_rx_stat(0, STAT_STALL);
}

static void ep0_send_zlp(void) {
    *(volatile uint16_t *)(BTABLE_TX_CNT(0)) = 0;  /* EP0 TX count = 0 */
    set_ep_tx_stat(0, STAT_VALID);
}

/* Process SETUP token on EP0 */
static void ep0_setup_handler(void) {
    /* Clear CTR_RX (write-0-to-clear by preserving the bit as 0) */
    USBD_EP(0) &= ~EP_CTR_RX;

    /* Read SETUP packet from PMA (half-word address → byte offset = addr * 2) */
    uint8_t bmRequestType = pma_read_byte(EP0_BUF_RX, 0);
    uint8_t bRequest      = pma_read_byte(EP0_BUF_RX, 1);
    uint16_t wValue       = (uint16_t)pma_read_byte(EP0_BUF_RX, 2)
                          | ((uint16_t)pma_read_byte(EP0_BUF_RX, 3) << 8);
    uint16_t wLength      = (uint16_t)pma_read_byte(EP0_BUF_RX, 6)
                          | ((uint16_t)pma_read_byte(EP0_BUF_RX, 7) << 8);

    (void)bmRequestType;

    switch (bRequest) {
    case 0x06: /* GET_DESCRIPTOR */ {
        const uint8_t *desc = 0;
        uint16_t desc_len = 0;
        uint8_t dtype = wValue >> 8;

        switch (dtype) {
        case 0x01: desc = DevDesc; desc_len = sizeof(DevDesc); break;
        case 0x02: desc = CfgDesc; desc_len = sizeof(CfgDesc); break;
        case 0x03: {
            uint8_t idx = wValue & 0xFF;
            if (idx < NUM_STRING_DESCS) {
                desc = StringDescs[idx];
                desc_len = desc[0];
            }
            break;
        }
        }

        if (!desc) { ep0_stall(); return; }

        descr_ptr = desc;
        descr_remaining = (wLength < desc_len) ? wLength : desc_len;
        ctrl_state = CTRL_DATA_IN;

        /* Send first chunk (up to EP0 max packet size = 64) */
        uint16_t chunk = (descr_remaining > 64) ? 64 : descr_remaining;
        pma_write_buf(EP0_BUF_TX, descr_ptr, chunk);
        *(volatile uint16_t *)(BTABLE_TX_CNT(0)) = chunk;
        descr_ptr += chunk;
        descr_remaining -= chunk;
        set_ep_tx_stat(0, STAT_VALID);
        break;
    }

    case 0x05: /* SET_ADDRESS */
        dev_address = wValue & 0x7F;
        ctrl_state = CTRL_IDLE;
        ep0_send_zlp();
        break;

    case 0x09: /* SET_CONFIGURATION */
        dev_config = wValue & 0xFF;
        ctrl_state = CTRL_IDLE;
        ep0_send_zlp();
        /* Enable EP2 after configuration is set */
        if (dev_config) ep2_setup();
        break;

    case 0x08: /* GET_CONFIGURATION */
        pma_write_buf(EP0_BUF_TX, &dev_config, 1);
        *(volatile uint16_t *)(BTABLE_TX_CNT(0)) = 1;
        set_ep_tx_stat(0, STAT_VALID);
        ctrl_state = CTRL_DATA_IN;
        break;

    case 0x00: /* GET_STATUS */
    {
        uint8_t status[2] = {0, 0};
        pma_write_buf(EP0_BUF_TX, status, 2);
        *(volatile uint16_t *)(BTABLE_TX_CNT(0)) = 2;
        set_ep_tx_stat(0, STAT_VALID);
        ctrl_state = CTRL_DATA_IN;
        break;
    }

    case 0x0A: /* SET_INTERFACE (ignore) */
    case 0x0B: /* GET_INTERFACE (ignore) */
        ep0_send_zlp();
        ctrl_state = CTRL_IDLE;
        break;

    case 0x01: /* CLEAR_FEATURE (ignore) */
    case 0x03: /* SET_FEATURE (ignore) */
        ep0_send_zlp();
        ctrl_state = CTRL_IDLE;
        break;

    case 0x22: /* SET_CONTROL_LINE_STATE (CDC class, ignore) */
        ep0_send_zlp();
        ctrl_state = CTRL_IDLE;
        break;

    case 0x20: /* SET_LINE_CODING (CDC class, ignore) */
        /* Need to accept 7 bytes of OUT data, then ZLP status */
        ctrl_state = CTRL_STATUS_OUT;
        ep0_send_zlp(); /* ACK the SETUP */
        break;

    default:
        ep0_stall();
        break;
    }
}

/* Process IN token on EP0 (data in or status complete) */
static void ep0_in_handler(void) {
    /* Clear CTR_TX */
    USBD_EP(0) &= ~EP_CTR_TX;

    switch (ctrl_state) {
    case CTRL_DATA_IN:
        if (descr_remaining > 0) {
            uint16_t chunk = (descr_remaining > 64) ? 64 : descr_remaining;
            pma_write_buf(EP0_BUF_TX, descr_ptr, chunk);
            *(volatile uint16_t *)(BTABLE_TX_CNT(0)) = chunk;
            descr_ptr += chunk;
            descr_remaining -= chunk;
            set_ep_tx_stat(0, STAT_VALID);
        } else {
            /* Data stage complete — set TX=NAK, RX=VALID for status OUT */
            set_ep_tx_stat(0, STAT_NAK);
            set_ep_rx_stat(0, STAT_VALID);
            ctrl_state = CTRL_IDLE;
        }
        break;

    case CTRL_IDLE:
        /* Status IN complete (e.g. after SET_ADDRESS) */
        /* Apply device address after SET_ADDRESS status */
        if (dev_address) {
            USBD_DADDR = DADDR_EF | dev_address;
            dev_address = 0; /* only apply once */
        }
        /* Set TX=NAK, RX=VALID for next SETUP */
        set_ep_tx_stat(0, STAT_NAK);
        set_ep_rx_stat(0, STAT_VALID);
        break;

    default:
        break;
    }
}

/* Process OUT token on EP0 (status out) */
static void ep0_out_handler(void) {
    /* Clear CTR_RX */
    USBD_EP(0) &= ~EP_CTR_RX;

    /* Status stage complete — ready for next SETUP */
    set_ep_tx_stat(0, STAT_NAK);
    set_ep_rx_stat(0, STAT_VALID);
    ctrl_state = CTRL_IDLE;
}

/* ================================================================
 * USB interrupt handler
 * ================================================================ */
void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBFS_IRQHandler(void) {
    uint16_t istr = USBD_ISTR;

    /* Bus reset */
    if (istr & ISTR_RESET) {
        USBD_ISTR = ~ISTR_RESET;
        USBD_DADDR = DADDR_EF;  /* address 0, enable */
        ctrl_state = CTRL_IDLE;
        ep0_setup();
    }

    /* Suspend */
    if (istr & ISTR_SUSP) {
        USBD_ISTR = ~ISTR_SUSP;
    }

    /* Correct transfer */
    while (USBD_ISTR & ISTR_CTR) {
        uint16_t istr_val = USBD_ISTR;
        uint8_t ep_id = istr_val & ISTR_EP_ID;
        uint8_t dir   = (istr_val & ISTR_DIR) ? 1 : 0;
        uint16_t ep_reg = USBD_EP(ep_id);

        if (ep_id == 0) {
            if (!dir && (ep_reg & EP_CTR_RX)) {
                if (ep_reg & EP_SETUP)
                    ep0_setup_handler();
                else
                    ep0_out_handler();
            } else if (dir && (ep_reg & EP_CTR_TX)) {
                ep0_in_handler();
            }
        } else if (ep_id == 2) {
            if (!dir && (ep_reg & EP_CTR_RX)) {
                /* EP2 OUT: data received from host */
                USBD_EP(2) &= ~EP_CTR_RX;  /* clear CTR_RX only, preserve all other bits */
                ep2_rx_len = *(volatile uint16_t *)(BTABLE_RX_CNT(2)) & 0x3FF;
                if (ep2_rx_len > 64) ep2_rx_len = 64;
                for (uint16_t i = 0; i < ep2_rx_len; i++)
                    ep2_rx_buf[i] = pma_read_byte(EP2_BUF_RX, i);
                ep2_rx_flag = 1;
                set_ep_rx_stat(2, STAT_VALID);  /* re-enable RX */
            }
            if (dir && (ep_reg & EP_CTR_TX)) {
                /* EP2 IN: data sent to host */
                USBD_EP(2) &= ~EP_CTR_TX;  /* clear CTR_TX only, preserve all other bits */
                set_ep_tx_stat(2, STAT_NAK);  /* don't re-send */
            }
        }
    }
}

/* ================================================================
 * Public API
 * ================================================================ */

void usb_init(void) {
    usb_clk_init();
    Delay_Ms(100);

    /* Disable USBFS peripheral completely to avoid conflicts */
    /* USBFS is at 0x50000000, disable it by setting PDWN bit */
    *(volatile uint32_t *)(0x50000000 + 0x40) = 0x0002;  /* CNTR: PDWN=1 */
    Delay_Us(10);

    /* Power down USBD first (like WCH IAP) */
    USBD_CNTLR = CNTR_FRES;  /* PDWN=1, FRES=1 */
    Delay_Ms(700);  /* WCH uses 700ms delay */

    /* Configure GPIO for USB */
    usb_gpio_init();

    /* Power up USBD (clear PDWN and FRES) */
    USBD_CNTLR = 0;
    Delay_Us(200);

    /* Clear pending interrupts */
    USBD_ISTR = 0;

    /* Enable USB, enable interrupts */
    USBD_CNTLR = CNTR_CTRM | CNTR_RESETM | CNTR_SUSPM;
    USBD_DADDR = DADDR_EF;  /* default address 0, enabled */

    /* Set up PMA buffers */
    usb_setup_buffers();

    /* Configure EP0 */
    ep0_setup();

    /* Enable USB interrupt */
    NVIC_EnableIRQ(USBFS_IRQn);
}

uint8_t usb_data_available(void) {
    return ep2_rx_flag;
}

uint16_t usb_read(uint8_t *buf, uint16_t max_len) {
    if (!ep2_rx_flag) return 0;
    uint16_t len = (ep2_rx_len < max_len) ? ep2_rx_len : max_len;
    memcpy(buf, ep2_rx_buf, len);
    ep2_rx_flag = 0;
    return len;
}

void usb_send(const uint8_t *data, uint16_t len) {
    if (len > 64) len = 64;
    pma_write_buf(EP2_BUF_TX, data, len);
    *(volatile uint16_t *)(BTABLE_TX_CNT(2)) = len;  /* EP2 TX count */
    set_ep_tx_stat(2, STAT_VALID);  /* trigger IN transfer */
}

void usb_poll(void) {
    if (ep2_rx_flag) {
        uint8_t buf[64];
        uint16_t len = usb_read(buf, sizeof(buf));
        if (len > 0)
            protocol_process(buf, len);
    }
}
