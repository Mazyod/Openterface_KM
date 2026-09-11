# HID Queue Reliability & Response Path Redesign

> **Status: DESIGN APPROVED — READY FOR IMPLEMENTATION**
>
> This document supersedes the earlier `queue-performance-optimization-plan.md`,
> correcting that document's misunderstanding of the CH32V20x USBFS "DMA mode"
> and introducing systematic fixes for bidirectional event loss on the
> keyboard/mouse communication path.
>
> The design is based on a full code review of `km_ring.c`,
> `ch32v20x_usbfs_device.c`, `usbd_compostie_km.c` and `peripheral_main.c`,
> plus the user-observed symptom of "only ~60% of CH9329 command replies
> arriving at the host serial port under stress testing".

---

## 1. Background & Problem Statement

The Openterface_KM firmware runs on a CH32V20x and acts as a USB/BLE device
that receives CH9329 commands from the host (KVM controller), translates them
into HID keyboard/mouse reports sent to the target machine, and sends command
acknowledgements back to the host.

**Observed problems**:

| Symptom | Manifestation | Impact |
|---------|---------------|--------|
| Lost keys/mouse on fast operation | Missed keys when typing fast; mouse-trail gaps during fast movement | Poor UX |
| High reply-loss rate | Only ~60% of CH9329 command replies reach the host under stress testing | Host believes the device is unresponsive; triggers retries or errors |
| Poor absolute-mouse dragging | Jumpy, non-continuous trails when dragging windows or drawing | Remote-desktop scenarios unusable |

**Problems with the original optimization plan**:
- Misread the USBFS `DEF_UEP_DMA_LOAD` as "zero-copy DMA" — it is actually
  only an offset register pointing into the USBFS internal RAM.
- Did not identify the cross-context queue race (BLE interrupt vs. main loop).
- Did not identify that the response queue is never serviced by the main loop.
- The SoA layout yields dubious benefit on CH32V20x (no data cache).

---

## 2. Root-Cause Analysis

### 2.1 Cross-context queue race (primary)

**Evidence**: `Queue_Push_*` is invoked from multiple contexts:
- USB ISR (`EP5_OUT_Callback` in `usb_endp.c` → `CH9329_RxBuffer_Add` → direct
  push) — USB interrupt context.
- BLE callback (`ble_usb_ServiceEvt` → `CH9329_DataParser` → direct push) —
  BLE protocol-stack context.
- Main loop (`USB_DataRx_To_KMHandle` → UART2 path → push; ECM frame handler →
  push).

Queue consumers (`USB_SendFromQueues()` drain, ECM bridge `Queue_Pop_Response`)
run in the main loop. Neither the push sequence ("write data → update tail")
nor the consume sequence ("read head/tail → take data → advance head") is
protected by a critical section.

**Consequences**: during fast operation (high-frequency pushes from
interrupts/callbacks plus a high-frequency main-loop drain), the following can
happen:
- Pop reads an empty queue before push updates tail → **lost event**.
- Both advance head/tail concurrently → **ordering corruption / overwrite**.
- `memcpy` races → **dirty report sent** (target sees wrong keys).

Additionally, `CH9329_RxBuffer_Add`'s static buffer (`rx_buf`/`rx_len`,
`RX_BUF_SIZE` = 2048) is itself shared across USB ISR, BLE callback, ECM frame
handling, and the main-loop UART2 path — the receive buffer needs the same
protection.

### 2.2 Response queue drain logic is broken (primary)

**Evidence (corrected after code verification)**:
- `USBD_SendFromRespQueue()` is a dead declaration in `km_ring.h:115` — no
  definition, no caller. The response queue is actually drained at the end of
  `USB_SendFromQueues()` (`km_ring.c:140-145`) into USBD `ENDP3`.
- `km_ring.c:134-138` contains an **empty `if`**: its comment says that in ECM
  mode the queue must NOT be drained here (the ECM bridge pops responses
  itself), but the `resp_queue` drain below was left **unconditional**.
