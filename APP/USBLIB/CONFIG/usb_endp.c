/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_endp.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Endpoint routines
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "hw_config.h"
#include "usb_istr.h"
#include "usb_pwr.h"
#include "usb_prop.h"
#include "UART.h"
#include "../../include/uac_handler.h"
#ifdef ENABLE_ETH
#include "../../include/eth_handler.h"
#endif /* ENABLE_ETH */

uint8_t USBD_Endp3_Busy;
#ifdef ENABLE_ETH
uint8_t USBD_Endp7_Busy;  /* ACM serial EP7 IN busy flag */
#endif
uint16_t USB_Rx_Cnt=0; 

/*********************************************************************
 * @fn      EP2_IN_Callback
 *
 * @brief  Endpoint 1 IN.
 *
 * @return  none
 */
void EP1_IN_Callback (void)
{
#ifdef ENABLE_ETH
    ETH_USBD_EP1_TxDone();
#endif
}

#ifdef ENABLE_ETH
/*********************************************************************
 * @fn      EP6_IN_Callback
 *
 * @brief  Endpoint 6 IN (ACM interrupt IN — notification sent).
 *
 * @return  none
 */
void EP6_IN_Callback(void)
{
    /* ACM notification delivered — no further action needed */
}
#endif /* ENABLE_ETH */



/*********************************************************************
 * @fn      EP2_OUT_Callback
 *
 * @brief  Endpoint 2 OUT.
 *
 * @return  none
 */
void EP2_OUT_Callback (void)
{
#ifdef ENABLE_ETH
    uint16_t len = GetEPRxCount( EP2_OUT & 0x7F );
    ETH_USBD_EP2_RxDone( len );
#else
	uint32_t len;
    len = GetEPRxCount( EP2_OUT & 0x7F );
    PMAToUserBufferCopy( &UART2_Tx_Buf[ ( Uart.Tx_LoadNum * DEF_USB_FS_PACK_LEN ) ], GetEPRxAddr( EP2_OUT & 0x7F ), len );
    Uart.Tx_PackLen[ Uart.Tx_LoadNum ] = len;
    Uart.Tx_LoadNum++;
    if( Uart.Tx_LoadNum >= DEF_UARTx_TX_BUF_NUM_MAX )
    {
        Uart.Tx_LoadNum = 0x00;
    }
    Uart.Tx_RemainNum++;

	if( Uart.Tx_RemainNum >= ( DEF_UARTx_TX_BUF_NUM_MAX - 2 ) )
    {
        Uart.USB_Down_StopFlag = 0x01;
    }
    else
    {
        SetEPRxValid( ENDP2 );
    }
#endif /* ENABLE_ETH */
}
/*********************************************************************
 * @fn      EP3_IN_Callback
 *
 * @brief  Endpoint 3 IN.
 *
 * @return  none
 */
void EP3_IN_Callback (void)
{
#ifdef ENABLE_ETH
    ETH_USBD_EP3_TxDone();
#else
	USBD_Endp3_Busy = 0;
	Uart.USB_Up_IngFlag = 0x00;
#endif /* ENABLE_ETH */
}

#ifdef ENABLE_ETH
/*********************************************************************
 * @fn      EP7_OUT_Callback
 *
 * @brief   Endpoint 7 OUT — CDC-ACM serial data from host.
 *          Mirrors the non-ETH EP2_OUT_Callback logic.
 *
 * @return  none
 */
void EP7_OUT_Callback(void)
{
    uint32_t len;
    len = GetEPRxCount(EP7_OUT & 0x7F);
    PMAToUserBufferCopy(&UART2_Tx_Buf[(Uart.Tx_LoadNum * DEF_USB_FS_PACK_LEN)],
                        ENDP7_RXADDR, len);
    Uart.Tx_PackLen[Uart.Tx_LoadNum] = len;
    Uart.Tx_LoadNum++;
    if (Uart.Tx_LoadNum >= DEF_UARTx_TX_BUF_NUM_MAX)
        Uart.Tx_LoadNum = 0x00;
    Uart.Tx_RemainNum++;
    if (Uart.Tx_RemainNum >= (DEF_UARTx_TX_BUF_NUM_MAX - 2))
        Uart.USB_Down_StopFlag = 0x01;
    else
        SetEPRxValid(ENDP7);
}

/*********************************************************************
 * @fn      EP7_IN_Callback
 *
 * @brief   Endpoint 7 IN — CDC-ACM serial data sent to host.
 *
 * @return  none
 */
void EP7_IN_Callback(void)
{
    USBD_Endp7_Busy = 0;
    Uart.USB_Up_IngFlag = 0x00;
}
#endif /* ENABLE_ETH */

/*********************************************************************
 * @fn      EP4_IN_Callback
 *
 * @brief  Endpoint 4 IN (UAC ISO IN — Host audio → Target PC).
 *         Called when the Target PC consumed one ISO frame.
 *
 * @return  none
 */
void EP4_IN_Callback(void)
{
#ifdef ENABLE_UAC
    UAC_USBD_EP3_TxDone();   /* loads next H→T frame into EP4 PMA buffer */
#endif
}

/*********************************************************************
 * @fn      EP5_OUT_Callback
 *
 * @brief   Endpoint 5 OUT — ISO OUT packet received from host.
 *          Called by the USB interrupt when a 1 ms ISO OUT token completes
 *          on EP5 (Target PC microphone → Host PC path).
 *          Reads the received byte-count from the EP5 status register and
 *          forwards it to UAC_USBD_EP4_RxDone() which copies the audio data
 *          from PMA into the ping-pong buffer and re-arms EP5.
 *
 * @return  none
 */
void EP5_OUT_Callback(void)
{
#ifdef ENABLE_UAC
    uint16_t len = GetEPRxCount(ENDP5);
    UAC_USBD_EP4_RxDone(len);
#endif
}


/*********************************************************************
 * @fn      USBD_ENDPx_DataUp
 *
 * @brief  USBD ENDPx DataUp Function
 * 
 * @param   endp - endpoint num.
 *          *pbuf - A pointer points to data.
 *          len - data length to transmit.
 * 
 * @return  data up status.
 */
uint8_t USBD_ENDPx_DataUp( uint8_t endp, uint8_t *pbuf, uint16_t len )
{
	if( endp == ENDP3 )
	{
		if (USBD_Endp3_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write( EP3_IN, pbuf, len );
		USBD_Endp3_Busy = 1;
		SetEPTxStatus( ENDP3, EP_TX_VALID );
	}
#ifdef ENABLE_ETH
	else if( endp == ENDP7 )
	{
		if (USBD_Endp7_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write( EP7_IN, pbuf, len );
		USBD_Endp7_Busy = 1;
		SetEPTxStatus( ENDP7, EP_TX_VALID );
	}
#endif /* ENABLE_ETH */
	else
	{
		return USB_ERROR;
	}
	return USB_SUCCESS;
}
