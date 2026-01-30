# Protocol Wrappers for DPI Evasion

## Overview

Protocol wrappers add legitimate protocol headers around VEIL packets to evade Deep Packet Inspection (DPI) systems that perform protocol fingerprinting. This addresses the limitation that statistical traffic shaping alone cannot fool DPI systems looking for actual protocol signatures.

## Problem Statement

**Before Protocol Wrappers:**
- DPI bypass modes only provided statistical traffic shaping (packet sizes, timing, padding)
- No actual protocol headers were included
- Sophisticated DPI systems could detect lack of protocol structure
- Example: "QUIC-Like" mode didn't include actual QUIC headers

**After Protocol Wrappers:**
- VEIL packets are wrapped in legitimate protocol frames
- DPI systems see valid protocol headers and structure
- Multi-layer evasion: statistical shaping + protocol mimicry

## Available Wrappers

### WebSocket Wrapper (RFC 6455)

**Status:** ✅ Implemented and enabled in QUIC-Like mode

**Description:**
Wraps VEIL packets in WebSocket binary frames, making traffic appear as legitimate WebSocket communication.

**WebSocket Frame Format:**
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data continued ...                |
+---------------------------------------------------------------+
```

**Frame Parameters:**
- `FIN`: Always 1 (complete frame)
- `RSV1-3`: Always 0 (no extensions)
- `Opcode`: 0x2 (Binary frame)
- `MASK`: 1 for client-to-server, 0 for server-to-client
- `Payload`: VEIL packet data (optionally XOR-masked)

**Overhead:**
- Minimum: 2 bytes (small payloads < 126 bytes, no masking)
- Typical: 6 bytes (small payloads < 126 bytes, with masking)
- Medium: 8 bytes (126-65535 byte payloads, with masking)
- Large: 14 bytes (>65535 byte payloads, with masking)

**DPI Evasion:**
- ✅ Evades protocol signature detection (looks like WebSocket)
- ✅ Evades simple packet inspection (valid RFC 6455 frames)
- ✅ Compatible with statistical traffic shaping
- ⚠️ Does NOT evade TLS-layer inspection (WebSocket typically runs over TLS)

## Usage

### Enabling Protocol Wrappers

**Automatic (via DPI Mode):**
```cpp
// QUIC-Like mode automatically enables WebSocket wrapper
auto profile = create_dpi_mode_profile(DPIBypassMode::kQUICLike);
// profile.protocol_wrapper == ProtocolWrapperType::kWebSocket
```

**Manual Configuration:**
```cpp
ObfuscationProfile profile;
profile.enabled = true;
profile.protocol_wrapper = ProtocolWrapperType::kWebSocket;
profile.is_client_to_server = true;  // Enable masking
```

### Wrapping and Unwrapping

**Wrap a VEIL packet:**
```cpp
#include "common/protocol_wrapper/websocket_wrapper.h"

std::vector<std::uint8_t> veil_packet = /* ... */;
bool client_to_server = true;  // Use masking

auto wrapped = WebSocketWrapper::wrap(veil_packet, client_to_server);
// wrapped now contains: [WS header][masked VEIL packet]
```

**Unwrap a WebSocket frame:**
```cpp
std::vector<std::uint8_t> received_frame = /* ... */;

auto unwrapped = WebSocketWrapper::unwrap(received_frame);
if (unwrapped.has_value()) {
  // unwrapped.value() contains original VEIL packet
  process_veil_packet(*unwrapped);
}
```

## Implementation Details

### WebSocket Masking (RFC 6455 Section 5.3)

**Why Masking?**
- RFC 6455 requires client-to-server frames to be masked
- Prevents cache poisoning attacks in HTTP proxies
- Uses XOR with a random 32-bit key

**Masking Algorithm:**
```cpp
for (size_t i = 0; i < payload.size(); ++i) {
  payload[i] ^= masking_key_bytes[i % 4];
}
```

**Key Generation:**
- Uses cryptographically secure random bytes
- New key for each frame
- Stored in frame header

### Integration with Packet Pipeline

```
┌─────────────────┐
│  VEIL Packet    │
│  [VL][Header]   │
│  [Frames]       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐     ProtocolWrapperType::kWebSocket
│  Obfuscation    │ ◄─────────────────┐
│  (Padding)      │                   │
└────────┬────────┘                   │
         │                            │
         ▼                            │
