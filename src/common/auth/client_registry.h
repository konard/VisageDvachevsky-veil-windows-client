#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace veil::auth {

/// Minimum allowed PSK size (256 bits)
inline constexpr std::size_t kMinPskSize = 32;

/// Maximum allowed PSK size (512 bits)
inline constexpr std::size_t kMaxPskSize = 64;

/// Maximum client_id length
inline constexpr std::size_t kMaxClientIdLength = 64;

/// Validate client_id format.
/// Client IDs must be non-empty, at most kMaxClientIdLength chars,
/// and contain only alphanumeric characters, hyphens, and underscores.
bool is_valid_client_id(const std::string& client_id);

/// Validate PSK size (must be between kMinPskSize and kMaxPskSize bytes).
bool is_valid_psk_size(std::size_t size);

/// Entry for a single client in the registry.
struct ClientEntry {
  std::vector<std::uint8_t> psk;  // Pre-shared key for this client
  bool enabled;                    // Whether the client is allowed to connect
};

/// ClientRegistry manages per-client PSKs for authentication.
///
/// This addresses Issue #87: PSK authentication doesn't scale (no per-client keys).
///
/// Key features:
/// - Thread-safe access via shared_mutex (multiple readers, single writer)
/// - Secure memory handling (PSKs are cleared with sodium_memzero on removal)
/// - Optional fallback PSK for backward compatibility with legacy clients
/// - Client enable/disable for revocation without key removal
///
/// Usage:
/// ```cpp
/// ClientRegistry registry;
/// registry.add_client("alice", psk_alice);
/// registry.add_client("bob", psk_bob);
/// registry.set_fallback_psk(legacy_psk);  // Optional
///
/// // Look up PSK for authentication
/// auto psk = registry.get_psk("alice");
/// if (psk) {
///   // Use *psk for authentication
/// }
///
/// // Disable a client (revocation)
/// registry.disable_client("alice");
/// ```
class ClientRegistry {
 public:
  ClientRegistry() = default;
  ~ClientRegistry();

  // Non-copyable (contains sensitive data)
  ClientRegistry(const ClientRegistry&) = delete;
  ClientRegistry& operator=(const ClientRegistry&) = delete;

  // Movable
  ClientRegistry(ClientRegistry&& other) noexcept;
  ClientRegistry& operator=(ClientRegistry&& other) noexcept;

  /// Set the fallback PSK for clients not in the registry.
  /// This provides backward compatibility with legacy clients.
  /// @param psk The fallback pre-shared key (must be kMinPskSize-kMaxPskSize bytes).
  /// @return true if PSK was set, false if invalid size.
  bool set_fallback_psk(std::vector<std::uint8_t> psk);

  /// Clear the fallback PSK.
  void clear_fallback_psk();

  /// Check if a fallback PSK is configured.
  bool has_fallback_psk() const;

  /// Add a client with a PSK.
  /// @param client_id Unique identifier for the client.
  /// @param psk The pre-shared key (must be kMinPskSize-kMaxPskSize bytes).
  /// @return true if client was added, false if invalid client_id, invalid PSK size, or client already exists.
  bool add_client(const std::string& client_id, std::vector<std::uint8_t> psk);

  /// Remove a client from the registry.
  /// PSK is securely cleared from memory.
  /// @param client_id The client to remove.
  /// @return true if client was removed, false if not found.
  bool remove_client(const std::string& client_id);

  /// Get the PSK for a specific client.
  /// @param client_id The client identifier.
  /// @return The PSK if client exists and is enabled, nullopt otherwise.
  std::optional<std::vector<std::uint8_t>> get_psk(const std::string& client_id) const;

  /// Get the PSK for a client, falling back to the fallback PSK if not found.
  /// @param client_id The client identifier (can be empty for fallback-only lookup).
  /// @return The client's PSK, or fallback PSK if client not found, or nullopt if neither exists.
  std::optional<std::vector<std::uint8_t>> get_psk_or_fallback(const std::string& client_id) const;

  /// Get the fallback PSK (if configured).
  /// @return The fallback PSK, or nullopt if not configured.
  std::optional<std::vector<std::uint8_t>> get_fallback_psk() const;

  /// Enable a previously disabled client.
  /// @param client_id The client to enable.
  /// @return true if client was enabled, false if not found.
  bool enable_client(const std::string& client_id);

  /// Disable a client (revocation without key removal).
  /// @param client_id The client to disable.
  /// @return true if client was disabled, false if not found.
  bool disable_client(const std::string& client_id);

  /// Check if a client exists in the registry.
  bool has_client(const std::string& client_id) const;

  /// Check if a client is enabled.
  /// @return true if client exists and is enabled, false otherwise.
  bool is_client_enabled(const std::string& client_id) const;

  /// Get the number of registered clients.
  std::size_t client_count() const;

  /// Get all client IDs in the registry.
  std::vector<std::string> get_client_ids() const;

  /// Get all PSKs in the registry (for trial decryption).
  /// Returns pairs of (client_id, psk) for enabled clients only.
  /// This is used by MultiClientHandshakeResponder for PSK lookup.
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> get_all_enabled_psks() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, ClientEntry> clients_;
  std::optional<std::vector<std::uint8_t>> fallback_psk_;
};

}  // namespace veil::auth
