# USB Serial Recovery Issue — Firmware Analysis and Fix Checklist

**Document Version:** v1.0
**Created:** 2026-08-31
**Status:** Fixes Applied (P0 + P1)
**Scope:** Openterface KM Firmware (CH32V208) + Openterface QT Host

---

## 1. Problem Description

### 1.1 Symptoms

After the target computer shuts down / loses power and reboots, the Openterface's USB serial device (CH32V208, VID:PID = 1A86:FE0C) cannot be properly recognized by the Host Linux system, resulting in inability to control the target computer.

### 1.2 Expected Behavior

After the target computer reboots, the serial device should automatically re-enumerate and appear as `/dev/ttyUSBx` (or `/dev/ttyACMx`), and the Host QT application should automatically restore communication.

### 1.3 Actual Behavior

- Composite device (345F:2132, HID keyboard/mouse) enumerates normally ✓
- Serial device (1A86:FE0C, CH32V208 UART function) is not enumerated or cannot communicate after enumeration ✗
- `/dev/ttyUSBx` device node is missing, or exists but cannot be read/written

### 1.4 Reproduction Scenarios

| Scenario | Description | Reproducible |
|----------|-------------|:------------:|
| A | Target PC reboots (Host stays powered, CH32V208 stays powered) | TBD |
| B | Target PC loses power (Host stays powered, CH32V208 loses power) | TBD |
| C | Target PC sleep/wake (Host stays powered) | TBD |
| D | Host PC sleep/wake (Target stays powered) | TBD |

> **TBD:** Is CH32V208's VBUS (5V power supply) sourced from the Host USB port or the Target USB port? This is critical for root cause analysis.

---

## 2. System Architecture

### 2.1 USB Topology

```
Host PC (Openterface QT)
├── USB Hub Port 1-4.4: Composite Device (345F:2132)    ← Independent HID MCU
│   ├── Interface 0: HID Keyboard
│   ├── Interface 1-3: HID Mouse
│
└── USB Hub Port 1-8.1: WCH UART TO KB-MS (1A86:E329)  ← CH32V208
    ├── Serial Function: 1A86:FE0C (USBFS peripheral)
    └── HID Bridge: via USBD peripheral to Target PC

CH32V208 Microcontroller
├── USBFS Peripheral (WCH custom driver)
│   ├── Connects to Host PC
│   ├── VID:PID = 1A86:FE0C
│   ├── Function: CH340-compatible serial bridge
│   └── Interrupt: USBFS_IRQHandler
│
└── USBD Peripheral (ST-compatible library)
    ├── Connects to Target PC
    ├── VID:PID = 1A86:FE0C (USBD side)
    ├── Function: HID keyboard/mouse bridge
    └── Interrupt: USB_LP_CAN1_RX0_IRQHandler
```

### 2.2 Key File Index

| File | Purpose |
|------|---------|
| `APP/ch32v20x_usbfs_device.c` | USBFS device driver (serial side) |
| `APP/ch32v20x_it.c` | Interrupt handler entry |
| `APP/usbd_compostie_km.c` | USB composite device management, sleep/wake handling |
| `APP/USBLIB/CONFIG/usb_prop.c` | USBD device driver (HID side) |
| `APP/USBLIB/CONFIG/usb_pwr.c` | USBD power management (suspend/resume) |
| `APP/USBLIB/CONFIG/hw_config.c` | USBD hardware configuration (GPIO, clock) |
| `APP/peripheral_main.c` | Main function, initialization entry |
| `APP/acm_bridge.c` | ACM serial bridge logic |
| `APP/include/ch32v20x_usbfs_device.h` | USBFS register definitions |

---

## 3. Identified Issues

### Issue #1: USBFS Bus Reset Handler Does Not Restore Critical Registers

**Severity:** 🔴 Critical
**File:** `APP/ch32v20x_usbfs_device.c`
**Location:** `USBFS_UIF_BUS_RST` branch in `USBFS_IRQHandler()`, approximately lines 1636–1654

**Description:**

When the USB host sends a Bus Reset signal (triggered by target PC reboot, Host USB port reset, etc.), the USBFS interrupt handler only restores endpoint registers and software state variables, but **omits restoring three critical hardware control registers**.