┌─────────────────┐                   │
│ Protocol Wrapper│ ◄─────────────────┘
│ (WebSocket)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ [WS Header]     │
│ [Masked VEIL]   │  ──► Send over UDP
└─────────────────┘
```

## Configuration Examples

### Example 1: QUIC-Like Mode (WebSocket Wrapper Enabled)

**Config File (`veil.conf`):**
```ini
[obfuscation]
dpi_mode = quic_like
```

**Result:**
- WebSocket wrapper automatically enabled
- Statistical traffic shaping: bursty, large packets
- Protocol headers: Valid WebSocket binary frames
- DPI sees: Legitimate WebSocket traffic

### Example 2: Custom Profile with WebSocket Wrapper

**Code:**
```cpp
ObfuscationProfile custom_profile;
custom_profile.enabled = true;
custom_profile.protocol_wrapper = ProtocolWrapperType::kWebSocket;
custom_profile.is_client_to_server = true;

// Custom statistical shaping
custom_profile.max_padding_size = 500;
custom_profile.min_padding_size = 50;
custom_profile.timing_jitter_enabled = true;
custom_profile.max_timing_jitter_ms = 100;
```

### Example 3: Server Configuration (No Masking)

**Code:**
```cpp
// Server-to-client direction
ObfuscationProfile server_profile;
server_profile.enabled = true;
server_profile.protocol_wrapper = ProtocolWrapperType::kWebSocket;
server_profile.is_client_to_server = false;  // No masking for server
```

## Performance Impact

### Measurements

| Wrapper Type | Overhead (bytes) | CPU Impact | Latency Impact |
|--------------|------------------|------------|----------------|
| None         | 0                | 0%         | 0 µs           |
| WebSocket    | 2-14             | ~1-2%      | <5 µs          |

**Test Conditions:**
- 1000-byte VEIL packets
- Intel i7-9700K @ 3.6 GHz
- Release build with -O3

**Overhead Breakdown:**
```
Small packet (100 bytes):
  - Frame header: 2 bytes (no masking) or 6 bytes (with masking)
  - Percentage: 2-6% overhead

Medium packet (1000 bytes):
  - Frame header: 6 bytes (with masking)
  - Percentage: 0.6% overhead

Large packet (10000 bytes):
  - Frame header: 6 bytes (with masking)
  - Percentage: 0.06% overhead
```

## Security Considerations

### What WebSocket Wrapper Provides

✅ **Protocol Signature Evasion:**
- DPI looking for WebSocket headers will find valid frames
- Passes simple protocol conformance checks

✅ **Statistical + Structural Evasion:**
- Combined with traffic shaping, provides multi-layer defense
- Harder to classify as VPN/proxy traffic

### What WebSocket Wrapper Does NOT Provide

❌ **TLS Encryption:**
- WebSocket wrapper only adds framing, not encryption
- VEIL's crypto layer already provides encryption
- For full TLS mimicry, consider TLS wrapper (future work)

✅ **HTTP Handshake (Now Available!):**
- HTTP Upgrade handshake emulation is now implemented
- Enable with `enable_http_handshake_emulation = true`
- Enabled by default in QUIC-Like mode
- See "HTTP Handshake Emulation" section below

❌ **Application-Layer Semantics:**
- Does not generate realistic WebSocket messages
- Only wraps binary data

### Recommendations

1. **Use with QUIC-Like Mode:** Combines wrapper with appropriate traffic shaping
2. **Consider Network Environment:** WebSocket wrapper most effective against protocol-aware DPI
3. **Layer Defenses:** Use multiple techniques (timing, size, protocol) together
4. **Monitor Effectiveness:** Test against specific DPI systems in your region

## HTTP Handshake Emulation

### Overview

The HTTP Handshake Emulator adds RFC 6455 compliant HTTP Upgrade handshake to WebSocket traffic. This makes VEIL traffic appear as legitimate WebSocket connection establishment, defeating DPI systems that expect full WebSocket protocol compliance.

### Problem

Without HTTP handshake:
```
Client -> Server: [WebSocket Binary Frame]  (no HTTP handshake)
Server -> Client: [WebSocket Binary Frame]
```

DPI Detection: "WebSocket frames without HTTP = VPN/Proxy"

### Solution

With HTTP handshake emulation:
```
Client -> Server: GET /path HTTP/1.1
                  Upgrade: websocket
                  Connection: Upgrade
                  Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
                  Sec-WebSocket-Version: 13