- The ECM bridge also consumes the same queue: `ecm_bridge.c:1149`
  `Queue_Pop_Response(&resp)` wraps the reply into Ethernet/IPv4/TCP back to
  the host.

**Consequences**:
- In ECM-bridge mode the main-loop `USB_SendFromQueues()` (which runs every
  pass *before* `ECM_Bridge_Poll()`) races the ECM bridge for the same
  `resp_queue`. Whichever drains first takes the reply; if the main loop
  wins, the reply is pushed toward USBD `ENDP3` — an endpoint that is not
  active in ECM mode — and the ECM network reply never goes out.
- Unprotected head/tail access across the two consumers loses replies even
  when both paths are active.
- Net effect under stress: the host sees only ~60% of replies.

### 2.3 Absolute-mouse queue semantics are wrong

**Evidence**: Absolute-mouse reports are stateful (absolute x,y), but are
currently queued as FIFO.

**Consequences**:
- During remote-desktop dragging the host floods coordinate frames at several
  hundred Hz, while the target polls EP3 at maybe 4–8 ms (drain rate 125–250
  frames/s).
- When the queue is full the oldest frame is dropped = **missing intermediate
  positions**.
- Absolute coordinates only need the newest frame; queuing every intermediate
  state is pure waste.

### 2.4 CH9329 receive buffer does repeated `memmove`

**Evidence**: `CH9329_RxBuffer_Add()` runs a `memmove` of the remaining bytes
after every data append and after every parsed frame.

**Consequences**:
- At high mouse-frame rates the main loop wastes cycles on shuffling.
- Indirectly worsens backlog.

---

## 3. Design Goals

1. **Eliminate the cross-context race** → protect every queue operation and
   the `CH9329_RxBuffer_Add` static buffer with a short critical section.
2. **Absolute mouse never backs up** → single-slot mailbox holding only the
   newest state.
3. **Response queue served by exactly one consumer** → fix the broken drain
   logic: the main-loop `USB_SendFromQueues()` drain must be gated so it does
   not run in ECM-bridge mode (where the ECM bridge is the sole consumer), and
   remove the dead `USBD_SendFromRespQueue()` declaration.
4. **Zero `memmove` per parsed frame in the receive path** → consume-offset
   cursor with on-demand compaction.
5. **No hardware-constraint changes** → no extra RAM budget, no change to the
   USB endpoint allocation.

---

## 4. Queue Semantic Layering

| Queue | Data semantics | New model | Overflow policy | Memory delta |
|-------|----------------|-----------|-----------------|--------------|
| **KB** (keyboard) | Stateful (full key state) | FIFO + critical section | Drop oldest | none |
| **MS_ABS** (absolute mouse) | **Stateful** (absolute x,y) | **Single-slot mailbox** | Overwrite old data | 168 B → 7 B |
| **MS_REL** (relative mouse) | Event (delta movement) | Deeper FIFO + critical section | Drop oldest | +144 B (28→64 slots) |
| **MEDIA** (media keys) | Stateful (bitmap) | FIFO + critical section | Drop oldest | none |
| **RESP** (replies) | Stateful (reply frame) | FIFO + critical section + **main-loop drain** | Drop oldest | none |

**Key insight**:
- Stateful reports (absolute mouse, media keys) only need the newest frame →
  mailbox, or FIFO deep enough.
- Event reports (relative mouse) are per-frame deltas → must stay FIFO, but
  can be deepened against bursts.
- The keyboard is stateful but already deduplicated → a 512-deep FIFO is
  sufficient; no mailbox conversion (risk outweighs benefit).

---

## 5. Detailed Design

### 5.1 Absolute mouse → single-slot "mailbox" model

```c
/* Absolute-mouse mailbox: holds only the "newest not-yet-sent" frame. */
typedef struct {
    uint8_t data[ABS_MS_DATA_LEN];   /* 6 or 5 bytes */
    volatile uint8_t valid;          /* 0 = empty, 1 = data pending */
} MS_Abs_Mailbox_t;

/* Global instance */
static MS_Abs_Mailbox_t ms_abs_mbox;
```

