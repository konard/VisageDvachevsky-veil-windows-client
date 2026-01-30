#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace veil::protocol_wrapper {

// HTTP WebSocket Upgrade handshake emulator for DPI evasion.
// Implements RFC 6455 Section 1.3 HTTP handshake to make WebSocket traffic
// appear legitimate to advanced DPI systems.
//
// Without the HTTP handshake, WebSocket frames alone can be detected as
// anomalous traffic ("WebSocket frames without HTTP = VPN").
//
// Usage:
//   // Client side:
//   auto [request, key] = HttpHandshakeEmulator::generate_upgrade_request("/");
//   send(request);
//   auto response = receive();
//   if (HttpHandshakeEmulator::validate_upgrade_response(response, key)) {
//     // Proceed with WebSocket frames
//   }
//
//   // Server side:
//   auto request = receive();
//   if (auto key = HttpHandshakeEmulator::parse_upgrade_request(request)) {
//     auto response = HttpHandshakeEmulator::generate_upgrade_response(*key);
//     send(response);
//     // Proceed with WebSocket frames
//   }
//
// Note: This is an emulator for DPI evasion purposes. It generates minimal
// but RFC-compliant HTTP messages for the WebSocket upgrade handshake.

// WebSocket handshake key size (16 bytes random, base64-encoded to 24 chars).
constexpr std::size_t kWebSocketKeyRawSize = 16;
constexpr std::size_t kWebSocketKeyBase64Size = 24;

// SHA-1 hash output size.
constexpr std::size_t kSha1HashSize = 20;

// WebSocket GUID (RFC 6455 Section 1.3).
constexpr std::string_view kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Handshake state for tracking connection establishment.
enum class HandshakeState : std::uint8_t {
  kNotStarted = 0,      // Handshake not initiated
  kRequestSent = 1,     // Client sent upgrade request
  kRequestReceived = 2, // Server received upgrade request
  kResponseSent = 3,    // Server sent upgrade response
  kCompleted = 4,       // Handshake completed successfully
  kFailed = 5,          // Handshake failed
};

// Parsed HTTP Upgrade request fields.
struct UpgradeRequest {
  std::string path;                // Request path (e.g., "/", "/ws")
  std::string host;                // Host header value
  std::string sec_websocket_key;   // Sec-WebSocket-Key value (base64)
  std::string origin;              // Origin header (optional)
  std::string sec_websocket_version; // Sec-WebSocket-Version (should be "13")
};

// Parsed HTTP Upgrade response fields.
struct UpgradeResponse {
  int status_code;                 // HTTP status code (101 for success)
  std::string sec_websocket_accept; // Sec-WebSocket-Accept value
};

class HttpHandshakeEmulator {
 public:
  // ============================================================================
  // Client-side methods
  // ============================================================================

  // Generate an HTTP Upgrade request for WebSocket handshake.
  // Returns the HTTP request bytes and the Sec-WebSocket-Key for later validation.
  //
  // path: The WebSocket endpoint path (e.g., "/", "/ws", "/veil")
  // host: The Host header value (e.g., "example.com", "192.168.1.1:8080")
  //
  // Returns: {request_bytes, sec_websocket_key}
  static std::pair<std::vector<std::uint8_t>, std::string> generate_upgrade_request(
      std::string_view path = "/",
      std::string_view host = "localhost");

  // Validate an HTTP Upgrade response from server.
  // Checks that status is 101 and Sec-WebSocket-Accept is correctly computed.
  //
  // response: The raw HTTP response bytes
  // client_key: The Sec-WebSocket-Key sent in the request
  //
  // Returns: true if valid 101 response with correct accept key
  static bool validate_upgrade_response(std::span<const std::uint8_t> response,
                                         std::string_view client_key);

  // ============================================================================
  // Server-side methods
  // ============================================================================

  // Parse an HTTP Upgrade request from client.
  // Extracts the Sec-WebSocket-Key needed to generate the response.
  //
  // request: The raw HTTP request bytes
  //
  // Returns: Parsed request fields, or std::nullopt if invalid
  static std::optional<UpgradeRequest> parse_upgrade_request(
      std::span<const std::uint8_t> request);

  // Generate an HTTP 101 Switching Protocols response.
  // Computes Sec-WebSocket-Accept from the client's key.
  //
  // client_key: The Sec-WebSocket-Key from the client's request
  //
  // Returns: The HTTP response bytes
  static std::vector<std::uint8_t> generate_upgrade_response(std::string_view client_key);

  // ============================================================================
  // Utility methods
  // ============================================================================

  // Generate a random Sec-WebSocket-Key (16 random bytes, base64-encoded).
  static std::string generate_websocket_key();

  // Compute Sec-WebSocket-Accept from client key (RFC 6455 Section 1.3).
  // accept = base64(SHA-1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
  static std::string compute_accept_key(std::string_view client_key);

  // Parse an HTTP Upgrade response.
  // Returns: Parsed response fields, or std::nullopt if invalid
  static std::optional<UpgradeResponse> parse_upgrade_response(
      std::span<const std::uint8_t> response);

  // ============================================================================
  // Base64 encoding/decoding (for Sec-WebSocket-Key handling)
  // ============================================================================

  // Encode bytes to base64 string.
  static std::string base64_encode(std::span<const std::uint8_t> data);

  // Decode base64 string to bytes.
  // Returns: Decoded bytes, or empty vector if invalid
  static std::vector<std::uint8_t> base64_decode(std::string_view base64);

  // ============================================================================
  // SHA-1 hash (for Sec-WebSocket-Accept computation)
  // RFC 6455 specifically requires SHA-1 for this purpose.
  // ============================================================================

  // Compute SHA-1 hash of input data.
  static std::array<std::uint8_t, kSha1HashSize> sha1(std::span<const std::uint8_t> data);

  // Compute SHA-1 hash of string data.
  static std::array<std::uint8_t, kSha1HashSize> sha1(std::string_view data);

 private:
  // Find header value in HTTP message.
  static std::optional<std::string> find_header(std::string_view headers,
                                                  std::string_view header_name);

  // Extract status code from HTTP response first line.
  static std::optional<int> parse_status_code(std::string_view first_line);
};

}  // namespace veil::protocol_wrapper
