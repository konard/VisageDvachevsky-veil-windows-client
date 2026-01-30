#include "common/protocol_wrapper/http_handshake_emulator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/crypto/random.h"

namespace veil::protocol_wrapper {

namespace {

// Base64 alphabet (RFC 4648).
constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Reverse lookup table for base64 decoding.
// Returns 64 for padding ('='), 255 for invalid characters.
constexpr std::array<std::uint8_t, 256> make_base64_decode_table() {
  std::array<std::uint8_t, 256> table{};
  for (std::size_t i = 0; i < 256; ++i) {
    table[i] = 255;  // Invalid
  }
  for (std::size_t i = 0; i < 64; ++i) {
    table[static_cast<std::uint8_t>(kBase64Alphabet[i])] = static_cast<std::uint8_t>(i);
  }
  table[static_cast<std::uint8_t>('=')] = 64;  // Padding
  return table;
}

constexpr auto kBase64DecodeTable = make_base64_decode_table();

// HTTP header constants.
constexpr std::string_view kCRLF = "\r\n";
constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

// SHA-1 implementation (RFC 3174).
// Note: SHA-1 is cryptographically broken for collision resistance,
// but RFC 6455 specifically requires it for WebSocket handshake.

// SHA-1 constants.
constexpr std::uint32_t kSha1K[] = {
    0x5A827999,  // Rounds 0-19
    0x6ED9EBA1,  // Rounds 20-39
    0x8F1BBCDC,  // Rounds 40-59
    0xCA62C1D6   // Rounds 60-79
};

constexpr std::uint32_t kSha1InitH[] = {
    0x67452301,
    0xEFCDAB89,
    0x98BADCFE,
    0x10325476,
    0xC3D2E1F0
};

// Left rotate 32-bit value.
constexpr std::uint32_t rotl32(std::uint32_t value, unsigned int count) {
  return (value << count) | (value >> (32 - count));
}

// SHA-1 function f(t, B, C, D) depends on the round.
constexpr std::uint32_t sha1_f(unsigned int t, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
  if (t < 20) {
    return (b & c) | ((~b) & d);  // Ch
  }
  if (t < 40) {
    return b ^ c ^ d;  // Parity
  }
  if (t < 60) {
    return (b & c) | (b & d) | (c & d);  // Maj
  }
  return b ^ c ^ d;  // Parity
}

// Get K constant for round t.
constexpr std::uint32_t sha1_k(unsigned int t) {
  if (t < 20) {
    return kSha1K[0];
  }
  if (t < 40) {
    return kSha1K[1];
  }
  if (t < 60) {
    return kSha1K[2];
  }
  return kSha1K[3];
}

// Process a 64-byte (512-bit) block.
void sha1_process_block(const std::uint8_t* block, std::uint32_t* h) {
  std::array<std::uint32_t, 80> w{};

  // Prepare message schedule.
  for (std::size_t t = 0; t < 16; ++t) {
    w[t] = (static_cast<std::uint32_t>(block[t * 4]) << 24) |
           (static_cast<std::uint32_t>(block[t * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[t * 4 + 2]) << 8) |
           static_cast<std::uint32_t>(block[t * 4 + 3]);
  }
  for (std::size_t t = 16; t < 80; ++t) {
    w[t] = rotl32(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
  }

  // Initialize working variables.
  std::uint32_t a = h[0];
  std::uint32_t b = h[1];
  std::uint32_t c = h[2];
  std::uint32_t d = h[3];
  std::uint32_t e = h[4];

  // Main loop.
  for (unsigned int t = 0; t < 80; ++t) {
    const std::uint32_t temp = rotl32(a, 5) + sha1_f(t, b, c, d) + e + sha1_k(t) + w[t];
    e = d;
    d = c;
    c = rotl32(b, 30);
    b = a;
    a = temp;
  }

  // Update hash values.
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

// Case-insensitive string comparison.
bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// Trim leading and trailing whitespace.
std::string_view trim(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())) != 0) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())) != 0) {
    sv.remove_suffix(1);
  }
  return sv;
}

}  // namespace

// ============================================================================
// Base64 encoding/decoding
// ============================================================================

