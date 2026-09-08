# HID Queue Reliability & Response Path Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the cross-context queue races and the response-queue drain bug that cause lost keystrokes/mouse events and the ~40% reply-loss rate under CH9329 stress, then re-model the absolute-mouse queue as a newest-wins mailbox and remove the O(N²) `memmove` from the receive parser.

**Architecture:** Phase 1 wraps every `Queue_Push_*` / `Queue_Pop_*` / `USB_SendFromQueues` head-tail read-modify-write and the `CH9329_RxBuffer_Add` static buffer in nesting-safe critical sections (MSTATUS save/restore — `__enable_irq()` is NOT nesting-safe on CH32V20x), fixes the unconditional `resp_queue` drain so exactly one consumer pops replies per USB mode, and deletes the dead `USBD_SendFromRespQueue()` declaration. Phase 2 converts the absolute-mouse FIFO into a single-slot mailbox and deepens the relative-mouse FIFO 28 → 64. Phase 3 replaces the per-frame `memmove` in the CH9329 receive path with a consume-offset cursor plus amortized-O(1) compaction.

**Tech Stack:** C firmware for WCH CH32V20x (RISC-V, 144 MHz), WCH USBFS/USBD controller drivers (`ch32v20x_usbfs_device.c`), no RTOS, no data cache. Build: `./build.sh <kvmgo|minikvm|keymod-plus>` with `riscv-none-embed-gcc` 8.2.0.

**Spec:** `docs/superpowers/specs/2026-09-08-hid-queue-reliability-redesign.md` (committed, design approved). This plan argues from that spec; executors read both. §5.2 defines the critical-section macros, §5.3 the drain gate, §5.4 the receive-buffer scheme, §6.1 the file list, §12 the checklist this plan implements.

## Global Constraints

- **Critical-section primitive**: only the nesting-safe pair
  `ENTER_CRITICAL()` / `EXIT_CRITICAL()` = `__get_MSTATUS()` + `__disable_irq()` /
  `__set_MSTATUS(_mstatus)`. Never exit with `__enable_irq()` — it does
  `csrs mstatus, MIE`, which breaks an outer critical section when nested.
- Macros are defined in `km_ring.h` but only *expanded* inside `.c` files; every
  translation unit that expands them must include a chip header
  (`ch32v20x_usbfs_device.h`, which pulls in `core_riscv.h`) before the macro
  use-site. Current macro users (`km_ring.c`, `usbd_compostie_km.c`,
  `ecm_bridge.c`) all include a chip header before `km_ring.h`, or at the top of
  the file — keep that ordering.
- `USBFS_Endp_DataUp(..., DEF_UEP_CPY_LOAD)` copies synchronously into the USBFS
  internal RAM before returning `READY` (verified in `ch32v20x_usbfs_device.c:487-604`).
  A source buffer may be reused/overwritten immediately after a `READY` return.
- `USBD_ENDPx_DataUp()` returns `0` (`USB_SUCCESS`) on success, non-zero on
  failure — the existing `== 0` test is correct; keep it.
- `MS_ABS_DATA_LEN` is 6 normally, 5 with `EP3_USE_STYLUS`. Size any stack copy
  with the macro, never a literal.
- Compile gate per code task (no hardware): `./build.sh kvmgo` must exit 0.
  The full 3-board gate runs once in the final validation task.
- Functional tests require a live device + host tooling. They are separate
  tasks; stop and hand them to the user rather than skipping them silently.
- Work in small commits; each task commits independently. Do not modify files
  outside the task's `Files:` list.

---

### Task 1: Add nesting-safe critical-section macros to `km_ring.h` and delete the dead `USBD_SendFromRespQueue()` declaration

**Files:**
- Modify: `APP/include/km_ring.h` (after the queue-size defines, ~line 13; and delete line 115)

**Interfaces:**
- Produces: `ENTER_CRITICAL()` / `EXIT_CRITICAL()` macros — used by every later
  task. No function signatures change.

- [ ] **Step 1: Add the macro block**

Insert after the queue-size `#define`s (after `#define MEDIA_QUEUE_SIZE 16`):

```c
/* Nesting-safe critical-section helpers (CH32V20x RISC-V).
 *
 * __enable_irq() unconditionally sets MIE (csrs mstatus, MIE) and is NOT
 * nesting-safe, so we save/restore MSTATUS instead: an inner EXIT can never
 * re-enable interrupts that an outer ENTER is still holding.
 * Requires __get_MSTATUS/__set_MSTATUS/__disable_irq declarations at the
 * expansion site — every .c that expands these macros includes a chip header
 * (ch32v20x_usbfs_device.h → core_riscv.h) before the use-site. */
#define ENTER_CRITICAL()  uint32_t _mstatus = __get_MSTATUS(); __disable_irq()
#define EXIT_CRITICAL()   __set_MSTATUS(_mstatus)
```

