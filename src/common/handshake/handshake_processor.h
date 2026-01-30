#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "common/auth/client_registry.h"
#include "common/crypto/crypto_engine.h"
#include "common/handshake/handshake_replay_cache.h"
#include "common/utils/rate_limiter.h"

namespace veil::handshake {

/// Maximum length of client_id in handshake messages.
/// Kept small to avoid bloating handshake packets.
inline constexpr std::size_t kMaxHandshakeClientIdLength = 64;

enum class MessageType : std::uint8_t { kInit = 1, kResponse = 2 };

struct HandshakeSession {
  std::uint64_t session_id;
  crypto::SessionKeys keys;
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> initiator_ephemeral;
  std::array<std::uint8_t, crypto::kX25519PublicKeySize> responder_ephemeral;
  std::string client_id;  // Optional: identifies which client was authenticated (Issue #87)
};

class HandshakeInitiator {
 public:
  using Clock = std::chrono::system_clock;

  /// Create an initiator with a PSK.
  /// @param psk The pre-shared key.
  /// @param skew_tolerance Maximum allowed timestamp difference.
  /// @param now_fn Clock function for timestamp generation.
  HandshakeInitiator(std::vector<std::uint8_t> psk, std::chrono::milliseconds skew_tolerance,
                     std::function<Clock::time_point()> now_fn = Clock::now);

  /// Create an initiator with a PSK and client_id (Issue #87).
  /// @param psk The pre-shared key for this client.
  /// @param client_id The client identifier to send in INIT message.
  /// @param skew_tolerance Maximum allowed timestamp difference.
  /// @param now_fn Clock function for timestamp generation.
  HandshakeInitiator(std::vector<std::uint8_t> psk, std::string client_id,
                     std::chrono::milliseconds skew_tolerance,
                     std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears all sensitive key material
  ~HandshakeInitiator();

  // Disable copy (contains sensitive data)
  HandshakeInitiator(const HandshakeInitiator&) = delete;
  HandshakeInitiator& operator=(const HandshakeInitiator&) = delete;

  // Enable move
  HandshakeInitiator(HandshakeInitiator&&) = default;
  HandshakeInitiator& operator=(HandshakeInitiator&&) = default;

  std::vector<std::uint8_t> create_init();
  std::optional<HandshakeSession> consume_response(std::span<const std::uint8_t> response);

  /// Get the client_id associated with this initiator (may be empty).
  const std::string& client_id() const { return client_id_; }

 private:
  std::vector<std::uint8_t> psk_;
  std::string client_id_;  // Issue #87: Optional client identifier
  std::chrono::milliseconds skew_tolerance_;
  std::function<Clock::time_point()> now_fn_;

  crypto::KeyPair ephemeral_;
  std::uint64_t init_timestamp_ms_{0};
  bool init_sent_{false};
};

class HandshakeResponder {
 public:
  using Clock = std::chrono::system_clock;
  struct Result {
    std::vector<std::uint8_t> response;
    HandshakeSession session;
  };

  HandshakeResponder(std::vector<std::uint8_t> psk, std::chrono::milliseconds skew_tolerance,
                     utils::TokenBucket rate_limiter,
                     std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears all sensitive key material
  ~HandshakeResponder();

  // Disable copy (contains sensitive data)
  HandshakeResponder(const HandshakeResponder&) = delete;
  HandshakeResponder& operator=(const HandshakeResponder&) = delete;

  // Disable move (contains non-movable replay cache with mutex)
  HandshakeResponder(HandshakeResponder&&) = delete;
  HandshakeResponder& operator=(HandshakeResponder&&) = delete;

  std::optional<Result> handle_init(std::span<const std::uint8_t> init_bytes);

 private:
  std::vector<std::uint8_t> psk_;
  std::chrono::milliseconds skew_tolerance_;
  utils::TokenBucket rate_limiter_;
  HandshakeReplayCache replay_cache_;
  std::function<Clock::time_point()> now_fn_;
};

/// MultiClientHandshakeResponder handles handshakes with per-client PSKs.
///
/// This addresses Issue #87: PSK authentication doesn't scale (no per-client keys).
///
/// Key features:
/// - Looks up PSK by client_id from the ClientRegistry
/// - Falls back to a global PSK if client_id is empty or not found
/// - Supports individual client revocation via the registry
/// - Returns the authenticated client_id in the HandshakeSession for audit trails
///
/// Usage:
/// ```cpp
/// auto registry = std::make_shared<auth::ClientRegistry>();
/// registry->add_client("alice", psk_alice);
/// registry->set_fallback_psk(legacy_psk);
///
/// MultiClientHandshakeResponder responder(registry, skew_tolerance, rate_limiter);
///
/// // Handle incoming handshake
/// auto result = responder.handle_init(init_bytes);
/// if (result) {
///   LOG_INFO("Client '{}' authenticated", result->session.client_id);
/// }
/// ```
class MultiClientHandshakeResponder {
 public:
  using Clock = std::chrono::system_clock;
  struct Result {
    std::vector<std::uint8_t> response;
    HandshakeSession session;
  };

  /// Create a responder with a client registry for per-client PSKs.
  /// @param registry The client registry for PSK lookups.
  /// @param skew_tolerance Maximum allowed timestamp difference.
  /// @param rate_limiter Token bucket for rate limiting handshake attempts.
  /// @param now_fn Clock function for timestamp generation.
  MultiClientHandshakeResponder(std::shared_ptr<auth::ClientRegistry> registry,
                                std::chrono::milliseconds skew_tolerance,
                                utils::TokenBucket rate_limiter,
                                std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears sensitive key material.
  ~MultiClientHandshakeResponder();

  // Non-copyable (contains sensitive data and non-copyable members).
  MultiClientHandshakeResponder(const MultiClientHandshakeResponder&) = delete;
  MultiClientHandshakeResponder& operator=(const MultiClientHandshakeResponder&) = delete;

  // Non-movable (contains non-movable replay cache with mutex).
  MultiClientHandshakeResponder(MultiClientHandshakeResponder&&) = delete;
  MultiClientHandshakeResponder& operator=(MultiClientHandshakeResponder&&) = delete;

  /// Handle an INIT message from a client.
  /// The client_id is extracted from the INIT message and used to look up the PSK.
  /// If client_id is empty or not found, falls back to the registry's fallback PSK.
  /// Returns nullopt if handshake fails (wrong PSK, replay, rate limit, etc.).
  std::optional<Result> handle_init(std::span<const std::uint8_t> init_bytes);

  /// Get the client registry.
  std::shared_ptr<auth::ClientRegistry> registry() const { return registry_; }

 private:
  /// Internal helper to process a decrypted INIT message.
  std::optional<Result> process_decrypted_init(
      const std::vector<std::uint8_t>& plaintext,
      std::span<const std::uint8_t, crypto::kAeadKeyLen> handshake_key,
      const std::vector<std::uint8_t>& psk,
      const std::string& client_id);

  std::shared_ptr<auth::ClientRegistry> registry_;
  std::chrono::milliseconds skew_tolerance_;
  utils::TokenBucket rate_limiter_;
  HandshakeReplayCache replay_cache_;
  std::function<Clock::time_point()> now_fn_;
};

}  // namespace veil::handshake
