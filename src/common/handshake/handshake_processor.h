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
#include "common/handshake/session_ticket.h"
#include "common/utils/rate_limiter.h"

namespace veil::handshake {

/// Maximum length of client_id in handshake messages.
/// Kept small to avoid bloating handshake packets.
inline constexpr std::size_t kMaxHandshakeClientIdLength = 64;

enum class MessageType : std::uint8_t {
  kInit = 1,
  kResponse = 2,
  kZeroRttInit = 3,     // 0-RTT INIT with session ticket (Issue #86)
  kZeroRttAccept = 4,   // Server accepts 0-RTT (Issue #86)
  kZeroRttReject = 5,   // Server rejects 0-RTT, fallback to 1-RTT (Issue #86)
};

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

/// ZeroRttInitiator supports 0-RTT session resumption for returning clients (Issue #86).
///
/// When a client has a valid session ticket from a previous handshake, it can
/// send the ticket + early data in a single INIT packet, reducing connection
/// latency by 50% (1 RTT instead of 2 RTT).
///
/// Security considerations:
/// - 0-RTT data is vulnerable to replay attacks (RFC 8446 Section 8).
/// - An anti-replay nonce is included to mitigate simple replays.
/// - Only idempotent operations should be performed using 0-RTT data.
/// - If the server rejects 0-RTT, the client must fall back to a full 1-RTT handshake.
///
/// Usage:
/// ```cpp
/// SessionTicketStore store;
/// auto ticket = store.get_ticket("server:4430");
/// if (ticket) {
///   ZeroRttInitiator initiator(psk, *ticket, skew_tolerance);
///   auto init_bytes = initiator.create_zero_rtt_init();
///   // Send init_bytes to server
///   // Server responds with kZeroRttAccept or kZeroRttReject
///   auto session = initiator.consume_zero_rtt_response(response);
///   if (!session) {
///     // Fallback to full 1-RTT handshake
///   }
/// }
/// ```
class ZeroRttInitiator {
 public:
  using Clock = std::chrono::system_clock;

  /// Create a 0-RTT initiator with a PSK and cached session ticket.
  /// @param psk The pre-shared key.
  /// @param ticket The cached session ticket from a previous handshake.
  /// @param now_fn Clock function for timestamp generation.
  ZeroRttInitiator(std::vector<std::uint8_t> psk, SessionTicket ticket,
                   std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears all sensitive key material.
  ~ZeroRttInitiator();

  // Non-copyable, non-movable (destructor zeroes sensitive key material).
  ZeroRttInitiator(const ZeroRttInitiator&) = delete;
  ZeroRttInitiator& operator=(const ZeroRttInitiator&) = delete;
  ZeroRttInitiator(ZeroRttInitiator&&) = delete;
  ZeroRttInitiator& operator=(ZeroRttInitiator&&) = delete;

  /// Create a 0-RTT INIT message containing the session ticket.
  /// @return Encrypted 0-RTT INIT packet.
  std::vector<std::uint8_t> create_zero_rtt_init();

  /// Process the server's response to a 0-RTT attempt.
  /// @param response The server's response bytes.
  /// @return A session if 0-RTT was accepted, nullopt if rejected (fallback to 1-RTT).
  std::optional<HandshakeSession> consume_zero_rtt_response(
      std::span<const std::uint8_t> response);

  /// Check if 0-RTT was rejected (need to fallback to 1-RTT).
  bool was_rejected() const { return rejected_; }

 private:
  std::vector<std::uint8_t> psk_;
  SessionTicket ticket_;
  std::function<Clock::time_point()> now_fn_;

  crypto::KeyPair ephemeral_;
  std::array<std::uint8_t, kAntiReplayNonceSize> anti_replay_nonce_{};
  std::uint64_t init_timestamp_ms_{0};
  bool init_sent_{false};
  bool rejected_{false};
};

/// ZeroRttResponder handles 0-RTT session resumption on the server side (Issue #86).
///
/// When a client presents a valid session ticket, the server can accept the
/// 0-RTT connection without a full handshake round-trip, using the cached
/// session keys from the ticket.
///
/// Security features:
/// - Anti-replay protection via nonce tracking.
/// - Ticket expiry validation.
/// - Rate limiting.
/// - Fallback rejection (kZeroRttReject) when ticket is invalid.
///
/// Usage:
/// ```cpp
/// auto ticket_manager = std::make_shared<SessionTicketManager>();
/// ZeroRttResponder responder(psk, ticket_manager, skew_tolerance, rate_limiter);
///
/// auto result = responder.handle_zero_rtt_init(init_bytes);
/// if (result) {
///   if (result->accepted) {
///     // 0-RTT accepted, use session immediately
///   } else {
///     // Send reject response, client will fallback to 1-RTT
///   }
/// }
/// ```
class ZeroRttResponder {
 public:
  using Clock = std::chrono::system_clock;
  struct Result {
    std::vector<std::uint8_t> response;
    HandshakeSession session;
    bool accepted;  // true = 0-RTT accepted, false = rejected (fallback to 1-RTT)
  };

  /// Create a 0-RTT responder with a ticket manager.
  /// @param psk The pre-shared key.
  /// @param ticket_manager The ticket manager for validation.
  /// @param skew_tolerance Maximum allowed timestamp difference.
  /// @param rate_limiter Token bucket for rate limiting.
  /// @param now_fn Clock function for timestamp generation.
  ZeroRttResponder(std::vector<std::uint8_t> psk,
                   std::shared_ptr<SessionTicketManager> ticket_manager,
                   std::chrono::milliseconds skew_tolerance, utils::TokenBucket rate_limiter,
                   std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears sensitive key material.
  ~ZeroRttResponder();

  // Non-copyable, non-movable.
  ZeroRttResponder(const ZeroRttResponder&) = delete;
  ZeroRttResponder& operator=(const ZeroRttResponder&) = delete;
  ZeroRttResponder(ZeroRttResponder&&) = delete;
  ZeroRttResponder& operator=(ZeroRttResponder&&) = delete;

  /// Handle a 0-RTT INIT message.
  /// @param init_bytes The encrypted 0-RTT INIT packet.
  /// @return Result with accept/reject status, or nullopt on complete failure.
  std::optional<Result> handle_zero_rtt_init(std::span<const std::uint8_t> init_bytes);

  /// Get the ticket manager.
  std::shared_ptr<SessionTicketManager> ticket_manager() const { return ticket_manager_; }

 private:
  std::vector<std::uint8_t> psk_;
  std::shared_ptr<SessionTicketManager> ticket_manager_;
  std::chrono::milliseconds skew_tolerance_;
  utils::TokenBucket rate_limiter_;
  HandshakeReplayCache replay_cache_;
  std::function<Clock::time_point()> now_fn_;
};

}  // namespace veil::handshake