**Current code (defective):**

```c
// ch32v20x_usbfs_device.c, lines 1636-1654
else if( intflag & USBFS_UIF_BUS_RST )
{
    USBFS_DevConfig = 0;
    USBFS_DevAddr = 0;
    USBFS_DevSleepStatus = 0;
    USBFS_DevEnumStatus = 0;
    USBFS_Connect = 0;

    USBFSD->DEV_ADDR = 0;
    USBFS_Device_Endp_Init( );        // ✓ Endpoint registers restored
    if (USB_ModeConfig_ActiveModeBridgeAcm()) {
        ACM_Bridge_Init();            // ✓ Bridge buffers cleared
    }
    else if (USB_ModeConfig_ActiveModeBridgeEcm()) {
        ECM_Bridge_Init();
    }
    USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    //  Missing INT_EN restore
    // ✗ Missing BASE_CTRL restore
    // ✗ Missing UDEV_CTRL restore
}
```

**Compared to power-on initialization (correct complete flow):**

```c
// ch32v20x_usbfs_device.c, lines 451-474
void USBFS_Device_Init( FunctionalState sta )
{
    if( sta )
    {
        USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us( 10 );
        USBFSH->BASE_CTRL = 0;

        USBFS_Device_Endp_Init( );
        USBFSD->INT_EN   = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER; // ★
        USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN; // ★
        USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;                        // ★
        NVIC_EnableIRQ( USBFS_IRQn );                                                    // ★
    }
}
```

**Missing registers and their impact:**

| Register | Key Bit | Function | Consequence of Loss |
|----------|---------|----------|---------------------|
| `BASE_CTRL` | `USBFS_UC_DMA_EN` (bit 0) | Enable DMA engine | Bulk endpoint (EP5/6/7) data transfer silently fails |
| `BASE_CTRL` | `USBFS_UC_INT_BUSY` (bit 3) | Return NAK when endpoint busy | Endpoint cannot respond to host correctly |
| `BASE_CTRL` | `USBFS_UC_DEV_PU_EN` (bit 5) | D+ internal pull-up resistor | Host may not detect device |
| `INT_EN` | `USBFS_UIE_*` | USB interrupt enable | Subsequent USB events cannot be handled |
| `UDEV_CTRL` | `USBFS_UD_PORT_EN` | Physical port enable | USB PHY may not work |

**Additional Notes:**

The USBFS Bus Reset handler does not perform an SIE Reset (unlike `USBFS_Device_Init`), so `BASE_CTRL` may retain its value from the last `USBFS_Device_Init` call. This means the bug's trigger depends on the scenario:

- **Cold boot (full power-on):** `USBFS_Device_Init` correctly sets all registers → subsequent Bus Reset doesn't change BASE_CTRL → may **not** trigger this bug
- **After STOP mode wake:** All registers lost → BASE_CTRL is default 0 during Bus Reset → **will definitely** trigger this bug
- **USBD Suspend auto-reset side effect:** USBD's `Suspend()` function executes `CNTR_FRES` (Force USB Reset); if USBFS and USBD share certain hardware resources, this may indirectly affect USBFS state

---

### Issue #2: USBFS Not Re-initialized After STOP Mode Wake

**Severity:** 🔴 Critical
**File:** `APP/usbd_compostie_km.c`
**Location:** `MCU_Sleep_Wakeup_Operate()` function, lines 43–65

**Description:**

After the MCU wakes from STOP mode (deep sleep), all USBFS peripheral registers are lost (peripheral power is off during STOP mode), but the wake handler only restores the clock and **completely skips re-initializing the USBFS device**.

**Current code (defective):**

```c
// usbd_compostie_km.c, lines 43-65
void MCU_Sleep_Wakeup_Operate( void )
{
    __disable_irq();
    EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
    EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );

    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFE);

    SystemInit();           // ✓ System clock restored
    SystemCoreClockUpdate();
    USBFS_RCC_Init();       // ✓ USBFS peripheral clock restored

    // ★ Missing: USBFS_Device_Init(ENABLE) — full USBFS re-initialization

    if( EXTI_GetFlagStatus( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 ) != RESET )
    {
        EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
        USBFS_Send_Resume( );  // Sends K signal, but D+ pull-up not enabled, signal may be invalid
    }
    else if( EXTI_GetFlagStatus( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 ) != RESET )
    {
        EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );
        USBFS_Send_Resume( );
    }
    __enable_irq( );
}
```