**Push (unconditional overwrite with newest)**:
```c
void Queue_Push_MouseAbs(uint8_t *data) {
    ENTER_CRITICAL();                       /* nesting-safe, see §5.2 */
    memcpy(ms_abs_mbox.data, data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 1;
    EXIT_CRITICAL();
}
```

**Pop (take and clear)**:
```c
bool Queue_Pop_MouseAbs(uint8_t *out) {
    if (!ms_abs_mbox.valid) return false;
    ENTER_CRITICAL();
    memcpy(out, ms_abs_mbox.data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 0;
    EXIT_CRITICAL();
    return true;
}
```

> Note: on CH32V20x, `__enable_irq()` unconditionally sets MIE and is **not**
> nesting-safe. Use the MSTATUS save/restore macros defined in §5.2.

**Benefits**:
- Memory 168 B → 7 B.
- Dragging trails fully continuous (endpoint sends newest position whenever it
  is free).
- Eliminates the whole "queue-full drops intermediate frame" problem class.
- Send latency of at most one frame (imperceptible under a 1 ms poll).

### 5.2 Critical-section protection for all queues

Define nesting-safe critical-section macros once (CH32V20x RISC-V). They
save/restore `MSTATUS` so nested use (e.g. `CH9329_RxBuffer_Add` →
`Queue_Push_Response`) is correct — an inner restore cannot re-enable
interrupts that an outer disable is still holding:

```c
/* km_ring.h — include core_riscv.h (via debug.h) for __get_MSTATUS etc. */
#define ENTER_CRITICAL()  uint32_t _mstatus = __get_MSTATUS(); __disable_irq()
#define EXIT_CRITICAL()   __set_MSTATUS(_mstatus)
```

> Do NOT use `__enable_irq()` to exit a critical section: it unconditionally
> sets MIE (bit 3 of `mstatus` 0x800 via `csrs`), which would break an outer
> critical section when nested.

Add the shortest possible critical section to every `Queue_Push_*`,
`Queue_Pop_*`, and to every read-modify-write of `head`/`tail` in
`USB_SendFromQueues()` (which manipulates queue indices directly rather than
calling the `Queue_Pop_*` helpers):

```c
void Queue_Push_Keyboard(uint8_t *data) {
    ENTER_CRITICAL();
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[0].data));
    kb_queue.tail = next;
    EXIT_CRITICAL();
}
```

**Why this is enough**:
- The critical section covers only the atomic sequence "read tail → check
  full → write data → update tail".
- Duration < 100 ns at 144 MHz (plus the data copy — still microseconds at
  worst), far shorter than a BLE interrupt or a USB frame interval.
- No interrupt loss or scheduling delay; nesting-safe by MSTATUS save/restore.

### 5.3 Fix response-queue drain logic (single consumer)

**Change 1 — gate the main-loop drain so it does not fight the ECM bridge.**

The `resp_queue` drain at the end of `USB_SendFromQueues()` (`km_ring.c:140-145`)
must be gated exactly as the empty `if` at `km_ring.c:134-138` intended. When
the ECM bridge is the active responder, it pops replies itself
(`ecm_bridge.c:1149`) and wraps them in Ethernet frames; the main-loop drain
into USBD `ENDP3` must then not run. When no ECM bridge is present, the drain
sends replies to the host over USBD `ENDP3`.

```c
/* km_ring.c — inside USB_SendFromQueues(), replacing the empty if + drain */
if (!USB_ModeConfig_ActiveModeBridgeEcm() &&
    !USB_ModeConfig_ActiveModeHasHostCdcEcm()) {
    while (resp_queue.head != resp_queue.tail) {
        Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
        if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0) {
            ENTER_CRITICAL();
            resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
            EXIT_CRITICAL();
        } else {
            break;  /* endpoint busy, retry next pass */
        }
    }
}
```

**Rationale for the gate**:
- `ActiveModeBridgeEcm()` → ECM bridge is the sole legitimate consumer of
  `resp_queue`; main loop must not touch it.
