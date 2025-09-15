/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32v20x_usbfs_device.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : ch32v20x series usb interrupt processing.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header File */
#include "ch32v20x_usbfs_device.h"
#include "usbd_composite_km.h"
#include "peripheral.h"
#include "RingMem.h"
#include "include/keyboard_handler.h"
/*******************************************************************************/
/* Variable Definition */

/* Global */
const    uint8_t  *pUSBFS_Descr;
#define pMySetupReqPakHD    ((PUSB_SETUP_REQ)EP0_DatabufHD)
#define RepDescSize         62
/* Setup Request */
volatile uint8_t  USBFS_SetupReqCode;
volatile uint8_t  USBFS_SetupReqType;
volatile uint16_t USBFS_SetupReqValue;
volatile uint16_t USBFS_SetupReqIndex;
volatile uint16_t USBFS_SetupReqLen;

/* USB Device Status */
volatile uint8_t  USBFS_DevConfig;
volatile uint8_t  USBFS_DevAddr;
volatile uint8_t  USBFS_DevSleepStatus;
volatile uint8_t  USBFS_DevEnumStatus;

/* HID Class Command */
volatile uint8_t  USBFS_HidIdle[ 3 ];
volatile uint8_t  USBFS_HidProtocol[ 3 ];

/* Endpoint Buffer */
__attribute__ ((aligned(4))) uint8_t USBFS_EP0_Buf[ DEF_USBD_UEP0_SIZE ];     //ep0(64)
__attribute__ ((aligned(4))) uint8_t USBFS_EP1_Buf[ DEF_USB_EP1_FS_SIZE ];    //ep1_in(64)
__attribute__ ((aligned(4))) uint8_t USBFS_EP2_Buf[ DEF_USB_EP2_FS_SIZE ];    //ep2_in(64)
__attribute__ ((aligned(4))) uint8_t USBFS_EP3_Buf[ DEF_USB_EP3_FS_SIZE ];    //ep3_in(64)

/* USB IN Endpoint Busy Flag */
volatile uint8_t  USBFS_Endp_Busy[ DEF_UEP_NUM ];

/******************************************************************************/
/* Interrupt Service Routine Declaration*/
void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#if DEF_USBD_SPEED == DEF_USBD_SPEED_LOW
  #define RepDataLoadLen    8
#else
  #define RepDataLoadLen    64
#endif

/******************************************************************************/
/* Endpoint Buffer */
__attribute__((aligned(4))) UINT8 EP0_DatabufHD[64];      //ep0(64)
__attribute__((aligned(4))) UINT8 EP1_DatabufHD[64 + 64]; //ep1_out(64)+ep1_in(64)
__attribute__((aligned(4))) UINT8 EP2_DatabufHD[64 + 64]; //ep2_out(64)+ep2_in(64)
__attribute__((aligned(4))) UINT8 EP3_DatabufHD[64 + 64]; //ep3_out(64)+ep3_in(64)
__attribute__((aligned(4))) UINT8 EP4_DatabufHD[64 + 64]; //ep4_out(64)+ep4_in(64)
__attribute__((aligned(4))) UINT8 EP5_DatabufHD[64 + 64]; //ep5_out(64)+ep5_in(64)
__attribute__((aligned(4))) UINT8 EP6_DatabufHD[64 + 64]; //ep6_out(64)+ep6_in(64)
__attribute__((aligned(4))) UINT8 EP7_DatabufHD[64 + 64]; //ep7_out(64)+ep7_in(64)

PUINT8 pEP0_RAM_Addr; //ep0(64)
PUINT8 pEP1_RAM_Addr; //ep1_out(64)+ep1_in(64)
PUINT8 pEP2_RAM_Addr; //ep2_out(64)+ep2_in(64)
PUINT8 pEP3_RAM_Addr; //ep3_out(64)+ep3_in(64)
PUINT8 pEP4_RAM_Addr; //ep4_out(64)+ep4_in(64)
PUINT8 pEP5_RAM_Addr; //ep5_out(64)+ep5_in(64)
PUINT8 pEP6_RAM_Addr; //ep6_out(64)+ep6_in(64)
PUINT8 pEP7_RAM_Addr; //ep7_out(64)+ep7_in(64)

