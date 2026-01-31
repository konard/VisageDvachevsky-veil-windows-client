# Technical Limitations and Improvement Roadmap

This document tracks architectural limitations and potential improvements for VEIL that don't contradict its core DPI evasion mission. Created as part of Issue #76 investigation.

---

## Analysis Summary

**Issue:** Very slow connection - YouTube loading takes >50 seconds
**Root Cause:** Multiple factors identified through log analysis and code review

### Key Findings from Log Analysis

1. **~20% packet drop rate due to replay window rejections** - Packets arriving ~30-32 sequences behind `highest` are being rejected despite a 1024-bit replay window. This indicates network reordering issues combined with potential server-side retransmission of old packets.

2. **High pending packet counts** - Server shows pending_count reaching 25-31 packets regularly, indicating transmission bottleneck.

3. **All operations on single thread** - Confirmed single-threaded architecture limiting throughput.

---

## DPI Evasion Gaps (from original analysis)

### Issue #1: WebSocket Wrapper Without HTTP Handshake

**Status:** CONFIRMED in code
**Severity:** High
**Component:** Protocol Wrappers (QUIC-Like mode)
**File:** `src/common/protocol_wrapper/websocket_wrapper.h`

**Description:**
QUIC-Like mode wraps packets in RFC 6455 WebSocket binary frames, but skips the HTTP Upgrade handshake. Advanced DPI can detect this anomaly.

**Current Behavior:**
```
Client -> Server: [WebSocket Binary Frame]  (no HTTP handshake)
```

**Expected Legitimate Traffic:**
```
Client -> Server: GET /path HTTP/1.1
                 Upgrade: websocket
Server -> Client: HTTP/1.1 101 Switching Protocols
[WebSocket frames begin]
```

**Impact:**
- DPI rule: "WebSocket frames without HTTP = VPN"
- Easy detection despite correct frame structure

**Proposed Solution:**
Add optional HTTP-over-UDP handshake emulation:
1. First packet: fake HTTP Upgrade request
2. Second packet: fake 101 response
3. Subsequent packets: WebSocket frames (as now)

**Implementation:**
- Add `HttpHandshakeEmulator` class
- Config flag: `enable_http_handshake_emulation`
- Overhead: 2 extra packets per connection (~1KB)

**Related:** `docs/protocol_wrappers.md:254-257`

---

### Issue #2: Missing TLS Record Layer Wrapper

**Status:** CONFIRMED - Not implemented
**Severity:** High
**Component:** Protocol Wrappers
**File:** New implementation needed