- `ActiveModeHasHostCdcEcm()` → host-side ECM present; combined-mode comment at
  `km_ring.c:134-138` already states replies are handled by the ECM bridge's
  own machinery (`ecm_bridge.c:1468`, `:1600`), not by this drain.
- Otherwise (plain HID/ACM) → drain into USBD `ENDP3` as today.

**Change 2 — remove the dead declaration.**

Delete `void USBD_SendFromRespQueue(void);` from `km_ring.h:115`. No definition
or caller exists; leaving it invites a future wrong fix.

**Change 3 — critical-section discipline in both consumers.**

Because pushes may come from USB/BLE interrupt contexts while the main loop
drains, every read-modify-write of `head`/`tail` inside `USB_SendFromQueues()`
and the ECM-bridge pop must hold a short critical section (see §5.2).

### 5.4 CH9329 receive buffer optimization (consume offset + on-demand compaction)

**Current problem**: `CH9329_RxBuffer_Add()` runs a `memmove` of the remaining
bytes after **every parsed frame**. When N frames arrive in a batch, this is
O(N²) shuffling.

**Design**: introduce a "consumed offset" cursor; per-frame parsing only
advances the cursor and does not shuffle. Compaction happens only when the
tail has insufficient room (at most one `memmove` per `Add`, amortized O(1)).
The buffer stays **contiguous**, so `CH9329_DataParser` is reused unchanged
(no ring-wrap handling needed):

```c
/* usbd_compostie_km.c — RX_BUF_SIZE stays 2048 (existing value) */
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_start = 0;  /* offset of first unparsed byte */
static uint16_t rx_len = 0;    /* valid bytes from rx_start */

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
    /* May run in USB ISR context (EP5_OUT_Callback), a BLE callback, the
       ECM frame handler, or the main-loop UART2 path — protect the shared
       static buffer with a critical section. */
    ENTER_CRITICAL();
    /* Compact when the tail cannot hold the new bytes (≤1 memmove per Add) */
    if (rx_start + rx_len + len > RX_BUF_SIZE) {
        if (rx_len + len > RX_BUF_SIZE) {
            /* Still too big: drop oldest bytes to make room for newest */
            uint16_t drop = (uint16_t)(rx_len + len - RX_BUF_SIZE);
            rx_start += drop; rx_len -= drop;
        }
        memmove(rx_buf, rx_buf + rx_start, rx_len);
        rx_start = 0;
    }
    memcpy(rx_buf + rx_start + rx_len, data, len);
    rx_len += len;

    /* Parse frame by frame: only advance rx_start, no shuffling.
       Parsing calls CH9329_DispatchCommand → CH9329_SendResponse →
       Queue_Push_Response, whose own ENTER/EXIT_CRITICAL nest safely. */
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf + rx_start, rx_len);
        if (consumed < 0) { rx_start = rx_len = 0; break; }  /* bad frame: flush */
        else if (consumed == 0) break;                        /* incomplete: wait for more */
        else { rx_start += (uint16_t)consumed; rx_len -= (uint16_t)consumed; }
    }
    EXIT_CRITICAL();
}
```

**Benefits**:
- **Zero `memmove` per parsed frame** (O(N²) → amortized O(1)).
- `CH9329_DataParser` unchanged, still operates on contiguous memory.
- Clear overflow semantics (drop oldest bytes, keep newest data).
- Shared static buffer (`rx_buf`/`rx_len`) protected across USB ISR, BLE
  callback, ECM frame handler and the main-loop UART2 path.

---

## 6. Implementation Plan

### 6.1 Files to change

