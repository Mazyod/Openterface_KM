# USB Firmware Industrial-Grade Optimization Plan

**Document Version:** 1.0  
**Date:** 2026-09-24  
**Status:** Draft  
**Target Hardware:** CH32V20x MCU (WCH CH32V208x)  
**USB Controllers:** USBFS (USB 2.0 OTG) + USBD (USB 1.1 FS)

---

## Executive Summary

This document outlines a comprehensive optimization plan to achieve industrial-grade stability for the USB composite device firmware. The current implementation exhibits high NAK rates (~50%), unpredictable response latency, and insufficient fault recovery mechanisms. Through systematic analysis, we have identified fundamental architectural issues and propose a multi-phase optimization approach addressing reliability, real-time performance, fault tolerance, and maintainability.

**Key Issues Identified:**
- Endpoint state management using simple boolean flags (race condition prone)
- Main loop scheduling lacks priority guarantees (unpredictable USB response)
- Error recovery relies on hardcoded timeouts without graded handling
- Missing runtime monitoring and diagnostic capabilities

**Expected Outcomes:**
- NAK rate reduction: 50% → <10%
- Data throughput improvement: 3-5x
- Keyboard/mouse response latency reduction: >50%
- Mean Time Between Failures (MTBF): >8760 hours (1 year continuous operation)

---

## 1. Problem Analysis

### 1.1 Current Architecture Overview

The firmware implements a dual USB controller architecture:

| Controller | Address | Purpose | Endpoints |
|------------|---------|---------|-----------|
| **USBFS** | 0x50000000 | HID endpoints (keyboard, mouse, media) | EP1-EP4 (IN) |
| **USBD** | 0x40005C00 | CDC ACM/ECM (serial/network) | EP3 (IN), EP5 (OUT) |

**Data Flow Path:**
```
Host → CH9329 Command → USBD EP5 OUT → Ring Buffer → Main Loop Parser
                                                          ↓
                                                    Keyboard/Mouse Queues
                                                          ↓
USBFS EP1/2/3/4 ← USB_SendFromQueues() ← Queue Drain
                                                          ↓
Response → USBD EP3 IN ← Queue_Push_Response()
```

### 1.2 Root Cause Analysis

#### Issue 1: High NAK Rate - Endpoint State Management

**Location:** `usb_endp.c` lines 276-282

**Current Implementation:**
```c
if (USBD_Endp3_Busy)
{
    return USB_ERROR;
}
USB_SIL_Write( EP3_IN, pbuf, len );
USBD_Endp3_Busy = 1;
SetEPTxStatus( ENDP3, EP_TX_VALID );
```

**Problem:**
- `USBD_Endp3_Busy` is cleared in `EP3_IN_Callback` (line 145)
- Callback only triggers after transfer completion
- If host polling interval is short, busy flag remains set for extended periods
- Subsequent send attempts are rejected, host receives NAK

**Impact:**
- 50% of IN tokens receive NAK responses
- Reduced effective bandwidth
- Increased latency for time-sensitive HID reports

#### Issue 2: Main Loop Response Delay

**Location:** `km_ring.c` lines 105-178

**Current Implementation:**
```c
void USB_SendFromQueues(void) {
    // Keyboard queue
    while (kb_queue.head != kb_queue.tail && USBFS_Endp_Busy[DEF_UEP1] == 0) {
        // ...
    }
    // Response queue
    while (resp_queue.head != resp_queue.tail) {
        if (USBD_Endp3_Busy) {
            break;  // endpoint busy, retry next pass
        }
        // ...
    }
}
```

**Problem:**
- `USB_SendFromQueues()` call frequency depends on other task processing time
- Blocking operations in main loop (Flash writes, BLE processing) cause queue drain delays
- Response queue and HID queue share main loop time, potentially interfering with each other

**Impact:**
- Unpredictable USB response timing
- Jitter in HID report delivery
- Potential queue overflow under high load

#### Issue 3: Endpoint Re-arming Delay