**Registers lost during STOP mode (all of them):**

- `USBFSD->BASE_CTRL` → Reset default (0x00)
- `USBFSD->UDEV_CTRL` → Reset default
- `USBFSD->INT_EN` → 0x00 (all interrupts disabled)
- `USBFSD->DEV_ADDR` → 0x00
- All endpoint registers (UEP*_MOD, UEP*_DMA, UEP*_CTRL, UEP*_TX_LEN)
- NVIC USBFS interrupt enable state

**Trigger condition analysis:**

MCU enters STOP mode only when `USBFS_DevSleepStatus == 0x03` (both bits set simultaneously):

| Bit | Meaning | Set Location |
|-----|---------|-------------|
| bit 0 (0x01) | Host enables Remote Wakeup | `USB_SET_FEATURE` handler (line 1350) |
| bit 1 (0x02) | USBFS port detects Suspend | `USBFS_UIF_SUSPEND` handler (line 1662) |

Checking the USBFS configuration descriptor (`ch32v20x_usbfs_device.c` line 286):
```c
// bmAttributes = 0x80 → Bus Powered, NO Remote Wakeup (bit 5 = 0)
0x09, 0x02, 0x27, 0x00, 0x01, 0x01, 0x00, 0x80, 0xf0,
```

Since the descriptor does **not declare Remote Wakeup capability**, `USBFS_GetActiveConfigAttributes() & 0x20` is false, the host's `SET_FEATURE(REMOTE_WAKEUP)` will be rejected, and bit 0 will not be set.

**Therefore, theoretically STOP mode should not be triggered.** However, the following situations may cause unintended entry into STOP:
1. Descriptor is dynamically modified (`USBFS_GetActiveConfigAttributes()` returns a different value)
2. Some Host OS ignores the descriptor's Remote Wakeup bit and forcibly sends SET_FEATURE
3. Other code paths accidentally set `USBFS_DevSleepStatus`

---

### Issue #3: USBFS Power-on Init Immediately Enables D+ Pull-up Without VBUS Stabilization Wait

**Severity:** 🟡 Medium
**File:** `APP/ch32v20x_usbfs_device.c`
**Location:** `USBFS_Device_Init()` function, lines 451–474

**Description:**

After completing register configuration, `USBFS_Device_Init()` **immediately** enables the D+ pull-up resistor without waiting for VBUS and power to fully stabilize. If CH32V208's power supply comes from the target PC (VBUS may be unstable during initial target power-on), this may cause:

1. D+ pull-up enabled before VBUS reaches valid voltage level
2. Host detects device connection and starts enumeration
3. But MCU power is unstable, USB PHY PLL may not lock correctly
4. Timing errors during enumeration → enumeration failure

**Compared to USBD initialization (has 20ms delay):**

```c
// hw_config.c, USB_Port_Set() + USB_Init() call
USB_Port_Set(DISABLE, DISABLE);   // First force D-/D+ low
Delay_Ms(20);                      // ★ Wait 20ms
USB_Port_Set(ENABLE, ENABLE);      // Then enable pull-up
```

USBFS side lacks a similar wait:

```c
// In USBFS_Device_Init()
USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
// ★ No delay, pull-up enabled immediately
```

---

### Issue #4: Firmware Does Not Monitor VBUS Status, Cannot Detect Physical Connection Changes

**Severity:** 🟡 Medium
**File:** Global
**Location:** All files

**Description:**

Searching the entire `APP/` directory for `VBUS`, `vbus`, `VBus` yields zero results. The firmware completely lacks VBUS (5V USB power) status monitoring.

**Impact:**

When target PC power loss/restoration causes VBUS disconnect/reconnect, the firmware cannot detect this physical-layer event. All state changes can only rely on USB protocol-layer interrupts (Bus Reset, Suspend), which have the bugs described in #1 and #2 above.

