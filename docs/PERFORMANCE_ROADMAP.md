# VEIL Performance Improvement Roadmap

## Overview

This document outlines the performance improvement roadmap for VEIL VPN based on comprehensive code review of the recent changes (PRs #88, #89, #90, #91) and analysis of remaining bottlenecks.

## Current Status

### Recently Fixed Issues (Completed)
- **Issue #78** - Out-of-order packet rejection causing ~20% packet loss (Fixed: unmark in replay window)
- **Issue #79** - High pending count (Fixed: reduced ACK interval to 20ms)
- **Issue #80** - SACK bitmap always 0x00000000 (Fixed: proper bit setting before shift)
- **Issue #85** - Single-threaded architecture limiting throughput (Fixed: pipeline parallelism)

### Open Performance Issues
- **Issue #83** - Predictable session rotation interval (DPI detection risk)
- **Issue #86** - No 0-RTT session resumption (adds latency)
- **Issue #87** - PSK authentication doesn't scale (no per-client keys)

---

## Phase 1: Immediate Performance Improvements (P0 - Critical)

### 1.1 Remove Verbose Debug Logging in Hot Path
**Priority:** P0
**Impact:** High throughput improvement
**Effort:** Low
**Dependencies:** None

**Problem:** The recent debugging fixes added LOG_WARN statements in critical hot paths:
- `transport_session.cpp:140-192` - LOG_WARN on every decrypt attempt
- `transport_session.cpp:305-322` - LOG_WARN on every ACK process
- `retransmit_buffer.cpp:23-24` - LOG_WARN on every insert
- `retransmit_buffer.cpp:111-130` - LOG_WARN on every cumulative ACK
- `tunnel.cpp:424-430` - LOG_WARN on every TUN write
- `server/main.cpp` - Multiple LOG_WARN in packet processing

**Solution:**
1. Change LOG_WARN to LOG_DEBUG for per-packet logging
2. Add conditional logging based on verbose mode
3. Consider structured metrics collection instead of logging

---

### 1.2 Optimize Sequence Obfuscation (3 Hash Operations per Packet)
**Priority:** P0
**Impact:** High CPU reduction
**Effort:** Medium
**Dependencies:** None

**Problem:** Each packet requires 3 `crypto_generichash` calls for sequence obfuscation (Feistel network):
- `crypto_engine.cpp:240-282` - obfuscate_sequence
- `crypto_engine.cpp:299-343` - deobfuscate_sequence

At high throughput, this becomes a bottleneck. Each 3-round Feistel network adds significant overhead.

**Solution Options:**
1. Use AES-NI based cipher if available (hardware acceleration)
2. Cache obfuscation results for recent sequences (LRU cache)
3. Use simpler XOR-based obfuscation with session key (less secure but faster)
4. Make obfuscation optional/configurable

---

### 1.3 Reduce Memory Allocations in Hot Path
**Priority:** P0
**Impact:** Medium throughput improvement
**Effort:** Medium
**Dependencies:** None

**Problem:** Multiple vector allocations per packet:
- `build_encrypted_packet` creates new vector for each packet
- `fragment_data` creates vector of frames
- ACK frame creation allocates new vectors

**Solution:**
1. Use object pools for packet buffers
2. Pre-allocate output buffers and pass by reference
3. Use `std::array` for fixed-size data instead of `std::vector`

---

## Phase 2: Protocol Optimizations (P1 - High)

### 2.1 Implement ACK Coalescing
**Priority:** P1
**Impact:** High network efficiency
**Effort:** Medium
**Dependencies:** None

**Problem:** Current implementation sends ACK for every DATA frame received. This creates significant overhead for small packets.

**Current code (tunnel.cpp:437-446, server/main.cpp:516-528):**
- Every data frame triggers immediate ACK send
- No batching of ACKs

**Solution:**
1. Implement delayed ACK (e.g., 50ms timer or every N packets)
2. Coalesce multiple ACKs into single packet
3. Use SACK bitmap more effectively to reduce ACK frequency

---

### 2.2 Improve Retransmit Buffer Efficiency
**Priority:** P1
**Impact:** Medium CPU reduction
**Effort:** Medium
**Dependencies:** None

**Problem:** `retransmit_buffer.cpp` uses `std::map` which has O(log n) operations. For high throughput scenarios, this adds up.

**Solution:**
1. Consider `std::unordered_map` for O(1) average case
2. Use circular buffer for sequence-ordered access
3. Implement separate data structure for timeout management

---

### 2.3 Add Session Rotation Jitter
**Priority:** P1
**Impact:** Security improvement (DPI resistance)
**Effort:** Low
**Dependencies:** Issue #83

**Problem:** Session rotation happens at predictable intervals, creating a DPI fingerprint.

**Current code (session_rotator.cpp:16-21):**
```cpp
bool SessionRotator::should_rotate(...) const {
  const bool too_many_packets = sent_packets >= max_packets_;
  const bool expired = (now - last_rotation_) >= interval_;
  return too_many_packets || expired;
}
```

**Solution:**
Add random jitter (e.g., ±20% of interval) to rotation timing.

---

## Phase 3: Architecture Improvements (P2 - Medium)

### 3.1 Implement Zero-Copy Packet Processing
**Priority:** P2
**Impact:** High throughput improvement
**Effort:** High
**Dependencies:** Phase 1 complete

**Problem:** Current design copies data multiple times:
1. Read from UDP socket into buffer
2. Copy to decrypt function
3. Copy to mux frame
4. Copy to TUN write

**Solution:**
1. Use scatter-gather I/O (iovec/WSABuf)
2. Pass span/view references instead of copying
3. Implement buffer pools with reference counting

---

### 3.2 Implement Congestion Control
**Priority:** P2
**Impact:** High network efficiency
**Effort:** High
**Dependencies:** ACK coalescing

**Problem:** No congestion control mechanism. Under high load:
- Packets can be sent faster than network can handle
- No backoff on packet loss
- Retransmit buffer can grow unbounded

**Solution:**
1. Implement basic AIMD (Additive Increase Multiplicative Decrease)
2. Add congestion window tracking
3. Implement slow start and congestion avoidance

---

### 3.3 Add Hardware Acceleration Support
**Priority:** P2
**Impact:** Very high throughput improvement
**Effort:** High
**Dependencies:** None

**Problem:** All crypto operations use software implementations.

**Solution:**
1. Detect and use AES-NI for obfuscation
2. Use hardware crypto offload where available
3. Consider AVX/AVX2 optimizations for batch processing

---

## Phase 4: Advanced Features (P3 - Low)

### 4.1 Implement 0-RTT Session Resumption
**Priority:** P3
**Impact:** Latency reduction for reconnects
**Effort:** High
**Dependencies:** Issue #86

**Problem:** Every connection requires full handshake.

**Solution:**
1. Cache session tickets
2. Implement session resumption protocol
3. Add replay protection for resumed sessions

---

### 4.2 Per-Client Key Infrastructure
**Priority:** P3
**Impact:** Security and scalability
**Effort:** Very High
**Dependencies:** Issue #87

**Problem:** Single PSK for all clients doesn't scale.

**Solution:**
1. Implement per-client key derivation
2. Add key management API
3. Support certificate-based authentication

---

## Implementation Order and Dependencies

```
Phase 1 (P0 - Immediate):
  1.1 Remove Debug Logging ───┐
  1.2 Optimize Obfuscation ───┼─→ Baseline performance improvement
  1.3 Reduce Allocations ─────┘

Phase 2 (P1 - High):
  2.1 ACK Coalescing ─────────┐
  2.2 Retransmit Buffer ──────┼─→ Protocol efficiency
  2.3 Session Rotation Jitter ┘

Phase 3 (P2 - Medium):
  3.1 Zero-Copy ──────────────┐
  3.2 Congestion Control ─────┼─→ Architecture improvements
  3.3 Hardware Acceleration ──┘
        │
        ↓ (depends on Phase 2)

Phase 4 (P3 - Low):
  4.1 0-RTT Resumption ───────┐
  4.2 Per-Client Keys ────────┘→ Advanced features
```

## Performance Targets

| Metric | Current | Phase 1 | Phase 2 | Phase 3 |
|--------|---------|---------|---------|---------|
| Throughput (single-threaded) | ~500 Mbps | ~700 Mbps | ~900 Mbps | ~1 Gbps |
| Throughput (pipeline mode) | ~1.5 Gbps | ~2 Gbps | ~3 Gbps | ~5 Gbps |
| Latency (median) | ~20 µs | ~15 µs | ~10 µs | ~5 µs |
| CPU per Gbps | High | Medium | Low | Very Low |

## Testing Strategy

1. **Unit Tests:** Each optimization should have benchmark tests
2. **Integration Tests:** End-to-end throughput tests
3. **Stress Tests:** High packet rate testing
4. **Compatibility:** Ensure changes are backward compatible

## References

- [Issue #76](https://github.com/VisageDvachevsky/veil-core/issues/76) - Very slow connection (parent issue)
- [Issue #83](https://github.com/VisageDvachevsky/veil-core/issues/83) - Predictable session rotation
- [Issue #86](https://github.com/VisageDvachevsky/veil-core/issues/86) - 0-RTT resumption
- [Issue #87](https://github.com/VisageDvachevsky/veil-core/issues/87) - Per-client keys
- [RFC 6298](https://tools.ietf.org/html/rfc6298) - Computing TCP's Retransmission Timer
- [QUIC Loss Detection](https://tools.ietf.org/html/draft-ietf-quic-recovery) - Reference for modern congestion control
