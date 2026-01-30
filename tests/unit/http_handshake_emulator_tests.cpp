#include "common/protocol_wrapper/http_handshake_emulator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace veil::protocol_wrapper;

// ============================================================================
// Base64 encoding/decoding tests
// ============================================================================

TEST(HttpHandshakeEmulatorTest, Base64EncodeEmpty) {
  std::vector<std::uint8_t> empty;
  auto encoded = HttpHandshakeEmulator::base64_encode(empty);
  EXPECT_EQ(encoded, "");
}

TEST(HttpHandshakeEmulatorTest, Base64EncodeBasic) {
  // "Hello" -> "SGVsbG8="
  std::vector<std::uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
  auto encoded = HttpHandshakeEmulator::base64_encode(data);
  EXPECT_EQ(encoded, "SGVsbG8=");
}

TEST(HttpHandshakeEmulatorTest, Base64EncodeOneByte) {
  // "M" -> "TQ=="
  std::vector<std::uint8_t> data = {'M'};
  auto encoded = HttpHandshakeEmulator::base64_encode(data);
  EXPECT_EQ(encoded, "TQ==");
}

TEST(HttpHandshakeEmulatorTest, Base64EncodeTwoBytes) {
  // "Ma" -> "TWE="
  std::vector<std::uint8_t> data = {'M', 'a'};
  auto encoded = HttpHandshakeEmulator::base64_encode(data);
  EXPECT_EQ(encoded, "TWE=");
}

TEST(HttpHandshakeEmulatorTest, Base64EncodeThreeBytes) {
  // "Man" -> "TWFu"
  std::vector<std::uint8_t> data = {'M', 'a', 'n'};
  auto encoded = HttpHandshakeEmulator::base64_encode(data);
  EXPECT_EQ(encoded, "TWFu");
}

TEST(HttpHandshakeEmulatorTest, Base64DecodeEmpty) {
  auto decoded = HttpHandshakeEmulator::base64_decode("");
  EXPECT_TRUE(decoded.empty());
}

TEST(HttpHandshakeEmulatorTest, Base64DecodeBasic) {
  auto decoded = HttpHandshakeEmulator::base64_decode("SGVsbG8=");
  std::vector<std::uint8_t> expected = {'H', 'e', 'l', 'l', 'o'};
  EXPECT_EQ(decoded, expected);
}

TEST(HttpHandshakeEmulatorTest, Base64DecodeOneByte) {
  auto decoded = HttpHandshakeEmulator::base64_decode("TQ==");
  std::vector<std::uint8_t> expected = {'M'};
  EXPECT_EQ(decoded, expected);
}

TEST(HttpHandshakeEmulatorTest, Base64RoundTrip) {
  // Test round-trip with various data sizes.
  for (std::size_t size = 0; size < 100; ++size) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
      data[i] = static_cast<std::uint8_t>(i % 256);
    }

    auto encoded = HttpHandshakeEmulator::base64_encode(data);
    auto decoded = HttpHandshakeEmulator::base64_decode(encoded);
    EXPECT_EQ(decoded, data) << "Failed for size " << size;
  }
}

TEST(HttpHandshakeEmulatorTest, Base64DecodeBinaryData) {
  // Test binary data with all possible byte values.
  std::vector<std::uint8_t> data(256);
  for (int i = 0; i < 256; ++i) {
    data[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
  }

  auto encoded = HttpHandshakeEmulator::base64_encode(data);
  auto decoded = HttpHandshakeEmulator::base64_decode(encoded);
  EXPECT_EQ(decoded, data);
}

// ============================================================================
// SHA-1 tests (RFC 3174 test vectors)
// ============================================================================

TEST(HttpHandshakeEmulatorTest, Sha1EmptyString) {
  // SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
  auto hash = HttpHandshakeEmulator::sha1("");
  std::vector<std::uint8_t> expected = {
      0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
      0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
  };
  EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()), expected);
}

TEST(HttpHandshakeEmulatorTest, Sha1Abc) {
  // SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
  auto hash = HttpHandshakeEmulator::sha1("abc");
  std::vector<std::uint8_t> expected = {
      0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
      0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
  };
  EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()), expected);
}

TEST(HttpHandshakeEmulatorTest, Sha1QuickBrownFox) {
  // SHA-1("The quick brown fox jumps over the lazy dog") = 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12
  auto hash = HttpHandshakeEmulator::sha1("The quick brown fox jumps over the lazy dog");
  std::vector<std::uint8_t> expected = {
      0x2f, 0xd4, 0xe1, 0xc6, 0x7a, 0x2d, 0x28, 0xfc, 0xed, 0x84,
      0x9e, 0xe1, 0xbb, 0x76, 0xe7, 0x39, 0x1b, 0x93, 0xeb, 0x12
  };
  EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()), expected);
}