std::string HttpHandshakeEmulator::base64_encode(std::span<const std::uint8_t> data) {
  std::string result;
  result.reserve((data.size() + 2) / 3 * 4);

  std::size_t i = 0;
  while (i + 2 < data.size()) {
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(data[i]) << 16) |
        (static_cast<std::uint32_t>(data[i + 1]) << 8) |
        static_cast<std::uint32_t>(data[i + 2]);

    result.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    result.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    result.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
    result.push_back(kBase64Alphabet[triple & 0x3F]);

    i += 3;
  }

  // Handle remaining bytes.
  if (i + 1 == data.size()) {
    // 1 remaining byte -> 2 base64 chars + "=="
    const std::uint32_t triple = static_cast<std::uint32_t>(data[i]) << 16;
    result.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    result.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    result.push_back('=');
    result.push_back('=');
  } else if (i + 2 == data.size()) {
    // 2 remaining bytes -> 3 base64 chars + "="
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(data[i]) << 16) |
        (static_cast<std::uint32_t>(data[i + 1]) << 8);
    result.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    result.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    result.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
    result.push_back('=');
  }

  return result;
}

std::vector<std::uint8_t> HttpHandshakeEmulator::base64_decode(std::string_view base64) {
  // Remove any whitespace and validate.
  std::string clean;
  clean.reserve(base64.size());
  for (char c : base64) {
    if (std::isspace(static_cast<unsigned char>(c)) == 0) {
      clean.push_back(c);
    }
  }

  if (clean.size() % 4 != 0) {
    return {};  // Invalid length
  }

  std::vector<std::uint8_t> result;
  result.reserve(clean.size() / 4 * 3);

  for (std::size_t i = 0; i < clean.size(); i += 4) {
    std::uint8_t a = kBase64DecodeTable[static_cast<std::uint8_t>(clean[i])];
    std::uint8_t b = kBase64DecodeTable[static_cast<std::uint8_t>(clean[i + 1])];
    std::uint8_t c = kBase64DecodeTable[static_cast<std::uint8_t>(clean[i + 2])];
    std::uint8_t d = kBase64DecodeTable[static_cast<std::uint8_t>(clean[i + 3])];

    // Check for invalid characters.
    if (a == 255 || b == 255 || (c == 255 && c != 64) || (d == 255 && d != 64)) {
      return {};
    }

    // Handle padding.
    const bool c_is_padding = (c == 64);
    const bool d_is_padding = (d == 64);

    if (c_is_padding) {
      c = 0;
    }
    if (d_is_padding) {
      d = 0;
    }

    const std::uint32_t triple = (static_cast<std::uint32_t>(a) << 18) |
                                  (static_cast<std::uint32_t>(b) << 12) |
                                  (static_cast<std::uint32_t>(c) << 6) |
                                  static_cast<std::uint32_t>(d);

    result.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFF));
    if (!c_is_padding) {
      result.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFF));
    }
    if (!d_is_padding) {
      result.push_back(static_cast<std::uint8_t>(triple & 0xFF));
    }
  }

  return result;
}

// ============================================================================
// SHA-1 implementation
// ============================================================================