**Location:** `usb_prop.c` lines 121-126

**Current Implementation:**
```c
if (USB_ModeConfig_ActiveModeHasHostCdcAcm())
{
    _ClearDTOG_TX(ENDP3);
    SetEPTxStatus(ENDP3, EP_TX_NAK);
}
```

**Problem:**
- EP3 initial state is `EP_TX_NAK`
- Endpoint only becomes `EP_TX_VALID` after host sends `CDC_SET_LINE_CODING`
- If host doesn't configure correctly, endpoint remains in NAK state indefinitely

**Impact:**
- Silent failure mode - no data transmission without clear error indication
- Difficult to diagnose in field deployments

#### Issue 4: Missing Double Buffering

**Location:** `ch32v20x_usbfs_device.c`

**Current Implementation:**
- HID endpoints use single buffer mode
- Each transfer requires host acknowledgment before next packet can be sent
- Increases transfer latency and reduces throughput

**Impact:**
- Maximum throughput limited by USB frame timing (1ms for Full Speed)
- Cannot pipeline packet preparation and transmission

---

## 2. Industrial-Grade Optimization Plan

### 2.1 Design Principles

The optimization follows five core principles for industrial-grade embedded systems:

1. **Reliability:** System must operate continuously without unrecoverable failures
2. **Fault Tolerance:** Graceful degradation and automatic recovery from transient faults
3. **Real-Time Performance:** Predictable response times with bounded latency
4. **Maintainability:** Clear code structure, comprehensive diagnostics, configurable parameters
5. **Testability:** Support for automated testing, fault injection, and field diagnostics

### 2.2 Optimization Direction 1: State Machine for Endpoint Management

**Objective:** Replace boolean flags with explicit state machines to eliminate race conditions and provide clear recovery paths.

**Implementation:**

```c
// In usb_endp.h
typedef enum {
    USB_EP_STATE_IDLE,      // Idle, ready to accept new transfer
    USB_EP_STATE_BUSY,      // Transfer in progress
    USB_EP_STATE_TIMEOUT,   // Timeout detected, pending recovery
    USB_EP_STATE_ERROR      // Error state, requires intervention
} USB_EP_State_t;

typedef struct {
    USB_EP_State_t state;
    uint32_t busy_start_tick;  // Timestamp when entering BUSY state
    uint8_t retry_count;       // Retry counter
    uint16_t error_count;      // Error counter for statistics
} USB_EP_Control_t;

// Global endpoint control block array
USB_EP_Control_t USB_EP_Control[DEF_UEP_NUM];

// State transition function (called from ISR and main loop)
void USB_EP_StateMachine(uint8_t endp) {
    USB_EP_Control_t *ep = &USB_EP_Control[endp];
    uint32_t now = systick_ms;
    
    switch (ep->state) {
        case USB_EP_STATE_IDLE:
            // Remain idle, waiting for upper layer request
            break;
            
        case USB_EP_STATE_BUSY:
            // Check for timeout
            if (now - ep->busy_start_tick > USB_EP_BUSY_TIMEOUT_MS) {
                ep->state = USB_EP_STATE_TIMEOUT;
                ep->retry_count++;
                // Trigger recovery process
                USB_EP_Recover(endp);
            }
            break;
            
        case USB_EP_STATE_TIMEOUT:
            // Attempt recovery
            if (USB_EP_TryRecover(endp)) {
                ep->state = USB_EP_STATE_IDLE;
                ep->retry_count = 0;
            } else if (ep->retry_count >= USB_EP_MAX_RETRIES) {
                ep->state = USB_EP_STATE_ERROR;
                // Report error to diagnostic system
                USB_Diag_ReportError(DIAG_UDP_STUCK, endp);
            }
            break;
            
        case USB_EP_STATE_ERROR:
            // Remain in error state, waiting for external reset
            break;
    }
}
```