- [ ] **Step 2: Delete the dead declaration**

Remove exactly this line (currently `km_ring.h:115`):

```c
void USBD_SendFromRespQueue(void);    // Send response packets via USBD (endpoint)
```

No definition and no caller exist anywhere in `APP/`; the function was part of
an earlier, incorrect root-cause analysis (see spec §2.2).

- [ ] **Step 3: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0 (macros are unused yet; deletion is the only behavioral delta —
verify with `grep -rn "USBD_SendFromRespQueue" APP/` that no reference remains).

- [ ] **Step 4: Commit**

```bash
git add APP/include/km_ring.h
git commit -m "refactor(km_ring): add nesting-safe critical-section macros, drop dead USBD_SendFromRespQueue decl

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 2: Protect all `Queue_Push_*` functions and `Queue_Init` in `km_ring.c`

**Files:**
- Modify: `APP/km_ring.c:20-62` (init + five push functions)

**Interfaces:**
- Consumes: `ENTER_CRITICAL()` / `EXIT_CRITICAL()` from Task 1.
- Produces: push functions that are safe against a concurrent pop/drain in the
  main loop while an ISR/callback pushes (spec §2.1, §5.2).

- [ ] **Step 1: Wrap `Queue_Init`**

Replace the `Queue_Init` body so head/tail reset cannot race an early USB/ISR
push during boot:

```c
/* Initialize queues */
void Queue_Init(void) {
    ENTER_CRITICAL();
    kb_queue.head = kb_queue.tail = 0;
    ms_rel_queue.head = ms_rel_queue.tail = 0;
    ms_abs_queue.head = ms_abs_queue.tail = 0;
    resp_queue.head = resp_queue.tail = 0;
    media_queue.head = media_queue.tail = 0;
    EXIT_CRITICAL();
}
```

(Note: `ms_abs_queue` still exists in Phase 1; Task 7 replaces it with the
mailbox and rewrites this function again.)

- [ ] **Step 2: Wrap `Queue_Push_Keyboard`**

Replace the whole function:

```c
//Keyboard message queue
void Queue_Push_Keyboard(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[kb_queue.tail].data));
    kb_queue.tail = next;
    EXIT_CRITICAL();
}
```

- [ ] **Step 3: Wrap `Queue_Push_MouseRel`**

```c
//MouseRel message queue
void Queue_Push_MouseRel(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (ms_rel_queue.tail + 1) % MS_REL_QUEUE_SIZE;
    if (next == ms_rel_queue.head) ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
    memcpy(ms_rel_queue.buf[ms_rel_queue.tail].data, data, sizeof(ms_rel_queue.buf[ms_rel_queue.tail].data));
    ms_rel_queue.tail = next;
    EXIT_CRITICAL();
}
```

- [ ] **Step 4: Wrap `Queue_Push_MouseAbs`**

```c
//MouseAbs message queue
void Queue_Push_MouseAbs(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (ms_abs_queue.tail + 1) % MS_ABS_QUEUE_SIZE;
    if (next == ms_abs_queue.head) ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
    memcpy(ms_abs_queue.buf[ms_abs_queue.tail].data, data, sizeof(ms_abs_queue.buf[ms_abs_queue.tail].data));
    ms_abs_queue.tail = next;
    EXIT_CRITICAL();
}
```

- [ ] **Step 5: Wrap `Queue_Push_Response`**

```c
//Response message queue (Message length varies)
void Queue_Push_Response(uint8_t *data, uint8_t len) {
    ENTER_CRITICAL();
    int next = (resp_queue.tail + 1) % RESP_QUEUE_SIZE;
    if (next == resp_queue.head) resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
    memcpy(resp_queue.buf[resp_queue.tail].data, data, len);
    resp_queue.buf[resp_queue.tail].len = len;
    resp_queue.tail = next;
    EXIT_CRITICAL();
}
```

- [ ] **Step 6: Wrap `Queue_Push_Media`**

```c
//Media key message queue
void Queue_Push_Media(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (media_queue.tail + 1) % MEDIA_QUEUE_SIZE;
    if (next == media_queue.head) media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
    memcpy(media_queue.buf[media_queue.tail].data, data, sizeof(media_queue.buf[media_queue.tail].data));
    media_queue.tail = next;
    EXIT_CRITICAL();
}
```

- [ ] **Step 7: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 8: Commit**

```bash
git add APP/km_ring.c
git commit -m "fix(km_ring): protect Queue_Init and all Queue_Push_* with critical sections

Pushes run in USB ISR / BLE callback / ECM handler / main-loop UART2 contexts
while the main loop drains; unprotected tail updates caused lost events and
ordering corruption (spec §2.1, §5.2).

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 3: Protect all `Queue_Pop_*` functions in `km_ring.c`

