#include "common/handshake/handshake_processor.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "common/crypto/random.h"
namespace {
// Internal magic bytes used inside encrypted payload (not visible to DPI)
constexpr std::array<std::uint8_t, 2> kMagic{'H', 'S'};
constexpr std::uint8_t kVersion = 1;

// AEAD tag size for ChaCha20-Poly1305
constexpr std::size_t kAeadTagLen = crypto_aead_chacha20poly1305_ietf_ABYTES;  // 16 bytes

// Label for deriving handshake encryption key from PSK
constexpr std::array<std::uint8_t, 24> kHandshakeKeyLabel{
    'V', 'E', 'I', 'L', '-', 'H', 'A', 'N', 'D', 'S', 'H', 'A',
    'K', 'E', '-', 'O', 'B', 'F', 'U', 'S', 'C', 'A', 'T', 'E'};

// Handshake padding configuration (DPI resistance)
constexpr std::uint16_t kMinPaddingSize = 32;   // Minimum padding bytes
constexpr std::uint16_t kMaxPaddingSize = 400;  // Maximum padding bytes

// Derive a key for handshake packet obfuscation from PSK
std::array<std::uint8_t, veil::crypto::kAeadKeyLen> derive_handshake_key(
    std::span<const std::uint8_t> psk) {
  // Use HKDF to derive handshake encryption key
  auto prk = veil::crypto::hkdf_extract({}, psk);  // empty salt
  auto key_material = veil::crypto::hkdf_expand(prk, kHandshakeKeyLabel, veil::crypto::kAeadKeyLen);

  // SECURITY: Clear PRK after use
  sodium_memzero(prk.data(), prk.size());

  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key{};
  std::copy_n(key_material.begin(), key.size(), key.begin());

  // SECURITY: Clear key material
  sodium_memzero(key_material.data(), key_material.size());

  return key;
}

// Encrypt a handshake packet using AEAD (nonce prepended to output)
std::vector<std::uint8_t> encrypt_handshake_packet(
    std::span<const std::uint8_t, veil::crypto::kAeadKeyLen> key,
    std::span<const std::uint8_t> plaintext) {
  // Generate random nonce
  auto nonce_bytes = veil::crypto::random_bytes(veil::crypto::kNonceLen);
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};
  std::copy_n(nonce_bytes.begin(), nonce.size(), nonce.begin());

  // Encrypt with empty AAD
  auto ciphertext = veil::crypto::aead_encrypt(key, nonce, {}, plaintext);

  // Prepend nonce to ciphertext
  std::vector<std::uint8_t> result;
  result.reserve(nonce.size() + ciphertext.size());
  result.insert(result.end(), nonce.begin(), nonce.end());
  result.insert(result.end(), ciphertext.begin(), ciphertext.end());

  return result;
}

// Decrypt a handshake packet (nonce is at the beginning)
std::optional<std::vector<std::uint8_t>> decrypt_handshake_packet(
    std::span<const std::uint8_t, veil::crypto::kAeadKeyLen> key,
    std::span<const std::uint8_t> encrypted) {
  if (encrypted.size() < veil::crypto::kNonceLen + kAeadTagLen) {
    return std::nullopt;
  }

  // Extract nonce
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};
  std::copy_n(encrypted.begin(), nonce.size(), nonce.begin());

  // Extract ciphertext
  auto ciphertext = encrypted.subspan(veil::crypto::kNonceLen);

  // Decrypt with empty AAD
  return veil::crypto::aead_decrypt(key, nonce, {}, ciphertext);
}

// Compute random padding size for handshake packets (DPI resistance)
std::uint16_t compute_random_padding_size() {
  if (kMinPaddingSize >= kMaxPaddingSize) {
    return kMinPaddingSize;
  }
  const auto random_val = veil::crypto::random_uint64();
  const auto range = static_cast<std::uint64_t>(kMaxPaddingSize) -
                     static_cast<std::uint64_t>(kMinPaddingSize) + 1U;
  return static_cast<std::uint16_t>(kMinPaddingSize + (random_val % range));
}

std::uint64_t to_millis(std::chrono::system_clock::time_point tp) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | data[offset + static_cast<std::size_t>(i)];
  }
  return value;
}

std::vector<std::uint8_t> build_hmac_payload(std::uint8_t type, std::uint64_t init_ts,
                                             std::uint64_t resp_ts, std::uint64_t session_id,
                                             std::span<const std::uint8_t, 32> init_pub,
                                             std::span<const std::uint8_t, 32> resp_pub) {
  std::vector<std::uint8_t> payload;
  payload.reserve(1 + 1 + 8 + 8 + 8 + init_pub.size() + resp_pub.size());
  payload.insert(payload.end(), kMagic.begin(), kMagic.end());
  payload.push_back(kVersion);
  payload.push_back(type);
  write_u64(payload, init_ts);
  write_u64(payload, resp_ts);
  write_u64(payload, session_id);
  payload.insert(payload.end(), init_pub.begin(), init_pub.end());
  payload.insert(payload.end(), resp_pub.begin(), resp_pub.end());
  return payload;
}

std::vector<std::uint8_t> build_init_hmac_payload(std::uint64_t ts,
                                                  std::span<const std::uint8_t, 32> pub) {
  std::vector<std::uint8_t> payload;
  payload.reserve(1 + 1 + 8 + pub.size());
  payload.insert(payload.end(), kMagic.begin(), kMagic.end());
  payload.push_back(kVersion);
  payload.push_back(static_cast<std::uint8_t>(veil::handshake::MessageType::kInit));
  write_u64(payload, ts);
  payload.insert(payload.end(), pub.begin(), pub.end());
  return payload;
}

std::vector<std::uint8_t> derive_info(std::span<const std::uint8_t, 32> init_pub,
                                      std::span<const std::uint8_t, 32> resp_pub) {
  std::vector<std::uint8_t> info;
  const std::array<std::uint8_t, 8> label{'V', 'E', 'I', 'L', 'H', 'S', '1', 0};
  info.insert(info.end(), label.begin(), label.end());
  info.insert(info.end(), init_pub.begin(), init_pub.end());
  info.insert(info.end(), resp_pub.begin(), resp_pub.end());
  return info;
}

