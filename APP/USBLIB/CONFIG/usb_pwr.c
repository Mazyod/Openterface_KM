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
/* Disable USB suspend to maintain connection when Target PC suspends.
 * When fSuspendEnabled is FALSE, the USBD controller will not enter
 * suspend mode when receiving SUSPEND interrupt from Target. Instead,
 * it will immediately resume, keeping the USBFS serial port connected
 * on the Host side. */
__IO bool fSuspendEnabled = FALSE;
__IO uint32_t EP[8];

struct
{
  __IO RESUME_STATE eState;
  __IO uint8_t bESOFcnt;
}
ResumeS;

__IO uint32_t remotewakeupon=0;

/*******************************************************************************
 * @fn      USBD_WakeupTarget
 *
 * @brief   Send USB remote wakeup signal to Target PC through USBD port.
 *          This function should be called when the Target is suspended and
 *          the Host wants to wake it up via serial command.
 *
 * @return  0: Success, 1: USBD not in suspended state
 */
uint8_t USBD_WakeupTarget(void)
{
    /* Only send wakeup if USBD is in suspended state */
    if (bDeviceState != SUSPENDED) {
        return 1;  /* Not in suspended state */
    }

    /* Trigger internal resume - this will send K-state on USB bus
     * to wake up the Target PC */
    Resume(RESUME_INTERNAL);

    return 0;  /* Success */
}

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
	uint32_t i =0;
	uint16_t wCNTR;

	wCNTR = _GetCNTR();
  for (i=0;i<8;i++) EP[i] = _GetENDPOINT(i);

	wCNTR|=CNTR_RESETM;
	_SetCNTR(wCNTR);

	/* Force USB reset to clear any pending transactions before suspend.
	 * This is needed to ensure clean state before entering low power mode. */
	wCNTR|=CNTR_FRES;
	_SetCNTR(wCNTR);

	wCNTR&=~CNTR_FRES;
	_SetCNTR(wCNTR);

	while((_GetISTR()&ISTR_RESET) == 0);

	_SetISTR((uint16_t)CLR_RESET);

	for (i=0;i<8;i++)
	_SetENDPOINT(i, EP[i]);

	wCNTR |= CNTR_FSUSP;
	_SetCNTR(wCNTR);

	wCNTR = _GetCNTR();
	wCNTR |= CNTR_LPMODE;
	_SetCNTR(wCNTR);

	/* Reset ESOF counter to prevent false triggers on resume.
	 * The ESOF handler monitors D+ and may force a reset if esof_counter
	 * accumulates during suspend, causing enumeration issues. */
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