std::array<std::uint8_t, kSha1HashSize> HttpHandshakeEmulator::sha1(
    std::span<const std::uint8_t> data) {
  // Initialize hash values.
  std::array<std::uint32_t, 5> h = {
      kSha1InitH[0], kSha1InitH[1], kSha1InitH[2], kSha1InitH[3], kSha1InitH[4]
  };

  // Process complete 64-byte blocks.
  const std::size_t full_blocks = data.size() / 64;
  for (std::size_t i = 0; i < full_blocks; ++i) {
    sha1_process_block(data.data() + i * 64, h.data());
  }

  // Prepare final padded block(s).
  const std::size_t remaining = data.size() % 64;
  std::array<std::uint8_t, 128> buffer{};  // Up to 2 blocks for padding

  // Copy remaining bytes.
  std::memcpy(buffer.data(), data.data() + full_blocks * 64, remaining);

  // Append '1' bit.
  buffer[remaining] = 0x80;

  // Determine if we need 1 or 2 blocks for padding.
  std::size_t padding_blocks = 1;
  if (remaining >= 56) {
    // Need 2 blocks: current block + another for length.
    padding_blocks = 2;
  }

  // Append length in bits (big-endian, 64-bit).
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8;
  const std::size_t length_offset = padding_blocks * 64 - 8;
  buffer[length_offset] = static_cast<std::uint8_t>(bit_len >> 56);
  buffer[length_offset + 1] = static_cast<std::uint8_t>(bit_len >> 48);
  buffer[length_offset + 2] = static_cast<std::uint8_t>(bit_len >> 40);
  buffer[length_offset + 3] = static_cast<std::uint8_t>(bit_len >> 32);
  buffer[length_offset + 4] = static_cast<std::uint8_t>(bit_len >> 24);
  buffer[length_offset + 5] = static_cast<std::uint8_t>(bit_len >> 16);
  buffer[length_offset + 6] = static_cast<std::uint8_t>(bit_len >> 8);
  buffer[length_offset + 7] = static_cast<std::uint8_t>(bit_len);

  // Process final block(s).
  for (std::size_t i = 0; i < padding_blocks; ++i) {
    sha1_process_block(buffer.data() + i * 64, h.data());
  }

  // Convert hash to bytes (big-endian).
  std::array<std::uint8_t, kSha1HashSize> result{};
  for (std::size_t i = 0; i < 5; ++i) {
    result[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
    result[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
    result[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
    result[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
  }

  return result;
}

std::array<std::uint8_t, kSha1HashSize> HttpHandshakeEmulator::sha1(std::string_view data) {
  return sha1(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

// ============================================================================
// WebSocket key generation and validation
// ============================================================================

std::string HttpHandshakeEmulator::generate_websocket_key() {
  // Generate 16 random bytes.
  auto random = crypto::random_bytes(kWebSocketKeyRawSize);

  // Base64 encode.
  return base64_encode(random);
}

std::string HttpHandshakeEmulator::compute_accept_key(std::string_view client_key) {
  // Concatenate client key with WebSocket GUID.
  std::string concat;
  concat.reserve(client_key.size() + kWebSocketGuid.size());
  concat.append(client_key);
  concat.append(kWebSocketGuid);

  // Compute SHA-1 hash.
  auto hash = sha1(concat);

  // Base64 encode the hash.
  return base64_encode(hash);
}

// ============================================================================
// HTTP message generation
// ============================================================================

std::pair<std::vector<std::uint8_t>, std::string>
HttpHandshakeEmulator::generate_upgrade_request(std::string_view path, std::string_view host) {
  // Generate Sec-WebSocket-Key.
  std::string key = generate_websocket_key();

  // Build HTTP request.
  std::string request;
  request.reserve(256);

  // Request line.
  request.append("GET ");
  request.append(path);
  request.append(" HTTP/1.1");
  request.append(kCRLF);

  // Headers.
  request.append("Host: ");
  request.append(host);
  request.append(kCRLF);

  request.append("Upgrade: websocket");
  request.append(kCRLF);

  request.append("Connection: Upgrade");
  request.append(kCRLF);

  request.append("Sec-WebSocket-Key: ");
  request.append(key);
  request.append(kCRLF);

  request.append("Sec-WebSocket-Version: 13");
  request.append(kCRLF);

  // Empty line to end headers.
  request.append(kCRLF);

  // Convert to bytes.
  std::vector<std::uint8_t> result(request.begin(), request.end());

  return {result, key};
}

std::vector<std::uint8_t> HttpHandshakeEmulator::generate_upgrade_response(
    std::string_view client_key) {
  // Compute accept key.
  std::string accept_key = compute_accept_key(client_key);

  // Build HTTP response.
  std::string response;
  response.reserve(256);

  // Status line.
  response.append("HTTP/1.1 101 Switching Protocols");
  response.append(kCRLF);

  // Headers.
  response.append("Upgrade: websocket");
  response.append(kCRLF);

  response.append("Connection: Upgrade");
  response.append(kCRLF);

  response.append("Sec-WebSocket-Accept: ");
  response.append(accept_key);
  response.append(kCRLF);

  // Empty line to end headers.
  response.append(kCRLF);

  // Convert to bytes.
  return std::vector<std::uint8_t>(response.begin(), response.end());
}

// ============================================================================
// HTTP message parsing
// ============================================================================

std::optional<std::string> HttpHandshakeEmulator::find_header(std::string_view headers,
                                                                std::string_view header_name) {
  // Search for header (case-insensitive).
  std::size_t pos = 0;
  while (pos < headers.size()) {
    // Find end of line.
    auto eol = headers.find(kCRLF, pos);
    if (eol == std::string_view::npos) {
      eol = headers.size();
    }

    std::string_view line = headers.substr(pos, eol - pos);

    // Check if this is the header we're looking for.
    auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      std::string_view name = line.substr(0, colon);
      if (iequals(trim(name), header_name)) {
        // Found it. Return the value.
        std::string_view value = line.substr(colon + 1);
        return std::string(trim(value));
      }
    }

    pos = eol + kCRLF.size();
    if (pos >= headers.size()) {
      break;
    }
  }

  return std::nullopt;
}

std::optional<int> HttpHandshakeEmulator::parse_status_code(std::string_view first_line) {
  // HTTP/1.1 101 Switching Protocols
  // Find first space.
  auto space1 = first_line.find(' ');
  if (space1 == std::string_view::npos) {
    return std::nullopt;
  }

  // Find second space.
  auto space2 = first_line.find(' ', space1 + 1);
  if (space2 == std::string_view::npos) {
    space2 = first_line.size();
  }

  // Extract status code.
  std::string_view code_str = first_line.substr(space1 + 1, space2 - space1 - 1);
  if (code_str.empty()) {
    return std::nullopt;
  }

  // Parse integer.
  int code = 0;
  for (char c : code_str) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    code = code * 10 + (c - '0');
  }

  return code;
}

std::optional<UpgradeRequest> HttpHandshakeEmulator::parse_upgrade_request(
    std::span<const std::uint8_t> request) {
  // Convert to string for parsing.
  std::string_view data(reinterpret_cast<const char*>(request.data()), request.size());

  // Find end of headers.
  auto header_end = data.find(kHeaderTerminator);
  if (header_end == std::string_view::npos) {
    return std::nullopt;  // Incomplete headers
  }

  // Parse request line.
  auto first_line_end = data.find(kCRLF);
  if (first_line_end == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view first_line = data.substr(0, first_line_end);

  // Validate it starts with "GET "
  if (!first_line.starts_with("GET ")) {
    return std::nullopt;
  }

  // Extract path.
  constexpr std::size_t path_start = 4;  // After "GET "
  auto path_end = first_line.find(' ', path_start);
  if (path_end == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view headers = data.substr(first_line_end + kCRLF.size(), header_end - first_line_end);

  // Check required WebSocket upgrade headers.
  auto upgrade = find_header(headers, "Upgrade");
  if (!upgrade || !iequals(*upgrade, "websocket")) {
    return std::nullopt;
  }

  auto connection = find_header(headers, "Connection");
  // Connection can be "Upgrade" or "keep-alive, Upgrade", etc.
  if (!connection) {
    return std::nullopt;
  }
  // Check that "Upgrade" is present in the Connection header.
  bool has_upgrade = false;
  std::string_view conn_view = *connection;
  std::size_t pos = 0;
  while (pos < conn_view.size()) {
    auto comma = conn_view.find(',', pos);
    if (comma == std::string_view::npos) {
      comma = conn_view.size();
    }
    if (iequals(trim(conn_view.substr(pos, comma - pos)), "Upgrade")) {
      has_upgrade = true;
      break;
    }
    pos = comma + 1;
  }
  if (!has_upgrade) {
    return std::nullopt;
  }

  auto sec_key = find_header(headers, "Sec-WebSocket-Key");
  if (!sec_key || sec_key->empty()) {
    return std::nullopt;
  }

  auto sec_version = find_header(headers, "Sec-WebSocket-Version");
  if (!sec_version || *sec_version != "13") {
    return std::nullopt;
  }

  // Build result.
  UpgradeRequest result;
  result.path = std::string(first_line.substr(path_start, path_end - path_start));
  result.sec_websocket_key = *sec_key;
  result.sec_websocket_version = *sec_version;

  if (auto host = find_header(headers, "Host")) {
    result.host = *host;
  }
  if (auto origin = find_header(headers, "Origin")) {
    result.origin = *origin;
  }

  return result;
}

std::optional<UpgradeResponse> HttpHandshakeEmulator::parse_upgrade_response(
    std::span<const std::uint8_t> response) {
  // Convert to string for parsing.
  std::string_view data(reinterpret_cast<const char*>(response.data()), response.size());

  // Find end of headers.
  auto header_end = data.find(kHeaderTerminator);
  if (header_end == std::string_view::npos) {
    return std::nullopt;  // Incomplete headers
  }

  // Parse status line.
  auto first_line_end = data.find(kCRLF);
  if (first_line_end == std::string_view::npos) {
    return std::nullopt;
  }

  std::string_view first_line = data.substr(0, first_line_end);

  auto status_code = parse_status_code(first_line);
  if (!status_code) {
    return std::nullopt;
  }

  std::string_view headers = data.substr(first_line_end + kCRLF.size(), header_end - first_line_end);

  // For 101 response, check WebSocket headers.
  UpgradeResponse result;
  result.status_code = *status_code;

  if (*status_code == 101) {
    auto accept = find_header(headers, "Sec-WebSocket-Accept");
    if (accept) {
      result.sec_websocket_accept = *accept;
    }
  }

  return result;
}

bool HttpHandshakeEmulator::validate_upgrade_response(std::span<const std::uint8_t> response,
                                                        std::string_view client_key) {
  auto parsed = parse_upgrade_response(response);
  if (!parsed) {
    return false;
  }

  // Check status code is 101.
  if (parsed->status_code != 101) {
    return false;
  }

  // Check Sec-WebSocket-Accept is correct.
  std::string expected_accept = compute_accept_key(client_key);
  return parsed->sec_websocket_accept == expected_accept;
}

}  // namespace veil::protocol_wrapper