bool timestamp_valid(std::uint64_t remote_ts, std::chrono::milliseconds skew,
                     const std::function<std::chrono::system_clock::time_point()>& now_fn) {
  const auto now_ms = to_millis(now_fn());
  const auto diff = (remote_ts > now_ms) ? (remote_ts - now_ms) : (now_ms - remote_ts);
  return diff <= static_cast<std::uint64_t>(skew.count());
}

}  // namespace

namespace veil::handshake {

HandshakeInitiator::HandshakeInitiator(std::vector<std::uint8_t> psk,
                                       std::chrono::milliseconds skew_tolerance,
                                       std::function<Clock::time_point()> now_fn)
    : psk_(std::move(psk)), skew_tolerance_(skew_tolerance), now_fn_(std::move(now_fn)) {
  if (psk_.empty()) {
    throw std::invalid_argument("psk required");
  }
}

HandshakeInitiator::HandshakeInitiator(std::vector<std::uint8_t> psk, std::string client_id,
                                       std::chrono::milliseconds skew_tolerance,
                                       std::function<Clock::time_point()> now_fn)
    : psk_(std::move(psk)),
      client_id_(std::move(client_id)),
      skew_tolerance_(skew_tolerance),
      now_fn_(std::move(now_fn)) {
  if (psk_.empty()) {
    throw std::invalid_argument("psk required");
  }
  if (client_id_.size() > kMaxHandshakeClientIdLength) {
    throw std::invalid_argument("client_id too long");
  }
}

HandshakeInitiator::~HandshakeInitiator() {
  // SECURITY: Clear all sensitive key material on destruction
  if (!psk_.empty()) {
    sodium_memzero(psk_.data(), psk_.size());
  }
  sodium_memzero(ephemeral_.secret_key.data(), ephemeral_.secret_key.size());
  sodium_memzero(ephemeral_.public_key.data(), ephemeral_.public_key.size());
}

std::vector<std::uint8_t> HandshakeInitiator::create_init() {
  ephemeral_ = crypto::generate_x25519_keypair();
  init_timestamp_ms_ = to_millis(now_fn_());
  init_sent_ = true;

  auto hmac_payload = build_init_hmac_payload(init_timestamp_ms_, ephemeral_.public_key);
  const auto mac = crypto::hmac_sha256(psk_, hmac_payload);

  // Generate random padding for DPI resistance
  const auto padding_size = compute_random_padding_size();
  const auto padding = veil::crypto::random_bytes(padding_size);

  // Build plaintext handshake packet (internal format with magic bytes + padding)
  std::vector<std::uint8_t> plaintext;
  plaintext.reserve(kMagic.size() + 1 + 1 + 8 + ephemeral_.public_key.size() + mac.size() + 2 + padding_size);
  plaintext.insert(plaintext.end(), kMagic.begin(), kMagic.end());
  plaintext.push_back(kVersion);
  plaintext.push_back(static_cast<std::uint8_t>(MessageType::kInit));
  write_u64(plaintext, init_timestamp_ms_);
  plaintext.insert(plaintext.end(), ephemeral_.public_key.begin(), ephemeral_.public_key.end());
  plaintext.insert(plaintext.end(), mac.begin(), mac.end());

  // Append padding length (2 bytes, big-endian)
  plaintext.push_back(static_cast<std::uint8_t>((padding_size >> 8) & 0xFF));
  plaintext.push_back(static_cast<std::uint8_t>(padding_size & 0xFF));

  // Append random padding
  plaintext.insert(plaintext.end(), padding.begin(), padding.end());

  // Derive handshake encryption key and encrypt the packet
  // Result: [12-byte nonce][encrypted payload + 16-byte AEAD tag]
  // This eliminates plaintext magic bytes - first bytes are random nonce
  auto handshake_key = derive_handshake_key(psk_);
  auto encrypted = encrypt_handshake_packet(handshake_key, plaintext);

  // SECURITY: Clear handshake key after use
  sodium_memzero(handshake_key.data(), handshake_key.size());

  return encrypted;
}

std::optional<HandshakeSession> HandshakeInitiator::consume_response(
    std::span<const std::uint8_t> response) {
  if (!init_sent_) {
    return std::nullopt;
  }

  // Decrypt the response first
  auto handshake_key = derive_handshake_key(psk_);
  auto decrypted = decrypt_handshake_packet(handshake_key, response);

  // SECURITY: Clear handshake key after use
  sodium_memzero(handshake_key.data(), handshake_key.size());

  if (!decrypted.has_value()) {
    return std::nullopt;
  }

  const auto& plaintext = *decrypted;

  // Minimum size: header + fields + padding_length (2 bytes)
  const std::size_t min_size = kMagic.size() + 1 + 1 + 8 + 8 + 8 + 32 + 32 + 2;
  if (plaintext.size() < min_size) {
    return std::nullopt;
  }
  // Maximum size with maximum padding
  const std::size_t max_size = min_size + kMaxPaddingSize;
  if (plaintext.size() > max_size) {
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), plaintext.begin())) {
    return std::nullopt;
  }
  if (plaintext[2] != kVersion || plaintext[3] != static_cast<std::uint8_t>(MessageType::kResponse)) {
    return std::nullopt;
  }
  const auto init_ts = read_u64(plaintext, 4);
  const auto resp_ts = read_u64(plaintext, 12);
  const auto session_id = read_u64(plaintext, 20);
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> responder_pub{};
  std::copy_n(plaintext.begin() + 28, responder_pub.size(), responder_pub.begin());
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> init_pub{};
  std::copy(ephemeral_.public_key.begin(), ephemeral_.public_key.end(), init_pub.begin());

  if (init_ts != init_timestamp_ms_) {
    return std::nullopt;
  }
  if (!timestamp_valid(resp_ts, skew_tolerance_, now_fn_)) {
    return std::nullopt;
  }

  const auto hmac_offset = 28 + responder_pub.size();
  std::array<std::uint8_t, crypto::kHmacSha256Len> provided_mac{};
  std::copy_n(plaintext.begin() + static_cast<std::ptrdiff_t>(hmac_offset), crypto::kHmacSha256Len, provided_mac.begin());

  const auto hmac_payload =
      build_hmac_payload(static_cast<std::uint8_t>(MessageType::kResponse), init_ts, resp_ts,
                         session_id, init_pub, responder_pub);
  const auto expected_mac = crypto::hmac_sha256(psk_, hmac_payload);
  if (!std::equal(expected_mac.begin(), expected_mac.end(), provided_mac.begin())) {
    return std::nullopt;
  }

  // Validate padding length field (after HMAC)
  const auto padding_len_offset = hmac_offset + crypto::kHmacSha256Len;
  if (plaintext.size() < padding_len_offset + 2) {
    return std::nullopt;
  }
  const auto padding_len = static_cast<std::uint16_t>(
      (plaintext[padding_len_offset] << 8) | plaintext[padding_len_offset + 1]);

  // Validate padding length is within allowed range
  if (padding_len < kMinPaddingSize || padding_len > kMaxPaddingSize) {
    return std::nullopt;
  }

  // Validate total packet size matches expected size with padding
  const std::size_t expected_total_size = padding_len_offset + 2 + padding_len;
  if (plaintext.size() != expected_total_size) {
    return std::nullopt;
  }

  auto shared = crypto::compute_shared_secret(ephemeral_.secret_key, responder_pub);
  const auto info = derive_info(init_pub, responder_pub);
  const auto keys = crypto::derive_session_keys(shared, psk_, info, true);

  // SECURITY: Clear shared secret immediately after key derivation
  sodium_memzero(shared.data(), shared.size());

  // SECURITY: Clear ephemeral private key after ECDH computation
  sodium_memzero(ephemeral_.secret_key.data(), ephemeral_.secret_key.size());

  HandshakeSession session{
      .session_id = session_id,
      .keys = keys,
      .initiator_ephemeral = init_pub,
      .responder_ephemeral = responder_pub,
      .client_id = client_id_,  // Issue #87: Include client_id in session
  };
  return session;
}

