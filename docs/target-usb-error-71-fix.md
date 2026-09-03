# USB Error -71 Fix: Target PC Suspend/Resume Issue

## Problem Description

When the Target PC suspends and wakes up, the serial port `/dev/ttyACM0` on the Host (Linux) disconnects and fails to re-enumerate. The kernel logs show:

```
usb 1-4.2: device descriptor read/8, error -71
```

Error -71 = EPROTO, indicating the device cannot properly respond to USB enumeration requests.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Host PC (Linux)                                 │
│                              /dev/ttyACM0                                    │
└────────────────────────────────────┬─────────────────────────────────────────┘
                                     │ USB (usb 1-4.2)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           CH32V208 (Openterface)                             │
│  ┌──────────────────────────────┐    ┌──────────────────────────────┐       │
│  │         USBFS Controller     │    │         USBD Controller      │       │
│  │   (Host side, serial port)   │    │   (Target side, K/M data)    │       │
│  └──────────────────────────────┘    └──────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────────────┘
                                     │ USB
                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Target PC                                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

The CH32V208 chip has two independent USB controllers:
- **USBFS** (Full Speed): Connected to Host PC, provides serial port `/dev/ttyACM0` (VID:PID 1A86:FE0C)
- **USBD**: Connected to Target PC, receives keyboard/mouse data

---

## Root Cause

**USBFS controller enters STOP mode, causing the entire MCU to stop running.**

When the Target PC suspends, USBD triggers a SUSPEND interrupt. The original code flow:

```
USBD SUSPEND Interrupt
    ↓
Suspend() function
    ↓
Set CNTR_FSUSP + CNTR_LPMODE
    ↓
Enter_LowPowerMode() sets bDeviceState = SUSPENDED
    ↓
USBFS detects USBFS_DevSleepStatus == 0x03
    ↓
MCU_Sleep_Wakeup_Operate()
    ↓
Enter STOP Mode ← THE PROBLEM!
    ↓
Entire MCU stops, including USBFS controller
    ↓
Host sends USB requests
    ↓
CH32V208 cannot respond (MCU is stopped)
    ↓
Host times out, disconnects device, error -71
```

STOP mode stops the entire MCU including all USB controllers. The Host cannot get any response from the device, leading to disconnection.

---

## Fixes Applied

### Fix 1: Prevent USBFS from Entering STOP Mode (Core Fix)

**File**: `APP/ch32v20x_usbfs_device.c`

**Problem**: USBFS enters STOP mode when `USBFS_DevSleepStatus == 0x03`, stopping the entire MCU.

**Solution**: Don't enter STOP mode, just reset the sleep status.

```c
// Before (problematic):
if( USBFS_DevSleepStatus == 0x03 )
{
    MCU_Sleep_Wakeup_Operate( );  // Enter STOP mode - MCU stops!
}

// After (fixed):
// Don't enter STOP mode, just reset the status
USBFS_DevSleepStatus = 0;
```

**Why it works**: The MCU keeps running, so USBFS can always respond to Host requests.

---

### Fix 2: Optimize USBD Suspend/Resume Flow

**File**: `APP/USBLIB/CONFIG/usb_pwr.c`

**Problem**: The original `Suspend()` function forced a USB reset (`CNTR_FRES`), causing the controller to be in an inconsistent state.

```c
// Before (problematic):
void Suspend(void)
{
    wCNTR |= CNTR_FRES;      // Force USB reset
    _SetCNTR(wCNTR);
    wCNTR &= ~CNTR_FRES;
    _SetCNTR(wCNTR);
    while((_GetISTR()&ISTR_RESET) == 0);  // Wait for reset
    
    wCNTR |= CNTR_FSUSP;
    _SetCNTR(wCNTR);
    wCNTR |= CNTR_LPMODE;
    _SetCNTR(wCNTR);
}

// After (fixed):
void Suspend(void)
{
    uint16_t wCNTR;
    uint32_t i;

    // Save endpoint state
    for (i=0;i<8;i++) EP[i] = _GetENDPOINT(i);

    // Standard USB suspend: just set FSUSP and LPMODE.
    // Do NOT force a USB reset here - that would put the controller
    // in a "just reset" state expecting re-enumeration, which breaks
    // the resume flow when Target wakes up.
    wCNTR = _GetCNTR();
    wCNTR |= CNTR_FSUSP;
    _SetCNTR(wCNTR);

    wCNTR = _GetCNTR();
    wCNTR |= CNTR_LPMODE;
    _SetCNTR(wCNTR);

    // Reset ESOF counter to prevent false triggers on resume
    esof_counter = 0;

    Enter_LowPowerMode();
}
```

