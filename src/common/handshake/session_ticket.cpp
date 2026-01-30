#include "common/handshake/session_ticket.h"

#include <sodium.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "common/crypto/random.h"

namespace {

// AEAD tag size for ChaCha20-Poly1305
constexpr std::size_t kAeadTagLen = crypto_aead_chacha20poly1305_ietf_ABYTES;  // 16 bytes

std::uint64_t to_millis(std::chrono::system_clock::time_point tp) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
}

void write_u64_be(std::uint8_t* out, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out[i] = static_cast<std::uint8_t>(value & 0xFF);
    value >>= 8;
  }
}

std::uint64_t read_u64_be(const std::uint8_t* data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | data[i];
  }
  return value;
}

// Serialize TicketPayload into bytes for encryption.
// Format: issued_at_ms(8) | client_id_hash(8) | send_key(32) | recv_key(32) | send_nonce(12) | recv_nonce(12)
// Total: 104 bytes
constexpr std::size_t kTicketPayloadSize = 8 + 8 + 32 + 32 + 12 + 12;  // 104 bytes

std::vector<std::uint8_t> serialize_payload(const veil::handshake::TicketPayload& payload) {
  std::vector<std::uint8_t> data(kTicketPayloadSize);
  std::size_t offset = 0;

  write_u64_be(data.data() + offset, payload.issued_at_ms);
  offset += 8;

  write_u64_be(data.data() + offset, payload.client_id_hash);
  offset += 8;

  std::copy_n(payload.send_key.begin(), payload.send_key.size(), data.begin() + static_cast<std::ptrdiff_t>(offset));
  offset += payload.send_key.size();

  std::copy_n(payload.recv_key.begin(), payload.recv_key.size(), data.begin() + static_cast<std::ptrdiff_t>(offset));
  offset += payload.recv_key.size();

  std::copy_n(payload.send_nonce.begin(), payload.send_nonce.size(), data.begin() + static_cast<std::ptrdiff_t>(offset));
  offset += payload.send_nonce.size();

  std::copy_n(payload.recv_nonce.begin(), payload.recv_nonce.size(), data.begin() + static_cast<std::ptrdiff_t>(offset));

  return data;
}

std::optional<veil::handshake::TicketPayload> deserialize_payload(std::span<const std::uint8_t> data) {
  if (data.size() != kTicketPayloadSize) {
    return std::nullopt;
  }

  veil::handshake::TicketPayload payload;
  std::size_t offset = 0;

  payload.issued_at_ms = read_u64_be(data.data() + offset);
  offset += 8;

  payload.client_id_hash = read_u64_be(data.data() + offset);
  offset += 8;

  std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), payload.send_key.size(), payload.send_key.begin());
  offset += payload.send_key.size();

  std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), payload.recv_key.size(), payload.recv_key.begin());
  offset += payload.recv_key.size();

  std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), payload.send_nonce.size(), payload.send_nonce.begin());
  offset += payload.send_nonce.size();

  std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), payload.recv_nonce.size(), payload.recv_nonce.begin());

  return payload;
}

}  // namespace