HandshakeResponder::HandshakeResponder(std::vector<std::uint8_t> psk,
                                       std::chrono::milliseconds skew_tolerance,
                                       utils::TokenBucket rate_limiter,
                                       std::function<Clock::time_point()> now_fn)
    : psk_(std::move(psk)),
      skew_tolerance_(skew_tolerance),
      rate_limiter_(std::move(rate_limiter)),
      now_fn_(std::move(now_fn)) {
  if (psk_.empty()) {
    throw std::invalid_argument("psk required");
  }
}

HandshakeResponder::~HandshakeResponder() {
  // SECURITY: Clear PSK on destruction
  if (!psk_.empty()) {
    sodium_memzero(psk_.data(), psk_.size());
  }
}

std::optional<HandshakeResponder::Result> HandshakeResponder::handle_init(
    std::span<const std::uint8_t> init_bytes) {
  // Rate limit before attempting decryption (prevents DoS via decrypt operations)
  if (!rate_limiter_.allow()) {
    return std::nullopt;
  }

  // Derive handshake key and attempt decryption
  auto handshake_key = derive_handshake_key(psk_);
  auto decrypted = decrypt_handshake_packet(handshake_key, init_bytes);

  if (!decrypted.has_value()) {
    // SECURITY: Clear handshake key even on failure
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  const auto& plaintext = *decrypted;

  // Minimum size: header + fields + HMAC + padding_length (2 bytes)
  constexpr std::size_t min_init_size =
      kMagic.size() + 1 + 1 + 8 + crypto::kX25519PublicKeySize + crypto::kHmacSha256Len + 2;
  if (plaintext.size() < min_init_size) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  // Maximum size with maximum padding
  const std::size_t max_init_size = min_init_size + kMaxPaddingSize;
  if (plaintext.size() > max_init_size) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), plaintext.begin())) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  if (plaintext[2] != kVersion || plaintext[3] != static_cast<std::uint8_t>(MessageType::kInit)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  const auto init_ts = read_u64(plaintext, 4);
  if (!timestamp_valid(init_ts, skew_tolerance_, now_fn_)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> init_pub{};
  std::copy_n(plaintext.begin() + 12, init_pub.size(), init_pub.begin());

  // Check replay cache BEFORE validating HMAC (anti-probing requirement)
  // If this (timestamp, ephemeral_key) pair was seen before, silently drop
  if (replay_cache_.mark_and_check(init_ts, init_pub)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;  // Replay detected - silently ignore
  }

  // Extract HMAC (32 bytes after the ephemeral public key)
  const auto mac_offset = 12 + init_pub.size();
  std::array<std::uint8_t, crypto::kHmacSha256Len> provided_mac{};
  std::copy_n(plaintext.begin() + static_cast<std::ptrdiff_t>(mac_offset), crypto::kHmacSha256Len, provided_mac.begin());

  const auto hmac_payload = build_init_hmac_payload(init_ts, init_pub);
  const auto expected_mac = crypto::hmac_sha256(psk_, hmac_payload);
  if (!std::equal(expected_mac.begin(), expected_mac.end(), provided_mac.begin())) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Validate padding length field (after HMAC)
  const auto padding_len_offset = mac_offset + crypto::kHmacSha256Len;
  if (plaintext.size() < padding_len_offset + 2) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  const auto padding_len = static_cast<std::uint16_t>(
      (plaintext[padding_len_offset] << 8) | plaintext[padding_len_offset + 1]);

  // Validate padding length is within allowed range
  if (padding_len < kMinPaddingSize || padding_len > kMaxPaddingSize) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Validate total packet size matches expected size with padding
  const std::size_t expected_total_size = padding_len_offset + 2 + padding_len;
  if (plaintext.size() != expected_total_size) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  auto responder_keys = crypto::generate_x25519_keypair();
  auto shared = crypto::compute_shared_secret(responder_keys.secret_key, init_pub);
  const auto info = derive_info(init_pub, responder_keys.public_key);
  const auto session_keys = crypto::derive_session_keys(shared, psk_, info, false);

  // SECURITY: Clear shared secret immediately after key derivation
  sodium_memzero(shared.data(), shared.size());

  // SECURITY: Clear responder's ephemeral private key after ECDH computation
  sodium_memzero(responder_keys.secret_key.data(), responder_keys.secret_key.size());

  const auto session_id = veil::crypto::random_uint64();
  const auto resp_ts = to_millis(now_fn_());

  auto hmac_payload_resp = build_hmac_payload(static_cast<std::uint8_t>(MessageType::kResponse),
                                              init_ts, resp_ts, session_id, init_pub,
                                              responder_keys.public_key);
  const auto mac = crypto::hmac_sha256(psk_, hmac_payload_resp);

  // Generate random padding for DPI resistance
  const auto padding_size = compute_random_padding_size();
  const auto padding = veil::crypto::random_bytes(padding_size);

  // Build plaintext response
  std::vector<std::uint8_t> response_plaintext;
  response_plaintext.reserve(kMagic.size() + 1 + 1 + 8 + 8 + 8 + responder_keys.public_key.size() + mac.size() + 2 + padding_size);
  response_plaintext.insert(response_plaintext.end(), kMagic.begin(), kMagic.end());
  response_plaintext.push_back(kVersion);
  response_plaintext.push_back(static_cast<std::uint8_t>(MessageType::kResponse));
  write_u64(response_plaintext, init_ts);
  write_u64(response_plaintext, resp_ts);
  write_u64(response_plaintext, session_id);
  response_plaintext.insert(response_plaintext.end(), responder_keys.public_key.begin(),
                            responder_keys.public_key.end());
  response_plaintext.insert(response_plaintext.end(), mac.begin(), mac.end());

  // Append padding length (2 bytes, big-endian)
  response_plaintext.push_back(static_cast<std::uint8_t>((padding_size >> 8) & 0xFF));
  response_plaintext.push_back(static_cast<std::uint8_t>(padding_size & 0xFF));

  // Append random padding
  response_plaintext.insert(response_plaintext.end(), padding.begin(), padding.end());

  // Encrypt the response to hide magic bytes from DPI
  auto encrypted_response = encrypt_handshake_packet(handshake_key, response_plaintext);

  // SECURITY: Clear handshake key after use
  sodium_memzero(handshake_key.data(), handshake_key.size());

  HandshakeSession session{
      .session_id = session_id,
      .keys = session_keys,
      .initiator_ephemeral = init_pub,
      .responder_ephemeral = responder_keys.public_key,
      .client_id = {},  // No client_id for single-PSK responder
  };

  return Result{.response = std::move(encrypted_response), .session = session};
}

// =============================================================================
// MultiClientHandshakeResponder Implementation (Issue #87)
// =============================================================================

MultiClientHandshakeResponder::MultiClientHandshakeResponder(
    std::shared_ptr<auth::ClientRegistry> registry, std::chrono::milliseconds skew_tolerance,
    utils::TokenBucket rate_limiter, std::function<Clock::time_point()> now_fn)
    : registry_(std::move(registry)),
      skew_tolerance_(skew_tolerance),
      rate_limiter_(std::move(rate_limiter)),
      now_fn_(std::move(now_fn)) {
  if (!registry_) {
    throw std::invalid_argument("registry required");
  }
}

// Registry is a shared_ptr, it will be cleaned up automatically
MultiClientHandshakeResponder::~MultiClientHandshakeResponder() = default;

std::optional<MultiClientHandshakeResponder::Result> MultiClientHandshakeResponder::handle_init(
    std::span<const std::uint8_t> init_bytes) {
  // Rate limit before attempting decryption (prevents DoS via decrypt operations)
  if (!rate_limiter_.allow()) {
    return std::nullopt;
  }

  // Trial decryption: Try each PSK until one succeeds
  // This is necessary because the client_id cannot be sent in plaintext
  // (would reveal client identity to eavesdroppers)

  // First, try all registered client PSKs
  auto client_psks = registry_->get_all_enabled_psks();
  for (const auto& [client_id, psk] : client_psks) {
    auto handshake_key = derive_handshake_key(psk);
    auto decrypted = decrypt_handshake_packet(handshake_key, init_bytes);

    if (decrypted.has_value()) {
      auto result = process_decrypted_init(*decrypted, handshake_key, psk, client_id);
      sodium_memzero(handshake_key.data(), handshake_key.size());
      if (result.has_value()) {
        return result;
      }
      // Decryption succeeded but validation failed - don't try other PSKs
      // (The packet was encrypted with this PSK but is invalid)
      return std::nullopt;
    }
    sodium_memzero(handshake_key.data(), handshake_key.size());
  }

  // Try fallback PSK if available
  auto fallback_psk = registry_->get_fallback_psk();
  if (fallback_psk.has_value()) {
    auto handshake_key = derive_handshake_key(*fallback_psk);
    auto decrypted = decrypt_handshake_packet(handshake_key, init_bytes);

    if (decrypted.has_value()) {
      auto result = process_decrypted_init(*decrypted, handshake_key, *fallback_psk, "");
      sodium_memzero(handshake_key.data(), handshake_key.size());
      return result;
    }
    sodium_memzero(handshake_key.data(), handshake_key.size());
  }

  // No PSK matched
  return std::nullopt;
}

std::optional<MultiClientHandshakeResponder::Result>
MultiClientHandshakeResponder::process_decrypted_init(
    const std::vector<std::uint8_t>& plaintext,
    std::span<const std::uint8_t, crypto::kAeadKeyLen> handshake_key,
    const std::vector<std::uint8_t>& psk,
    const std::string& client_id) {
  // Minimum size: header + fields + HMAC + padding_length (2 bytes)
  constexpr std::size_t min_init_size =
      kMagic.size() + 1 + 1 + 8 + crypto::kX25519PublicKeySize + crypto::kHmacSha256Len + 2;
  if (plaintext.size() < min_init_size) {
    return std::nullopt;
  }
  // Maximum size with maximum padding
  const std::size_t max_init_size = min_init_size + kMaxPaddingSize;
  if (plaintext.size() > max_init_size) {
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), plaintext.begin())) {
    return std::nullopt;
  }
  if (plaintext[2] != kVersion || plaintext[3] != static_cast<std::uint8_t>(MessageType::kInit)) {
    return std::nullopt;
  }
  const auto init_ts = read_u64(plaintext, 4);
  if (!timestamp_valid(init_ts, skew_tolerance_, now_fn_)) {
    return std::nullopt;
  }
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> init_pub{};
  std::copy_n(plaintext.begin() + 12, init_pub.size(), init_pub.begin());

  // Check replay cache BEFORE validating HMAC (anti-probing requirement)
  if (replay_cache_.mark_and_check(init_ts, init_pub)) {
    return std::nullopt;  // Replay detected
  }

  // Extract HMAC (32 bytes after the ephemeral public key)
  const auto mac_offset = 12 + init_pub.size();
  std::array<std::uint8_t, crypto::kHmacSha256Len> provided_mac{};
  std::copy_n(plaintext.begin() + static_cast<std::ptrdiff_t>(mac_offset), crypto::kHmacSha256Len,
              provided_mac.begin());

  const auto hmac_payload = build_init_hmac_payload(init_ts, init_pub);
  const auto expected_mac = crypto::hmac_sha256(psk, hmac_payload);
  if (!std::equal(expected_mac.begin(), expected_mac.end(), provided_mac.begin())) {
    return std::nullopt;
  }

  // Validate padding length field (after HMAC)
  const auto padding_len_offset = mac_offset + crypto::kHmacSha256Len;
  if (plaintext.size() < padding_len_offset + 2) {
    return std::nullopt;
  }
  const auto padding_len = static_cast<std::uint16_t>((plaintext[padding_len_offset] << 8) |
                                                       plaintext[padding_len_offset + 1]);

  // Validate padding length is within allowed range
  if (padding_len < kMinPaddingSize || padding_len > kMaxPaddingSize) {
    return std::nullopt;
  }

  // Validate total packet size matches expected size with padding
  const std::size_t expected_total_size = padding_len_offset + 2 + padding_len;
  if (plaintext.size() != expected_total_size) {
    return std::nullopt;
  }

  auto responder_keys = crypto::generate_x25519_keypair();
  auto shared = crypto::compute_shared_secret(responder_keys.secret_key, init_pub);
  const auto info = derive_info(init_pub, responder_keys.public_key);
  const auto session_keys = crypto::derive_session_keys(shared, psk, info, false);

  // SECURITY: Clear shared secret immediately after key derivation
  sodium_memzero(shared.data(), shared.size());

  // SECURITY: Clear responder's ephemeral private key after ECDH computation
  sodium_memzero(responder_keys.secret_key.data(), responder_keys.secret_key.size());

  const auto session_id = veil::crypto::random_uint64();
  const auto resp_ts = to_millis(now_fn_());

  auto hmac_payload_resp = build_hmac_payload(static_cast<std::uint8_t>(MessageType::kResponse),
                                              init_ts, resp_ts, session_id, init_pub,
                                              responder_keys.public_key);
  const auto mac = crypto::hmac_sha256(psk, hmac_payload_resp);

  // Generate random padding for DPI resistance
  const auto padding_size = compute_random_padding_size();
  const auto padding = veil::crypto::random_bytes(padding_size);

  // Build plaintext response
  std::vector<std::uint8_t> response_plaintext;
  response_plaintext.reserve(kMagic.size() + 1 + 1 + 8 + 8 + 8 +
                             responder_keys.public_key.size() + mac.size() + 2 + padding_size);
  response_plaintext.insert(response_plaintext.end(), kMagic.begin(), kMagic.end());
  response_plaintext.push_back(kVersion);
  response_plaintext.push_back(static_cast<std::uint8_t>(MessageType::kResponse));
  write_u64(response_plaintext, init_ts);
  write_u64(response_plaintext, resp_ts);
  write_u64(response_plaintext, session_id);
  response_plaintext.insert(response_plaintext.end(), responder_keys.public_key.begin(),
                            responder_keys.public_key.end());
  response_plaintext.insert(response_plaintext.end(), mac.begin(), mac.end());

  // Append padding length (2 bytes, big-endian)
  response_plaintext.push_back(static_cast<std::uint8_t>((padding_size >> 8) & 0xFF));
  response_plaintext.push_back(static_cast<std::uint8_t>(padding_size & 0xFF));

  // Append random padding
  response_plaintext.insert(response_plaintext.end(), padding.begin(), padding.end());

  // Encrypt the response
  auto encrypted_response = encrypt_handshake_packet(handshake_key, response_plaintext);

  HandshakeSession session{
      .session_id = session_id,
      .keys = session_keys,
      .initiator_ephemeral = init_pub,
      .responder_ephemeral = responder_keys.public_key,
      .client_id = client_id,  // Issue #87: Include authenticated client_id
  };

  return Result{.response = std::move(encrypted_response), .session = session};
}