**Why it works**: Standard USB suspend doesn't require forcing a reset. The forced reset put the controller in a "just reset" state, expecting re-enumeration, but then immediately suspended it, causing state inconsistency.

---

### Fix 3: Fix Resume_Init() Recovery Flow

**File**: `APP/USBLIB/CONFIG/usb_pwr.c`

**Problem**: The original `Resume_Init()` didn't clear `CNTR_FSUSP`, leaving the USB transceiver in suspended state.

```c
// Before:
void Resume_Init(void)
{
  wCNTR &= (~CNTR_LPMODE);
  _SetCNTR(wCNTR);
  
  Leave_LowPowerMode();
  _SetCNTR(IMR_MSK);
}

// After (fixed):
void Resume_Init(void)
{
  uint16_t wCNTR;
  uint32_t i;

  // Exit low power mode first
  wCNTR = _GetCNTR();
  wCNTR &= (~CNTR_LPMODE);
  _SetCNTR(wCNTR);

  // Critical: Clear FSUSP bit to fully resume USB transceiver.
  // Without clearing FSUSP, the USB transceiver remains in
  // suspended state and won't respond to Host requests.
  wCNTR = _GetCNTR();
  wCNTR &= (~CNTR_FSUSP);
  _SetCNTR(wCNTR);

  // Restore endpoint state that was saved before suspend
  for (i=0;i<8;i++)
    _SetENDPOINT(i, EP[i]);

  Leave_LowPowerMode();

  // Re-enable USB interrupts with full mask
  _SetCNTR(IMR_MSK);

  // Delay to allow USB transceiver to stabilize after resume
  {
    volatile uint32_t delay;
    for(delay = 0; delay < 10000; delay++);
  }
}
```

**Why it works**: Clearing `CNTR_FSUSP` is essential to fully resume the USB transceiver. Without this, the transceiver remains in suspended state and cannot respond to Host requests.

---

### Fix 4: Reset USBFS_DevSleepStatus After STOP Mode Wakeup

**File**: `APP/usbd_compostie_km.c`

**Problem**: After STOP mode wakeup, `USBFS_DevSleepStatus` remains at 0x03, causing immediate re-entry into STOP mode.

```c
void MCU_Sleep_Wakeup_Operate( void )
{
    __disable_irq();
    // ... STOP mode wakeup code ...
    
    // Fully re-initialize USBFS (all registers lost during STOP mode)
    USBFS_Device_Init(ENABLE);

    // Critical: Reset sleep status to prevent immediate re-entry into STOP mode.
    // Without this, USBFS_DevSleepStatus remains 0x03 after wakeup,
    // causing the next SUSPEND interrupt to trigger STOP mode again,
    // leading to enumeration failures.
    USBFS_DevSleepStatus = 0;

    // ... rest of wakeup code ...
}
```

**Why it works**: Prevents the device from immediately re-entering STOP mode after wakeup, breaking the suspend-stop-suspend loop.

---

## Fixed Flow

```
Target PC Suspends
    ↓
USBD SUSPEND Interrupt
    ↓
Suspend() - Set FSUSP + LPMODE, no forced reset
    ↓
bDeviceState = SUSPENDED
    ↓
USBFS detects suspend
    ↓
USBFS_DevSleepStatus = 0  ← Don't enter STOP mode
    ↓
MCU keeps running!
    ↓
Host sends USB requests
    ↓
CH32V208 can respond (MCU is still running)
    ↓
Device stays connected ✓
    ↓
Target PC Wakes Up
    ↓
USBD WKUP Interrupt
    ↓
Resume_Init() - Clear FSUSP, restore endpoints
    ↓
Normal communication restored ✓
```

---

## Modified Files Summary

| File | Changes |
|------|---------|
| `APP/ch32v20x_usbfs_device.c` | Prevent USBFS from entering STOP mode (core fix) |
| `APP/USBLIB/CONFIG/usb_pwr.c` | Optimize Suspend() and Resume_Init(), remove forced reset |
| `APP/usbd_compostie_km.c` | Reset USBFS_DevSleepStatus after wakeup |

---

## Why the Fixes Work

1. **Keep MCU Running**: By not entering STOP mode, USBFS can always respond to Host requests.
2. **Standard USB Suspend Flow**: No forced reset, following USB specifications.
3. **Proper Recovery Flow**: Clear FSUSP, restore endpoint state, ensure normal communication when Target wakes up.
4. **Prevent Loop**: Reset `USBFS_DevSleepStatus` to avoid immediately re-entering suspend.

---

## Testing

After applying these fixes, the device should:
1. Maintain serial port connection (`/dev/ttyACM0`) when Target PC suspends
2. Successfully re-enumerate and resume communication when Target PC wakes up
3. No longer show USB error -71 in kernel logs