| File | Type of change | Size | Notes |
|------|----------------|------|-------|
| `APP/include/km_ring.h` | Refactor | Medium | Add `ENTER_CRITICAL()`/`EXIT_CRITICAL()` macros; redefine the absolute-mouse queue as a mailbox; delete the dead `USBD_SendFromRespQueue()` declaration (line 115); deepen `MS_REL_QUEUE_SIZE` 28 → 64 |
| `APP/km_ring.c` | Refactor | Large | Protect all Push functions with critical sections; protect every `head`/`tail` read-modify-write in `USB_SendFromQueues()`; fix the `resp_queue` drain gate (empty `if` at lines 134-138); implement the absolute-mouse mailbox; adapt `USB_SendFromQueues()`'s MS_ABS section |
| `APP/usbd_compostie_km.c` | Refactor | Medium | `CH9329_RxBuffer_Add` (lines 357-397): critical section + consume-offset scheme (drop per-frame `memmove`). Also confirms `RX_BUF_SIZE` stays 2048 |
| `APP/ecm_bridge.c` | Small fix | Small | The ECM-bridge `Queue_Pop_Response` at line 1149 runs in the main loop alongside `USB_SendFromQueues()` — no new critical section strictly needed (both main-loop), but its read of the queue must tolerate the main-loop drain being gated off in ECM modes (it already pops right after its own push, so it stays the sole consumer there) |

> Note: `peripheral_main.c` needs **no** change — `resp_queue` is drained
> inside `USB_SendFromQueues()`, which the main loop already calls every pass.

### 6.2 Implementation order

1. **Phase 1 (stop the bleeding)**:
   - Add critical-section macros + protect every `Queue_Push_*` and the
     `head`/`tail` read-modify-writes in `USB_SendFromQueues()`.
   - Fix the `resp_queue` drain gate (single consumer per mode) and delete the
     dead `USBD_SendFromRespQueue()` declaration.
   - Protect `CH9329_RxBuffer_Add` with a critical section.
   - Verify: stress-test reply rate should approach 100%.

2. **Phase 2 (core optimization)**:
   - Convert absolute mouse to the mailbox model.
   - Deepen the relative-mouse queue 28 → 64.
   - Verify: fast-drag trails are continuous.

3. **Phase 3 (performance)**:
   - Convert `CH9329_RxBuffer_Add` to the consume-offset scheme.
   - Verify: main-loop load drops 15–25%.

---

## 7. Validation & Benchmarks

### 7.1 Functional validation

| Scenario | Method | Pass criteria |
|----------|--------|---------------|
| Stress-test reply rate | Host sends 100 consecutive CH9329 commands | Reply rate ≥ 99% |
| Fast typing | Sustained fast key presses (> 20 keys/s) | No lost keys |
| Fast mouse movement | Fast window drag / drawing | Continuous trail, no jumps |
| BLE + UART2 concurrency | Send commands from BLE and UART2 simultaneously | No deadlock, no loss |

### 7.2 Performance benchmarks

| Metric | Method | Expected |
|--------|--------|----------|
| Main-loop average load | GPIO toggle + oscilloscope | Down 15–25% |
| Queue-operation latency | Critical-section width (logic analyzer) | < 100 ns |
| Absolute-mouse send latency | USB capture | ≤ 1 frame (1–2 ms) |

---

## 8. Expected Benefits

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Input-event loss (fast operation) | High (race + overflow) | Very low (critical section + state layering) | >90% reduction |
| Reply-loss rate (stress test) | ~40% | ~0% (main-loop drain) | Eliminated |
| Absolute-mouse trail integrity | Gaps | Fully continuous | 100% usable |
| Main-loop average load | Medium | Lower (less `memmove`, event-driven) | 15–25% reduction |
| RAM usage | ~1.2 KB | Net ~−20 B (mailbox saves 161 B, MS_REL deepening +144 B) | Roughly flat |

---

## 9. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Over-long critical section drops interrupts | Low | Medium | Only wrap the essential instruction sequence (< 100 ns) |
| Mailbox drops intermediate coordinates | Medium | Low | Expected behavior (absolute mouse only needs newest state) |
| Consume-offset buffer overflow | Low | Low | Drop oldest bytes; newest data prioritized |
| Response queue drained too late | Low | Medium | Checked every main-loop pass; reply frames are small (< 64 B) and drain fast |

---

## 10. Comparison with the Original Plan