// =============================================================================
// ZeroRttInitiator Implementation (Issue #86)
// =============================================================================

ZeroRttInitiator::ZeroRttInitiator(std::vector<std::uint8_t> psk, SessionTicket ticket,
                                   std::function<Clock::time_point()> now_fn)
    : psk_(std::move(psk)),
      ticket_(std::move(ticket)),
      now_fn_(std::move(now_fn)) {
  if (psk_.empty()) {
    throw std::invalid_argument("psk required");
  }
  if (ticket_.ticket_data.empty()) {
    throw std::invalid_argument("ticket_data required");
  }
}

ZeroRttInitiator::~ZeroRttInitiator() {
  // SECURITY: Clear all sensitive key material
  if (!psk_.empty()) {
    sodium_memzero(psk_.data(), psk_.size());
  }
  sodium_memzero(ephemeral_.secret_key.data(), ephemeral_.secret_key.size());
  sodium_memzero(ephemeral_.public_key.data(), ephemeral_.public_key.size());
  sodium_memzero(anti_replay_nonce_.data(), anti_replay_nonce_.size());
  sodium_memzero(ticket_.cached_keys.send_key.data(), ticket_.cached_keys.send_key.size());
  sodium_memzero(ticket_.cached_keys.recv_key.data(), ticket_.cached_keys.recv_key.size());
}