Server -> Client: HTTP/1.1 101 Switching Protocols
                  Upgrade: websocket
                  Connection: Upgrade
                  Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=

[WebSocket binary frames begin - as before]
```

### Enabling HTTP Handshake Emulation

**Automatic (QUIC-Like Mode):**
```cpp
// QUIC-Like mode now includes HTTP handshake emulation by default
auto profile = create_dpi_mode_profile(DPIBypassMode::kQUICLike);
// profile.enable_http_handshake_emulation == true
```

**Manual Configuration:**
```cpp
ObfuscationProfile profile;
profile.enabled = true;
profile.protocol_wrapper = ProtocolWrapperType::kWebSocket;
profile.is_client_to_server = true;
profile.enable_http_handshake_emulation = true;  // Enable handshake
```

### API Usage

**Client Side:**
```cpp
#include "common/protocol_wrapper/http_handshake_emulator.h"

// Generate upgrade request
auto [request, key] = HttpHandshakeEmulator::generate_upgrade_request("/", "server.com");
send(request);

// Receive and validate response
auto response = receive();
if (HttpHandshakeEmulator::validate_upgrade_response(response, key)) {
  // Handshake successful - proceed with WebSocket frames
  start_websocket_communication();
}
```

**Server Side:**
```cpp
// Receive and parse upgrade request
auto request = receive();
auto parsed = HttpHandshakeEmulator::parse_upgrade_request(request);
if (parsed) {
  // Generate response
  auto response = HttpHandshakeEmulator::generate_upgrade_response(parsed->sec_websocket_key);
  send(response);
  // Handshake successful - proceed with WebSocket frames
}
```

### HTTP Message Format

**Upgrade Request (Client to Server):**
```http
GET / HTTP/1.1
Host: localhost
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: <24-char base64 key>
Sec-WebSocket-Version: 13

```

**Upgrade Response (Server to Client):**
```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <28-char base64 accept>

