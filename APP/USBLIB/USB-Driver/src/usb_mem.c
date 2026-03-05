/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_mem.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Utility functions for memory transfers to/from PMA
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"


/*******************************************************************************
 * @fn           UserToPMABufferCopy
 *
 * @brief        Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param        pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes: no. of bytes to be copied.
 *
 * @param        None	.
 */
void UserToPMABufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint32_t n = (wNBytes + 1) >> 1;   
  uint32_t i, temp1, temp2;
  uint16_t *pdwVal;
  pdwVal = (uint16_t *)(wPMABufAddr * 2 + PMAAddr);
	
  for (i = n; i != 0; i--)
  {
    temp1 = (uint16_t) * pbUsrBuf;
    pbUsrBuf++;
    temp2 = temp1 | (uint16_t) * pbUsrBuf << 8;
    *pdwVal++ = temp2;
    pdwVal++;
    pbUsrBuf++;
  }
}

/*******************************************************************************
 * @fn          PMAToUserBufferCopy
 *
 * @brief       Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param       pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes:  no. of bytes to be copied.
 *
 * @param       None. 
 */
void PMAToUserBufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint32_t n = (wNBytes + 1) >> 1;
  uint32_t i;
  uint32_t *pdwVal;

  /* PMA is 16-bit wide, mapped into 32-bit address space:
   * each 16-bit PMA word occupies one 32-bit slot (upper 16 bits unused).
   * wPMABufAddr is a byte offset into PMA; multiply by 2 to get the
   * 32-bit-spaced address.  Read the low 16 bits of each 32-bit slot.   */
  pdwVal = (uint32_t *)(wPMABufAddr * 2 + PMAAddr);

  for (i = n; i != 0; i--)
  {
    uint16_t val = (uint16_t)(*pdwVal++);  /* read 16-bit PMA word from 32-bit slot */
    *pbUsrBuf++ = (uint8_t)(val & 0xFFu);  /* low byte  */
    *pbUsrBuf++ = (uint8_t)(val >> 8u);    /* high byte */
  }
}