std::vector<std::uint8_t> ZeroRttInitiator::create_zero_rtt_init() {
  ephemeral_ = crypto::generate_x25519_keypair();
  init_timestamp_ms_ = to_millis(now_fn_());
  init_sent_ = true;

  // Generate anti-replay nonce
  auto nonce_bytes = crypto::random_bytes(kAntiReplayNonceSize);
  std::copy_n(nonce_bytes.begin(), kAntiReplayNonceSize, anti_replay_nonce_.begin());

  // Build HMAC payload for 0-RTT INIT
  // Includes: magic, version, type, timestamp, ephemeral_pub, anti_replay_nonce
  // Note: ticket_data is not included — it is separately authenticated via AEAD (server-only key)
  std::vector<std::uint8_t> hmac_payload;
  hmac_payload.reserve(kMagic.size() + 1 + 1 + 8 + ephemeral_.public_key.size() + kAntiReplayNonceSize);
  hmac_payload.insert(hmac_payload.end(), kMagic.begin(), kMagic.end());
  hmac_payload.push_back(kVersion);
  hmac_payload.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttInit));
  write_u64(hmac_payload, init_timestamp_ms_);
  hmac_payload.insert(hmac_payload.end(), ephemeral_.public_key.begin(), ephemeral_.public_key.end());
  hmac_payload.insert(hmac_payload.end(), anti_replay_nonce_.begin(), anti_replay_nonce_.end());

  const auto mac = crypto::hmac_sha256(psk_, hmac_payload);

  // Generate random padding for DPI resistance
  const auto padding_size = compute_random_padding_size();
  const auto padding = veil::crypto::random_bytes(padding_size);

  // Build 0-RTT INIT plaintext:
  // magic(2) | version(1) | type(1) | timestamp(8) | ephemeral_pub(32) |
  // anti_replay_nonce(16) | ticket_len(2) | ticket_data(var) | hmac(32) |
  // padding_len(2) | padding(var)
  const auto ticket_len = static_cast<std::uint16_t>(ticket_.ticket_data.size());
  std::vector<std::uint8_t> plaintext;
  plaintext.reserve(kMagic.size() + 1 + 1 + 8 + 32 + kAntiReplayNonceSize +
                    2 + ticket_.ticket_data.size() + 32 + 2 + padding_size);
  plaintext.insert(plaintext.end(), kMagic.begin(), kMagic.end());
  plaintext.push_back(kVersion);
  plaintext.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttInit));
  write_u64(plaintext, init_timestamp_ms_);
  plaintext.insert(plaintext.end(), ephemeral_.public_key.begin(), ephemeral_.public_key.end());
  plaintext.insert(plaintext.end(), anti_replay_nonce_.begin(), anti_replay_nonce_.end());

  // Ticket length (2 bytes, big-endian)
  plaintext.push_back(static_cast<std::uint8_t>((ticket_len >> 8) & 0xFF));
  plaintext.push_back(static_cast<std::uint8_t>(ticket_len & 0xFF));

  // Ticket data
  plaintext.insert(plaintext.end(), ticket_.ticket_data.begin(), ticket_.ticket_data.end());

  // HMAC
  plaintext.insert(plaintext.end(), mac.begin(), mac.end());

  // Padding length (2 bytes, big-endian)
  plaintext.push_back(static_cast<std::uint8_t>((padding_size >> 8) & 0xFF));
  plaintext.push_back(static_cast<std::uint8_t>(padding_size & 0xFF));

  // Random padding
  plaintext.insert(plaintext.end(), padding.begin(), padding.end());

  // Encrypt with PSK-derived handshake key
  auto handshake_key = derive_handshake_key(psk_);
  auto encrypted = encrypt_handshake_packet(handshake_key, plaintext);

  // SECURITY: Clear handshake key after use
  sodium_memzero(handshake_key.data(), handshake_key.size());

  return encrypted;
}

