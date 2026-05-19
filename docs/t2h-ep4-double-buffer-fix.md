# Target-to-Host Audio EP4 Double-Buffer Fix

This note documents the fix for distorted target-to-host USB audio at 16 kHz stereo, 16-bit PCM.

## Summary

The target-to-host audio path is:

```text
Target speaker output -> USBFS EP6 ISO OUT -> T2H FIFO -> USBD EP4 ISO IN -> host microphone input
```

The final fix is to configure USBD EP4 ISO IN as a hardware double-buffered endpoint and write alternating PMA slots before releasing each slot back to the USB peripheral.

The cleaned firmware image from the debugging session is:

```text
build/openterface-km-firmware-t2h-ep4-dbuf-fix.hex
```

The normal build output is still:

```text
build/openterface-km-firmware.hex
```

## Symptom

Target-to-host audio was badly distorted for packet-sensitive tones. A 440 Hz sine wave exposed the defect clearly: instead of a clean 440 Hz capture, the host often received a dominant component near 880 Hz with heavy distortion and poor tone correlation.

Bad baseline result, using target output device 6 and host input device 1:

```text
440 Hz T2H:
SNR      : -11.4 dB
THD      : 835.52%
Jitter   : 138.52 Hz
Residual : -5.3 dBFS
SINAD    : -48.5 dB
```

The failure was not random broadband noise. A firmware-generated 440 Hz diagnostic tone on EP4 also came back near 880 Hz, which isolated the issue to the host-facing EP4 transmit side rather than USBFS EP6 capture.

## Root Cause

USBD EP4 ISO IN was single-buffering its PMA transmit data. At full-speed 1 ms UAC timing, the host-facing capture path could consume partially refreshed or stale PMA contents, which folded a 440 Hz tone into a strong second harmonic near 880 Hz.

FIFO tuning on the USBFS producer side did not fix the problem, and neither did USBFS EP6 experiments. The decisive isolation step was a firmware-generated EP4 tone: when that tone also folded to 880 Hz, the remaining root cause was the EP4 ISO IN PMA path itself.

## Code Changes

Key files:

- APP/USBLIB/CONFIG/usb_conf.h
- APP/USBLIB/CONFIG/usb_prop.c
- APP/USBLIB/CONFIG/usb_desc.c
- APP/USBLIB/CONFIG/usb_desc.h
- APP/USBLIB/CONFIG/usb_endp.c
- APP/UART/UART.h
- APP/uac_handler.c

EP4 now has two 64-byte PMA transmit buffers:

```c
#define ENDP4_ISO_TXADDR    (0x100)
#define ENDP4_ISO_TXADDR1   (0x140)
```

During USB reset, EP4 is configured as a double-buffered isochronous IN endpoint:

```c
SetEPType(ENDP4, EP_ISOCHRONOUS);
SetEPDoubleBuff(ENDP4);
SetEPDblBuffAddr(ENDP4, ENDP4_ISO_TXADDR, ENDP4_ISO_TXADDR1);
SetEPDblBuffCount(ENDP4, EP_DBUF_IN, 64);
SetEPTxStatus(ENDP4, EP_TX_DIS);
```

When AudioStreaming IN interface alt setting 1 is selected, both EP4 PMA slots are primed with silence and the double-buffer counters are armed:

```c
UserToPMABufferCopy((uint8_t*)silence, ENDP4_ISO_TXADDR, 64);
UserToPMABufferCopy((uint8_t*)silence, ENDP4_ISO_TXADDR1, 64);
SetEPDblBuffCount(ENDP4, EP_DBUF_IN, 64);
SetEPTxStatus(ENDP4, EP_TX_VALID);
```

The EP4 transmit path now writes alternating PMA slots and releases the just-prepared slot with `FreeUserBuffer(ENDP4, EP_DBUF_IN)`:

```c
uint16_t pma_addr = g_t2h_ep4_pong ? ENDP4_ISO_TXADDR1 : ENDP4_ISO_TXADDR;
UserToPMABufferCopy((uint8_t *)src, pma_addr, UAC_USBD_ISO_PACKET_SIZE);
SetEPDblBuffCount(ENDP4, EP_DBUF_IN, UAC_USBD_ISO_PACKET_SIZE);
FreeUserBuffer(ENDP4, EP_DBUF_IN);
g_t2h_ep4_pong ^= 1u;
```

The 512-byte USBD PMA budget is tight once both EP2 OUT and EP4 IN are double-buffered. To make the layout fit cleanly, the host-facing CDC packet slots were reduced to 16 bytes and the matching descriptor/constants were updated:

```c
#define DEF_USBD_MAX_PACK_SIZE      16
#define DEF_USB_FS_PACK_LEN         16
```

and in the descriptor:

```c
0x10, 0x00,   // CDC wMaxPacketSize 16
```

## Verification

After flashing `build/openterface-km-firmware-t2h-ep4-dbuf-fix.hex`, the real target-to-host bridge was clean.

```text
440 Hz T2H:
SNR      : 63.4 dB
THD      : 0.00%
Jitter   : 1.32 Hz
Residual : -94.7 dBFS
SINAD    : 89.8 dB

997 Hz T2H:
SNR      : 63.8 dB
THD      : 0.01%
Jitter   : 12.23 Hz
Residual : -94.8 dBFS
SINAD    : 89.8 dB
```

The only remaining dropout was the initial startup mute:

```text
t=0.000s, duration about 225 ms
```

A direct FFT cross-check on the fixed 440 Hz path showed the old failure was gone:

```text
Strongest FFT peak : 440.00 Hz
Corr @ 440 Hz     : strong
Corr @ 880 Hz     : near zero
```

## Recommended Retest

Use 440 Hz first because it catches the old second-harmonic fold clearly:

```powershell
c:\Users\admin\.venv_openterface\Scripts\python.exe .\tools\audio_jitter_test.py --output 6 --input 1 --rate 16000 --channels 2 --duration 5 --tone 440 --report build\t2h-ep4-dbuf-fix-440-report.png
```

Then cross-check with 997 Hz:

```powershell
c:\Users\admin\.venv_openterface\Scripts\python.exe .\tools\audio_jitter_test.py --output 6 --input 1 --rate 16000 --channels 2 --duration 5 --tone 997 --report build\t2h-ep4-dbuf-fix-997-report.png
```

Passing criteria:

- 440 Hz should peak at 440 Hz, not 880 Hz.
- THD should stay near 0%.
- Residual should stay near -95 dBFS or lower.
- SINAD should be comfortably above 60 dB.
- Only the startup mute should appear as a dropout.

## Rejected Attempts

These experiments were tested and should not be repeated as the primary fix for this issue:

- T2H FIFO-only start-level and high-water tuning.
- T2H underrun concealment changes alone.
- USBFS EP6 OUT hardware ping-pong buffering.
- USBFS EP6 producer-side experiments without changing EP4.
- Treating the issue as a stereo phase cancellation problem.

The successful fix is on the host-facing consumer endpoint: USBD EP4 ISO IN hardware double-buffering.