| Item | Original plan | New design |
|------|---------------|------------|
| DMA zero-copy | ❌ Misread USBFS DMA mode | ✅ Recognized as an offset into internal RAM, not true zero-copy |
| SoA layout | ❌ Assumed cache gains (no cache exists) | ✅ Dropped; replaced with semantic layering |
| Queue race | ❌ Not identified | ✅ Critical sections on all operations |
| Response path | ❌ Not addressed | ✅ Main-loop drain added |
| Absolute mouse | ❌ Kept FIFO | ✅ Single-slot mailbox |
| Expected gain | 40% perf (unverified) | Fixes event loss (root causes verified) |

---

## 11. Open Questions

- [x] **Critical-section API on CH32V20x RISC-V** — resolved during design:
      `__enable_irq()` does an unconditional `csrs mstatus, MIE` and is *not*
      nesting-safe, so the save/restore pair `__get_MSTATUS()` /
      `__set_MSTATUS()` is the correct primitive (see §5.2). The obsolete
      `USBD_SendFromRespQueue()` question is moot — that symbol is a **dead
      declaration** in `km_ring.h` (no definition, no callers) and is simply
      deleted.
- [ ] **ECM bridge pop timing** — §5.3 relies on the observation that the
      `Queue_Pop_Response` at `ecm_bridge.c:1149` is always preceded, in the
      same main-loop pass, by a `Queue_Push_Response` (peripheral.c), so the
      gated drain in `USB_SendFromQueues` cannot starve it. Needs a code
      walk-through confirmation at implementation time.
- [ ] Add queue-depth diagnostics for debugging (e.g., print each queue's
      length via `USB_DEBUG`)? — optional, only if stress tests still fail.

---

## 12. Implementation Checklist

- [x] Phase 1: cross-context queue integrity (root causes 2.1 + 2.2)
  - [x] Add `ENTER_CRITICAL()` / `EXIT_CRITICAL()` macros to `km_ring.h`
        (MSTATUS save/restore per §5.2)
  - [x] Wrap all `Queue_Push_*` / `Queue_Pop_*` tail/head updates in
        `km_ring.c`
  - [x] Fix the `resp_queue` empty-if bug at `km_ring.c:134-138` and gate
        the drain in `USB_SendFromQueues` per §5.3 (skip when ECM bridge /
        host CDC-ECM active)
  - [x] Wrap the `Queue_Pop_Response` in `ecm_bridge.c` with the same
        critical section (done inside `Queue_Pop_Response` itself)
  - [x] Delete the dead `USBD_SendFromRespQueue()` declaration in `km_ring.h`
  - [x] Wrap `CH9329_RxBuffer_Add` framing-state access in a critical
        section (USB ISR vs. main-loop callers)
  - [ ] Functional test: stress-test reply rate (target > 95% from ~60%)
- [x] Phase 2: queue-model optimization
  - [x] Redefine absolute-mouse queue as a mailbox (`MS_Abs_Mailbox_t`) in
        `km_ring.h` (newest-wins, per §4.2)
  - [x] Implement mailbox push/pop in `km_ring.c` and route
        `Mouse_HandleAbsoluteData` through it
  - [x] Deepen relative-mouse queue 28 → 64
  - [ ] Functional test: drag trails, fast typing
- [x] Phase 3: receive-buffer optimization
  - [x] Convert `CH9329_RxBuffer_Add` to consume-offset scheme (removes the
        per-byte `memmove`, per §5.4)
  - [ ] Performance test: main-loop load
- [ ] Final validation
  - [ ] Full-scenario functional test
  - [ ] Performance benchmark
  - [ ] Code review
  - [ ] Submit PR

---

## 13. Status Tracking

| Stage | Status | Date |
|-------|--------|------|
| Design | ✅ Complete | 2026-09-08 |
| Phase 1 implementation | ✅ Complete | 2026-09-09 |
| Phase 2 implementation | ✅ Complete | 2026-09-09 |
| Phase 3 implementation | ✅ Complete | 2026-09-09 |
| Validation | ⏳ Pending (needs hardware) | — |

---

**Document maintenance**: this document is updated as implementation
progresses. Significant design changes require re-approval.