**Files:**
- Modify: `APP/km_ring.c:64-99` (five pop functions)

**Interfaces:**
- Consumes: macros from Task 1.
- Produces: `Queue_Pop_Response()` now safe for the ECM-bridge consumer
  (`ecm_bridge.c:1149`). This is the implementation of spec §12's
  "wrap the `Queue_Pop_Response` in `ecm_bridge.c`" item — encapsulating the
  lock inside the pop function protects the ECM bridge without editing
  `ecm_bridge.c` (spec §6.1 table note already says no edit is strictly
  required there; this satisfies both).

- [ ] **Step 1: Wrap `Queue_Pop_Keyboard`**

```c
bool Queue_Pop_Keyboard(KB_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (kb_queue.head != kb_queue.tail) {
        memcpy(out->data, kb_queue.buf[kb_queue.head].data, sizeof(out->data));
        kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 2: Wrap `Queue_Pop_MouseRel`**

```c
bool Queue_Pop_MouseRel(MS_Rel_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (ms_rel_queue.head != ms_rel_queue.tail) {
        memcpy(out->data, ms_rel_queue.buf[ms_rel_queue.head].data, sizeof(out->data));
        ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 3: Wrap `Queue_Pop_MouseAbs`**

```c
bool Queue_Pop_MouseAbs(MS_Abs_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (ms_abs_queue.head != ms_abs_queue.tail) {
        memcpy(out->data, ms_abs_queue.buf[ms_abs_queue.head].data, sizeof(out->data));
        ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 4: Wrap `Queue_Pop_Response`**

```c
bool Queue_Pop_Response(Resp_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (resp_queue.head != resp_queue.tail) {
        memcpy(out->data, resp_queue.buf[resp_queue.head].data, resp_queue.buf[resp_queue.head].len);
        out->len = resp_queue.buf[resp_queue.head].len;
        resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 5: Wrap `Queue_Pop_Media`**

```c
bool Queue_Pop_Media(Media_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (media_queue.head != media_queue.tail) {
        memcpy(out->data, media_queue.buf[media_queue.head].data, sizeof(out->data));
        media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 6: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 7: Commit**

```bash
git add APP/km_ring.c
git commit -m "fix(km_ring): protect all Queue_Pop_* with critical sections

Queue_Pop_Response is the ECM bridge's consumer (ecm_bridge.c:1149); locking
inside the pop protects it without touching ecm_bridge.c.

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: Fix `USB_SendFromQueues()` — gate the `resp_queue` drain and protect every head read-modify-write

**Files:**
- Modify: `APP/km_ring.c:101-146` (whole `USB_SendFromQueues`)

**Interfaces:**
- Consumes: macros (Task 1); `USB_ModeConfig_ActiveModeBridgeEcm()` /
  `USB_ModeConfig_ActiveModeHasHostCdcEcm()` (already declared in
  `APP/include/usb_mode_config.h` and included by `km_ring.c`); `RESP_QUEUE_SIZE`.
- Produces: a single-consumer `resp_queue` service per USB mode (spec §2.2,
  §5.3). `USB_SendFromQueues()` keeps its signature — `peripheral_main.c:126`
  needs no change.

- [ ] **Step 1: Code walk-through to confirm the gate conditions**

Before editing, verify the two consumers of `resp_queue` and their run
contexts (this closes spec §11's open question):

1. `grep -rn "Queue_Pop_Response\|resp_queue" APP --include="*.c"` → the only
   pop is `ecm_bridge.c:1149`, inside `handle_host_control_frame()`.
2. Read `ecm_bridge.c:1093-1190`: `handle_host_control_frame()` calls
   `CH9329_RxBuffer_Add()` (line 1143 — reply pushed synchronously into
   `resp_queue` by the parser) and then immediately pops it (line 1149) in the
   same call chain. Pushes and the pop are therefore back-to-back in one pass.
3. Confirm `ActiveModeBridgeEcm()` is true only in ECM-bridge modes where the
   USBD ECM bulk-IN owns ENDP3 (`usb_endp.c:282-297` uses 64-byte bulk in that
   mode), i.e. a raw CH9329 reply written to ENDP3 by this drain would corrupt
   the ECM network stream — the ~60%-loss mechanism.
4. Confirm `ActiveModeHasHostCdcEcm()` marks the combined ACM+ECM mode whose
   comment at `km_ring.c:134-138` says the ECM bridge handles responses.

If the walk-through contradicts the spec's gate (e.g. finds a live ACM-origin
CH9329 path in combined mode with no other consumer), STOP and report — do not
silently change the gate. Expected result: the gate below is correct.

- [ ] **Step 2: Replace the whole function**

Replace `km_ring.c:101-146` (function signature through its closing brace) with:

```c
/* Try to send all queued packets until the endpoint is busy or queue is empty */
void USB_SendFromQueues(void) {
    while (kb_queue.head != kb_queue.tail && USBFS_Endp_Busy[DEF_UEP1] == 0) {
        KB_Report_t *pkt = &kb_queue.buf[kb_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP1, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    while (ms_rel_queue.head != ms_rel_queue.tail && USBFS_Endp_Busy[DEF_UEP2] == 0) {
        MS_Rel_Report_t *pkt = &ms_rel_queue.buf[ms_rel_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP2, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            ms_rel_queue.head = (ms_rel_queue.head + 1) % MS_REL_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    while (ms_abs_queue.head != ms_abs_queue.tail && USBFS_Endp_Busy[DEF_UEP3] == 0) {
        MS_Abs_Report_t *pkt = &ms_abs_queue.buf[ms_abs_queue.head];
        if (USBFS_Endp_DataUp(DEF_UEP3, pkt->data, sizeof(pkt->data), DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            ms_abs_queue.head = (ms_abs_queue.head + 1) % MS_ABS_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    while (media_queue.head != media_queue.tail && USBFS_Endp_Busy[DEF_UEP4] == 0) {
        Media_Report_t *pkt = &media_queue.buf[media_queue.head];
        /* Report ID 0x01 = ACPI: 2 bytes (ID + 1 data)
         * Report ID 0x02 = Consumer: 4 bytes (ID + 3 data) */
        uint16_t send_len = (pkt->data[0] == 0x01) ? 2 : 4;
        if (USBFS_Endp_DataUp(DEF_UEP4, pkt->data, send_len, DEF_UEP_CPY_LOAD) == READY) {
            ENTER_CRITICAL();
            media_queue.head = (media_queue.head + 1) % MEDIA_QUEUE_SIZE;
            EXIT_CRITICAL();
        }
        else break;
    }

    /* Response queue drain — exactly one consumer per USB mode.
     *
     * The old code had an empty `if (ActiveModeHasHostCdcEcm()) { }` here whose
     * comment said the queue must NOT be drained in ECM modes, but the drain
     * below ran unconditionally anyway. In ECM-bridge / host-CDC-ECM modes the
     * ECM bridge pops resp_queue itself (ecm_bridge.c Queue_Pop_Response, right
     * after its own push) and wraps replies in Ethernet; draining here would
     * race it and inject raw CH9329 bytes into the ECM bulk stream. */
    if (!USB_ModeConfig_ActiveModeBridgeEcm() &&
        !USB_ModeConfig_ActiveModeHasHostCdcEcm()) {
        while (resp_queue.head != resp_queue.tail) {
            Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
            if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0) {
                ENTER_CRITICAL();
                resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
                EXIT_CRITICAL();
            }
            else break;  /* endpoint busy, retry next pass */
        }
    }
}
```

- [ ] **Step 3: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 4: Commit**

```bash
git add APP/km_ring.c
git commit -m "fix(km_ring): gate resp_queue drain to one consumer and protect head advances

USB_SendFromQueues drained resp_queue unconditionally into USBD ENDP3 even in
ECM modes where the ECM bridge owns the queue (ecm_bridge.c:1149 pops right
after its own push). Both consumers raced head/tail; a raw CH9329 reply won by
this drain corrupted the ECM stream. Drain only when no ECM bridge is the
active responder, and hold a critical section around every head advance while
ISR/callback pushes can run (spec §5.3).

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: Wrap `CH9329_RxBuffer_Add` framing state in a critical section (Phase 1 minimal version)

**Files:**
- Modify: `APP/usbd_compostie_km.c:357-397` (`rx_buf`/`rx_len` statics + `CH9329_RxBuffer_Add`)

**Interfaces:**
- Consumes: macros from Task 1.
- Produces: a `CH9329_RxBuffer_Add()` whose shared static buffer is protected
  across USB ISR (`EP5_OUT_Callback`), BLE callback (`peripheral.c:995`), ECM
  frame handler (`ecm_bridge.c:1143`) and main-loop UART2 path
  (`USB_DataRx_To_KMHandle`). Semantics unchanged — Phase 3 (Task 10) replaces
  the internals, this task only adds the lock.

- [ ] **Step 1: Add the critical section**

Wrap the body of `CH9329_RxBuffer_Add` (everything after the
`if (len == 0 || data == NULL) return;` guard) with `ENTER_CRITICAL()` /
`EXIT_CRITICAL()`. The parsing loop below it calls `CH9329_DataParser`, which
may push a response via `Queue_Push_Response` — its own `ENTER_CRITICAL` nests
safely under this outer section by MSTATUS save/restore.

Replace the statics and the whole function:

```c
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_len = 0;

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    /* Callers span USB ISR (EP5_OUT_Callback), a BLE callback, the ECM frame
     * handler and the main-loop UART2 path — protect the shared static buffer.
     * The parser below may Queue_Push_Response; that nested ENTER_CRITICAL is
     * safe because the macros save/restore MSTATUS. */
    ENTER_CRITICAL();

    // Prevent buffer overflow; if overflow risk, retain only the latter half
    if (rx_len + len > RX_BUF_SIZE) {
        uint16_t keep = RX_BUF_SIZE / 2;
        if (keep > rx_len) keep = rx_len;
        memmove(rx_buf, rx_buf + (rx_len - keep), keep);
        rx_len = keep;
    }

    // Append new data
    memcpy(rx_buf + rx_len, data, len);
    rx_len += len;

    // Try parsing frames as long as data exists
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf, rx_len);
        if (consumed < 0) {
            // Parsing error — clear buffer
            rx_len = 0;
            break;
        } else if (consumed == 0) {
            // Incomplete frame — wait for more data
            break;
        } else {
            // Remove consumed bytes from buffer
            if ((uint16_t)consumed < rx_len) {
                memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
                rx_len -= (uint16_t)consumed;
            } else {
                // Exactly consumed all data
                rx_len = 0;
                break;
            }
        }
    }

    EXIT_CRITICAL();
}
```

- [ ] **Step 2: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add APP/usbd_compostie_km.c
git commit -m "fix(ch9329): protect CH9329_RxBuffer_Add framing buffer with critical section

rx_buf/rx_len is shared across USB ISR, BLE callback, ECM handler and main-loop
UART2 contexts; framing state mutated without a lock lost or corrupted frames
(spec §2.1). Nested Queue_Push_Response is safe via MSTATUS save/restore.

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 6: Phase 1 functional validation — reply-rate stress test (hardware)

**Files:** none (test only)

**Interfaces:**
- Consumes: Tasks 1-5.

- [ ] **Step 1: Flash the firmware**

Build and flash per the repo's normal flow (`tools/flash.sh` or the
`./build_flash_cycle.sh` used for the board under test). Confirm the board
re-enumerates.

- [ ] **Step 2: Run the reply-rate stress test**

With the device in the USB mode the host normally uses (the mode where ~60%
replies were observed — per the user report this is the CH9329-over-ECM/TCP
path; the serial variant is below), send 200 consecutive CH9329 `GET_INFO`
(0x01) commands and count correctly-addressed replies (response command byte
`0x81`).

ECM/TCP path — reuse the repo's own integration harness:

```bash
python tools/test_ch9329_ecm_full.py --iface usb0 -v
```

Serial/ACM path (if the failing mode was serial) — one-off script:

```python
import glob, serial, time
port = glob.glob("/dev/cu.usbmodem*")[0]
ser = serial.Serial(port, 115200, timeout=2)
frame = bytes([0x57, 0xAB, 0x00, 0x01, 0x00])
frame += bytes([sum(frame) & 0xFF])
ok = 0
for _ in range(200):
    ser.reset_input_buffer()
    ser.write(frame)
    resp = ser.read(64)
    if len(resp) >= 4 and resp[0] == 0x57 and resp[1] == 0xAB and resp[3] == 0x81:
        ok += 1
print(f"reply rate: {ok}/200 = {ok / 2:.1f}%")
```

Expected: reply rate ≥ 99% (was ~60%).

- [ ] **Step 3: Report results**

Pass criteria from spec §7.1: ≥ 99% reply rate. If it passes, tell the user
Phase 1 is done and ask them to confirm before Phase 2. If it fails, do not
proceed — investigate with the queue-depth diagnostics option (spec §11,
optional) and report back.

---

### Task 7: Convert the absolute-mouse queue to a single-slot mailbox

**Files:**
- Modify: `APP/include/km_ring.h` (queue-struct block + extern, lines ~75-80 and ~92)
- Modify: `APP/km_ring.c` (global, `Queue_Init`, `Queue_Push_MouseAbs`,
  `Queue_Pop_MouseAbs`, and the MS_ABS section of `USB_SendFromQueues`)

**Interfaces:**
- Consumes: macros (Task 1); `MS_Abs_Report_t`, `MS_ABS_DATA_LEN` (already in
  `km_ring.h`).
- Produces:
  - `typedef struct { MS_Abs_Report_t slot; volatile uint8_t valid; } MS_Abs_Mailbox_t;`
  - `extern MS_Abs_Mailbox_t ms_abs_mbox;`
  - `ms_abs_queue` and `MS_Abs_Queue_t` are **removed** — no other file
    references them (verified: only `km_ring.h`/`km_ring.c` use them).
  - `Queue_Push_MouseAbs(uint8_t*)`, `Queue_Pop_MouseAbs(MS_Abs_Report_t*)`
    keep their signatures (mouse_handler.c callers unchanged).

- [ ] **Step 1: Header — replace the queue type and extern**

In `km_ring.h`, replace the `MS_Abs_Queue_t` typedef:

```c
/* Mouse absolute report — single-slot mailbox (newest wins).
 * Absolute coordinates are stateful: only the newest not-yet-sent frame
 * matters, so a FIFO only ever stores stale positions (spec §4, §5.1). */
typedef struct {
    MS_Abs_Report_t slot;       /* newest pending absolute report */
    volatile uint8_t valid;     /* 1 = slot holds an unsent report */
} MS_Abs_Mailbox_t;
```

Then replace the extern declaration `extern MS_Abs_Queue_t ms_abs_queue;` with:

```c
extern MS_Abs_Mailbox_t ms_abs_mbox;
```

(Delete the old `MS_Abs_Queue_t` typedef entirely; do not keep a dead type.)

- [ ] **Step 2: `km_ring.c` — global and `Queue_Init`**

Replace `MS_Abs_Queue_t ms_abs_queue;` with:

```c
MS_Abs_Mailbox_t ms_abs_mbox;
```

Replace `Queue_Init`'s absolute line (`ms_abs_queue.head = ms_abs_queue.tail = 0;`)
with a mailbox reset inside the existing critical section:

```c
    ms_abs_mbox.valid = 0;
```

- [ ] **Step 3: `km_ring.c` — mailbox push**

Replace `Queue_Push_MouseAbs`:

```c
//MouseAbs message queue — mailbox: unconditionally overwrite with the newest state
void Queue_Push_MouseAbs(uint8_t *data) {
    ENTER_CRITICAL();
    memcpy(ms_abs_mbox.slot.data, data, sizeof(ms_abs_mbox.slot.data));
    ms_abs_mbox.valid = 1;
    EXIT_CRITICAL();
}
```

- [ ] **Step 4: `km_ring.c` — mailbox pop**

Replace `Queue_Pop_MouseAbs`:

```c
bool Queue_Pop_MouseAbs(MS_Abs_Report_t *out) {
    bool popped = false;
    ENTER_CRITICAL();
    if (ms_abs_mbox.valid) {
        memcpy(out->data, ms_abs_mbox.slot.data, sizeof(out->data));
        ms_abs_mbox.valid = 0;
        popped = true;
    }
    EXIT_CRITICAL();
    return popped;
}
```

- [ ] **Step 5: `km_ring.c` — drain the mailbox in `USB_SendFromQueues()`**

Replace the MS_ABS `while` loop (the one referencing `ms_abs_queue` and
`DEF_UEP3`) with a single-shot send of the newest state:

```c
    /* Absolute mouse: mailbox — send the newest pending report whenever the
     * endpoint is free. Copy under the critical section so a concurrent push
     * cannot tear the bytes the USB controller is about to read, and clear
     * valid before DataUp: a push during the copy simply marks valid again and
     * the newest state goes out next pass (no loss, at most one stale frame). */
    if (USBFS_Endp_Busy[DEF_UEP3] == 0) {
        uint8_t abs_report[MS_ABS_DATA_LEN];
        uint8_t have = 0;
        ENTER_CRITICAL();
        if (ms_abs_mbox.valid) {
            memcpy(abs_report, ms_abs_mbox.slot.data, sizeof(abs_report));
            ms_abs_mbox.valid = 0;
            have = 1;
        }
        EXIT_CRITICAL();
        if (have &&
            USBFS_Endp_DataUp(DEF_UEP3, abs_report, sizeof(abs_report), DEF_UEP_CPY_LOAD) != READY) {
            /* Endpoint became busy between check and arm (should not happen:
             * only this drain arms EP3). Restore so the report is not lost. */
            ENTER_CRITICAL();
            memcpy(ms_abs_mbox.slot.data, abs_report, sizeof(abs_report));
            ms_abs_mbox.valid = 1;
            EXIT_CRITICAL();
        }
    }
```

- [ ] **Step 6: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0. Grep that no `ms_abs_queue` or `MS_Abs_Queue_t` reference
remains: `grep -rn "ms_abs_queue\|MS_Abs_Queue_t" APP/` → no output.

- [ ] **Step 7: Commit**

```bash
git add APP/include/km_ring.h APP/km_ring.c
git commit -m "perf(km_ring): model absolute mouse as single-slot newest-wins mailbox

Absolute coordinates are stateful; a 28-deep FIFO only stored stale positions
and dropped the newest when full, causing drag gaps. Mailbox holds 6+1 bytes
instead of 168 and always sends the newest state the moment EP3 is free
(spec §5.1). Draining copies under the critical section so an ISR push cannot
tear the frame being armed.

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 8: Deepen the relative-mouse queue 28 → 64

**Files:**
- Modify: `APP/include/km_ring.h:10`

**Interfaces:**
- Consumes: nothing new.
- Produces: `MS_REL_QUEUE_SIZE == 64` — a capacity of 63 queued relative
  reports against fast-movement bursts; +144 B RAM (spec §4 table).

- [ ] **Step 1: Change the constant**

```c
#define MS_REL_QUEUE_SIZE  64     // Relative mouse report queue size
```

- [ ] **Step 2: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add APP/include/km_ring.h
git commit -m "perf(km_ring): deepen relative-mouse queue 28 -> 64

Relative reports are per-frame deltas that must not be dropped during bursts;
28 slots overflowed under fast movement. 63-deep capacity costs +144 B RAM.

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 9: Phase 2 functional validation — fast typing, fast dragging (hardware)

**Files:** none (test only)

**Interfaces:**
- Consumes: Tasks 7-8.

- [ ] **Step 1: Flash and run the scenario checks**

Flash the current build. With the host software driving the device as a
keyboard/mouse to a target:

| Scenario | Method | Pass criteria |
|----------|--------|---------------|
| Fast typing | Sustained typing > 20 keys/s (hold a key repeat or paste burst) | No lost keys; keyboard report shows every press/release |
| Fast relative mouse | Rapid flick/continuous movement of the physical or virtual mouse | Continuous trail, no gaps |
| Absolute-mouse drag | Drag a window / draw continuously in remote-desktop session | Fully continuous trail (spec §7.1) |
| Media keys | Rapid repeated media-key presses | Every press registers |

- [ ] **Step 2: Report results**

Tell the user the outcome and ask them to confirm before Phase 3.

---

### Task 10: Convert `CH9329_RxBuffer_Add` to consume-offset scheme (Phase 3)

**Files:**
- Modify: `APP/usbd_compostie_km.c:357-397` (statics + `CH9329_RxBuffer_Add`)

**Interfaces:**
- Consumes: macros (Task 1), `CH9329_DataParser` (unchanged signature, operates
  on contiguous memory starting at `rx_buf + rx_start`).
- Produces: `static uint16_t rx_start` consume cursor. Behavior changes:
  - Zero `memmove` per parsed frame (O(N²) → amortized O(1)); at most one
    `memmove` per `Add`.
  - Overflow policy: drop the **oldest** bytes just enough to fit the **newest**
    data (spec §5.4). This replaces the old keep-latter-half policy.
  - `CH9329_DataParser` is still called with a contiguous buffer; no ring-wrap.

- [ ] **Step 1: Replace the statics and function**

Replace `static uint8_t rx_buf[RX_BUF_SIZE]; static uint16_t rx_len = 0;` plus
the whole `CH9329_RxBuffer_Add` with:

```c
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_start = 0;  /* offset of the first unparsed byte */
static uint16_t rx_len = 0;    /* number of valid bytes from rx_start */

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    if (len == 0 || data == NULL) return;

    /* Callers span USB ISR (EP5_OUT_Callback), a BLE callback, the ECM frame
     * handler and the main-loop UART2 path — protect the shared static buffer.
     * The parser below may Queue_Push_Response; that nested ENTER_CRITICAL is
     * safe because the macros save/restore MSTATUS. */
    ENTER_CRITICAL();

    /* Reset the cursor whenever the buffer empties so rx_start cannot drift to
     * the end of the array across many small frames. */
    if (rx_len == 0) rx_start = 0;

    /* Compact only when the tail cannot hold the new bytes:
     * at most one memmove per Add (amortized O(1)). */
    if ((uint32_t)rx_start + rx_len + len > RX_BUF_SIZE) {
        if ((uint32_t)rx_len + len > RX_BUF_SIZE) {
            /* Still too big even after compaction: drop the oldest bytes so the
             * newest data survives. */
            uint16_t drop = (uint16_t)((uint32_t)rx_len + len - RX_BUF_SIZE);
            rx_start += drop;
            rx_len -= drop;
        }
        if (rx_len > 0) memmove(rx_buf, rx_buf + rx_start, rx_len);
        rx_start = 0;
    }

    /* Append new data */
    memcpy(rx_buf + rx_start + rx_len, data, len);
    rx_len += len;

    /* Parse frame by frame: a consumed frame only advances rx_start — no
     * shuffling. CH9329_DataParser runs on contiguous memory and is unchanged. */
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf + rx_start, rx_len);
        if (consumed < 0) {
            /* Parsing error — clear buffer */
            rx_start = 0;
            rx_len = 0;
            break;
        } else if (consumed == 0) {
            /* Incomplete frame — wait for more data */
            break;
        } else {
            rx_start += (uint16_t)consumed;
            rx_len -= (uint16_t)consumed;
        }
    }

    EXIT_CRITICAL();
}
```

- [ ] **Step 2: Compile gate**

Run: `./build.sh kvmgo`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
git add APP/usbd_compostie_km.c
git commit -m "perf(ch9329): consume-offset receive buffer, zero memmove per parsed frame

The parser advanced by memmoving the remainder after every frame (O(N^2) under
bursts). A rx_start cursor now absorbs consumed bytes; one compaction memmove
happens at most per Add. Overflow drops the oldest bytes to keep the newest.
Frame parsing still sees contiguous memory (spec §5.4).

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 11: Phase 3 validation, full build matrix, spec status update, final review

**Files:**
- Modify: `docs/superpowers/specs/2026-09-08-hid-queue-reliability-redesign.md` (§13 status table + §12 checklist)
- Test: none new — full regression + benchmarks

**Interfaces:**
- Consumes: Tasks 1-10.

- [ ] **Step 1: Build all three board variants**

Run each and confirm exit 0 (catches board-specific `USB_WORK_MODE_*`
conditional compilation, e.g. ECM modes only compiled on some boards):

```bash
./build.sh kvmgo
./build.sh minikvm
./build.sh keymod-plus
```

- [ ] **Step 2: Performance sanity check (spec §7.2)**

Flash `kvmgo`. Method available without extra scope hardware: time N main-loop
iterations via the existing `systick_ms` / GPIO-toggle diagnostics and compare
to the pre-change baseline log if available; or toggle a GPIO each pass and
measure the loop period with an oscilloscope/logic analyzer.

Expected: main-loop average load down 15–25% (mostly from Task 10's removed
`memmove`s); critical-section width < 1 µs.

- [ ] **Step 3: Full-scenario functional regression (spec §7.1)**

With hardware attached:

```bash
# host/target functional suites in the repo (skip those needing unavailable modes)
pytest tests/ -x -v --timeout 120
```

Plus the manual scenarios from Tasks 6 and 9. Pass criteria: reply rate ≥ 99%,
no lost keys/mouse, continuous absolute drags.

- [ ] **Step 4: Update the spec status**

In `docs/superpowers/specs/2026-09-08-hid-queue-reliability-redesign.md`:
- Tick the completed §12 checklist items (Phase 1, 2, 3 implementation items;
  leave validation items matching what actually ran).
- Update §13 Status Tracking:

```markdown
| Stage | Status | Date |
|-------|--------|------|
| Design | ✅ Complete | 2026-09-08 |
| Phase 1 implementation | ✅ Complete | 2026-09-08 |
| Phase 2 implementation | ✅ Complete | 2026-09-08 |
| Phase 3 implementation | ✅ Complete | 2026-09-08 |
| Validation | ✅ Complete | 2026-09-08 |
```

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-09-08-hid-queue-reliability-redesign.md
git commit -m "docs: mark HID queue reliability redesign implementation complete

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

- [ ] **Step 6: Final review pass**

Read the combined diff of Tasks 1-10 (`git log --oneline -N` and
`git diff <pre-phase-1>..HEAD`) and check against the spec's Risks table
(§9): critical sections only around index RMW + tiny copies; mailbox drops
stale coordinates by design; overflow drops oldest; drain retried each pass.
Surface anything surprising to the user.

---

## Self-Review Notes

- **Spec coverage**: §5.2 macros → Task 1; §5.2 push/pop protection → Tasks
  2-3; §5.3 drain gate + dead-declaration removal → Tasks 1 & 4; §5.4
  receive-buffer → Tasks 5 (lock) & 10 (offset); §5.1 mailbox → Task 7;
  MS_REL deepening → Task 8; §7 validation → Tasks 6, 9, 11; §13 status →
  Task 11. `peripheral_main.c` intentionally untouched (spec §6.1 note).
- **Deviations from the spec text**: (1) spec §12 asked to wrap the ECM-bridge
  pop in `ecm_bridge.c`; this plan instead locks inside `Queue_Pop_Response()`
  (Task 3), which covers that pop with zero edits to `ecm_bridge.c` and matches
  spec §6.1's own "no edit strictly required" note. (2) The mailbox drain
  copies to a stack buffer under the lock and clears `valid` before arming
  (Task 7 step 5) so an ISR push cannot tear the frame being copied by
  `USBFS_Endp_DataUp`; spec §5.1's simpler snippet omitted this, but the
  synchronous-copy semantics (Global Constraints) make the refined version
  strictly safer. (3) Task 10 adds an `rx_len == 0 → rx_start = 0` reset and
  guards the compaction `memmove` with `rx_len > 0`, two edge-case fixes not
  spelled out in spec §5.4's pseudocode.
- **Cross-task type consistency**: `MS_Abs_Mailbox_t` / `ms_abs_mbox` defined
  in Task 7 are used only inside `km_ring.c` and declared in `km_ring.h`;
  `Queue_Push_MouseAbs`/`Queue_Pop_MouseAbs` signatures never change, so
  `mouse_handler.c` is untouched.