If VBUS monitoring were added, the firmware could execute a complete `USBFS_Device_Init(DISABLE)` → `USBFS_Device_Init(ENABLE)` sequence on VBUS restoration, bypassing all bugs in the Resume/Reset handlers.

---

### Issue #5: STOP Mode Wake Uses GPIO EXTI Wake, Not USB Wake

**Severity:**  Medium
**File:** `APP/usbd_compostie_km.c`
**Location:** `MCU_Sleep_Wakeup_Operate()` function, lines 43–65

**Description:**

`USB_Sleep_Wakeup_CFG()` configures `EXTI_Line20` (USBFS wake line), but `MCU_Sleep_Wakeup_Operate()` checks wake sources on keyboard/mouse GPIO pins (EXTI_Line12-15, EXTI_Line4-7), not USB events.

If USB bus activity (e.g., Host sending Bus Reset) is the only wake source during STOP mode, the MCU may not wake up correctly.

---

## 4. Host (Openterface QT) Recovery Mechanism Analysis

The QT side has 4 layers of recovery mechanisms, which should theoretically handle most disconnect/reconnect scenarios:

| Layer | Mechanism | Trigger Condition | Max Latency |
|-------|-----------|-------------------|-------------|
| 1 | HotplugMonitor | USB device plug/unplug | ~2s (polling) |
| 2 | DeviceLifecycleManager | Device disconnect | 60s quick reconnect window |
| 3 | ConnectionWatchdog | Communication timeout/error | ~31s (exponential backoff) |
| 4 | RTS hardware reset | Target USB disconnect >2s | ~6.5s |

**RTS hardware reset** (`FactoryResetManager`) is a powerful recovery method:
- RTS held low for 4 seconds → CH32V208 hardware reset
- Theoretically allows CH32V208 to fully reboot from scratch

**However, RTS reset has a prerequisite: the serial device node (`/dev/ttyUSBx`) must exist.** If CH32V208 completely loses power causing the Host device node to disappear, RTS reset cannot execute (no serial port to operate on). In this case, it relies on HotplugMonitor detecting the device reappearing before proceeding with the normal connection flow.

**QT-side potential issues:**

1. `openSerialPort()` does not set a timeout (`SerialPortManager.cpp` line 258); in blocking mode, if the device response is delayed, `read()` will wait indefinitely
2. HotplugMonitor uses 2-second polling (not udev event-driven), detection delay may cause recovery flow to start too slowly
3. Multiple recovery mechanisms may compete with each other (RTS reset vs USB Hub port reset vs reconnection strategy)

---

## 5. Failure Chain Analysis

### Scenario A: Target PC Reboots, CH32V208 Powered from Host (stays powered)

```
Target PC reboots
  → Target USB port sends Bus Reset to CH32V208's USBD peripheral
  → USBD triggers ISTR_RESET interrupt
  → USBD_Reset() correctly re-initializes USBD endpoints
  → USBD side should recover normally

  → USBFS unaffected (Host USB connection always online)
  → Serial should stay connected

  Expected: Serial should work normally
  If abnormal: May be caused by USBD and USBFS sharing hardware resources
```

### Scenario B: Target PC Loses Power, CH32V208 Powered from Target (complete power loss)

```
Target PC loses power
  → CH32V208 completely powers off, all registers lost
  → Host USB detects device disconnect
  → /dev/ttyUSBx disappears
  → QT enters recovery wait

Target PC powers on
  → VBUS restored, CH32V208 begins power-on
  → Voltage regulator charges → 3.3V stable (~5ms)
  → Firmware begins executing main()
  → USB_Init() → USBD initialization (~25ms)
  → USBFS_Device_Init() → USBFS initialization (~30ms)
    → BASE_CTRL correctly set ✓
    → D+ pull-up enabled immediately
  → Delay_Ms(1000) → Main loop starts (~1030ms)

  → Host detects D+ pull-up → sends USB Reset
  → USBFS Bus Reset interrupt triggers
    → USBFS_Device_Endp_Init() ✓
    → BASE_CTRL unchanged (no SIE Reset) ✓
    → Enumeration should succeed

  → QT detects device → opens serial port → CMD_RESET + CMD_GET_INFO
  → If firmware is ready → Communication restored ✓

  Possible failure causes:
    1. VBUS instability causing USB PHY initialization anomaly (Issue #3)
    2. Host USB Hub port in abnormal state, cannot initiate enumeration correctly
    3. Linux ch340 driver in abnormal state during rapid disconnect/reconnect
    4. STOP mode accidentally triggered (Issue #2), USBFS dead after wake
```

