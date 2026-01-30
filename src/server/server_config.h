#pragma once

#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include "tunnel/tunnel.h"
#include "tun/routing.h"

namespace veil::server {

// Per-client PSK configuration entry (Issue #87).
// Each client can have a unique PSK for authentication.
struct ClientPskEntry {
  std::string client_id;             // Unique identifier (alphanumeric, hyphens, underscores)
  std::vector<std::uint8_t> psk;     // Pre-shared key (32-64 bytes)
  bool enabled{true};                // Whether client is allowed to connect
};

// Server-specific configuration.
struct ServerConfig {
  // General settings.
  std::string config_file;
  bool daemon_mode{false};
  bool verbose{false};

  // Tunnel configuration.
  tunnel::TunnelConfig tunnel;

  // NAT configuration.
  tun::NatConfig nat;

  // Session management.
  std::size_t max_clients{256};
  std::chrono::seconds session_timeout{300};
  std::chrono::seconds cleanup_interval{60};

  // Network.
  std::string listen_address{"0.0.0.0"};
  std::uint16_t listen_port{4433};

  // IP pool for clients.
  std::string ip_pool_start{"10.8.0.2"};
  std::string ip_pool_end{"10.8.0.254"};

  // Daemon settings.
  std::string pid_file{"/var/run/veil-server.pid"};
  std::string log_file;
  std::string user;
  std::string group;

  // Per-client PSK authentication (Issue #87).
  // Each client can have a unique PSK for individual revocation and audit.
  std::vector<ClientPskEntry> client_psks;

  // Fallback PSK for backward compatibility with legacy clients.
  // If set, clients without a specific PSK entry will use this key.
  std::vector<std::uint8_t> fallback_psk;
};

// Parse command-line arguments into configuration.
bool parse_args(int argc, char* argv[], ServerConfig& config, std::error_code& ec);

// Load configuration from INI file.
bool load_config_file(const std::string& path, ServerConfig& config, std::error_code& ec);

// Validate configuration.
bool validate_config(const ServerConfig& config, std::string& error);

// Finalize configuration (auto-detect interfaces, etc.).
// Call this after parsing but before validation.
bool finalize_config(ServerConfig& config, std::error_code& ec);

}  // namespace veil::server
