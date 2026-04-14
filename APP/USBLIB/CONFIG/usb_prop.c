/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_prop.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : All processing related to Virtual Com Port Demo
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "../../include/Version_selection.h"
#include "usb_pwr.h"
#include "hw_config.h"
#include "UART.h"
#include "ch32v20x_usb.h"
#include "../../include/uac_desc.h"
#include "../../include/uac_handler.h"
#ifdef ENABLE_ETH
#include "../../include/eth_handler.h"
#endif /* ENABLE_ETH */

uint8_t Request = 0;

extern uint8_t USBD_Endp3_Busy;

DEVICE Device_Table =
{
	EP_NUM,
	1
};

DEVICE_PROP Device_Property =
{
	USBD_init,
	USBD_Reset,
	USBD_Status_In,
	USBD_Status_Out,
	USBD_Data_Setup,
	USBD_NoData_Setup,
	USBD_Get_Interface_Setting,
	USBD_GetDeviceDescriptor,
	USBD_GetConfigDescriptor,
	USBD_GetStringDescriptor,
	0,
	DEF_USBD_UEP0_SIZE                                 
};

USER_STANDARD_REQUESTS User_Standard_Requests =
{
	USBD_GetConfiguration,
	USBD_SetConfiguration,
	USBD_GetInterface,
	USBD_SetInterface,
	USBD_GetStatus, 
	USBD_ClearFeature,
	USBD_SetEndPointFeature,
	USBD_SetDeviceFeature,
	USBD_SetDeviceAddress
};

ONE_DESCRIPTOR Device_Descriptor =
{
	(uint8_t*)USBD_DeviceDescriptor,
	USBD_SIZE_DEVICE_DESC
};

ONE_DESCRIPTOR Config_Descriptor =
{
	(uint8_t*)USBD_ConfigDescriptor,
	USBD_SIZE_CONFIG_DESC
};

ONE_DESCRIPTOR String_Descriptor[5] =
{
	{(uint8_t*)USBD_StringLangID,  USBD_SIZE_STRING_LANGID},
	{(uint8_t*)USBD_StringVendor,  USBD_SIZE_STRING_VENDOR},
	{(uint8_t*)USBD_StringProduct, USBD_SIZE_STRING_PRODUCT},
	{(uint8_t*)USBD_StringSerial,  USBD_SIZE_STRING_SERIAL},
#ifdef ENABLE_ETH
	{(uint8_t*)USBD_StringMAC,     USBD_SIZE_STRING_MAC},
#else
	{NULL, 0},  /* index 4 unused in non-ETH builds */
#endif
};

/*********************************************************************
 * @fn      USBD_SetConfiguration.
 *
 * @brief     Update the device state to configured.
 *
 * @return    None.
 */
void USBD_SetConfiguration(void)
{
  DEVICE_INFO *pInfo = &Device_Info;

  if (pInfo->Current_Configuration != 0)
  {
    bDeviceState = CONFIGURED;
  }
}

/*******************************************************************************
 * @fn         USBD_SetDeviceAddress.
 *
 * @brief      Update the device state to addressed.
 *
 * @return     None.
 */
void USBD_SetDeviceAddress (void)
{
  bDeviceState = ADDRESSED;
}


/*********************************************************************
 * @fn      USBD_SetDeviceFeature.
 *
 * @brief   SetDeviceFeature Routine.
 *
 * @return  none
 */
void USBD_SetDeviceFeature (void)
{

}



/*********************************************************************
 * @fn      USBD_ClearFeature.
 *
 * @brief   ClearFeature Routine.
 *
 * @return  none
 */
void USBD_ClearFeature(void)
{

}





/*********************************************************************
 * @fn      USBD_Status_In.
 *
 * @brief    USBD Status In Routine.
 *
 * @return   None.
 */
void USBD_Status_In(void)
{
    uint32_t Request_No = pInformation->USBbRequest;
    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_SET_LINE_CODING)
        {
#ifndef ENABLE_ETH
            UART2_USB_Init();
#endif
        }
    }
}

/*******************************************************************************
 * @fn       USBD_Status_Out
 *
 * @brief    USBD Status OUT Routine.
 *
 * @return   None.
 */
void USBD_Status_Out(void)
{
    
}

/*******************************************************************************
 * @fn       USBD_init.
 *
 * @brief    init routine.
 * 
 * @return   None.
 */
void USBD_init(void)
{
  uint8_t	i;
    
  pInformation->Current_Configuration = 0;
  PowerOn();
  for (i=0;i<8;i++) _SetENDPOINT(i,_GetENDPOINT(i) & 0x7F7F & EPREG_MASK);//all clear
  _SetISTR((uint16_t)0x00FF);//all clear
  USB_SIL_Init();
  bDeviceState = UNCONNECTED;
  
  USB_Port_Set(DISABLE, DISABLE);	
  Delay_Ms(20);
  USB_Port_Set(ENABLE, ENABLE);    
}