std::optional<HandshakeSession> ZeroRttInitiator::consume_zero_rtt_response(
    std::span<const std::uint8_t> response) {
  if (!init_sent_) {
    return std::nullopt;
  }

  // Decrypt the response
  auto handshake_key = derive_handshake_key(psk_);
  auto decrypted = decrypt_handshake_packet(handshake_key, response);

  // SECURITY: Clear handshake key after use
  sodium_memzero(handshake_key.data(), handshake_key.size());

  if (!decrypted.has_value()) {
    return std::nullopt;
  }

  const auto& plaintext = *decrypted;

  // Minimum size for accept/reject: magic(2) + version(1) + type(1) + timestamp(8) +
  // session_id(8) + hmac(32) + padding_len(2) = 54 bytes
  constexpr std::size_t min_response_size = 2 + 1 + 1 + 8 + 8 + 32 + 2;
  if (plaintext.size() < min_response_size) {
    return std::nullopt;
  }

  if (!std::equal(kMagic.begin(), kMagic.end(), plaintext.begin())) {
    return std::nullopt;
  }

  if (plaintext[2] != kVersion) {
    return std::nullopt;
  }

  const auto msg_type = static_cast<MessageType>(plaintext[3]);

  if (msg_type == MessageType::kZeroRttReject) {
    // Server rejected 0-RTT, client must fall back to 1-RTT
    rejected_ = true;
    return std::nullopt;
  }

  if (msg_type != MessageType::kZeroRttAccept) {
    return std::nullopt;
  }

  // Parse 0-RTT Accept response:
  // magic(2) | version(1) | type(1) | init_ts(8) | session_id(8) | hmac(32) |
  // padding_len(2) | padding(var)
  const auto init_ts = read_u64(plaintext, 4);
  const auto session_id = read_u64(plaintext, 12);

  // Verify the init timestamp matches ours
  if (init_ts != init_timestamp_ms_) {
    return std::nullopt;
  }

  // Extract and verify HMAC
  constexpr std::size_t hmac_offset = 2 + 1 + 1 + 8 + 8;  // 20
  std::array<std::uint8_t, crypto::kHmacSha256Len> provided_mac{};
  std::copy_n(plaintext.begin() + hmac_offset, crypto::kHmacSha256Len, provided_mac.begin());

  // Build HMAC verification payload
  std::vector<std::uint8_t> hmac_verify_payload;
  hmac_verify_payload.reserve(kMagic.size() + 1 + 1 + 8 + 8 + kAntiReplayNonceSize);
  hmac_verify_payload.insert(hmac_verify_payload.end(), kMagic.begin(), kMagic.end());
  hmac_verify_payload.push_back(kVersion);
  hmac_verify_payload.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttAccept));
  write_u64(hmac_verify_payload, init_ts);
  write_u64(hmac_verify_payload, session_id);
  hmac_verify_payload.insert(hmac_verify_payload.end(), anti_replay_nonce_.begin(), anti_replay_nonce_.end());

  const auto expected_mac = crypto::hmac_sha256(psk_, hmac_verify_payload);
  if (!std::equal(expected_mac.begin(), expected_mac.end(), provided_mac.begin())) {
    return std::nullopt;
  }

  // Validate padding
  constexpr std::size_t padding_len_offset = hmac_offset + crypto::kHmacSha256Len;
  if (plaintext.size() < padding_len_offset + 2) {
    return std::nullopt;
  }
  const auto padding_len = static_cast<std::uint16_t>(
      (plaintext[padding_len_offset] << 8) | plaintext[padding_len_offset + 1]);
  if (padding_len < kMinPaddingSize || padding_len > kMaxPaddingSize) {
    return std::nullopt;
  }
  const std::size_t expected_total_size = padding_len_offset + 2 + padding_len;
  if (plaintext.size() != expected_total_size) {
    return std::nullopt;
  }

  // 0-RTT accepted: use cached session keys from the ticket
  // The ephemeral keys are not used for key derivation in 0-RTT
  // (that's the trade-off for reduced latency)
  HandshakeSession session{
      .session_id = session_id,
      .keys = ticket_.cached_keys,
      .initiator_ephemeral = ephemeral_.public_key,
      .responder_ephemeral = {},  // No responder ephemeral in 0-RTT
      .client_id = ticket_.client_id,
  };

  // SECURITY: Clear ephemeral private key after use
  sodium_memzero(ephemeral_.secret_key.data(), ephemeral_.secret_key.size());

  return session;
}

