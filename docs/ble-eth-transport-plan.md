# BLE-Eth Transport Improvement Plan

This plan breaks the BLE transport work into file-level tasks so each step can be implemented and validated independently.

## Goal

Improve high-flow stability for interactive SSH and terminal workloads without regressing the current working banner/login path.

## Status (2026-06-03)

- Phase 1 (sender sliding window) is implemented on the host when metadata ACKs are enabled (window size configurable via `--ack-window` in `tools/test_ble_eth_ssh.py`).
  - Host ACK matching also treats `expected_seq` as cumulative progress, so a later metadata ACK can unblock earlier pending fragments if individual ACK frames were dropped/delayed.
- Phase 2 (receiver window feedback) is implemented end-to-end:
  - firmware includes `rx_window` in DATA ACK payloads when supported
  - host clamps the effective in-flight window to the advertised `rx_window`
- Phase 3 pacing/batching is the next lever for high-flow stability (reduce burst pressure and BLE write amplification).

## Fix Summary (2026-06-03)

Key changes that improved stability for large terminal output (for example `top`):

- Firmware: cap BLE-Eth notify payloads to a conservative size (currently 180 bytes) even when MTU is larger.
- Firmware: only consume queued TX bytes after notify success (avoid “lost bytes” when notify fails and must be retried).
- Firmware: DATA ACK metadata + receiver window (`rx_window`) so the host can retry and throttle instead of overflowing firmware queues.
- Firmware: defer CONNECT success until TCP is actually established (SYN+ACK), and return deterministic `0xE1` timeouts.
- Host: enable strict ACK/window mode when advertised, clamp to `rx_window`, and add pacing to reduce burst overruns.
- Host: harden tunnel lifecycle (avoid early-data race; device-initiated close completes pending waits; close/disconnect is idempotent).
- System: keep SRAM headroom so BLE advertising/connection stays stable across disconnect/reconnect cycles.

Quick validation workflow:

```bash
# Quick smoke: banner through strict ACK/window mode
python3 tools/test_ble_eth_ssh.py --target 192.168.11.1 --port 22 --test-only --ack-window 6

# Terminal A: start proxy
python3 tools/ssh_through_ble_eth.py --proxy-only --local-port 18048

# Terminal B: stress with high-volume snapshots (non-interactive)
bash tools/ble_top_poll.sh --port 18048 --interval 1 --top-n 120 --top-stats pid,command,cpu,mem
```

## Phase 0: Baseline Measurement

Measure the current ceiling before changing behavior.

Files:

- `tools/ble_top_poll.sh`
- `tools/ssh_through_ble_eth.py`
- `tools/ble_eth_tunnel.py`

Work:

- Run repeatable `top`/snapshot probes at fixed window sizes.
- Record pass/fail rate, time-to-failure, reconnect count, and ACK timeout count.
- Keep one baseline result per known-good transport mode.

Exit criteria:

- We know the current stable range and the current failure mode.

## Phase 1: Sender Sliding Window

Reduce stop-and-wait pressure by allowing a small number of fragments in flight.

Files:

- `tools/ble_eth_tunnel.py`
- `tools/ssh_through_ble_eth.py`
- `APP/ble_eth_protocol.c`
- `APP/include/ble_eth_protocol.h`

Work:

- Replace the current single-fragment ACK wait with a small window (start with 2, then 4 if stable).
- Track outstanding fragment sequence numbers on the host.
- Make ACK matching tolerant of delayed responses within the active window.
- Keep legacy compatibility mode available for older firmware.

Exit criteria:

- Bursty terminal output no longer stalls on every fragment.
- Interactive sessions still recover cleanly on disconnect.

## Phase 2: Receiver Window Feedback

Let firmware tell the host how much it can accept before the sender should pause.

Files:

- `APP/include/ble_eth_protocol.h`
- `APP/ble_eth_protocol.c`
- `tools/ble_eth_tunnel.py`

Work:

- Add a receiver-window field or equivalent metadata to DATA ACK payloads.
- Expose firmware queue pressure / reassembly pressure as a host-readable signal.
- Use that signal to throttle host bursts before the queue overflows.

Exit criteria:

- The sender can back off before BLE-side congestion causes disconnects.

## Phase 3: Pacing and Batching

Replace fixed delays with adaptive pacing and reduce small write amplification.

Files:

- `tools/ble_eth_tunnel.py`
- `tools/ssh_through_ble_eth.py`
- `APP/ble_eth_protocol.c`

Work:

- Measure RTT and adapt the inter-frame gap dynamically.
- Increase gap temporarily after timeout/retry bursts.
- Coalesce adjacent small writes where safe.
- Keep frame fragmentation aligned to BLE payload limits.

Exit criteria:

- High-flow terminal redraws become smoother and less burst-sensitive.

## Phase 4: Validation Harness

Add a repeatable test path for the transport changes.

Files:

- `tools/ble_top_poll.sh`
- `tools/test_ble_eth_ssh.py`
- `tools/ssh_through_ble_eth.py`
- `docs/ble-eth-ssh-usage.md`

Work:

- Add fixed-size window tests for `top` and repeated command bursts.
- Record stable sizes and failure thresholds.
- Add a short stress loop for reconnect churn and banner/login smoke.
- Document the safe window recommendations and the lower-volume `top` alternatives.

Exit criteria:

- We can compare transport changes against the same workload matrix each time.

## Suggested Execution Order

1. Baseline measurement (with current window + `rx_window` feedback enabled).
2. Adaptive pacing and batching.
3. Validation harness refresh and doc updates.

## Practical Notes

- Keep legacy mode working until strict mode is proven stable.
- Favor correctness over throughput in the first implementation of each phase.
- Re-test banner/login after every phase before moving on.