/*******************************************************************************
 * @fn      USBD_SetInterface_Impl
 *
 * @brief   Called by the USB stack when a SET_INTERFACE request completes.
 *          Enables or disables EP3 (ISO IN) and EP4 (ISO OUT) depending on
 *          whether the host selected alternate setting 1 (active) or 0 (idle).
 *          pInformation->USBwIndex0  = interface number
 *          pInformation->USBwValue0  = alternate setting
 *
 * @return  None.
 */
void USBD_SetInterface_Impl(void)
{
    uint8_t iface = pInformation->USBwIndex0;
    uint8_t alt   = pInformation->USBwValue0;

#ifdef ENABLE_ETH
    /* IF1 = CDC-ECM Data interface: alt0=zero-bandwidth, alt1=active bulk */
    if (iface == 1)
    {
        g_eth_data_alt = alt;
        if (alt == 1)
        {
            SetEPRxCount(ENDP2, 64);
            SetEPRxStatus(ENDP2, EP_RX_VALID);  /* arm for Ethernet frame reception */
            ETH_SendNotification(1);             /* tell host link is up */
        }
        else
        {
            SetEPRxStatus(ENDP2, EP_RX_DIS);    /* zero-bandwidth: stop receiving */
        }
    }
#endif /* ENABLE_ETH */

#ifdef ENABLE_UAC
    /* IF3 = AudioStreaming IN (EP4 ISO IN, 0x84 — Target mic → Host PC)
     * Host opens this interface to receive microphone audio.           */
    if (iface == 3)
    {
        if (alt == 1)
        {
            /* Prime EP4 with a silence frame so the first ISO IN token
             * from Windows gets a valid (zero) payload immediately.
             * Without this the endpoint never produces its first packet
             * and Windows does not show the device in the audio mixer.  */
            static const uint8_t silence[64] = {0};
            UserToPMABufferCopy((uint8_t*)silence, ENDP4_ISO_TXADDR, 64);
            SetEPTxCount(ENDP4, 64);
            SetEPTxStatus(ENDP4, EP_TX_VALID);
        }
        else
        {
            SetEPTxCount(ENDP4, 0);
            SetEPTxStatus(ENDP4, EP_TX_DIS);
        }
        g_uac_state.usbd_as_in_alt = alt;
    }
    /* IF4 = AudioStreaming OUT (EP5 ISO OUT, 0x05 — Host PC → Target spk)
     * Host opens this interface to send playback audio.               */
    else if (iface == 4)
    {
        if (alt == 1)
        {
            /* Activate EP5 ISO OUT: arm for reception */
            SetEPRxCount(ENDP5, 64);
            SetEPRxStatus(ENDP5, EP_RX_VALID);
        }
        else
        {
            SetEPRxStatus(ENDP5, EP_RX_DIS);
        }
        g_uac_state.usbd_as_out_alt = alt;
    }
#endif /* ENABLE_UAC */
}

/*******************************************************************************
 * @fn      USBD_Reset
 *
 * @brief   USBD reset routine
 *
 * @return  None.
 */