// =============================================================================
// ZeroRttResponder Implementation (Issue #86)
// =============================================================================

ZeroRttResponder::ZeroRttResponder(std::vector<std::uint8_t> psk,
                                   std::shared_ptr<SessionTicketManager> ticket_manager,
                                   std::chrono::milliseconds skew_tolerance,
                                   utils::TokenBucket rate_limiter,
                                   std::function<Clock::time_point()> now_fn)
    : psk_(std::move(psk)),
      ticket_manager_(std::move(ticket_manager)),
      skew_tolerance_(skew_tolerance),
      rate_limiter_(std::move(rate_limiter)),
      now_fn_(std::move(now_fn)) {
  if (psk_.empty()) {
    throw std::invalid_argument("psk required");
  }
  if (!ticket_manager_) {
    throw std::invalid_argument("ticket_manager required");
  }
}

ZeroRttResponder::~ZeroRttResponder() {
  // SECURITY: Clear PSK
  if (!psk_.empty()) {
    sodium_memzero(psk_.data(), psk_.size());
  }
}

std::optional<ZeroRttResponder::Result> ZeroRttResponder::handle_zero_rtt_init(
    std::span<const std::uint8_t> init_bytes) {
  // Rate limit
  if (!rate_limiter_.allow()) {
    return std::nullopt;
  }

  // Decrypt
  auto handshake_key = derive_handshake_key(psk_);
  auto decrypted = decrypt_handshake_packet(handshake_key, init_bytes);

  if (!decrypted.has_value()) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  const auto& plaintext = *decrypted;

  // Minimum 0-RTT INIT size:
  // magic(2) + version(1) + type(1) + timestamp(8) + ephemeral_pub(32) +
  // anti_replay_nonce(16) + ticket_len(2) + ticket(min 1) + hmac(32) + padding_len(2)
  constexpr std::size_t min_size =
      2 + 1 + 1 + 8 + crypto::kX25519PublicKeySize + kAntiReplayNonceSize + 2 + 1 + crypto::kHmacSha256Len + 2;
  if (plaintext.size() < min_size) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Validate header
  if (!std::equal(kMagic.begin(), kMagic.end(), plaintext.begin())) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  if (plaintext[2] != kVersion ||
      plaintext[3] != static_cast<std::uint8_t>(MessageType::kZeroRttInit)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Parse fields
  const auto init_ts = read_u64(plaintext, 4);
  if (!timestamp_valid(init_ts, skew_tolerance_, now_fn_)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  std::array<std::uint8_t, crypto::kX25519PublicKeySize> init_pub{};
  std::copy_n(plaintext.begin() + 12, init_pub.size(), init_pub.begin());

  // Check replay
  if (replay_cache_.mark_and_check(init_ts, init_pub)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Extract anti-replay nonce
  constexpr std::size_t nonce_offset = 12 + crypto::kX25519PublicKeySize;  // 44
  std::array<std::uint8_t, kAntiReplayNonceSize> anti_replay_nonce{};
  std::copy_n(plaintext.begin() + nonce_offset, kAntiReplayNonceSize, anti_replay_nonce.begin());

  // Check anti-replay nonce
  if (ticket_manager_->check_and_mark_nonce(anti_replay_nonce)) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;  // Replay detected
  }

  // Extract ticket length
  constexpr std::size_t ticket_len_offset = nonce_offset + kAntiReplayNonceSize;  // 60
  if (plaintext.size() < ticket_len_offset + 2) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  const auto ticket_len = static_cast<std::uint16_t>(
      (plaintext[ticket_len_offset] << 8) | plaintext[ticket_len_offset + 1]);

  const std::size_t ticket_data_offset = ticket_len_offset + 2;
  if (plaintext.size() < ticket_data_offset + ticket_len) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Extract ticket data
  std::span<const std::uint8_t> ticket_data_span(plaintext.data() + ticket_data_offset, ticket_len);

  // HMAC offset is after ticket data
  const std::size_t mac_offset = ticket_data_offset + ticket_len;
  if (plaintext.size() < mac_offset + crypto::kHmacSha256Len + 2) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Verify HMAC
  std::array<std::uint8_t, crypto::kHmacSha256Len> provided_mac{};
  std::copy_n(plaintext.begin() + static_cast<std::ptrdiff_t>(mac_offset), crypto::kHmacSha256Len,
              provided_mac.begin());

  std::vector<std::uint8_t> hmac_payload;
  hmac_payload.reserve(kMagic.size() + 1 + 1 + 8 + 32 + kAntiReplayNonceSize);
  hmac_payload.insert(hmac_payload.end(), kMagic.begin(), kMagic.end());
  hmac_payload.push_back(kVersion);
  hmac_payload.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttInit));
  write_u64(hmac_payload, init_ts);
  hmac_payload.insert(hmac_payload.end(), init_pub.begin(), init_pub.end());
  hmac_payload.insert(hmac_payload.end(), anti_replay_nonce.begin(), anti_replay_nonce.end());

  const auto expected_mac = crypto::hmac_sha256(psk_, hmac_payload);
  if (!std::equal(expected_mac.begin(), expected_mac.end(), provided_mac.begin())) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Validate padding
  const std::size_t padding_len_offset = mac_offset + crypto::kHmacSha256Len;
  if (plaintext.size() < padding_len_offset + 2) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  const auto padding_len = static_cast<std::uint16_t>(
      (plaintext[padding_len_offset] << 8) | plaintext[padding_len_offset + 1]);
  if (padding_len < kMinPaddingSize || padding_len > kMaxPaddingSize) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }
  const std::size_t expected_total = padding_len_offset + 2 + padding_len;
  if (plaintext.size() != expected_total) {
    sodium_memzero(handshake_key.data(), handshake_key.size());
    return std::nullopt;
  }

  // Validate the session ticket
  auto ticket_payload = ticket_manager_->validate_ticket(ticket_data_span);

  if (!ticket_payload.has_value()) {
    // Ticket invalid or expired: send reject response for 1-RTT fallback
    const auto resp_padding_size = compute_random_padding_size();
    const auto resp_padding = veil::crypto::random_bytes(resp_padding_size);

    // Build reject HMAC payload
    std::vector<std::uint8_t> reject_hmac;
    reject_hmac.reserve(kMagic.size() + 1 + 1 + 8);
    reject_hmac.insert(reject_hmac.end(), kMagic.begin(), kMagic.end());
    reject_hmac.push_back(kVersion);
    reject_hmac.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttReject));
    write_u64(reject_hmac, init_ts);

    const auto reject_mac = crypto::hmac_sha256(psk_, reject_hmac);

    // Build reject response: magic(2) + version(1) + type(1) + init_ts(8) + hmac(32) + padding_len(2) + padding
    std::vector<std::uint8_t> reject_plaintext;
    reject_plaintext.reserve(kMagic.size() + 1 + 1 + 8 + 32 + 2 + resp_padding_size);
    reject_plaintext.insert(reject_plaintext.end(), kMagic.begin(), kMagic.end());
    reject_plaintext.push_back(kVersion);
    reject_plaintext.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttReject));
    write_u64(reject_plaintext, init_ts);
    reject_plaintext.insert(reject_plaintext.end(), reject_mac.begin(), reject_mac.end());
    reject_plaintext.push_back(static_cast<std::uint8_t>((resp_padding_size >> 8) & 0xFF));
    reject_plaintext.push_back(static_cast<std::uint8_t>(resp_padding_size & 0xFF));
    reject_plaintext.insert(reject_plaintext.end(), resp_padding.begin(), resp_padding.end());

    auto encrypted_reject = encrypt_handshake_packet(handshake_key, reject_plaintext);
    sodium_memzero(handshake_key.data(), handshake_key.size());

    // Return reject result with empty session keys
    HandshakeSession reject_session{
        .session_id = 0,
        .keys = {},
        .initiator_ephemeral = init_pub,
        .responder_ephemeral = {},
        .client_id = {},
    };

    return Result{
        .response = std::move(encrypted_reject),
        .session = reject_session,
        .accepted = false,
    };
  }

  // Ticket valid: accept 0-RTT
  const auto session_id = veil::crypto::random_uint64();

  // Reconstruct session keys from ticket payload
  // Note: The ticket stores keys from server's perspective (sent as responder),
  // so we use them directly
  crypto::SessionKeys session_keys{
      .send_key = ticket_payload->send_key,
      .recv_key = ticket_payload->recv_key,
      .send_nonce = ticket_payload->send_nonce,
      .recv_nonce = ticket_payload->recv_nonce,
  };

  // Build accept HMAC payload
  std::vector<std::uint8_t> accept_hmac;
  accept_hmac.reserve(kMagic.size() + 1 + 1 + 8 + 8 + kAntiReplayNonceSize);
  accept_hmac.insert(accept_hmac.end(), kMagic.begin(), kMagic.end());
  accept_hmac.push_back(kVersion);
  accept_hmac.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttAccept));
  write_u64(accept_hmac, init_ts);
  write_u64(accept_hmac, session_id);
  accept_hmac.insert(accept_hmac.end(), anti_replay_nonce.begin(), anti_replay_nonce.end());

  const auto accept_mac = crypto::hmac_sha256(psk_, accept_hmac);

  // Generate padding
  const auto resp_padding_size = compute_random_padding_size();
  const auto resp_padding = veil::crypto::random_bytes(resp_padding_size);

  // Build accept response:
  // magic(2) | version(1) | type(1) | init_ts(8) | session_id(8) | hmac(32) | padding_len(2) | padding
  std::vector<std::uint8_t> accept_plaintext;
  accept_plaintext.reserve(kMagic.size() + 1 + 1 + 8 + 8 + 32 + 2 + resp_padding_size);
  accept_plaintext.insert(accept_plaintext.end(), kMagic.begin(), kMagic.end());
  accept_plaintext.push_back(kVersion);
  accept_plaintext.push_back(static_cast<std::uint8_t>(MessageType::kZeroRttAccept));
  write_u64(accept_plaintext, init_ts);
  write_u64(accept_plaintext, session_id);
  accept_plaintext.insert(accept_plaintext.end(), accept_mac.begin(), accept_mac.end());
  accept_plaintext.push_back(static_cast<std::uint8_t>((resp_padding_size >> 8) & 0xFF));
  accept_plaintext.push_back(static_cast<std::uint8_t>(resp_padding_size & 0xFF));
  accept_plaintext.insert(accept_plaintext.end(), resp_padding.begin(), resp_padding.end());

  auto encrypted_accept = encrypt_handshake_packet(handshake_key, accept_plaintext);

  // SECURITY: Clear handshake key
  sodium_memzero(handshake_key.data(), handshake_key.size());

  // SECURITY: Clear ticket payload keys
  sodium_memzero(ticket_payload->send_key.data(), ticket_payload->send_key.size());
  sodium_memzero(ticket_payload->recv_key.data(), ticket_payload->recv_key.size());

  HandshakeSession session{
      .session_id = session_id,
      .keys = session_keys,
      .initiator_ephemeral = init_pub,
      .responder_ephemeral = {},  // No responder ephemeral in 0-RTT
      .client_id = {},  // Note: ticket stores client_id_hash (FNV-1a), not the original string
  };

  return Result{
      .response = std::move(encrypted_accept),
      .session = session,
      .accepted = true,
  };
}

}  // namespace veil::handshake