### Scenario C: Host PC Sleep/Wake

```
Host PC sleeps
  → USB bus enters Suspend state
  → CH32V208 USBFS detects Suspend
  → USBFS_DevSleepStatus |= 0x02
  → If bit 0 also set (Remote Wakeup enabled) → enters STOP mode
  → All registers lost!

Host PC wakes
  → USB bus resumes → Bus Reset
  → MCU may be woken (via EXTI)
  → MCU_Sleep_Wakeup_Operate():
    → SystemInit() ✓
    → USBFS_RCC_Init() ✓
    → ★ USBFS_Device_Init() NOT called!
    → USBFS completely dead

  → Serial will never recover
  → This is a direct consequence of Issue #2
```

---

## 6. Recommended Fixes

### Fix 1: USBFS Bus Reset Handler Restores Critical Registers

**Priority:** P0 (Fix Immediately)
**Modified File:** `APP/ch32v20x_usbfs_device.c`
**Modified Location:** `USBFS_UIF_BUS_RST` branch in `USBFS_IRQHandler()`

```c
else if( intflag & USBFS_UIF_BUS_RST )
{
    USBFS_DevConfig = 0;
    USBFS_DevAddr = 0;
    USBFS_DevSleepStatus = 0;
    USBFS_DevEnumStatus = 0;
    USBFS_Connect = 0;

    USBFSD->DEV_ADDR = 0;
    USBFS_Device_Endp_Init( );

    /* Restore critical control registers (consistent with USBFS_Device_Init) */
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;

    if (USB_ModeConfig_ActiveModeBridgeAcm()) {
        ACM_Bridge_Init();
    }
    else if (USB_ModeConfig_ActiveModeBridgeEcm()) {
        ECM_Bridge_Init();
    }
    USBFSD->INT_FG = USBFS_UIF_BUS_RST;
}
```

### Fix 2: Fully Re-initialize USBFS After STOP Mode Wake

**Priority:** P0 (Fix Immediately)
**Modified File:** `APP/usbd_compostie_km.c`
**Modified Location:** `MCU_Sleep_Wakeup_Operate()` function

```c
void MCU_Sleep_Wakeup_Operate( void )
{
    __disable_irq();
    EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
    EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );

    PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFE);

    SystemInit();
    SystemCoreClockUpdate();
    USBFS_RCC_Init();

    /* Fully re-initialize USBFS (all registers lost during STOP mode) */
    USBFS_Device_Init(ENABLE);

    if( EXTI_GetFlagStatus( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 ) != RESET )
    {
        EXTI_ClearFlag( EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15 );
        USBFS_Send_Resume( );
    }
    else if( EXTI_GetFlagStatus( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 ) != RESET )
    {
        EXTI_ClearFlag( EXTI_Line4 | EXTI_Line5 | EXTI_Line6 | EXTI_Line7 );
        USBFS_Send_Resume( );
    }
    __enable_irq( );
}
```

### Fix 3: Add D+ Pull-up Delay to USBFS Power-on Initialization

**Priority:** P1 (Recommended Fix)
**Modified File:** `APP/ch32v20x_usbfs_device.c`
**Modified Location:** `USBFS_Device_Init()` function

```c
void USBFS_Device_Init( FunctionalState sta )
{
    if( sta )
    {
        USBFSH->BASE_CTRL = USBFS_UC_RESET_SIE | USBFS_UC_CLR_ALL;
        Delay_Us( 10 );
        USBFSH->BASE_CTRL = 0;

        USBFS_Device_Endp_Init( );
        USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;

        /* Wait for power stability before enabling D+ pull-up */
        Delay_Ms( 100 );

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
```

---

## 7. Items for Further Investigation