TEST(HttpHandshakeEmulatorTest, Sha1LongString) {
  // SHA-1("aaaa...aaa") (1000000 'a's) = 34aa973cd4c4daa4f61eeb2bdbad27316534016f
  // This test verifies the multi-block processing.
  std::string input(1000000, 'a');
  auto hash = HttpHandshakeEmulator::sha1(input);
  std::vector<std::uint8_t> expected = {
      0x34, 0xaa, 0x97, 0x3c, 0xd4, 0xc4, 0xda, 0xa4, 0xf6, 0x1e,
      0xeb, 0x2b, 0xdb, 0xad, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6f
  };
  EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()), expected);
}

// ============================================================================
// WebSocket key generation and accept computation
// ============================================================================

TEST(HttpHandshakeEmulatorTest, GenerateWebSocketKey) {
  auto key = HttpHandshakeEmulator::generate_websocket_key();

  // Key should be base64-encoded 16 bytes = 24 characters.
  EXPECT_EQ(key.size(), 24);

  // Should be valid base64.
  auto decoded = HttpHandshakeEmulator::base64_decode(key);
  EXPECT_EQ(decoded.size(), 16);
}

TEST(HttpHandshakeEmulatorTest, GenerateWebSocketKeyUniqueness) {
  // Generate multiple keys and ensure they're different.
  auto key1 = HttpHandshakeEmulator::generate_websocket_key();
  auto key2 = HttpHandshakeEmulator::generate_websocket_key();
  auto key3 = HttpHandshakeEmulator::generate_websocket_key();

  EXPECT_NE(key1, key2);
  EXPECT_NE(key2, key3);
  EXPECT_NE(key1, key3);
}