**Benefits:**
- Clearly distinguishes normal busy from abnormal lockup
- Provides configurable timeout and retry strategies
- Generates statistics for diagnostics
- Offers explicit entry points for fault recovery

### 2.3 Optimization Direction 2: Dual-Layer Scheduling Architecture

**Objective:** Ensure USB transaction processing has highest priority and deterministic timing.

#### Option A: Lightweight Dual Polling (Minimal Change)

```c
// In peripheral_main.c
while(1) {
    // Layer 1: Highest priority - USB transaction processing
    USB_EP_StateMachineAll();    // Update all endpoint state machines
    USB_ProcessTxQueues();       // Process transmit queues (formerly USB_SendFromQueues)
    USB_ProcessRxQueues();       // Process receive queues (new)
    
    // Layer 2: Normal priority tasks (ordered by priority)
    if (needs_rgb_update())   RGB_Update();
    if (needs_ble_process())  TMOS_SystemProcess();
    // ... other tasks called on demand
    
    // Never-blocking idle processing
    __WFI(); // Wait for interrupt wakeup
}
```

#### Option B: Event-Driven Scheduling (Moderate Change)

```c
// Define event bitmask
typedef enum {
    EVT_USB_TX_READY   = 0x0001,
    EVT_USB_RX_READY   = 0x0002,
    EVT_BLE_EVENT      = 0x0004,
    EVT_RGB_UPDATE     = 0x0008,
    // ... other events
} SystemEvent_t;

volatile SystemEvent_t system_events = 0;

// Set event bits in various ISRs
void USBFS_IRQHandler(void) {
    // ... original handling ...
    if (tx_complete) system_events |= EVT_USB_TX_READY;
    if (rx_ready)    system_events |= EVT_USB_RX_READY;
}

// Main loop becomes event-driven
while(1) {
    SystemEvent_t evt = system_events;
    system_events = 0; // Atomic clear
    
    if (evt & EVT_USB_TX_READY)  USB_ProcessTxQueues();
    if (evt & EVT_USB_RX_READY)  USB_ProcessRxQueues();
    if (evt & EVT_BLE_EVENT)     TMOS_SystemProcess();
    // ...
    
    // If no events, enter low-power wait
    if (!evt) __WFI();
}
```

**Benefits:**
- USB transaction processing synchronized with interrupts, minimizing response latency
- Tasks decoupled through events, reducing coupling
- Easy to add new tasks without affecting existing logic
- Natural support for low-power standby

### 2.4 Optimization Direction 3: Layered Error Handling and Fault Tolerance

**Objective:** Implement three-tier error handling system with graded recovery strategies and fault isolation.

#### Tier 1: Transaction-Level Error Handling (in USB transfer functions)

```c
// Enhanced error handling in USBFS_Endp_DataUp
uint8_t USBFS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mode) {
    // 1. Parameter validation (compile-time configurable switch)
    #if USB_PARAM_CHECK_ENABLED
    if (!pbuf || len > USB_EP_MAX_PACKET_SIZE(endp)) {
        USB_Diag_ReportError(DIAG_USB_INVALID_PARAM, endp);
        return USB_ERROR;
    }
    #endif
    
    // 2. State check
    if (USB_EP_Control[endp].state != USB_EP_STATE_IDLE) {
        USB_EP_Control[endp].error_count++;
        return USB_ERROR_BUSY;
    }
    
    // 3. Attempt transmission
    USB_EP_Control[endp].state = USB_EP_STATE_BUSY;
    USB_EP_Control[endp].busy_start_tick = systick_ms;
    
    if (USB_SIL_Write(...) == SUCCESS) {
        SetEPTxStatus(endp, EP_TX_VALID);
        return USB_SUCCESS;
    } else {
        // Transmission failed, immediately recover state
        USB_EP_Control[endp].state = USB_EP_STATE_IDLE;
        USB_EP_Control[endp].error_count++;
        return USB_ERROR_HW;
    }
}
```

#### Tier 2: Endpoint-Level Recovery (in state machine)

