/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_pwr.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Connection/disconnection & power management
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_pwr.h"
#include "hw_config.h"

/* External reference to ESOF counter in usb_istr.c.
 * Reset to 0 during suspend to prevent false ESOF triggers on resume. */
extern __IO uint32_t esof_counter;

__IO uint32_t bDeviceState = UNCONNECTED;
__IO bool fSuspendEnabled = TRUE;
__IO uint32_t EP[8];

struct
{
  __IO RESUME_STATE eState;
  __IO uint8_t bESOFcnt;
}
ResumeS;

__IO uint32_t remotewakeupon=0;

/*******************************************************************************
 * @fn      PowerOn
 *
 * @brief   Enable power on.
 *
 * @return  USB_SUCCESS.
 */
RESULT PowerOn(void)
{
  uint16_t wRegVal;

  wRegVal = CNTR_FRES;
  _SetCNTR(wRegVal);
  wInterrupt_Mask = 0;
  _SetCNTR(wInterrupt_Mask);
  _SetISTR(0);
  wInterrupt_Mask = CNTR_RESETM | CNTR_SUSPM | CNTR_WKUPM;
  _SetCNTR(wInterrupt_Mask);
  
  return USB_SUCCESS;
}

/*******************************************************************************
 * @fn      PowerOff
 *
 * @brief   handles switch-off conditions
 *
 * @return  USB_SUCCESS.
 */
RESULT PowerOff()
{
  _SetCNTR(CNTR_FRES); 
  _SetISTR(0); 
  _SetCNTR(CNTR_FRES + CNTR_PDWN);

  return USB_SUCCESS;
}

/*******************************************************************************
 * @fn        Suspend
 *
 * @brief     sets suspend mode operating conditions
 *
 * @return    USB_SUCCESS.
 */
void Suspend(void)
{
	uint16_t wCNTR;

	/* Save endpoint state (not strictly needed without forced reset,
	 * but kept for safety) */
	{
		uint32_t i;
		for (i=0;i<8;i++) EP[i] = _GetENDPOINT(i);
	}

	/* Standard USB suspend: just set FSUSP and LPMODE.
	 * Do NOT force a USB reset here - that would put the controller
	 * in a "just reset" state expecting re-enumeration, which breaks
	 * the resume flow when Target wakes up. */
	wCNTR = _GetCNTR();
	wCNTR |= CNTR_FSUSP;
	_SetCNTR(wCNTR);

	wCNTR = _GetCNTR();
	wCNTR |= CNTR_LPMODE;
	_SetCNTR(wCNTR);

	/* Reset ESOF counter to prevent false triggers on resume */
	esof_counter = 0;

	Enter_LowPowerMode();
}

/*******************************************************************************
 * @fn       Resume_Init
 *
 * @brief    Handles wake-up restoring normal operations
 *
 * @return   USB_SUCCESS.
 */
void Resume_Init(void)
{
  uint16_t wCNTR;
  uint32_t i;

  /* Exit low power mode first */
  wCNTR = _GetCNTR();
  wCNTR &= (~CNTR_LPMODE);
  _SetCNTR(wCNTR);

  /* Clear FSUSP bit to fully resume USB transceiver.
   * This is critical - without clearing FSUSP, the USB transceiver
   * remains in suspended state and won't respond to Host requests. */
  wCNTR = _GetCNTR();
  wCNTR &= (~CNTR_FSUSP);
  _SetCNTR(wCNTR);

  /* Restore endpoint state that was saved before suspend */
  for (i=0;i<8;i++)
    _SetENDPOINT(i, EP[i]);

  Leave_LowPowerMode();

  /* Re-enable USB interrupts with full mask.
   * This ensures RESET, SUSPEND, WKUP interrupts are all enabled
   * for proper USB operation after resume. */
  _SetCNTR(IMR_MSK);

  /* Small delay to allow USB transceiver to stabilize after resume.
   * This prevents race conditions where Host sends requests before
   * device is fully ready. */
  {
    volatile uint32_t delay;
    for(delay = 0; delay < 10000; delay++);
  }
}

/*******************************************************************************
 * @fn       Resume
 *
 * @brief    This is the state machine handling resume operations and
 *                 timing sequence. The control is based on the Resume structure
 *                 variables and on the ESOF interrupt calling this subroutine
 *                 without changing machine state.
 *
 * @param    a state machine value (RESUME_STATE)
 *                  RESUME_ESOF doesn't change ResumeS.eState allowing
 *                  decrementing of the ESOF counter in different states.
 *
 * @return  None.
 */
void Resume(RESUME_STATE eResumeSetVal)
{
  uint16_t wCNTR;

  if (eResumeSetVal != RESUME_ESOF)
	{
   ResumeS.eState = eResumeSetVal;
	}
	
  switch (ResumeS.eState)
  {
    case RESUME_EXTERNAL:
      if (remotewakeupon ==0)
      {
        Resume_Init();
        ResumeS.eState = RESUME_OFF;
      }
      else 
      {
        ResumeS.eState = RESUME_ON;
      }
      break;
			
    case RESUME_INTERNAL:
      Resume_Init();
      ResumeS.eState = RESUME_START;
      remotewakeupon = 1;
      break;
		
    case RESUME_LATER:
      ResumeS.bESOFcnt = 2;
      ResumeS.eState = RESUME_WAIT;
      break;
		
    case RESUME_WAIT:
      ResumeS.bESOFcnt--;
      if (ResumeS.bESOFcnt == 0)
        ResumeS.eState = RESUME_START;
      break;
			
    case RESUME_START:
      wCNTR = _GetCNTR();
      wCNTR |= CNTR_RESUME;
      _SetCNTR(wCNTR);
      ResumeS.eState = RESUME_ON;
      ResumeS.bESOFcnt = 10;
      break;
		
    case RESUME_ON:    
      ResumeS.bESOFcnt--;
      if (ResumeS.bESOFcnt == 0)
      {
        wCNTR = _GetCNTR();
        wCNTR &= (~CNTR_RESUME);
        _SetCNTR(wCNTR);
        ResumeS.eState = RESUME_OFF;
        remotewakeupon = 0;
      }
      break;
			
    case RESUME_OFF:
			
    case RESUME_ESOF:
			
    default:
      ResumeS.eState = RESUME_OFF;
      break;
  }
}