const UINT8    *pDescr;
volatile UINT8  USBFS_Dev_SetupReqCode = 0xFF;   /* USB2.0 full-speed device setup package command */
volatile UINT16 USBFS_Dev_SetupReqLen = 0x00;    /* USB2.0 full-speed device setup package length */
volatile UINT8  USBFS_Dev_SetupReqValueH = 0x00; /* USB2.0 full-speed device setup package value high byte */
volatile UINT8  USBFS_Dev_Config = 0x00;         /* USB2.0 full-speed device configuration value */
volatile UINT8  USBFS_Dev_Address = 0x00;        /* USB2.0 full-speed device address value */
volatile UINT8  USBFS_Dev_SleepStatus = 0x00;    /* USB2.0 full-speed device sleep state */
volatile UINT8  USBFS_Dev_EnumStatus = 0x00;     /* USB2.0 full-speed device enumeration status */
volatile UINT8  USBFS_Dev_Endp0_Tog = 0x01;      /* USB2.0 Full-speed device endpoint 0 sync flag*/
volatile UINT8  USBFS_Dev_Speed = 0x01;          /* USB2.0 Full-speed device speed */

volatile UINT16 USBFS_Endp1_Up_Flag = 0x00;   /* USB2.0 full-speed device endpoint 1 data upload status: 0: free; 1: upload; */
volatile UINT8  USBFS_Endp1_Down_Flag = 0x00; /* USB2.0 full-speed device endpoint 1 Pass successful logo */
volatile UINT8  USBFS_Endp1_Down_Len = 0x00;  /* USB2.0 full-speed device endpoint 1 download length */
volatile BOOL   USBFS_Endp1_T_Tog = 0;        /* USB2.0 full speed device endpoint 1 sends tog bit flip */
volatile BOOL   USBFS_Endp1_R_Tog = 0;

volatile UINT16 USBFS_Endp2_Up_Flag = 0x00;    /* USB2.0 full-speed device endpoint 2 Data upload status: 0: free; 1: upload; */
volatile UINT16 USBFS_Endp2_Up_LoadPtr = 0x00; /* USB2.0 full-speed device endpoint 2 Data upload loading offset */
volatile UINT8  USBFS_Endp2_Down_Flag = 0x00;  /* USB2.0 full-speed device endpoint 2 download success flag */
volatile BOOL   USBFS_Endp2_T_Tog = 0;         /* USB2.0 full speed device endpoint 2 sends tog bit flip */
volatile BOOL   USBFS_Endp2_R_Tog = 0;

volatile UINT32V Endp2_send_seq = 0x00;
volatile UINT8   DevConfig;
volatile UINT8   SetupReqCode;
volatile UINT16  SetupReqLen;

/******************************************************************************/
/* Device Descriptor */
const UINT8 MyDevDescrHD[] = {

    0x12, 0x01, 0x10, 0x01, 0xFF, 0x00, 0x00, DEF_USBD_UEP0_SIZE,
    0x86, 0x1A, 0x23, 0x75, 0x63, 0x02, 0x00, 0x02,
    0x00, 0x01};

/* Configration Descriptor */
const UINT8 MyCfgDescrHD[] =
    {
        0x09, 0x02, 0x27, 0x00, 0x01, 0x01, 0x00, 0x80, 0xf0, //Configure descriptor, interface descriptor, endpoint descriptor
        0x09, 0x04, 0x00, 0x00, 0x03, 0xff, 0x01, 0x02, 0x00,
        0x07, 0x05, 0x82, 0x02, 0x20, 0x00, 0x00, //Bulk upload endpoint
        0x07, 0x05, 0x02, 0x02, 0x20, 0x00, 0x00, //Bulk download endpoint
        0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01};

/* Language Descriptor */
const UINT8 MyLangDescrHD[] =
    {
        0x04, 0x03, 0x09, 0x04};

/* Manufactor Descriptor */
const UINT8 MyManuInfoHD[] =
    {
        0x0E, 0x03, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0};

/* Product Information */
const UINT8 MyProdInfoHD[] =
    {
        0x0C, 0x03, 'C', 0, 'H', 0, '1', 0, '0', 0, 'x', 0};

/* Product descriptor */
const uint8_t StrDesc[28] =
    {
        0x1C, 0x03, 0x55, 0x00, 0x53, 0x00, 0x42, 0x00,
        0x32, 0x00, 0x2E, 0x00, 0x30, 0x00, 0x2D, 0x00,
        0x53, 0x00, 0x65, 0x00, 0x72, 0x00, 0x69, 0x00,
        0x61, 0x00, 0x6C, 0x00};