```c
void USB_EP_Recover(uint8_t endp) {
    // 1. Hardware reset endpoint
    USB_EP_HW_Reset(endp);
    
    // 2. Clear buffers
    USB_EP_FlushBuffer(endp);
    
    // 3. Reset toggle bits (prevent desynchronization)
    _ClearDTOG_TX(endp);
    _ClearDTOG_RX(endp);
    
    // 4. Report recovery event
    USB_Diag_ReportEvent(DIAG_EP_RECOVERED, endp);
}
```

#### Tier 3: System-Level Fault Tolerance (watchdog + safe state)

```c
// In independent watchdog feed function
void IWDG_Feed_Safe(void) {
    // Only feed watchdog when system is in healthy state
    if (USB_Diag_GetHealthStatus() == HEALTH_OK ||
        (USB_Diag_GetHealthStatus() == HEALTH_DEGRADED && 
         USB_Diag_GetErrorCount() < CRITICAL_ERROR_THRESHOLD)) {
        IWDG->CNT = IWDG_RELOAD_VALUE; // Feed watchdog
    } else {
        // System unhealthy, don't feed - let it reset
        // Can trigger safe mode or fault indicator
        USB_SafeMode_Activate();
    }
}
```

**Benefits:**
- Errors detected and handled at lowest level, preventing propagation
- Provides clear recovery paths and fault isolation
- Generates statistics for field diagnostics
- Prevents permanent system lockup through watchdog mechanism

### 2.5 Optimization Direction 4: Runtime Monitoring and Diagnostic System

**Objective:** Implement lightweight runtime monitoring framework for system visibility.

```c
// In usb_diag.h
typedef enum {
    HEALTH_OK = 0,
    HEALTH_DEGRADED = 1,
    HEALTH_CRITICAL = 2,
    HEALTH_FAILED = 3
} SystemHealth_t;

// Diagnostic data structure (placed in specific RAM segment for debugger access)
__attribute__((section(".diag_data"))) 
typedef struct {
    uint32_t uptime_ms;
    SystemHealth_t health;
    
    // USB statistics
    struct {
        uint32_t tx_packets;
        uint32_t rx_packets;
        uint32_t nak_count;
        uint32_t timeout_recoveries;
        uint32_t error_count[DEF_UEP_NUM];
    } usb;
    
    // Task execution time statistics (for real-time analysis)
    struct {
        uint32_t max_latency;
        uint32_t min_latency;
        uint32_t avg_latency;
        uint32_t last_timestamp;
    } task_latency[MAX_TASKS];
    
    // Error history log (ring buffer)
    struct {
        uint16_t code;
        uint16_t param;
        uint32_t timestamp;
    } error_log[DIAG_ERROR_LOG_SIZE];
    
    uint16_t log_head;
    uint16_t log_tail;
} Diag_Data_t;

// Global diagnostic data
extern Diag_Data_t g_diag;

// Simple reporting interface (extensible to UART/USB diagnostic endpoint)
void USB_Diag_ReportError(Diag_Error_Code_t code, uint16_t param) {
    // Atomic error recording
    g_diag.error_log[g_diag.log_head].code = code;
    g_diag.error_log[g_diag.log_head].param = param;
    g_diag.error_log[g_diag.log_head].timestamp = systick_ms;
    g_diag.log_head = (g_diag.log_head + 1) % DIAG_ERROR_LOG_SIZE;
    
    // Update health status
    if (code >= DIAG_CRITICAL_START) {
        g_diag.health = HEALTH_CRITICAL;
    } else if (g_diag.health == HEALTH_OK) {
        g_diag.health = HEALTH_DEGRADED;
    }
}
```

**Benefits:**
- Provides real-time visibility into system health
- Helps field engineers quickly locate issues
- Supports statistics-based preventive maintenance
- Does not affect real-time performance (uses atomic operations and ring buffers)

### 2.6 Optimization Direction 5: Resource Management and Code Robustness

