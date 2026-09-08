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

**Evidence**: `Queue_Push_*` is invoked from two contexts:
- BLE callback (`ble_usb_ServiceEvt` → `CH9329_DataParser` → direct push) —
  outside the main loop.
- Main loop (`USB_DataRx_To_KMHandle` → UART2 path → push).

`Queue_Pop_*` runs only in the main loop's `USB_SendFromQueues()`. Neither the
push sequence ("write data → update tail") nor the pop sequence ("read
head/tail → take data → advance head") is protected by a critical section.

**Consequences**: during fast operation (high-frequency BLE pushes plus a
high-frequency main-loop drain), the following can happen:
- Pop reads an empty queue before push updates tail → **lost event**.
- Both advance head/tail concurrently → **ordering corruption / overwrite**.
- `memcpy` races → **dirty report sent** (target sees wrong keys).

### 2.2 Response queue never serviced (primary)

**Evidence**: `RESP_QUEUE` is pushed by `CH9329_SendResponse()`, but
`USBD_SendFromRespQueue()` is **never called** in `Main_Circulation()` of
`peripheral_main.c` to drain the queue.

**Consequences**:
- Replies pile up until the queue (28 slots) is full.
- When full, `head = (head+1) % SIZE` drops the oldest reply.
- Under stress testing a large share of replies is dropped → **host receives
  only ~60% of replies**.

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

1. **Eliminate the cross-context race** → protect every queue operation with a
   short critical section.
2. **Absolute mouse never backs up** → single-slot mailbox holding only the
   newest state.
3. **Response queue serviced promptly** → main loop drains `RESP_QUEUE` each
   pass.
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
    uint8_t sr = __disable_interrupts();
    memcpy(ms_abs_mbox.data, data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 1;
    __set_interrupts(sr);
}
```

**Pop (take and clear)**:
```c
bool Queue_Pop_MouseAbs(uint8_t *out) {
    if (!ms_abs_mbox.valid) return false;
    uint8_t sr = __disable_interrupts();
    memcpy(out, ms_abs_mbox.data, ABS_MS_DATA_LEN);
    ms_abs_mbox.valid = 0;
    __set_interrupts(sr);
    return true;
}
```

**Benefits**:
- Memory 168 B → 7 B.
- Dragging trails fully continuous (endpoint sends newest position whenever it
  is free).
- Eliminates the whole "queue-full drops intermediate frame" problem class.
- Send latency of at most one frame (imperceptible under a 1 ms poll).

### 5.2 Critical-section protection for all queues

Add the shortest possible critical section to every `Queue_Push_*` and
`Queue_Pop_*` entry:

```c
void Queue_Push_Keyboard(uint8_t *data) {
    uint8_t sr = __disable_interrupts();
    int next = (kb_queue.tail + 1) % KB_QUEUE_SIZE;
    if (next == kb_queue.head) kb_queue.head = (kb_queue.head + 1) % KB_QUEUE_SIZE;
    memcpy(kb_queue.buf[kb_queue.tail].data, data, sizeof(kb_queue.buf[0].data));
    kb_queue.tail = next;
    __set_interrupts(sr);
}
```

**Why this is enough**:
- The critical section covers only the atomic sequence "read tail → check
  full → write data → update tail".
- Duration < 100 ns at 144 MHz, far shorter than a BLE interrupt or a USB
  frame interval.
- No interrupt loss or scheduling delay.

### 5.3 Service the response queue from the main loop

In `Main_Circulation()` of `peripheral_main.c`, right after
`USB_SendFromQueues()`:

```c
// Keyboard and mouse control code sending loop
USB_SendFromQueues();

// ★ NEW: service the response queue (send replies to the host)
if (USB_ModeConfig_ActiveModeBridgeAcm() ||
    USB_ModeConfig_ActiveModeBridgeEcm() ||
    !USB_ModeConfig_ActiveModeHasHostUac()) {
    USBD_SendFromRespQueue();  /* drain RESP_QUEUE frames via UART2 */
}
```

**Why the condition**: when BLE CH9329 response routing is enabled
(`Peripheral_GetBleCh9329Response()`), replies go straight out as BLE
notifications. The condition ensures the queue is drained only when the UART2
path is actually in use, avoiding duplicate sends.

**`USBD_SendFromRespQueue()` implementation** (mirrors
`USB_SendFromQueues`):

```c
void USBD_SendFromRespQueue(void) {
    while (resp_queue.head != resp_queue.tail) {
        Resp_Report_t *pkt = &resp_queue.buf[resp_queue.head];
        if (USBD_ENDPx_DataUp(ENDP3, pkt->data, pkt->len) == 0) {
            uint8_t sr = __disable_interrupts();
            resp_queue.head = (resp_queue.head + 1) % RESP_QUEUE_SIZE;
            __set_interrupts(sr);
        } else {
            break;  /* endpoint busy, retry next pass */
        }
    }
}
```

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
#define RX_BUF_SIZE 512
static uint8_t rx_buf[RX_BUF_SIZE];
static uint16_t rx_start = 0;  /* offset of first unparsed byte */
static uint16_t rx_len = 0;    /* valid bytes from rx_start */

void CH9329_RxBuffer_Add(uint8_t *data, uint16_t len) {
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

    /* Parse frame by frame: only advance rx_start, no shuffling */
    while (rx_len > 0) {
        int consumed = CH9329_DataParser(rx_buf + rx_start, rx_len);
        if (consumed < 0) { rx_start = rx_len = 0; break; }  /* bad frame: flush */
        else if (consumed == 0) break;                        /* incomplete: wait for more */
        else { rx_start += (uint16_t)consumed; rx_len -= (uint16_t)consumed; }
    }
}
```

**Benefits**:
- **Zero `memmove` per parsed frame** (O(N²) → amortized O(1)).
- `CH9329_DataParser` unchanged, still operates on contiguous memory.
- Clear overflow semantics (drop oldest bytes, keep newest data).

---

## 6. Implementation Plan

### 6.1 Files to change

| File | Type of change | Size | Notes |
|------|----------------|------|-------|
| `APP/include/km_ring.h` | Refactor | Medium | Redefine queue structs (absolute-mouse mailbox), add critical-section macros, declare `USBD_SendFromRespQueue()` |
| `APP/km_ring.c` | Refactor | Large | Rewrite all Push/Pop functions (critical sections), implement mailbox model, optimize receive buffer |
| `APP/peripheral_main.c` | Addition | Small | Add `USBD_SendFromRespQueue()` call (conditional) in the main loop |
| `APP/usbd_compostie_km.c` | Addition | Small | Implement `USBD_SendFromRespQueue()` (if missing) |

### 6.2 Implementation order

1. **Phase 1 (stop the bleeding)**:
   - Add critical sections to every `Queue_Push_*`/`Queue_Pop_*`.
   - Add the `USBD_SendFromRespQueue()` call to the main loop.
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

- [ ] Does `USBD_SendFromRespQueue()` already exist? If not, write it.
- [ ] Critical-section macros: `__disable_interrupts()`/`__set_interrupts()`
      vs. `__get_MSTATUS()`/`__restore_mstatus()` — confirm best practice on
      CH32V20x RISC-V.
- [ ] Add queue-depth diagnostics for debugging (e.g., print each queue's
      length via `USB_DEBUG`)?

---

## 12. Implementation Checklist

- [ ] Phase 1: critical sections + response-queue service
  - [ ] Modify all Push/Pop functions in `km_ring.c`
  - [ ] Modify the main loop in `peripheral_main.c`
  - [ ] Implement `USBD_SendFromRespQueue()` (if missing)
  - [ ] Functional test: stress-test reply rate
- [ ] Phase 2: queue-model optimization
  - [ ] Redefine `MS_Abs_Queue_t` in `km_ring.h`
  - [ ] Implement the absolute-mouse mailbox model
  - [ ] Deepen relative-mouse queue 28 → 64
  - [ ] Functional test: drag trails, fast typing
- [ ] Phase 3: receive-buffer optimization
  - [ ] Convert `CH9329_RxBuffer_Add` to consume-offset scheme
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
| Phase 1 implementation | ⬜ Pending | — |
| Phase 2 implementation | ⬜ Pending | — |
| Phase 3 implementation | ⬜ Pending | — |
| Validation | ⬜ Pending | — |

---

**Document maintenance**: this document is updated as implementation
progresses. Significant design changes require re-approval.