const uint8_t Return1[2] = {0x31, 0x00};
const uint8_t Return2[2] = {0xC3, 0x00};
const uint8_t Return3[2] = {0x9F, 0xEE};

void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
/*********************************************************************
 * @fn      USBFS_RCC_Init
 *
 * @brief   Initializes the usbfs clock configuration.
 *
 * @return  none
 */
void USBFS_RCC_Init( void )
{
    RCC_ClocksTypeDef RCC_ClocksStatus={0};
    RCC_GetClocksFreq(&RCC_ClocksStatus);
    if( RCC_ClocksStatus.SYSCLK_Frequency == 144000000 )
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div3 );
    }
    else if( RCC_ClocksStatus.SYSCLK_Frequency == 96000000 ) 
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div2 );
    }
    else if( RCC_ClocksStatus.SYSCLK_Frequency == 48000000 ) 
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div1 );
    }
#if defined(CH32V20x_D8W) || defined(CH32V20x_D8)
    else if ( RCC_ClocksStatus.SYSCLK_Frequency == 240000000 && RCC_USB5PRE_JUDGE() == SET )
    {
        RCC_USBCLKConfig( RCC_USBCLKSource_PLLCLK_Div5 );
    }
#endif
    RCC_AHBPeriphClockCmd( RCC_AHBPeriph_USBFS, ENABLE );
}

/*********************************************************************
 * @fn      USBFS_Device_Endp_Init
 *
 * @brief   Initializes USB device endpoints.
 *
 * @return  none
 */
void USBFS_Device_Endp_Init( void )
{
    USBFSD->UEP4_1_MOD = USBFS_UEP1_TX_EN;
    USBFSD->UEP2_3_MOD = USBFS_UEP2_TX_EN | USBFS_UEP3_TX_EN;

    USBFSD->UEP0_DMA = (uint32_t)USBFS_EP0_Buf;
    USBFSD->UEP1_DMA = (uint32_t)USBFS_EP1_Buf;
    USBFSD->UEP2_DMA = (uint32_t)USBFS_EP2_Buf;
    USBFSD->UEP3_DMA = (uint32_t)USBFS_EP3_Buf;

    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_RES_ACK;
    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP1_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP2_TX_CTRL = USBFS_UEP_T_RES_NAK;
    USBFSD->UEP3_TX_CTRL = USBFS_UEP_T_RES_NAK;

    /* Clear End-points Busy Status */
    for(uint8_t i=0; i<DEF_UEP_NUM; i++ )
    {
        USBFS_Endp_Busy[ i ] = 0;
    }    
}

/*********************************************************************
 * @fn      USBFS_Device_Init
 *
 * @brief   Initializes USB device.
 *
 * @return  none
 */
void USBFS_Device_Init( FunctionalState sta )
{
    if( sta )
    {
        /* Reset USB module */
        USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us( 10 );
        USBFSH->BASE_CTRL = 0;

        /* Initialize USB device configuration */
        USBFS_Device_Endp_Init( );
        USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
        USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
        USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
        NVIC_EnableIRQ( USBFS_IRQn );
    }
    else
    {
        USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us( 10 );
        USBFSD->BASE_CTRL = 0;
        NVIC_DisableIRQ( USBFS_IRQn );
    }
}

/*********************************************************************
 * @fn      USBFS_Endp_DataUp
 *
 * @brief   usbfs device data upload
 *
 * @return  none
 */
uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod)
{
    uint8_t endp_mode;
    uint8_t buf_load_offset;

    /* DMA config, endp_ctrl config, endp_len config */
    if( ( endp >= DEF_UEP1 ) && ( endp <= DEF_UEP7 ) )
    {
        if( USBFS_Endp_Busy[ endp ] == 0 )
        {
            if( (endp == DEF_UEP1) || (endp == DEF_UEP4) )
            {
                /* endp1/endp4 */
                endp_mode = USBFSD_UEP_MOD( 0 );
                if( endp == DEF_UEP1 )
                {
                    endp_mode = (uint8_t)( endp_mode >> 4 );
                }
            }
            else if( ( endp == DEF_UEP2 ) || ( endp == DEF_UEP3 ) )
            {
                /* endp2/endp3 */
                endp_mode = USBFSD_UEP_MOD( 1 );
                if( endp == DEF_UEP3 )
                {
                    endp_mode = (uint8_t)( endp_mode >> 4 );
                }
            }
            else if( ( endp == DEF_UEP5 ) || ( endp == DEF_UEP6 ) )
            {
                /* endp5/endp6 */
                endp_mode = USBFSD_UEP_MOD( 2 );
                if( endp == DEF_UEP6 )
                {
                    endp_mode = (uint8_t)( endp_mode >> 4 );
                }
            }
            else
            {
                /* endp7 */
                endp_mode = USBFSD_UEP_MOD( 3 );
            }

            if( endp_mode & USBFSD_UEP_TX_EN )
            {
                if( endp_mode & USBFSD_UEP_RX_EN )
                {
                    if( endp_mode & USBFSD_UEP_BUF_MOD )
                    {
                        if( USBFSD_UEP_TX_CTRL(endp) & USBFS_UEP_T_TOG )
                        {
                            buf_load_offset = 192;
                        }
                        else
                        {
                            buf_load_offset = 128;
                        }
                    }
                    else
                    {
                        buf_load_offset = 64;
                    }
                }
                else
                {
                    if( endp_mode & USBFSD_UEP_BUF_MOD )
                    {
                        /* double tx buffer */
                        if( USBFSD_UEP_TX_CTRL( endp ) & USBFS_UEP_T_TOG )
                        {
                            buf_load_offset = 64;
                        }
                        else
                        {
                            buf_load_offset = 0;
                        }
                    }
                    else
                    {
                        buf_load_offset = 0;
                    }
                }
                if( buf_load_offset == 0 )
                {
                    if( mod == DEF_UEP_DMA_LOAD )
                    {
                        /* DMA mode */
                        USBFSD_UEP_DMA( endp ) = (uint16_t)(uint32_t)pbuf;
                    }
                    else
                    {
                        /* copy mode */
                        memcpy( USBFSD_UEP_BUF( endp ), pbuf, len );
                    }
                }
                else
                {
                    memcpy( USBFSD_UEP_BUF( endp ) + buf_load_offset, pbuf, len );
                }
                /* Set end-point busy */
                USBFS_Endp_Busy[ endp ] = 0x01;
                /* tx length */
                USBFSD_UEP_TLEN( endp ) = len;
                /* response ack */
                USBFSD_UEP_TX_CTRL( endp ) = ( USBFSD_UEP_TX_CTRL( endp ) & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_ACK;
            }
        }
        else
        {
            return NoREADY;
        }
    }
    else
    {
        return NoREADY;
    }
    return READY;
}

/*********************************************************************
 * @fn      USBFS_IRQHandler
 *
 * @brief   USB device transfer process.
 *
 * @return  none
 */
void USBFS_IRQHandler( void )
{
    uint8_t  intflag, intst, errflag;
    uint16_t len;

    intflag = USBFSD->INT_FG;
    intst = USBFSD->INT_ST;

    if( intflag & USBFS_UIF_TRANSFER )
    {
        switch( intst & USBFS_UIS_TOKEN_MASK )
        {
            /* data-in stage processing */
            case USBFS_UIS_TOKEN_IN:
                switch( intst & ( USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK ) )
                {
                    /* end-point 0 data in interrupt */
                    case USBFS_UIS_TOKEN_IN | DEF_UEP0:
                        if( USBFS_SetupReqLen == 0 )
                        {
                            USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                        }

                        if ( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                        {
                            /* Non-standard request endpoint 0 Data upload */
                        }
                        else
                        {
                            switch( USBFS_SetupReqCode )
                            {
                                case USB_GET_DESCRIPTOR:
                                    len = USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                                    memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                                    USBFS_SetupReqLen -= len;
                                    pUSBFS_Descr += len;
                                    USBFSD->UEP0_TX_LEN = len;
                                    USBFSD->UEP0_TX_CTRL ^= USBFS_UEP_T_TOG;
                                    break;

                                case USB_SET_ADDRESS:
                                    USBFSD->DEV_ADDR = ( USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT ) | USBFS_DevAddr;
                                    break;

                                default:
                                    break;
                            }
                        }
                        break;

                    /* end-point 1 data in interrupt */
                    case USBFS_UIS_TOKEN_IN | DEF_UEP1:
                        USBFSD->UEP1_TX_CTRL = ( USBFSD->UEP1_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_NAK;
                        USBFSD->UEP1_TX_CTRL ^= USBFS_UEP_T_TOG;
                        USBFS_Endp_Busy[ DEF_UEP1 ] = 0;
                        break;

                    /* end-point 2 data in interrupt */
                    case USBFS_UIS_TOKEN_IN | DEF_UEP2:
                        USBFSD->UEP2_TX_CTRL = ( USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_NAK;
                        USBFSD->UEP2_TX_CTRL ^= USBFS_UEP_T_TOG;
                        USBFS_Endp_Busy[ DEF_UEP2 ] = 0;
                        break;

                    /* end-point 3 data in interrupt */
                    case USBFS_UIS_TOKEN_IN | DEF_UEP3:
                        USBFSD->UEP3_TX_CTRL = ( USBFSD->UEP3_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_NAK;
                        USBFSD->UEP3_TX_CTRL ^= USBFS_UEP_T_TOG;
                        USBFS_Endp_Busy[ DEF_UEP3 ] = 0;
                        break;

                    default :
                        break;
                }
                break;

            /* data-out stage processing */
            case USBFS_UIS_TOKEN_OUT:
                switch( intst & ( USBFS_UIS_TOKEN_MASK | USBFS_UIS_ENDP_MASK ) )
                {
                    /* end-point 0 data out interrupt */
                    case USBFS_UIS_TOKEN_OUT | DEF_UEP0:
                        if( intst & USBFS_UIS_TOG_OK )
                        {
                            if( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                            {
                                if( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS )
                                {
                                    switch( USBFS_SetupReqCode )
                                    {
                                        case HID_SET_REPORT:
                                            Keyboard_SetLEDStatus(USBFS_EP0_Buf[ 0 ]);
                                            USBFS_SetupReqLen = 0;
                                            break;
                                        default:
                                            break;
                                    }
                                }
                            }
                            else
                            {
                                /* Standard request end-point 0 Data download */
                                /* Add your code here */
                            }
                        }
                        if( USBFS_SetupReqLen == 0 )
                        {
                            USBFSD->UEP0_TX_LEN  = 0;
                            USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                        }
                        break;

                    default:
                        break;
                }
                break;

            /* Setup stage processing */
            case USBFS_UIS_TOKEN_SETUP:
                USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK;
                USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

                /* Store All Setup Values */
                USBFS_SetupReqType  = pUSBFS_SetupReqPak->bRequestType;
                USBFS_SetupReqCode  = pUSBFS_SetupReqPak->bRequest;
                USBFS_SetupReqLen   = pUSBFS_SetupReqPak->wLength;
                USBFS_SetupReqValue = pUSBFS_SetupReqPak->wValue;
                USBFS_SetupReqIndex = pUSBFS_SetupReqPak->wIndex;
                len = 0;
                errflag = 0;

                if( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
                {
                    if( ( USBFS_SetupReqType & USB_REQ_TYP_MASK ) == USB_REQ_TYP_CLASS )
                    {
                        /* Class Request */
                        switch( USBFS_SetupReqCode )
                        {
                            case HID_SET_REPORT:
                                break;

                            case HID_SET_IDLE:
                                if( USBFS_SetupReqIndex == 0x00 )
                                {
                                    USBFS_HidIdle[ 0 ] = (uint8_t)( USBFS_SetupReqValue >> 8 );
                                }
                                else if( USBFS_SetupReqIndex == 0x01 )
                                {
                                    USBFS_HidIdle[ 1 ] = (uint8_t)( USBFS_SetupReqValue >> 8 );
                                }
                                else if( USBFS_SetupReqIndex == 0x02 )
                                {
                                    USBFS_HidIdle[ 2 ] = (uint8_t)( USBFS_SetupReqValue >> 8 );
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_SET_PROTOCOL:
                                if( USBFS_SetupReqIndex == 0x00 )
                                {
                                    USBFS_HidProtocol[ 0 ] = (uint8_t)USBFS_SetupReqValue;
                                }
                                else if( USBFS_SetupReqIndex == 0x01 )
                                {
                                    USBFS_HidProtocol[ 1 ] = (uint8_t)USBFS_SetupReqValue;
                                }
                                else if( USBFS_SetupReqIndex == 0x02 )
                                {
                                    USBFS_HidProtocol[ 2 ] = (uint8_t)USBFS_SetupReqValue;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_GET_IDLE:
                                if( USBFS_SetupReqIndex == 0x00 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidIdle[ 0 ];
                                    len = 1;
                                }
                                else if( USBFS_SetupReqIndex == 0x01 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidIdle[ 1 ];
                                    len = 1;
                                }
                                else if( USBFS_SetupReqIndex == 0x02 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidIdle[ 2 ];
                                    len = 1;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            case HID_GET_PROTOCOL:
                                if( USBFS_SetupReqIndex == 0x00 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidProtocol[ 0 ];
                                    len = 1;
                                }
                                else if( USBFS_SetupReqIndex == 0x01 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidProtocol[ 1 ];
                                    len = 1;
                                }
                                else if( USBFS_SetupReqIndex == 0x02 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = USBFS_HidProtocol[ 2 ];
                                    len = 1;
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                                break;

                            default:
                                errflag = 0xFF;
                                break;
                        }
                    }
                }
                else
                {
                    /* usb standard request processing */
                    switch( USBFS_SetupReqCode )
                    {
                        /* get device/configuration/string/report/... descriptors */
                        case USB_GET_DESCRIPTOR:
                            switch( (uint8_t)( USBFS_SetupReqValue >> 8 ) )
                            {
                                /* get usb device descriptor */
                                case USB_DESCR_TYP_DEVICE:
                                    pUSBFS_Descr = MyDevDescr;
                                    len = DEF_USBD_DEVICE_DESC_LEN;
                                    break;

                                /* get usb configuration descriptor */
                                case USB_DESCR_TYP_CONFIG:
                                    pUSBFS_Descr = MyCfgDescr;
                                    len = DEF_USBD_CONFIG_DESC_LEN;
                                    break;

                                /* get usb hid descriptor */
                                case USB_DESCR_TYP_HID:
                                    if( USBFS_SetupReqIndex == 0x00 )
                                    {
                                        pUSBFS_Descr = &MyCfgDescr[ 18 ];
                                        len = 9;
                                    }
                                    else if( USBFS_SetupReqIndex == 0x01 )
                                    {
                                        pUSBFS_Descr = &MyCfgDescr[ 43 ];
                                        len = 9;
                                    }
                                    else if( USBFS_SetupReqIndex == 0x02 )
                                    {
                                        pUSBFS_Descr = &MyCfgDescr[ 68 ];  // HID descriptor for absolute mouse
                                        len = 9;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                    break;

                                /* get usb report descriptor */
                                case USB_DESCR_TYP_REPORT:
                                    if( USBFS_SetupReqIndex == 0x00 )
                                    {
                                        pUSBFS_Descr = KeyRepDesc;
                                        len = DEF_USBD_REPORT_DESC_LEN_KB;
                                    }
                                    else if( USBFS_SetupReqIndex == 0x01 )
                                    {
                                        pUSBFS_Descr = MouseRepDesc;
                                        len = DEF_USBD_REPORT_DESC_LEN_REL_MS;
                                    }
                                    else if( USBFS_SetupReqIndex == 0x02 )
                                    {
                                        pUSBFS_Descr = AbsMouseRepDesc;
                                        len = DEF_USBD_REPORT_DESC_LEN_ABS_MS;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                    break;

                                /* get usb string descriptor */
                                case USB_DESCR_TYP_STRING:
                                    switch( (uint8_t)( USBFS_SetupReqValue & 0xFF ) )
                                    {
                                        /* Descriptor 0, Language descriptor */
                                        case DEF_STRING_DESC_LANG:
                                            pUSBFS_Descr = MyLangDescr;
                                            len = DEF_USBD_LANG_DESC_LEN;
                                            break;

                                        /* Descriptor 1, Manufacturers String descriptor */
                                        case DEF_STRING_DESC_MANU:
                                            pUSBFS_Descr = MyManuInfo;
                                            len = DEF_USBD_MANU_DESC_LEN;
                                            break;

                                        /* Descriptor 2, Product String descriptor */
                                        case DEF_STRING_DESC_PROD:
                                            pUSBFS_Descr = MyProdInfo;
                                            len = DEF_USBD_PROD_DESC_LEN;
                                            break;

                                        /* Descriptor 3, Serial-number String descriptor */
                                        case DEF_STRING_DESC_SERN:
                                            pUSBFS_Descr = MySerNumInfo;
                                            len = DEF_USBD_SN_DESC_LEN;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                    break;

                                default :
                                    errflag = 0xFF;
                                    break;
                            }

                            /* Copy Descriptors to Endp0 DMA buffer */
                            if( USBFS_SetupReqLen > len )
                            {
                                USBFS_SetupReqLen = len;
                            }
                            len = ( USBFS_SetupReqLen >= DEF_USBD_UEP0_SIZE ) ? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                            memcpy( USBFS_EP0_Buf, pUSBFS_Descr, len );
                            pUSBFS_Descr += len;
                            break;

                        /* Set usb address */
                        case USB_SET_ADDRESS:
                            USBFS_DevAddr = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            break;

                        /* Get usb configuration now set */
                        case USB_GET_CONFIGURATION:
                            USBFS_EP0_Buf[ 0 ] = USBFS_DevConfig;
                            if( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        /* Set usb configuration to use */
                        case USB_SET_CONFIGURATION:
                            USBFS_DevConfig = (uint8_t)( USBFS_SetupReqValue & 0xFF );
                            USBFS_DevEnumStatus = 0x01;
                            break;

                        /* Clear or disable one usb feature */
                        case USB_CLEAR_FEATURE:
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* clear one device feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    /* clear usb sleep status, device not prepare to sleep */
                                    USBFS_DevSleepStatus &= ~0x01;
                                }
                            	else
                                {
                                    errflag = 0xFF;
                                }
							}
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {
                                    /* Clear End-point Feature */
                                    switch( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) )
                                    {
                                        case ( DEF_UEP_IN | DEF_UEP1 ):
                                            /* Set End-point 1 OUT ACK */
                                            USBFSD->UEP1_TX_CTRL = USBFS_UEP_T_RES_NAK;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            /* Set End-point 2 IN NAK */
                                            USBFSD->UEP2_TX_CTRL = USBFS_UEP_T_RES_NAK;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP3 ):
                                            /* Set End-point 3 IN NAK */
                                            USBFSD->UEP3_TX_CTRL = USBFS_UEP_T_RES_NAK;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* set or enable one usb feature */
                        case USB_SET_FEATURE:
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                /* Set Device Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_REMOTE_WAKEUP )
                                {
                                    if( MyCfgDescr[ 7 ] & 0x20 )
                                    {
                                        /* Set Wake-up flag, device prepare to sleep */
                                        USBFS_DevSleepStatus |= 0x01;
                                    }
                                    else
                                    {
                                        errflag = 0xFF;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                /* Set Endpoint Feature */
                                if( (uint8_t)( USBFS_SetupReqValue & 0xFF ) == USB_REQ_FEAT_ENDP_HALT )
                                {
                                    switch( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) )
                                    {
                                        case ( DEF_UEP_IN | DEF_UEP1 ):
                                            USBFSD->UEP1_TX_CTRL = ( USBFSD->UEP1_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_STALL;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP2 ):
                                            USBFSD->UEP2_TX_CTRL = ( USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_STALL;
                                            break;

                                        case ( DEF_UEP_IN | DEF_UEP3 ):
                                            USBFSD->UEP3_TX_CTRL = ( USBFSD->UEP3_TX_CTRL & ~USBFS_UEP_T_RES_MASK ) | USBFS_UEP_T_RES_STALL;
                                            break;

                                        default:
                                            errflag = 0xFF;
                                            break;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        /* This request allows the host to select another setting for the specified interface  */
                        case USB_GET_INTERFACE:
                            USBFS_EP0_Buf[ 0 ] = 0x00;
                            if( USBFS_SetupReqLen > 1 )
                            {
                                USBFS_SetupReqLen = 1;
                            }
                            break;

                        case USB_SET_INTERFACE:
                            break;

                        /* host get status of specified device/interface/end-points */
                        case USB_GET_STATUS:
                            USBFS_EP0_Buf[ 0 ] = 0x00;
                            USBFS_EP0_Buf[ 1 ] = 0x00;
                            if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_DEVICE )
                            {
                                if( USBFS_DevSleepStatus & 0x01 )
                                {
                                    USBFS_EP0_Buf[ 0 ] = 0x02;
                                }
                                else
                                {
                                    USBFS_EP0_Buf[ 0 ] = 0x00;
                                }
                            }
                            else if( ( USBFS_SetupReqType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )
                            {
                                if( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) == ( DEF_UEP_IN | DEF_UEP1 ) )
                                {
                                    if( ( USBFSD->UEP1_TX_CTRL & USBFS_UEP_T_RES_MASK ) == USBFS_UEP_T_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else if( (uint8_t)( USBFS_SetupReqIndex & 0xFF ) == ( DEF_UEP_IN | DEF_UEP2 ) )
                                {
                                    if( ( USBFSD->UEP2_TX_CTRL & USBFS_UEP_T_RES_MASK ) == USBFS_UEP_T_RES_STALL )
                                    {
                                        USBFS_EP0_Buf[ 0 ] = 0x01;
                                    }
                                }
                                else
                                {
                                    errflag = 0xFF;
                                }
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            if( USBFS_SetupReqLen > 2 )
                            {
                                USBFS_SetupReqLen = 2;
                            }
                            break;

                        default:
                            errflag = 0xFF;
                            break;
                    }
                }

                /* errflag = 0xFF means a request not support or some errors occurred, else correct */
                if( errflag == 0xFF )
                {
                    /* if one request not support, return stall */
                    USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL;
                    USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
                }
                else
                {
                    /* end-point 0 data Tx/Rx */
                    if( USBFS_SetupReqType & DEF_UEP_IN )
                    {
                        len = ( USBFS_SetupReqLen > DEF_USBD_UEP0_SIZE )? DEF_USBD_UEP0_SIZE : USBFS_SetupReqLen;
                        USBFS_SetupReqLen -= len;
                        USBFSD->UEP0_TX_LEN = len;
                        USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                    }
                    else
                    {
                        if( USBFS_SetupReqLen == 0 )
                        {
                            USBFSD->UEP0_TX_LEN = 0;
                            USBFSD->UEP0_TX_CTRL = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
                        }
                        else
                        {
                            USBFSD->UEP0_RX_CTRL = USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
                        }
                    }
                }
                break;

            default :
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    else if( intflag & USBFS_UIF_BUS_RST )
    {
        /* usb reset interrupt processing */
        USBFS_DevConfig = 0;
        USBFS_DevAddr = 0;
        USBFS_DevSleepStatus = 0;
        USBFS_DevEnumStatus = 0;

        USBFSD->DEV_ADDR = 0;
        USBFS_Device_Endp_Init( );
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    else if( intflag & USBFS_UIF_SUSPEND )
    {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        Delay_Us(10);
        /* usb suspend interrupt processing */
        if( USBFSD->MIS_ST & USBFS_UMS_SUSPEND )
        {
            USBFS_DevSleepStatus |= 0x02;
            if( USBFS_DevSleepStatus == 0x03 )
            {
                /* Handling usb sleep here */
                MCU_Sleep_Wakeup_Operate( );
            }
        }
        else
        {
            USBFS_DevSleepStatus &= ~0x02;
        }
    }
    else
    {
        /* other interrupts */
        USBFSD->INT_FG = intflag;
    }
}


/*********************************************************************
 * @fn      USBFS_Send_Resume
 *
 * @brief   Send usb k signal, Wake up usb host
 *
 * @return  none
 */
void USBFS_Send_Resume( void )
{
    USBFSD->UDEV_CTRL ^= USBFS_UD_LOW_SPEED;
    Delay_Ms( 8 );
    USBFSD->UDEV_CTRL ^= USBFS_UD_LOW_SPEED;
    Delay_Ms( 1 );

}
void DevEP2_IN_Deal(UINT8 l)
{
    USBFSD->UEP2_TX_LEN = l;
    USBFSD->UEP2_TX_CTRL = (USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_ACK;
}
/*********************************************************************
 * @fn      USBSendData
 *
 * @brief   Send data to the host
 *
 * @return  none
 */
uint8_t USBSendData(void)
{
    if(USBFSD->UEP2_TX_CTRL & USBFS_UEP_T_RES_MASK == USBFS_UEP_T_RES_ACK)
    {
        return 0x01;
    }
    if(RingMemBLE.CurrentLen > 32)
    {
        RingMemRead(&RingMemBLE, pEP2_IN_DataBuf, 32);
        DevEP2_IN_Deal(32);
    }
    else
    {
        uint8_t len = RingMemBLE.CurrentLen;
        RingMemRead(&RingMemBLE, pEP2_IN_DataBuf, len);
        DevEP2_IN_Deal(len);
    }
    return SUCCESS;
}