**Objective:** Implement defensive programming and resource monitoring for embedded systems.

#### Stack Usage Monitoring

```c
// Check at task switches or interrupt exits
#if STACK_CHECK_ENABLED
void CheckStackUsage(void) {
    uint32_t *sp = (uint32_t)__get_MSP();
    uint32_t usage = (uint32_t)&_estack - (uint32_t)sp;
    if (usage > g_diag.max_stack_usage) {
        g_diag.max_stack_usage = usage;
    }
    // If approaching limit, trigger warning
    if (usage > STACK_WARNING_THRESHOLD * STACK_SIZE) {
        USB_Diag_ReportError(DIAG_STACK_OVERFLOW_WARN, usage);
    }
}
#endif
```

#### Interrupt Latency Measurement

```c
// Measure latency at interrupt entry and exit
void USBFS_IRQHandler(void) {
    uint32_t entry_time = systick_ms;
    
    // ... original interrupt handling ...
    
    uint32_t latency = systick_ms - entry_time;
    if (latency > g_diag.max_irq_latency) {
        g_diag.max_irq_latency = latency;
    }
    if (latency > IRQ_LATENCY_WARNING_MS) {
        USB_Diag_ReportError(DIAG_IRQ_LATENCY_HIGH, latency);
    }
}
```

#### Memory Usage Checking (if heap is used)

```c
// Memory pool usage statistics
void *USB_Malloc(size_t size) {
    void *ptr = internal_malloc(size);
    if (ptr) {
        g_diag.heap_used += size;
        if (g_diag.heap_used > g_diag.heap_peak) {
            g_diag.heap_peak = g_diag.heap_used;
        }
    }
    return ptr;
}
```

---

## 3. Implementation Roadmap

### Phase 1: Basic Robustness (1-2 weeks)

**Objectives:**
- Eliminate race conditions in endpoint state management
- Add timeout detection and recovery for USB transactions
- Implement basic error reporting mechanism
- Add watchdog feed health status check
- Optimize main loop: elevate USB processing to highest priority

**Deliverables:**
- Endpoint state machine implementation
- USB transaction timeout detection and recovery
- Basic error reporting interface
- Watchdog health-aware feeding
- Optimized main loop structure

**Success Criteria:**
- NAK rate reduced to <30%
- No unrecoverable endpoint lockups in 24-hour stress test
- All error conditions logged with timestamps

### Phase 2: Fault Tolerance and Monitoring (2-3 weeks)

**Objectives:**
- Implement dual-layer scheduling architecture (event-driven or dual polling)
- Build runtime diagnostic system (health status + error log)
- Add parameter checking and assertions (compile-time switches)
- Implement endpoint-level hardware recovery process
- Add stack usage and interrupt latency monitoring

**Deliverables:**
- Event-driven or dual-polling scheduler
- Runtime diagnostic framework with health monitoring
- Parameter validation and assertion system
- Endpoint hardware recovery procedures
- Stack and interrupt latency monitoring

**Success Criteria:**
- NAK rate reduced to <10%
- USB IN endpoint response latency jitter <1ms
- 99.9% of USB packets sent/received within expected time window
- All critical parameters configurable without code changes

### Phase 3: Optimization and Validation (1-2 weeks)

**Objectives:**
- Implement double buffering optimization (for high-throughput endpoints)
- Add unit test framework support (simulate USB transactions)
- Conduct stress testing and fault injection validation
- Adjust timeout and retry parameters based on diagnostic data
- Write factory test and field diagnostic guide

**Deliverables:**
- Double buffering implementation for selected endpoints
- Unit test framework with USB transaction simulation
- Stress test and fault injection test suite
- Tuned timeout and retry parameters
- Factory test and field diagnostic documentation

**Success Criteria:**
- NAK rate <5% under normal load
- Data throughput improved 3-5x
- Keyboard/mouse response latency reduced >50%
- Mean Time Between Failures (MTBF) >8760 hours (1 year)

---

## 4. Verification Criteria