TEST(HttpHandshakeEmulatorTest, ComputeAcceptKeyRfc6455Example) {
  // RFC 6455 example:
  // Client Key: "dGhlIHNhbXBsZSBub25jZQ=="
  // Expected Accept: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  auto accept = HttpHandshakeEmulator::compute_accept_key(client_key);
  EXPECT_EQ(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(HttpHandshakeEmulatorTest, ComputeAcceptKeyDeterministic) {
  // Same input should always produce same output.
  std::string client_key = "xqBt3ImNzJbYqRINxEFlkg==";
  auto accept1 = HttpHandshakeEmulator::compute_accept_key(client_key);
  auto accept2 = HttpHandshakeEmulator::compute_accept_key(client_key);
  EXPECT_EQ(accept1, accept2);
}

// ============================================================================
// HTTP Upgrade request generation and parsing
// ============================================================================

TEST(HttpHandshakeEmulatorTest, GenerateUpgradeRequest) {
  auto [request, key] = HttpHandshakeEmulator::generate_upgrade_request("/", "localhost");

  // Request should not be empty.
  EXPECT_FALSE(request.empty());

  // Key should be valid.
  EXPECT_EQ(key.size(), 24);

  // Convert to string for analysis.
  std::string request_str(request.begin(), request.end());

  // Should contain required headers.
  EXPECT_TRUE(request_str.find("GET / HTTP/1.1\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Host: localhost\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Upgrade: websocket\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Connection: Upgrade\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Sec-WebSocket-Key: " + key + "\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Sec-WebSocket-Version: 13\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("\r\n\r\n") != std::string::npos);
}

TEST(HttpHandshakeEmulatorTest, GenerateUpgradeRequestCustomPath) {
  auto [request, key] = HttpHandshakeEmulator::generate_upgrade_request("/ws/veil", "example.com:8443");

  std::string request_str(request.begin(), request.end());
  EXPECT_TRUE(request_str.find("GET /ws/veil HTTP/1.1\r\n") != std::string::npos);
  EXPECT_TRUE(request_str.find("Host: example.com:8443\r\n") != std::string::npos);
}

TEST(HttpHandshakeEmulatorTest, ParseUpgradeRequest) {
  // Generate a request and parse it back.
  auto [request, key] = HttpHandshakeEmulator::generate_upgrade_request("/test", "myhost.local");

  auto parsed = HttpHandshakeEmulator::parse_upgrade_request(request);
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->path, "/test");
  EXPECT_EQ(parsed->host, "myhost.local");
  EXPECT_EQ(parsed->sec_websocket_key, key);
  EXPECT_EQ(parsed->sec_websocket_version, "13");
}

TEST(HttpHandshakeEmulatorTest, ParseUpgradeRequestInvalid) {
  // Invalid request (not a WebSocket upgrade).
  std::string invalid = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  std::vector<std::uint8_t> data(invalid.begin(), invalid.end());

  auto parsed = HttpHandshakeEmulator::parse_upgrade_request(data);
  EXPECT_FALSE(parsed.has_value());
}

TEST(HttpHandshakeEmulatorTest, ParseUpgradeRequestIncomplete) {
  // Incomplete request (no header terminator).
  std::string incomplete = "GET / HTTP/1.1\r\nHost: localhost\r\n";
  std::vector<std::uint8_t> data(incomplete.begin(), incomplete.end());

  auto parsed = HttpHandshakeEmulator::parse_upgrade_request(data);
  EXPECT_FALSE(parsed.has_value());
}

// ============================================================================
// HTTP Upgrade response generation and parsing
// ============================================================================

TEST(HttpHandshakeEmulatorTest, GenerateUpgradeResponse) {
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  auto response = HttpHandshakeEmulator::generate_upgrade_response(client_key);

  // Response should not be empty.
  EXPECT_FALSE(response.empty());

  // Convert to string for analysis.
  std::string response_str(response.begin(), response.end());

  // Should contain required headers.
  EXPECT_TRUE(response_str.find("HTTP/1.1 101 Switching Protocols\r\n") != std::string::npos);
  EXPECT_TRUE(response_str.find("Upgrade: websocket\r\n") != std::string::npos);
  EXPECT_TRUE(response_str.find("Connection: Upgrade\r\n") != std::string::npos);
  EXPECT_TRUE(response_str.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);
  EXPECT_TRUE(response_str.find("\r\n\r\n") != std::string::npos);
}

TEST(HttpHandshakeEmulatorTest, ParseUpgradeResponse) {
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  auto response = HttpHandshakeEmulator::generate_upgrade_response(client_key);

  auto parsed = HttpHandshakeEmulator::parse_upgrade_response(response);
  ASSERT_TRUE(parsed.has_value());

  EXPECT_EQ(parsed->status_code, 101);
  EXPECT_EQ(parsed->sec_websocket_accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(HttpHandshakeEmulatorTest, ParseUpgradeResponseInvalid) {
  // Invalid response (not 101).
  std::string invalid = "HTTP/1.1 400 Bad Request\r\n\r\n";
  std::vector<std::uint8_t> data(invalid.begin(), invalid.end());

  auto parsed = HttpHandshakeEmulator::parse_upgrade_response(data);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->status_code, 400);
}

// ============================================================================
// Validate upgrade response
// ============================================================================

TEST(HttpHandshakeEmulatorTest, ValidateUpgradeResponse) {
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  auto response = HttpHandshakeEmulator::generate_upgrade_response(client_key);

  EXPECT_TRUE(HttpHandshakeEmulator::validate_upgrade_response(response, client_key));
}

TEST(HttpHandshakeEmulatorTest, ValidateUpgradeResponseWrongKey) {
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  std::string wrong_key = "xqBt3ImNzJbYqRINxEFlkg==";
  auto response = HttpHandshakeEmulator::generate_upgrade_response(client_key);

  EXPECT_FALSE(HttpHandshakeEmulator::validate_upgrade_response(response, wrong_key));
}

TEST(HttpHandshakeEmulatorTest, ValidateUpgradeResponseNon101) {
  std::string response = "HTTP/1.1 400 Bad Request\r\n\r\n";
  std::vector<std::uint8_t> data(response.begin(), response.end());

  EXPECT_FALSE(HttpHandshakeEmulator::validate_upgrade_response(data, "somekey"));
}

// ============================================================================
// Full handshake flow test
// ============================================================================

TEST(HttpHandshakeEmulatorTest, FullHandshakeFlow) {
  // Client generates upgrade request.
  auto [request, client_key] = HttpHandshakeEmulator::generate_upgrade_request("/veil", "192.168.1.1:8443");

  // Server parses the request.
  auto parsed_request = HttpHandshakeEmulator::parse_upgrade_request(request);
  ASSERT_TRUE(parsed_request.has_value());
  EXPECT_EQ(parsed_request->path, "/veil");
  EXPECT_EQ(parsed_request->host, "192.168.1.1:8443");
  EXPECT_EQ(parsed_request->sec_websocket_key, client_key);

  // Server generates response.
  auto response = HttpHandshakeEmulator::generate_upgrade_response(parsed_request->sec_websocket_key);

  // Client validates the response.
  EXPECT_TRUE(HttpHandshakeEmulator::validate_upgrade_response(response, client_key));

  // Parse the response to verify contents.
  auto parsed_response = HttpHandshakeEmulator::parse_upgrade_response(response);
  ASSERT_TRUE(parsed_response.has_value());
  EXPECT_EQ(parsed_response->status_code, 101);
  EXPECT_EQ(parsed_response->sec_websocket_accept, HttpHandshakeEmulator::compute_accept_key(client_key));
}

TEST(HttpHandshakeEmulatorTest, MultipleHandshakes) {
  // Test multiple independent handshakes.
  for (int i = 0; i < 10; ++i) {
    auto [request, client_key] = HttpHandshakeEmulator::generate_upgrade_request("/", "localhost");

    auto parsed_request = HttpHandshakeEmulator::parse_upgrade_request(request);
    ASSERT_TRUE(parsed_request.has_value());

    auto response = HttpHandshakeEmulator::generate_upgrade_response(parsed_request->sec_websocket_key);
    EXPECT_TRUE(HttpHandshakeEmulator::validate_upgrade_response(response, client_key));
  }
}