```

### Security

The Sec-WebSocket-Accept header is computed per RFC 6455:
```
accept = base64(SHA-1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
```

This ensures:
- The server actually processed the specific client request
- Response cannot be replayed from another connection
- DPI sees cryptographically valid WebSocket handshake

### Performance Impact

| Component | Overhead |
|-----------|----------|
| HTTP Request | ~200-300 bytes |
| HTTP Response | ~150-200 bytes |
| Total | ~400-500 bytes per connection |
| Round Trips | 2 (request + response) |

This is a **one-time cost per connection**, not per packet.

### DPI Evasion Effectiveness

| DPI Detection Method | Without Handshake | With Handshake |
|----------------------|-------------------|----------------|
| WebSocket frame detection | ⚠️ Detected | ✅ Passes |
| HTTP protocol check | ❌ Fails | ✅ Passes |
| Full WebSocket compliance | ❌ Fails | ✅ Passes |
| TLS-layer inspection | ❌ Fails | ❌ Fails (use TLS wrapper) |

## Future Wrappers

### Potential Additions

1. **TLS Record Wrapper**
   - Wrap in TLS 1.3 application data records
   - Highest stealth (appears as normal HTTPS)
   - Higher overhead (~5-20 bytes per record)

2. **QUIC Header Wrapper**
   - Actual QUIC long/short headers
   - More complex than WebSocket
   - Better for mimicking HTTP/3

3. **DNS-over-HTTPS (DoH) Wrapper**
   - Tunnel through DNS TXT records
   - High latency, good for censorship bypass
   - Requires DNS infrastructure

## API Reference

### WebSocketWrapper Class

```cpp
namespace veil::protocol_wrapper {

class WebSocketWrapper {
 public:
  // Wrap data in WebSocket binary frame
  static std::vector<std::uint8_t> wrap(
      std::span<const std::uint8_t> data,
      bool client_to_server = false);

  // Unwrap WebSocket frame to get payload
  static std::optional<std::vector<std::uint8_t>> unwrap(
      std::span<const std::uint8_t> frame);

  // Parse frame header
  static std::optional<std::pair<WebSocketFrameHeader, std::size_t>>
      parse_header(std::span<const std::uint8_t> data);

  // Build frame header bytes
  static std::vector<std::uint8_t> build_header(
      const WebSocketFrameHeader& header);

  // Apply/remove XOR masking
  static void apply_mask(std::span<std::uint8_t> data,
                         std::uint32_t masking_key);

  // Generate random masking key
  static std::uint32_t generate_masking_key();
};

}  // namespace veil::protocol_wrapper
```

### ObfuscationProfile Integration

```cpp
namespace veil::obfuscation {

enum class ProtocolWrapperType : std::uint8_t {
  kNone = 0,       // No wrapper (default)
  kWebSocket = 1,  // WebSocket RFC 6455
};

struct ObfuscationProfile {
  // ... existing fields ...

  ProtocolWrapperType protocol_wrapper{ProtocolWrapperType::kNone};
  bool is_client_to_server{true};  // For WebSocket masking
  bool enable_http_handshake_emulation{false};  // Enable HTTP Upgrade handshake
};

// Helper functions
const char* protocol_wrapper_to_string(ProtocolWrapperType wrapper);
std::optional<ProtocolWrapperType> protocol_wrapper_from_string(const std::string& str);

}  // namespace veil::obfuscation
```

### HttpHandshakeEmulator Class

```cpp
namespace veil::protocol_wrapper {

class HttpHandshakeEmulator {
 public:
  // Generate HTTP Upgrade request
  // Returns: {request_bytes, sec_websocket_key}
  static std::pair<std::vector<std::uint8_t>, std::string>
      generate_upgrade_request(std::string_view path = "/",
                               std::string_view host = "localhost");

  // Validate HTTP Upgrade response from server
  static bool validate_upgrade_response(std::span<const std::uint8_t> response,
                                         std::string_view client_key);

  // Parse HTTP Upgrade request from client
  static std::optional<UpgradeRequest> parse_upgrade_request(
      std::span<const std::uint8_t> request);

  // Generate HTTP 101 Switching Protocols response
  static std::vector<std::uint8_t> generate_upgrade_response(
      std::string_view client_key);

  // Utility: Generate random Sec-WebSocket-Key
  static std::string generate_websocket_key();

  // Utility: Compute Sec-WebSocket-Accept from client key
  static std::string compute_accept_key(std::string_view client_key);

  // Utility: Base64 encoding/decoding
  static std::string base64_encode(std::span<const std::uint8_t> data);
  static std::vector<std::uint8_t> base64_decode(std::string_view base64);

  // Utility: SHA-1 hash (required by RFC 6455)
  static std::array<std::uint8_t, 20> sha1(std::span<const std::uint8_t> data);
  static std::array<std::uint8_t, 20> sha1(std::string_view data);
};

}  // namespace veil::protocol_wrapper
```

## Testing

### Unit Tests

```cpp
TEST(WebSocketWrapper, WrapAndUnwrap) {
  std::vector<std::uint8_t> data = {0x01, 0x02, 0x03, 0x04};

  auto wrapped = WebSocketWrapper::wrap(data, true);
  EXPECT_GT(wrapped.size(), data.size());  // Has header

  auto unwrapped = WebSocketWrapper::unwrap(wrapped);
  ASSERT_TRUE(unwrapped.has_value());
  EXPECT_EQ(*unwrapped, data);
}
```

### DPI Testing

```bash
# Capture VEIL traffic with WebSocket wrapper
tcpdump -i lo -w veil_websocket.pcap port 8443

# Analyze with Wireshark
wireshark veil_websocket.pcap

# Expected: Valid WebSocket binary frames
# Frame Type: Binary (0x2)
# FIN: 1
# MASK: 1 (client-to-server)
```

## Troubleshooting

### Common Issues

**Issue:** Packets not being wrapped
- Check: `profile.protocol_wrapper == ProtocolWrapperType::kWebSocket`
- Check: Wrapper integrated in send path

**Issue:** Unwrap returns nullopt
- Check: Received data is complete frame
- Check: Frame has valid header
- Debug: Use `parse_header()` to inspect frame structure

**Issue:** DPI still detects VEIL
- Consider: WebSocket typically runs over TLS/HTTPS
- Consider: Full WebSocket handshake may be expected
- Consider: Combine with other evasion techniques

## References

- [RFC 6455: The WebSocket Protocol](https://www.rfc-editor.org/rfc/rfc6455.html)
- [WebSocket Security Considerations](https://www.rfc-editor.org/rfc/rfc6455.html#section-10)
- [DPI Evasion Techniques Survey](https://www.ndss-symposium.org/ndss-paper/how-china-detects-and-blocks-shadowsocks/)