**Description:**
Real WebSocket almost always runs over TLS (wss://). Plain ws:// over UDP is extremely rare, creating easy DPI signature.

**Current Behavior:**
```
[UDP Header][WebSocket Frame][ChaCha20-Poly1305 ciphertext]
```

**Expected (wss:// equivalent):**
```
[UDP Header][TLS Record][Application Data (WebSocket frame)]
```

**Impact:**
- DPI filter: "WebSocket without TLS = block"
- Missing from legitimate traffic patterns

**Proposed Solution:**
Implement TLS 1.3 record wrapper (already planned in Future Wrappers):
- Wrap packets in TLS application data records
- Overhead: 5-20 bytes per record
- New mode: `ProtocolWrapperType::kTLS`

**References:**
- RFC 8446 (TLS 1.3)
- `docs/protocol_wrappers.md:271-276`

---

### Issue #3: Predictable Session Rotation Interval

**Status:** CONFIRMED in code
**Severity:** Medium
**Component:** Session Management
**File:** `src/common/session/session_rotator.cpp:16-21`

**Code Evidence:**
```cpp
bool SessionRotator::should_rotate(std::uint64_t sent_packets,
                                   std::chrono::steady_clock::time_point now) const {
  const bool too_many_packets = sent_packets >= max_packets_;
  const bool expired = (now - last_rotation_) >= interval_;  // Fixed interval!
  return too_many_packets || expired;
}
```

**Description:**
Session ID rotates every exactly 30 seconds (configurable but fixed). ML-based DPI can detect this perfect periodicity.

**Current Behavior:**
```
Session timings: [0s] -> [30s] -> [60s] -> [90s] -> [120s]
                  ^      ^       ^       ^       ^
              Perfect 30s intervals = automation detected
```

**Real P2P Traffic:**
```
Connection durations: 13s, 2m47s, 38s, 5m19s, 1m02s
                      ^ Irregular, human-like patterns
```

**Impact:**
- ML classifier feature: "Std deviation of session duration < threshold"
- Long-term observation reveals pattern

**Proposed Solution:**
Add exponential jitter to rotation interval:

```cpp
duration compute_rotation_interval() {
  const duration base = 30s;
  const duration jitter_range = 10s;

  // Exponential distribution: most intervals ~30s, some much longer
  double u = uniform_random(0.0, 1.0);
  double jitter = -log(u) * jitter_range.count();
  jitter = clamp(jitter, -10.0, 20.0);  // Range: 20-50s

  return base + seconds(static_cast<int>(jitter));
}
```

**Complexity:** Low (1-2 hour implementation)

---

## Performance Bottlenecks

### Issue #4: Single-Threaded Architecture Limits Throughput

**Status:** CONFIRMED in code
**Severity:** High (CRITICAL for performance)
**Component:** Core Architecture
**File:** `src/transport/event_loop/event_loop.h`

**Code Evidence:**
```cpp
// Thread Safety:
//   This class is designed for single-threaded operation. All methods except
//   stop() and is_running() must be called from the thread that calls run().
```

**Description:**
All I/O processing happens on single main thread. Target performance is 500 Mbps, vs 10+ Gbps for multi-threaded VPNs.

**Current Architecture:**
```
Main Thread:
  +-- UDP socket polling
  +-- TUN device I/O
  +-- Encryption/decryption
  +-- Retransmit processing
  +-- Session management

Result: 1 CPU core used, 7-63 cores idle
```

**Comparison:**
| Protocol | Architecture | Throughput |
|----------|--------------|------------|
| WireGuard | Kernel module, multi-core | 10+ Gbps |
| OpenVPN | Multi-process | 1-2 Gbps |
| **VEIL** | Single-threaded | **500 Mbps** |

**Impact:**
- Can't handle 100+ clients at high speeds
- 95% of server hardware unused
- Higher costs (need more servers)

**Proposed Solution (Multi-Phase):**

**Phase 1: Pipeline Parallelism**
```
Thread 1 (RX):      UDP receive -> decrypt
       | (lock-free queue)
Thread 2 (Process): Reassembly -> routing
       | (lock-free queue)
Thread 3 (TX):      Encrypt -> UDP send
```
Target: 1-2 Gbps

**Phase 2: Per-Client Threading**
```
SO_REUSEPORT + thread pool
Each client session -> dedicated thread
```
Target: 3-5 Gbps

**Concerns:**
- May affect obfuscation timing precision
- Increased complexity (thread safety)
- Trade-off: performance vs stealth

**Priority:** Medium (needs careful evaluation)

---

### Issue #5: No 0-RTT Resumption

**Status:** CONFIRMED - Not implemented
**Severity:** Low
**Component:** Handshake
**File:** `src/common/handshake/handshake_processor.h`

**Description:**
Client must complete 1-RTT handshake before sending data. Modern protocols support 0-RTT for returning clients.

**Current:**
```
RTT = 200ms (international)

Connection:     200ms (INIT -> RESPONSE)
First request:  200ms
Total:          400ms
```

**With 0-RTT:**
```
Connection + data: 200ms (1 RTT)
Total:             200ms (50% faster)
```

**Proposed Solution:**
- Issue session tickets after first handshake
- Client caches ticket
- On reconnect: send ticket + encrypted data in INIT
- Server validates ticket and processes data immediately

**Security Notes:**
- 0-RTT vulnerable to replay (need anti-replay token)
- Only for idempotent operations
- Document risks clearly

**Priority:** Low (minor UX improvement)

---

## Scalability Limitations

### Issue #6: PSK Authentication Doesn't Scale

**Status:** CONFIRMED in code
**Severity:** Medium
**Component:** Authentication
**File:** `src/common/handshake/`

**Description:**
Single Pre-Shared Key (PSK) for all clients limits scalability and granular access control.

**Problems:**

**1. No Individual Revocation:**
```
WireGuard:
  Remove client public key -> access revoked immediately

VEIL:
  Client knows PSK -> must generate new PSK for ALL clients
  -> 1000 clients must all update simultaneously
```

**2. Key Distribution:**
- How to securely give PSK to new client?
- No scalable key distribution mechanism

**3. Compromise Impact:**
- One client compromised = PSK leaked
- All clients must rotate keys

**Proposed Solutions:**

**Option A: Per-Client PSK** (simpler)
```cpp
// Server config
client_keys = {
  "alice": "psk_alice_...",
  "bob":   "psk_bob_...",
}

// Client sends client_id in INIT
// Server looks up corresponding PSK
```

**Option B: Public-Key Auth** (better, like WireGuard)
```
Client generates keypair
Server has list of authorized public keys
Can revoke individual clients
```

**Complexity:** Very High (protocol change)
**Priority:** Low (acceptable for small deployments <100 clients)

---

## Testing Gaps

### Issue #7: No Machine Learning Based DPI Testing

**Status:** CONFIRMED - Not implemented
**Severity:** High
**Component:** Testing
**File:** New test suite needed

**Description:**
VEIL validated against signature-based DPI (nDPI) and statistical analysis, but NOT against ML-based classifiers used by modern DPI systems (GFW, TSPU).

**Current Testing:**
- nDPI (signature-based)
- Entropy analysis
- Packet size distribution
- ~~Machine learning classifiers~~ NOT TESTED

**Risk:**
ML models can detect subtle patterns:
- HMAC-based deterministic padding (mathematical pattern)
- Exponential distribution heartbeats (model signature)
- Session rotation patterns (periodicity)

**Example ML Detection:**
```python
features = extract_features(traffic):
  - Packet size histogram (100 bins)
  - Inter-arrival time autocorrelation
  - Burst patterns (packets/second over time)
  - Entropy time series
  - Session duration distribution

classifier = RandomForest(features, n_estimators=100)
if classifier.predict(features) == "VPN":
  block()
```

**Proposed Solution:**

**Phase 1: Build Dataset**
1. Collect VEIL pcaps (all 4 modes, 10+ hours each)
2. Collect legitimate traffic:
   - IoT devices (smart home sensors)
   - WebSocket apps (chat, gaming)
   - Video streaming
   - Web browsing

**Phase 2: Train Classifiers**
1. Extract features (100+ dimensions)
2. Train multiple models:
   - Random Forest
   - Gradient Boosting (XGBoost)
   - Neural Network (LSTM for time series)
3. Measure detection accuracy

**Phase 3: Iterate**
- If detection >5% -> improve obfuscation
- Adversarial training loop
- Repeat until <5% false positive AND <5% false negative

**Tools:**
- Python: scikit-learn, TensorFlow
- Feature extraction: tshark, Python scapy
- Benchmark: VEIL vs Shadowsocks vs V2Ray

**Complexity:** High (requires ML expertise)
**Priority:** **Critical** (unknown real-world effectiveness)

---

## NEW: Performance Issues Found in Logs

### Issue #8: Excessive Out-of-Order Packet Rejection

**Status:** NEW - Found in logs
**Severity:** High (CRITICAL for performance)
**Component:** Replay Protection
**File:** `src/common/session/replay_window.cpp`

**Log Evidence:**
```
2026-01-29 00:00:10.559 [warning] Packet replay detected or out of window: sequence=1871, highest=1902
2026-01-29 00:00:10.559 [warning] Packet replay detected or out of window: sequence=1872, highest=1902
...
// ~20% of packets rejected!
```

**Description:**
Packets arriving only 30-32 sequences behind the highest are being rejected. The replay window is 1024 bits, but packets are being dropped. Analysis shows these are likely server retransmissions arriving after newer packets.

**Root Cause Analysis:**
1. Server retransmits packet with seq=1871
2. Client receives newer packets first (network reordering or different paths)
3. Client's `highest` advances to 1902
4. Original packet seq=1871 arrives late
5. Packet is within window (1902-1871=31 < 1024) BUT may have already been marked as received
6. Replay protection rejects it

**Impact:**
- ~20% packet loss
- TCP retransmissions at higher layer
- Massive latency increase
- YouTube taking 50+ seconds to load

**Investigation Needed:**
1. Check if these are actual retransmissions (same seq, different arrival time)
2. Verify replay window bit handling for out-of-order packets
3. Check server retransmission logic

**Proposed Solutions:**
1. Add logging to distinguish replay vs out-of-window
2. Increase replay window if needed (though 1024 should be enough)
3. Review retransmission logic to avoid unnecessary resends
4. Consider SACK-style acknowledgment improvements

---

### Issue #9: High Retransmit Buffer Pending Count

**Status:** NEW - Found in logs
**Severity:** Medium
**Component:** Retransmission
**File:** `src/transport/mux/retransmit_buffer.cpp`

**Log Evidence:**
```
2026-01-28 22:00:11.921 [warning] process_ack called: stream_id=0, ack=1756, bitmap=0x00000000, pending_before=25
2026-01-28 22:00:11.965 [warning] process_ack done: pending_after=28
```

**Description:**
Server consistently has 20-30+ packets pending in retransmit buffer. This indicates:
1. ACKs not being sent frequently enough
2. Network congestion
3. Possible head-of-line blocking

**Current ACK Interval:**
```cpp
// EventLoopConfig in event_loop.h
std::chrono::milliseconds ack_interval{50};  // 50ms default
```

**Impact:**
- Memory usage grows with pending packets
- Retransmissions may cause congestion collapse
- Overall throughput reduction

**Proposed Solutions:**
1. Tune ACK interval (try 20-30ms)
2. Implement cumulative ACK optimization
3. Add congestion control (CUBIC or BBR-style)

---

### Issue #10: Cumulative ACK Not Using SACK Bitmap

**Status:** NEW - Found in logs
**Severity:** Medium
**Component:** ACK Processing
**File:** `src/transport/mux/ack_bitmap.h`

**Log Evidence:**
```
process_ack called: stream_id=0, ack=1756, bitmap=0x00000000, pending_before=25
```

**Description:**
The SACK bitmap is always `0x00000000`, indicating selective acknowledgments are not being used effectively. Only cumulative ACKs are working.

**Impact:**
- Lost packets require full retransmission wait
- Cannot skip already-received packets
- Head-of-line blocking on packet loss

**Proposed Solution:**
Review AckBitmap implementation and ensure SACK bits are being set properly for out-of-order received packets.

---

## Summary & Priorities

| Issue | GitHub | Impact | Complexity | Priority | Est. Effort |
|-------|--------|--------|-----------|----------|-------------|
| Out-of-order rejection | [#78](https://github.com/VisageDvachevsky/veil-core/issues/78) | **Performance-critical** | Medium | **CRITICAL** | 1-2 weeks |
| High pending count | [#79](https://github.com/VisageDvachevsky/veil-core/issues/79) | Performance | Low | High | 1 week |
| SACK not working | [#80](https://github.com/VisageDvachevsky/veil-core/issues/80) | Performance | Medium | High | 1 week |
| WebSocket w/o HTTP | [#81](https://github.com/VisageDvachevsky/veil-core/issues/81) | Detection risk | Medium | High | 1-2 weeks |
| No TLS wrapper | [#82](https://github.com/VisageDvachevsky/veil-core/issues/82) | Detection risk | High | High | 3-4 weeks |
| Fixed rotation time | [#83](https://github.com/VisageDvachevsky/veil-core/issues/83) | ML detection | Low | Medium | 1 day |
| No ML testing | [#84](https://github.com/VisageDvachevsky/veil-core/issues/84) | Unknown risk | High | **Critical** | 4-6 weeks |
| Single-threaded | [#85](https://github.com/VisageDvachevsky/veil-core/issues/85) | Performance | Very High | Medium | 2-3 months |
| No 0-RTT | [#86](https://github.com/VisageDvachevsky/veil-core/issues/86) | UX latency | High | Low | 2-3 weeks |
| PSK auth | [#87](https://github.com/VisageDvachevsky/veil-core/issues/87) | Scalability | Very High | Low | 1-2 months |

---

## Roadmap Recommendation

**Immediate (This Week) - Performance Critical:**
1. **[#78](https://github.com/VisageDvachevsky/veil-core/issues/78):** Investigate out-of-order packet rejection - root cause analysis
2. **[#80](https://github.com/VisageDvachevsky/veil-core/issues/80):** Verify SACK bitmap functionality
3. **[#79](https://github.com/VisageDvachevsky/veil-core/issues/79):** Tune ACK interval and retransmission parameters

**Short-term (1-3 months):**
1. [#83](https://github.com/VisageDvachevsky/veil-core/issues/83): Add jitter to session rotation (1 day)
2. [#81](https://github.com/VisageDvachevsky/veil-core/issues/81): HTTP handshake emulation (critical for QUIC-Like mode)
3. [#82](https://github.com/VisageDvachevsky/veil-core/issues/82): TLS record wrapper (major DPI evasion improvement)
4. [#84](https://github.com/VisageDvachevsky/veil-core/issues/84): Begin ML-based DPI testing (foundational)

**Long-term (3-6 months):**
1. [#85](https://github.com/VisageDvachevsky/veil-core/issues/85): Multi-threaded architecture (if performance becomes bottleneck)
2. [#87](https://github.com/VisageDvachevsky/veil-core/issues/87): Per-client authentication (if scaling beyond 100 clients)
3. [#86](https://github.com/VisageDvachevsky/veil-core/issues/86): 0-RTT (nice-to-have UX improvement)

---

## Dependencies Between Issues

```
                    +-------------+
                    |   #78       |
                    | Out-of-order|
                    |  rejection  |
                    +------+------+
                           |
                           v
+-------------+     +------+------+     +-------------+
|    #79      |---->|    #80      |---->| Performance |
| High pending|     | SACK bitmap |     |   Baseline  |
+-------------+     +-------------+     +------+------+
                                               |
                    +-------------+            |
                    |    #85      |<-----------+
                    |Multi-thread |
                    +-------------+
                           |
+-------------+     +------+------+
|    #81      |---->|    #82      |
|WebSocket HTTP|    | TLS wrapper |
+-------------+     +------+------+
                           |
                    +------+------+
                    |    #84      |
                    |  ML Testing |
                    +-------------+
```

**Explanation:**
- Issues #78, #79, #80 are interdependent and should be fixed together for performance baseline
- Issue #85 (multi-threading) depends on having a working single-threaded baseline
- Issues #81 and #82 are related (both are protocol wrappers)
- Issue #84 should verify all DPI evasion improvements

---

## Document History

- **2026-01-28:** Initial document created based on Issue #76 investigation
  - Confirmed 7 issues from original analysis
  - Added 3 new issues found in log analysis (#8, #9, #10)
  - Created dependency map and roadmap