void USBD_Reset(void)
{
  pInformation->Current_Configuration = 0;
  pInformation->Current_Feature = USBD_ConfigDescriptor[7];
  pInformation->Current_Interface = 0;

  SetBTABLE(BTABLE_ADDRESS);

  SetEPType(ENDP0, EP_CONTROL);
  SetEPTxStatus(ENDP0, EP_TX_STALL);
  SetEPRxAddr(ENDP0, ENDP0_RXADDR);
  SetEPTxAddr(ENDP0, ENDP0_TXADDR);
  Clear_Status_Out(ENDP0);
  SetEPRxCount(ENDP0, Device_Property.MaxPacketSize);
  SetEPRxValid(ENDP0);
  _ClearDTOG_RX(ENDP0);
  _ClearDTOG_TX(ENDP0);

  SetEPType(ENDP1, EP_INTERRUPT);
  SetEPTxStatus(ENDP1, EP_TX_NAK);
  SetEPTxAddr(ENDP1, ENDP1_TXADDR);
  SetEPRxStatus(ENDP1, EP_RX_DIS);
  _ClearDTOG_TX(ENDP1);
  _ClearDTOG_RX(ENDP1);

  SetEPType(ENDP2, EP_BULK);
  SetEPTxStatus(ENDP2, EP_TX_DIS);
  SetEPRxAddr(ENDP2, ENDP2_RXADDR);
#ifdef ENABLE_ETH
  SetEPRxCount(ENDP2, 64);          /* ECM: 64B max-packet bulk OUT */
  SetEPRxStatus(ENDP2, EP_RX_DIS);  /* CDC-ECM: disabled until SET_INTERFACE IF1 alt=1 */
#else
  SetEPRxCount(ENDP2, 32);          /* ACM: 32B (PMA space shared with UAC EPs) */
  SetEPRxStatus(ENDP2, EP_RX_VALID); /* CDC-ACM: always active */
#endif
  _ClearDTOG_RX(ENDP2);
  _ClearDTOG_TX(ENDP2);

  SetEPType(ENDP3, EP_BULK);
  SetEPTxStatus(ENDP3, EP_TX_NAK);
  SetEPTxAddr(ENDP3, ENDP3_TXADDR);
  SetEPRxStatus(ENDP3, EP_RX_DIS);
  _ClearDTOG_TX(ENDP3);
  _ClearDTOG_RX(ENDP3);

#ifdef ENABLE_UAC
  /* EP4: UAC ISO IN (Host audio → Target PC), single-buffer 64 B, disabled until alt=1 */
  SetEPType(ENDP4, EP_ISOCHRONOUS);
  SetEPTxAddr(ENDP4, ENDP4_ISO_TXADDR);
  SetEPTxCount(ENDP4, 0);
  _ClearDTOG_RX(ENDP4);
  _ClearDTOG_TX(ENDP4);
  SetEPTxStatus(ENDP4, EP_TX_DIS);
  SetEPRxStatus(ENDP4, EP_RX_DIS);

  /* EP5: UAC ISO OUT (Target PC mic → Host), single-buffer 64 B, disabled until alt=1 */
  SetEPType(ENDP5, EP_ISOCHRONOUS);
  SetEPRxAddr(ENDP5, ENDP5_ISO_RXADDR);
  SetEPRxCount(ENDP5, 64);
  _ClearDTOG_RX(ENDP5);
  _ClearDTOG_TX(ENDP5);
  SetEPRxStatus(ENDP5, EP_RX_DIS);  /* disabled until host selects alt=1 */
  SetEPTxStatus(ENDP5, EP_TX_DIS);

  /* Clear UAC bridge buffers and state flags so stale audio data from a
   * previous session cannot be forwarded after re-enumeration.          */
  UAC_Init();
#endif /* ENABLE_UAC */

/* Note: when ENABLE_ETH is defined, only EP0-3 are used (pure CDC-ECM).
 * No CDC-ACM serial endpoints (EP6/EP7) are initialized. */

  SetDeviceAddress(0);

  USBD_Endp3_Busy = 0;

  bDeviceState = ATTACHED;
}

/*******************************************************************************
 * @fn      USBD_GetDeviceDescriptor.
 *
 * @brief   Gets the device descriptor.
 *
 * @param   Length.
 *
 * @return  The address of the device descriptor.
 */
uint8_t *USBD_GetDeviceDescriptor(uint16_t Length)
{
  return Standard_GetDescriptorData(Length, &Device_Descriptor);
}

/*******************************************************************************
 * @fn       USBD_GetConfigDescriptor.
 *
 * @brief    get the configuration descriptor.
 *
 * @param    Length.
 *
 * @return   The address of the configuration descriptor.
 */
uint8_t *USBD_GetConfigDescriptor(uint16_t Length)
{
  return Standard_GetDescriptorData(Length, &Config_Descriptor);
}

/*******************************************************************************
 * @fn        USBD_GetStringDescriptor
 *
 * @brief     Gets the string descriptors according to the needed index
 *
 * @param     Length.
 *
 * @return    The address of the string descriptors.
 */
uint8_t *USBD_GetStringDescriptor(uint16_t Length)
{
  uint8_t wValue0 = pInformation->USBwValue0;
  uint8_t max_idx = 3;
#ifdef ENABLE_ETH
  max_idx = 4;  /* string index 4 = MAC address */
#endif
  if (wValue0 > max_idx)
  {
    return NULL;
  }
  else
  {
    return Standard_GetDescriptorData(Length, &String_Descriptor[wValue0]);
  }
}

/*********************************************************************
 * @fn      USBD_Get_Interface_Setting.
 *
 * @brief  test the interface and the alternate setting according to the
 *                  supported one.
 *
 * @param  Interface: interface number.
 *                  AlternateSetting: Alternate Setting number.
 *
 * @return USB_UNSUPPORT or USB_SUCCESS.
 */