| # | Investigation Item | Method | Purpose |
|---|-------------------|--------|---------|
| 1 | CH32V208 VBUS power source | Check hardware schematic | Determine behavior during power loss |
| 2 | Whether USBFS and USBD share physical pins | Check CH32V208 datasheet AFIO mapping | Confirm if two USB peripherals can interfere |
| 3 | Whether USBD `Suspend()` `CNTR_FRES` affects USBFS | Add USBFS register value logging during USBD Suspend | Confirm shared resource conflict |
| 4 | Host Linux kernel `dmesg` logs | `sudo dmesg \| grep -i "usb\|ch34\|1a86"` | Confirm specific enumeration failure errors |
| 5 | Whether STOP mode is actually triggered | Add GPIO toggle at `MCU_Sleep_Wakeup_Operate()` entry | Confirm STOP mode trigger frequency |
| 6 | Full power loss test | Unplug all USB cables, wait 10s, reconnect | Distinguish "state machine issue" from "hardware issue" |

---

## 8. Workarounds (No Firmware Changes)

Before firmware fixes are applied, the issue can be mitigated through the following approaches:

### 8.1 Full Power Cycle Restart

1. Unplug all USB connections from both Host PC and Target PC
2. Wait 10 seconds (ensure CH32V208 fully discharges)
3. Reconnect Target PC USB
4. Reconnect Host PC USB
5. Check if `/dev/ttyUSBx` appears

### 8.2 Host-side USB Port Reset

Reset USB port via sysfs, forcing VBUS disconnect then restore:

```bash
# Find device USB path (adjust based on actual device)
DEVICE_PATH=$(find /sys/bus/usb/devices/ -name "idVendor" -exec grep -l "1a86" {} \; | head -1 | xargs dirname)

# Reset
echo "0" | sudo tee ${DEVICE_PATH}/authorized
sleep 1
echo "1" | sudo tee ${DEVICE_PATH}/authorized
```

### 8.3 QT-side RTS Reset

If the serial node exists but is unresponsive, CH32V208 can be forcibly reset via RTS signal:

```bash
# Send RTS reset signal using python
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB0')
s.setRTS(True)   # RTS low → CH32V208 reset pin pulled low
time.sleep(4)    # Wait 4 seconds
s.setRTS(False)  # RTS released
time.sleep(2)    # Wait for CH32V208 reboot
s.close()
"
```

---

## Appendix A: USB Register Quick Reference

### BASE_CTRL (USBFSD Offset) Bit Definitions

```
Bit 7 6 5 4 3 2 1 0
    │ │ │ │ │ │ │ │
    │ │ │ │ │ │ │ └─ USBFS_UC_DMA_EN     (0x01) DMA engine enable
    │ │ │ │ │ │ └─── USBFS_UC_CLR_ALL     (0x02) Clear all endpoint buffers
    │ │ │ │ │ ───── USBFS_UC_RESET_SIE   (0x04) Reset serial engine
    │ │ │ │ └─────── (reserved)
    │ │ │ └───────── USBFS_UC_INT_BUSY    (0x08) Return NAK/STALL when endpoint busy
    │ │ └─────────── (reserved)
    │ └───────────── USBFS_UC_DEV_PU_EN   (0x20) Device D+ internal pull-up enable
    ─────────────── (reserved)
```

### UDEV_CTRL (USBFSD Offset) Bit Definitions

```
Bit 7 6 5 4 3 2 1 0
    │ │ │ │ │ │ │ │
    │ │ │ │ │ │ │ └─ (reserved)
    │ │ │ │ │ │ └─── (reserved)
    │ │ │ │ │ ───── (reserved)
    │ │ │ │ └─────── (reserved)
    │ │ │ └───────── (reserved)
    │ │ └─────────── (reserved)
    │ └───────────── USBFS_UD_PD_DIS      Disable physical port pull-down
    └─────────────── USBFS_UD_PORT_EN     Physical port enable
```

## Appendix B: Related Documents

- `Openterface_QT/docs/serial-port-recovery-issue.md` — Host-side issue analysis
- CH32V208 Datasheet — USBFS/USBD register descriptions
- USB 2.0 Specification — Section 7.1 (Reset), Section 7.1.7 (Suspend)