namespace veil::handshake {

// =============================================================================
// SessionTicketManager Implementation
// =============================================================================

SessionTicketManager::SessionTicketManager(std::chrono::milliseconds ticket_lifetime,
                                           std::function<Clock::time_point()> now_fn)
    : ticket_lifetime_(ticket_lifetime), now_fn_(std::move(now_fn)) {
  // Generate a random ticket encryption key
  auto key_bytes = crypto::random_bytes(kTicketKeySize);
  std::copy_n(key_bytes.begin(), kTicketKeySize, ticket_key_.begin());
  sodium_memzero(key_bytes.data(), key_bytes.size());
}

SessionTicketManager::~SessionTicketManager() {
  // SECURITY: Clear ticket encryption key
  sodium_memzero(ticket_key_.data(), ticket_key_.size());
}

SessionTicket SessionTicketManager::issue_ticket(const crypto::SessionKeys& keys,
                                                  const std::string& client_id) {
  const auto now_ms = to_millis(now_fn_());

  // Build the payload
  TicketPayload payload{
      .issued_at_ms = now_ms,
      .client_id_hash = fnv1a_hash(client_id),
      .send_key = keys.send_key,
      .recv_key = keys.recv_key,
      .send_nonce = keys.send_nonce,
      .recv_nonce = keys.recv_nonce,
  };

  auto plaintext = serialize_payload(payload);

  // SECURITY: Clear payload keys after serialization
  sodium_memzero(payload.send_key.data(), payload.send_key.size());
  sodium_memzero(payload.recv_key.data(), payload.recv_key.size());

  // Encrypt the payload with the ticket key
  // Format: [12-byte nonce][encrypted payload + 16-byte AEAD tag]
  auto nonce_bytes = crypto::random_bytes(crypto::kNonceLen);
  std::array<std::uint8_t, crypto::kNonceLen> nonce{};
  std::copy_n(nonce_bytes.begin(), nonce.size(), nonce.begin());

  auto ciphertext = crypto::aead_encrypt(ticket_key_, nonce, {}, plaintext);

  // SECURITY: Clear plaintext after encryption
  sodium_memzero(plaintext.data(), plaintext.size());

  // Build ticket data: nonce + ciphertext
  std::vector<std::uint8_t> ticket_data;
  ticket_data.reserve(nonce.size() + ciphertext.size());
  ticket_data.insert(ticket_data.end(), nonce.begin(), nonce.end());
  ticket_data.insert(ticket_data.end(), ciphertext.begin(), ciphertext.end());

  SessionTicket ticket{
      .ticket_data = std::move(ticket_data),
      .issued_at_ms = now_ms,
      .lifetime_ms = static_cast<std::uint64_t>(ticket_lifetime_.count()),
      .cached_keys = keys,
      .client_id = client_id,
  };

  return ticket;
}

std::optional<TicketPayload> SessionTicketManager::validate_ticket(
    std::span<const std::uint8_t> ticket_data) {
  // Minimum size: nonce(12) + payload(104) + tag(16)
  constexpr std::size_t min_ticket_size = crypto::kNonceLen + kTicketPayloadSize + kAeadTagLen;
  if (ticket_data.size() != min_ticket_size) {
    return std::nullopt;
  }

  // Extract nonce
  std::array<std::uint8_t, crypto::kNonceLen> nonce{};
  std::copy_n(ticket_data.begin(), nonce.size(), nonce.begin());

  // Extract ciphertext
  auto ciphertext = ticket_data.subspan(crypto::kNonceLen);

  // Decrypt
  auto plaintext = crypto::aead_decrypt(ticket_key_, nonce, {}, ciphertext);
  if (!plaintext.has_value()) {
    return std::nullopt;
  }

  auto payload = deserialize_payload(*plaintext);

  // SECURITY: Clear plaintext
  sodium_memzero(plaintext->data(), plaintext->size());

  if (!payload.has_value()) {
    return std::nullopt;
  }

  // Check ticket expiry
  const auto now_ms = to_millis(now_fn_());
  if (now_ms > payload->issued_at_ms + static_cast<std::uint64_t>(ticket_lifetime_.count())) {
    // SECURITY: Clear expired payload keys
    sodium_memzero(payload->send_key.data(), payload->send_key.size());
    sodium_memzero(payload->recv_key.data(), payload->recv_key.size());
    return std::nullopt;
  }

  return payload;
}

bool SessionTicketManager::check_and_mark_nonce(
    std::span<const std::uint8_t, kAntiReplayNonceSize> nonce) {
  // FNV-1a hash the nonce for compact storage.
  // Trade-off: with ~4096 active nonces, collision probability is ~2^-49 (negligible).
  // A hash collision would cause a false positive (legitimate request falsely rejected as replay),
  // which is acceptable for this use case — the client simply falls back to a 1-RTT handshake.
  std::uint64_t nonce_hash = 14695981039346656037ULL;
  for (std::size_t i = 0; i < kAntiReplayNonceSize; ++i) {
    nonce_hash ^= static_cast<std::uint64_t>(nonce[i]);
    nonce_hash *= 1099511628211ULL;
  }

  const auto now_ms = to_millis(now_fn_());
  const auto expiry_ms = now_ms + static_cast<std::uint64_t>(ticket_lifetime_.count());

  std::lock_guard lock(nonce_mutex_);

  auto it = used_nonces_.find(nonce_hash);
  if (it != used_nonces_.end()) {
    return true;  // Replay detected
  }

  // Limit nonce cache size
  if (used_nonces_.size() >= kMaxTotalTickets) {
    cleanup_expired_nonces();
  }

  used_nonces_[nonce_hash] = expiry_ms;
  return false;
}

void SessionTicketManager::cleanup_expired_nonces() {
  // Note: caller must hold nonce_mutex_ (or this is called from check_and_mark_nonce which holds it)
  const auto now_ms = to_millis(now_fn_());
  for (auto it = used_nonces_.begin(); it != used_nonces_.end();) {
    if (it->second <= now_ms) {
      it = used_nonces_.erase(it);
    } else {
      ++it;
    }
  }
}

std::uint64_t SessionTicketManager::fnv1a_hash(const std::string& str) {
  // FNV-1a 64-bit hash
  std::uint64_t hash = 14695981039346656037ULL;
  for (char c : str) {
    hash ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(c));
    hash *= 1099511628211ULL;
  }
  return hash;
}

