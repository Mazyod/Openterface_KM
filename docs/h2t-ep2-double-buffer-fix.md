# Host-to-Target Audio EP2 Double-Buffer Fix

This note documents the fix for distorted host-to-target USB audio at 16 kHz stereo, 16-bit PCM.

## Summary

The host-to-target audio path is:

```text
Host speaker output -> USBD EP2 ISO OUT -> H2T FIFO -> USBFS EP5 ISO IN -> target microphone input
```

The final fix is to configure USBD EP2 ISO OUT as a hardware double-buffered endpoint and copy the completed PMA slot in the EP2 OUT callback before releasing that slot back to the USB peripheral.

The verified firmware image from the debugging session was:

```text
build/openterface-km-firmware-h2t-usbd-ep2-dbuf.hex
```

The normal build output is still:

```text
build/openterface-km-firmware.hex
```

## Symptom

Host-to-target audio was audible but badly distorted for packet-sensitive tones. A 440 Hz sine wave exposed the problem clearly, while 997 Hz was much cleaner and 1000 Hz could hide the defect because it aligns exactly with the 1 ms USB audio packet interval at 16 kHz.

Bad baseline result, using host output device 16 and target input device 17:

```text
440 Hz H2T:
SNR      : 16.2 dB
Jitter   : 106 Hz
Residual : -13.9 dBFS
SINAD    : -14.4 dB
```

Packet-level analysis showed an every-other-packet pattern:

```text
Dominant packet-gain FFT peak : 500 Hz
Even/odd packet ratio         : about -14 dB
Adjacent packet behavior      : repeated/stale 1 ms packet content
```

## Root Cause

The H2T producer side was single-buffering USBD EP2 ISO OUT PMA data. At full-speed UAC timing, that let the callback path occasionally process stale or alternating PMA contents, producing a stable odd/even 1 ms packet modulation. The artifact was signal-correlated, so silence tests looked clean but active 440 Hz audio sounded noisy.

EP5 transmit-side experiments did not fix the problem, which confirmed that the repeated packet was entering the H2T FIFO before USBFS EP5 transmitted it.

## Code Changes

Key files:

- `APP/USBLIB/CONFIG/usb_conf.h`
- `APP/USBLIB/CONFIG/usb_prop.c`
- `APP/uac_handler.c`

EP2 now has two 64-byte PMA receive buffers:

```c
#define ENDP2_ISO_RXADDR    (0x160)
#define ENDP2_ISO_RXADDR1   (0x1A0)
```

During USB reset, EP2 is configured as a double-buffered isochronous OUT endpoint:

```c
SetEPType(ENDP2, EP_ISOCHRONOUS);
SetEPDoubleBuff(ENDP2);
SetEPTxStatus(ENDP2, EP_TX_DIS);
SetEPDblBuffAddr(ENDP2, ENDP2_ISO_RXADDR, ENDP2_ISO_RXADDR1);
SetEPDblBuffCount(ENDP2, EP_DBUF_OUT, 64);
SetEPRxStatus(ENDP2, EP_RX_DIS);
```

When AudioStreaming OUT interface alt setting 1 is selected, EP2 is re-armed:

```c
UAC_H2T_Reset();
SetEPDblBuffCount(ENDP2, EP_DBUF_OUT, 64);
_ClearDTOG_RX(ENDP2);
_ClearDTOG_TX(ENDP2);
SetEPRxStatus(ENDP2, EP_RX_VALID);
```

The EP2 OUT callback reads the completed double-buffer count, copies from the matching PMA address, and releases the consumed buffer:

```c
uint16_t len0 = GetEPDblBuf0Count(ENDP2);
uint16_t len1 = GetEPDblBuf1Count(ENDP2);

PMAToUserBufferCopy(usbd_tmp, pma_addr, copy_len);
FreeUserBuffer(ENDP2, EP_DBUF_OUT);
```

## Verification

After flashing `build/openterface-km-firmware-h2t-usbd-ep2-dbuf.hex` and replugging the device, H2T audio was clean.

```text
440 Hz H2T:
SNR      : 72.2 dB
THD      : 0.01%
Jitter   : 1.60 Hz
Residual : -108.1 dBFS
SINAD    : 94.3 dB

997 Hz H2T:
SNR      : 73.2 dB
THD      : 0.00%
Jitter   : 2.94 Hz
Residual : -108.1 dBFS
SINAD    : 94.4 dB
```

Only the startup mute was detected as a dropout:

```text
t=0.000s, duration about 47 ms
```

Packet-level verification after the fix:

```text
Packet gain mean : 0.289634
Packet gain std  : 0.000003
Even/odd ratio   : -0.00 dB
Old 500 Hz peak  : gone
```

## Recommended Retest

Use 440 Hz first because it catches the packet modulation that 1000 Hz can hide:

```powershell
C:/Users/admin/AppData/Local/Programs/Python/Python310/python.exe .\tools\audio_jitter_test.py --output 16 --input 17 --rate 16000 --channels 2 --duration 5 --tone 440 --report audio_jitter_h2t_ep2_dbuf_440hz.png
```

Then cross-check with 997 Hz:

```powershell
C:/Users/admin/AppData/Local/Programs/Python/Python310/python.exe .\tools\audio_jitter_test.py --output 16 --input 17 --rate 16000 --channels 2 --duration 5 --tone 997 --report audio_jitter_h2t_ep2_dbuf_997hz.png
```

Passing criteria:

- 440 Hz SINAD should be far above 60 dB.
- Residual should be near -100 dBFS or lower.
- Packet-gain analysis should not show a dominant 500 Hz odd/even peak.
- Even/odd packet level should be close to 0 dB.

## Rejected Attempts

These experiments were tested and should not be repeated as fixes for this issue:

- Increasing only the H2T prebuffer level.
- Sample-level FIFO drift stretch/compress.
- USBFS EP5 IN explicit TX toggle changes.
- Changing target-facing EP5 IN descriptor to asynchronous sync type.
- USBFS EP5 software double-buffering.

The successful fix is on the host-facing producer endpoint: USBD EP2 ISO OUT hardware double-buffering.