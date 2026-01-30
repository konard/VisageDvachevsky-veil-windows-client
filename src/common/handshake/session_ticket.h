#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/crypto/crypto_engine.h"

namespace veil::handshake {

/// Maximum age of a session ticket before it becomes invalid.
/// After this duration, the client must perform a full 1-RTT handshake.
inline constexpr auto kDefaultTicketLifetime = std::chrono::hours(24);

/// Maximum number of cached tickets per client on the server side.
/// Prevents memory exhaustion from ticket accumulation.
inline constexpr std::size_t kMaxTicketsPerClient = 4;

/// Maximum total tickets in the server-side store.
inline constexpr std::size_t kMaxTotalTickets = 4096;

/// Size of the anti-replay nonce embedded in each 0-RTT INIT message.
inline constexpr std::size_t kAntiReplayNonceSize = 16;

/// Size of the ticket encryption key (server-only secret).
inline constexpr std::size_t kTicketKeySize = 32;

/// Session ticket issued by the server after a successful handshake.
/// The client caches this and presents it on reconnection for 0-RTT.
///
/// Security considerations (RFC 8446 Section 8):
/// - 0-RTT data is vulnerable to replay attacks.
/// - Only idempotent operations should use 0-RTT.
/// - Anti-replay nonce + timestamp prevent simple replays.
/// - Server tracks used nonces to detect replay attempts.
struct SessionTicket {
  /// Opaque ticket data (encrypted by server, opaque to client).
  std::vector<std::uint8_t> ticket_data;

  /// Timestamp when the ticket was issued (milliseconds since epoch).
  std::uint64_t issued_at_ms{0};

  /// Ticket lifetime in milliseconds.
  std::uint64_t lifetime_ms{0};

  /// Cached session keys from the original handshake.
  /// These are used to bootstrap the 0-RTT connection.
  crypto::SessionKeys cached_keys;

  /// The PSK identity hint (empty for anonymous, client_id for per-client).
  std::string client_id;

  /// Check if the ticket has expired.
  bool is_expired(std::uint64_t now_ms) const {
    return now_ms > issued_at_ms + lifetime_ms;
  }
};

/// Internal ticket payload stored on the server side.
/// This is what gets encrypted into the opaque ticket_data.
struct TicketPayload {
  std::uint64_t issued_at_ms{0};
  std::uint64_t client_id_hash{0};  // FNV-1a hash of client_id for fast lookup
  std::array<std::uint8_t, crypto::kAeadKeyLen> send_key{};
  std::array<std::uint8_t, crypto::kAeadKeyLen> recv_key{};
  std::array<std::uint8_t, crypto::kNonceLen> send_nonce{};
  std::array<std::uint8_t, crypto::kNonceLen> recv_nonce{};
};

/// Server-side ticket manager that issues and validates session tickets.
///
/// Thread safety: All public methods are thread-safe (internally synchronized).
///
/// Usage:
/// ```cpp
/// // After successful 1-RTT handshake on server:
/// SessionTicketManager manager;
/// auto ticket = manager.issue_ticket(session.keys, session.client_id);
/// // Send ticket to client in RESPONSE or as a post-handshake message
///
/// // On 0-RTT reconnection:
/// auto payload = manager.validate_ticket(ticket_data);
/// if (payload) {
///   // Resume session with cached keys
/// }
/// ```
class SessionTicketManager {
 public:
  using Clock = std::chrono::system_clock;

  /// Create a ticket manager with a random encryption key.
  /// @param ticket_lifetime How long tickets remain valid.
  /// @param now_fn Clock function for timestamp generation.
  explicit SessionTicketManager(
      std::chrono::milliseconds ticket_lifetime =
          std::chrono::duration_cast<std::chrono::milliseconds>(kDefaultTicketLifetime),
      std::function<Clock::time_point()> now_fn = Clock::now);

  /// SECURITY: Destructor clears ticket encryption key.
  ~SessionTicketManager();

  // Non-copyable (contains sensitive key material).
  SessionTicketManager(const SessionTicketManager&) = delete;
  SessionTicketManager& operator=(const SessionTicketManager&) = delete;

  // Non-movable (contains mutex).
  SessionTicketManager(SessionTicketManager&&) = delete;
  SessionTicketManager& operator=(SessionTicketManager&&) = delete;

  /// Issue a session ticket for the given session keys.
  /// @param keys The session keys to cache in the ticket.
  /// @param client_id Optional client identifier.
  /// @return The session ticket to send to the client.
  SessionTicket issue_ticket(const crypto::SessionKeys& keys, const std::string& client_id = "");

  /// Validate and decrypt a ticket presented by a client.
  /// @param ticket_data The opaque ticket data from the client.
  /// @return The decrypted payload if valid, nullopt otherwise.
  std::optional<TicketPayload> validate_ticket(std::span<const std::uint8_t> ticket_data);

  /// Check if an anti-replay nonce has been used.
  /// @param nonce The nonce to check.
  /// @return true if the nonce was already used (replay detected).
  bool check_and_mark_nonce(std::span<const std::uint8_t, kAntiReplayNonceSize> nonce);

  /// Remove expired nonces from the anti-replay cache.
  void cleanup_expired_nonces();

  /// Get the current ticket lifetime.
  std::chrono::milliseconds ticket_lifetime() const { return ticket_lifetime_; }

 private:
  /// Compute FNV-1a hash of a string for fast lookup.
  static std::uint64_t fnv1a_hash(const std::string& str);

  std::array<std::uint8_t, kTicketKeySize> ticket_key_{};
  std::chrono::milliseconds ticket_lifetime_;
  std::function<Clock::time_point()> now_fn_;

  /// Anti-replay nonce tracking: nonce -> expiry timestamp (ms).
  mutable std::mutex nonce_mutex_;
  std::unordered_map<std::uint64_t, std::uint64_t> used_nonces_;
};

/// Client-side ticket store for caching session tickets.
///
/// Thread safety: All public methods are thread-safe (internally synchronized).
///
/// Usage:
/// ```cpp
/// SessionTicketStore store;
/// // After receiving ticket from server:
/// store.store_ticket("server1.example.com", ticket);
///
/// // On reconnection:
/// auto ticket = store.get_ticket("server1.example.com");
/// if (ticket) {
///   // Use ticket for 0-RTT handshake
/// }
/// ```
class SessionTicketStore {
 public:
  using Clock = std::chrono::system_clock;

  explicit SessionTicketStore(std::function<Clock::time_point()> now_fn = Clock::now);

  /// Store a session ticket for a server.
  /// @param server_id Server identifier (e.g., address:port).
  /// @param ticket The session ticket to cache.
  void store_ticket(const std::string& server_id, SessionTicket ticket);

  /// Retrieve a valid (non-expired) ticket for a server.
  /// @param server_id Server identifier.
  /// @return The cached ticket if available and not expired, nullopt otherwise.
  std::optional<SessionTicket> get_ticket(const std::string& server_id);

  /// Remove a ticket for a server (e.g., after failed 0-RTT attempt).
  /// @param server_id Server identifier.
  void remove_ticket(const std::string& server_id);

  /// Remove all expired tickets from the store.
  void cleanup_expired();

  /// Get the number of cached tickets.
  std::size_t size() const;

 private:
  std::function<Clock::time_point()> now_fn_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, SessionTicket> tickets_;
};

}  // namespace veil::handshake