### 4.1 Reliability Standards

- **Continuous Operation:** 7x24 hours without unrecoverable errors
- **Error Recovery Time:** <10ms for recoverable faults
- **Watchdog Reset:** Only triggered for truly unrecoverable conditions
- **Memory Leaks:** Zero memory leaks detected in 72-hour stress test

### 4.2 Real-Time Performance Standards

- **USB IN Endpoint Response Latency Jitter:** <1ms
- **Packet Delivery:** 99.9% of USB packets within expected time window
- **WCET Measurement:** Worst-case execution time measurable and predictable for highest priority tasks
- **Interrupt Latency:** Maximum interrupt latency <100μs

### 4.3 Maintainability Standards

- **Documentation:** All public interfaces have complete comments and usage examples
- **Error States:** Unique error codes with clear recovery paths
- **Configurability:** Critical parameters (timeouts, retry counts) modifiable through configuration
- **Static Analysis:** Zero warnings from static analysis tools (Cppcheck, SonarQube)

### 4.4 Testability Standards

- **Automated Testing:** Test suite with USB bus protocol analyzer
- **Fault Injection:** Support for simulating NAK, timeout, error packets
- **Field Diagnostics:** USB protocol analyzer interface for field debugging
- **Unit Test Coverage:** >80% coverage for critical paths

---

## 5. Risk Assessment and Mitigation

### 5.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| State machine introduces new race conditions | Medium | High | Extensive code review, formal verification of state transitions |
| Event-driven scheduling increases complexity | High | Medium | Incremental implementation, comprehensive testing at each step |
| Diagnostic system adds overhead | Low | Medium | Compile-time switches, measure overhead, optimize hot paths |
| Double buffering requires significant refactoring | Medium | High | Prototype first, validate performance gain before full implementation |

### 5.2 Schedule Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Phase 1 takes longer than expected | Medium | Medium | Prioritize critical fixes, defer non-essential features |
| Testing reveals unexpected issues | High | Medium | Build test infrastructure early, continuous integration |
| Hardware limitations discovered late | Low | High | Early prototyping, validate assumptions in Phase 1 |

---

## 6. Conclusion

This optimization plan addresses fundamental architectural issues in the USB firmware through a systematic, phased approach. By replacing boolean flags with state machines, implementing priority-based scheduling, adding layered error handling, and building comprehensive diagnostics, the firmware will achieve industrial-grade stability and performance.

The plan balances immediate improvements (Phase 1) with long-term architectural enhancements (Phase 2-3), allowing incremental validation and risk mitigation. Each phase has clear success criteria and deliverables, enabling objective assessment of progress.

**Key Success Factors:**
- Rigorous testing at each phase
- Continuous monitoring of diagnostic data
- Willingness to adjust parameters based on field data
- Commitment to code quality and documentation

**Next Steps:**
1. Review and approve this optimization plan
2. Set up development environment for Phase 1 implementation
3. Establish baseline metrics (NAK rate, latency, throughput)
4. Begin Phase 1 implementation with weekly progress reviews

---

## Appendix A: Related Documents

- [USB Endpoint Configuration](controllable-usb-endpoint-modes.md)
- [HID Queue Reliability Fix](HID_QUEUE_RELIABILITY_FIX.md)
- [Queue Performance Optimization Plan](queue-performance-optimization-plan.md)
- [CH9329 Communication Protocol](ch9329-communication-protocol.md)

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **NAK** | Negative Acknowledgment - USB response indicating device cannot accept/send data |
| **WCET** | Worst-Case Execution Time - maximum time a task can take |
| **MTBF** | Mean Time Between Failures - average time between system failures |
| **ISR** | Interrupt Service Routine - function called in response to hardware interrupt |
| **PMA** | Packet Memory Area - dedicated USB controller memory |
| **DTOG** | Data Toggle - bit used for synchronization in USB transfers |

## Appendix C: Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-09-24 | TechxArtisan Studio | Initial draft |