// =============================================================================
// SessionTicketStore Implementation
// =============================================================================

SessionTicketStore::SessionTicketStore(std::function<Clock::time_point()> now_fn)
    : now_fn_(std::move(now_fn)) {}

void SessionTicketStore::store_ticket(const std::string& server_id, SessionTicket ticket) {
  std::lock_guard lock(mutex_);
  tickets_.insert_or_assign(server_id, std::move(ticket));
}

std::optional<SessionTicket> SessionTicketStore::get_ticket(const std::string& server_id) {
  const auto now_ms = to_millis(now_fn_());

  std::lock_guard lock(mutex_);
  auto it = tickets_.find(server_id);
  if (it == tickets_.end()) {
    return std::nullopt;
  }

  if (it->second.is_expired(now_ms)) {
    // SECURITY: Clear expired ticket keys
    sodium_memzero(it->second.cached_keys.send_key.data(), it->second.cached_keys.send_key.size());
    sodium_memzero(it->second.cached_keys.recv_key.data(), it->second.cached_keys.recv_key.size());
    tickets_.erase(it);
    return std::nullopt;
  }

  return it->second;
}

void SessionTicketStore::remove_ticket(const std::string& server_id) {
  std::lock_guard lock(mutex_);
  auto it = tickets_.find(server_id);
  if (it != tickets_.end()) {
    // SECURITY: Clear ticket keys before removal
    sodium_memzero(it->second.cached_keys.send_key.data(), it->second.cached_keys.send_key.size());
    sodium_memzero(it->second.cached_keys.recv_key.data(), it->second.cached_keys.recv_key.size());
    tickets_.erase(it);
  }
}

void SessionTicketStore::cleanup_expired() {
  const auto now_ms = to_millis(now_fn_());

  std::lock_guard lock(mutex_);
  for (auto it = tickets_.begin(); it != tickets_.end();) {
    if (it->second.is_expired(now_ms)) {
      // SECURITY: Clear expired ticket keys
      sodium_memzero(it->second.cached_keys.send_key.data(), it->second.cached_keys.send_key.size());
      sodium_memzero(it->second.cached_keys.recv_key.data(), it->second.cached_keys.recv_key.size());
      it = tickets_.erase(it);
    } else {
      ++it;
    }
  }
}

std::size_t SessionTicketStore::size() const {
  std::lock_guard lock(mutex_);
  return tickets_.size();
}

}  // namespace veil::handshake