RESULT USBD_Get_Interface_Setting(uint8_t Interface, uint8_t AlternateSetting)
{
  /* Interface layout:
   *   IF0: CDC Comm / ECM Comm   — alt 0 only
   *   IF1: CDC Data / ECM Data   — alt 0 only (ACM) or alt 0+1 (ECM)
   *   IF2: AudioControl          — alt 0 only          (non-ETH only)
   *   IF3: AS IN  (EP4 ISO IN)   — alt 0+1             (non-ETH only)
   *   IF4: AS OUT (EP5 ISO OUT)  — alt 0+1             (non-ETH only) */
  if (Interface == 0)
  {
    if (AlternateSetting > 0)
      return USB_UNSUPPORT;
  }
  else if (Interface == 1)
  {
#ifdef ENABLE_ETH
    if (AlternateSetting > 1)  /* ECM: alt0 (zero-bw) + alt1 (active) */
      return USB_UNSUPPORT;
#else
    if (AlternateSetting > 0)  /* ACM: alt0 only */
      return USB_UNSUPPORT;
#endif
  }
#ifndef ENABLE_ETH
  else if (Interface == 2)
  {
    if (AlternateSetting > 0)
      return USB_UNSUPPORT;
  }
  else if (Interface <= 4)
  {
    if (AlternateSetting > 1)
      return USB_UNSUPPORT;
  }
#endif
  else
  {
    return USB_UNSUPPORT;
  }

  return USB_SUCCESS;
}

/*********************************************************************
 * @fn      USB_CDC_GetLineCoding.
 *
 * @brief   send the linecoding structure to the PC host.
 *
 * @param   Length
 *
 * @return  Inecoding structure base address.
 */
uint8_t *USB_CDC_GetLineCoding( uint16_t Length )
{
    if( Length == 0 )
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return( NULL );
    }
    return (uint8_t *)&Uart.Com_Cfg[ 0 ];
}

/*********************************************************************
 * @fn      USB_CDC_SetLineCoding.
 *
 * @brief   Set the linecoding structure fields.
 *
 * @param   Length
 *
 * @return  Inecoding structure base address.
 */
uint8_t *USB_CDC_SetLineCoding( uint16_t Length )
{
    if( Length == 0 )
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return( NULL );
    }
    return(uint8_t *)&Uart.Com_Cfg[ 0 ];
}


/*********************************************************************
 * @fn      USBD_Data_Setup
 *
 * @brief    handle the data class specific requests
 *
 * @param    Request Nb.
 *
 * @return   USB_UNSUPPORT or USB_SUCCESS.
 */
RESULT USBD_Data_Setup(uint8_t RequestNo)
{
  uint32_t Request_No = pInformation->USBbRequest;
  uint8_t *(*CopyRoutine)(uint16_t);
  CopyRoutine = NULL;
  if (Type_Recipient == (STANDARD_REQUEST | INTERFACE_RECIPIENT))
  {
    return USB_UNSUPPORT;
  }
  else if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
  {
    if (Request_No == CDC_GET_LINE_CODING)
    {
#ifdef ENABLE_ETH
      return USB_UNSUPPORT;  /* pure ECM: no ACM serial port */
#else
      CopyRoutine = &USB_CDC_GetLineCoding;
#endif
    }
    else if (Request_No == CDC_SET_LINE_CODING)
    {
#ifdef ENABLE_ETH
      return USB_UNSUPPORT;  /* pure ECM: no ACM serial port */
#else
      CopyRoutine = &USB_CDC_SetLineCoding;
#endif
    }
    else
    {
      return USB_UNSUPPORT;
    }
  }
  if (CopyRoutine)
  {
    pInformation->Ctrl_Info.CopyData = CopyRoutine;
    pInformation->Ctrl_Info.Usb_wOffset = 0;
    (*CopyRoutine)( 0 );
  }
  else
  {
    return( USB_UNSUPPORT );
  }
  return USB_SUCCESS;
}

/*******************************************************************************
 * @fn      USBD_NoData_Setup.
 *
 * @brief   handle the no data class specific requests.
 *
 * @param   Request Nb.
 *
 * @return  USB_UNSUPPORT or USB_SUCCESS.
 */
RESULT USBD_NoData_Setup(uint8_t RequestNo)      
{
  uint32_t Request_No = pInformation->USBbRequest;

  if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
  {
#ifdef ENABLE_ETH
    /* CDC-ECM: SET_ETHERNET_PACKET_FILTER (0x43) — acknowledge, accept all */
    if (Request_No == 0x43)
      return USB_SUCCESS;
#endif
    /* CDC-ACM control signals (always present) */
    if (Request_No == CDC_SET_LINE_CTLSTE || Request_No == CDC_SEND_BREAK)
      return USB_SUCCESS;
    return USB_UNSUPPORT;
  }             
  return USB_SUCCESS;
}
