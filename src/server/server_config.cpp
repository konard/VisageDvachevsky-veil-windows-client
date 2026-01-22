#include "server/server_config.h"

#include <arpa/inet.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include <CLI/CLI.hpp>

#include "common/logging/logger.h"
#include "tun/routing.h"

namespace veil::server {

namespace {
// Helper to safely parse integer with validation
template <typename T>
bool safe_parse_int(const std::string& value, T& out, const std::string& field_name,
                    std::error_code& ec) {
  try {
    if constexpr (std::is_unsigned_v<T>) {
      // For unsigned types, use stoull and check for negative values
      if (!value.empty() && value[0] == '-') {
        LOG_ERROR("Configuration error: {} value '{}' cannot be negative", field_name, value);
        ec = std::make_error_code(std::errc::result_out_of_range);
        return false;
      }
      unsigned long long parsed = std::stoull(value);
      if (parsed > std::numeric_limits<T>::max()) {
        LOG_ERROR("Configuration error: {} value '{}' is out of range", field_name, value);
        ec = std::make_error_code(std::errc::result_out_of_range);
        return false;
      }
      out = static_cast<T>(parsed);
    } else {
      // For signed types, use stoll
      long long parsed = std::stoll(value);
      if (parsed < std::numeric_limits<T>::min() || parsed > std::numeric_limits<T>::max()) {
        LOG_ERROR("Configuration error: {} value '{}' is out of range", field_name, value);
        ec = std::make_error_code(std::errc::result_out_of_range);
        return false;
      }
      out = static_cast<T>(parsed);
    }
    return true;
  } catch (const std::invalid_argument&) {
    LOG_ERROR("Configuration error: {} value '{}' is not a valid number", field_name, value);
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  } catch (const std::out_of_range&) {
    LOG_ERROR("Configuration error: {} value '{}' is out of range", field_name, value);
    ec = std::make_error_code(std::errc::result_out_of_range);
    return false;
  }
}

// Helper to validate IPv4 address format
bool is_valid_ipv4(const std::string& ip) {
  struct in_addr addr;
  return inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

// Convert IPv4 string to uint32_t (network byte order -> host byte order)
std::uint32_t ipv4_to_uint(const std::string& ip) {
  struct in_addr addr;
  if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
    return 0;
  }
  return ntohl(addr.s_addr);
}
}  // namespace

bool parse_ini_value(const std::string& line, std::string& key, std::string& value) {
  if (line.empty() || line[0] == '#' || line[0] == ';') {
    return false;
  }
  if (line[0] == '[') {
    return false;
  }

  auto pos = line.find('=');
  if (pos == std::string::npos) {
    return false;
  }

  key = line.substr(0, pos);
  value = line.substr(pos + 1);

  while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
    key.pop_back();
  }
  while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) {
    key.erase(0, 1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.erase(0, 1);
  }

  return !key.empty();
}

std::string get_current_section(const std::string& line) {
  if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
    return line.substr(1, line.size() - 2);
  }
  return "";
}

bool parse_args(int argc, char* argv[], ServerConfig& config, std::error_code& ec) {
  CLI::App app{"VEIL VPN Server"};

  // General options.
  app.add_option("-c,--config", config.config_file, "Configuration file path");
  app.add_flag("-d,--daemon", config.daemon_mode, "Run as daemon");
  app.add_flag("-v,--verbose", config.verbose, "Enable verbose logging");

  // Network.
  app.add_option("-l,--listen", config.listen_address, "Listen address")->default_val("0.0.0.0");
  app.add_option("-p,--port", config.listen_port, "Listen port")->default_val(4433);

  // TUN device.
  app.add_option("--tun-name", config.tunnel.tun.device_name, "TUN device name")->default_val("veil0");
  app.add_option("--tun-ip", config.tunnel.tun.ip_address, "TUN device IP address")
      ->default_val("10.8.0.1");
  app.add_option("--tun-netmask", config.tunnel.tun.netmask, "TUN device netmask")
      ->default_val("255.255.255.0");
  app.add_option("--mtu", config.tunnel.tun.mtu, "MTU size")->default_val(1400);

  // Crypto.
  app.add_option("-k,--key", config.tunnel.key_file, "Pre-shared key file");
  app.add_option("--obfuscation-seed", config.tunnel.obfuscation_seed_file, "Obfuscation seed file");

  // NAT.
  app.add_option("--external-interface", config.nat.external_interface, "External interface for NAT")
      ->default_val("eth0");
  app.add_flag("--enable-nat", config.nat.enable_forwarding, "Enable NAT/masquerading")
      ->default_val(true);

  // Session management.
  app.add_option("--max-clients", config.max_clients, "Maximum number of clients")->default_val(256);
  int session_timeout_seconds = 300;
  app.add_option("--session-timeout", session_timeout_seconds, "Session timeout in seconds")
      ->default_val(300);

  // IP pool.
  app.add_option("--ip-pool-start", config.ip_pool_start, "IP pool start")->default_val("10.8.0.2");
  app.add_option("--ip-pool-end", config.ip_pool_end, "IP pool end")->default_val("10.8.0.254");

  // Daemon settings.
  app.add_option("--pid-file", config.pid_file, "PID file path");
  app.add_option("--log-file", config.log_file, "Log file path");
  app.add_option("--user", config.user, "Run as user");
  app.add_option("--group", config.group, "Run as group");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  // Load config file if specified.
  if (!config.config_file.empty()) {
    if (!load_config_file(config.config_file, config, ec)) {
      return false;
    }
  }

  // Convert session timeout.
  config.session_timeout = std::chrono::seconds(session_timeout_seconds);

  // Set up tunnel config.
  config.tunnel.local_port = config.listen_port;
  config.tunnel.verbose = config.verbose;

  // Set up NAT config.
  config.nat.internal_interface = config.tunnel.tun.device_name;

  return true;
}

bool load_config_file(const std::string& path, ServerConfig& config, std::error_code& ec) {
  std::ifstream file(path);
  if (!file) {
    ec = std::error_code(errno, std::generic_category());
    LOG_ERROR("Failed to open config file: {}", path);
    return false;
  }

  std::string line;
  std::string section;

  while (std::getline(file, line)) {
    std::string new_section = get_current_section(line);
    if (!new_section.empty()) {
      section = new_section;
      continue;
    }

    std::string key;
    std::string value;
    if (!parse_ini_value(line, key, value)) {
      continue;
    }

    if (section == "server" || section.empty()) {
      if (key == "listen_address") {
        config.listen_address = value;
      } else if (key == "listen_port") {
        std::uint16_t port;
        if (!safe_parse_int(value, port, "listen_port", ec)) {
          return false;
        }
        config.listen_port = port;
      } else if (key == "daemon") {
        config.daemon_mode = (value == "true" || value == "1" || value == "yes");
      } else if (key == "verbose") {
        config.verbose = (value == "true" || value == "1" || value == "yes");
      }
    } else if (section == "tun") {
      if (key == "device_name") {
        config.tunnel.tun.device_name = value;
      } else if (key == "ip_address") {
        config.tunnel.tun.ip_address = value;
      } else if (key == "netmask") {
        config.tunnel.tun.netmask = value;
      } else if (key == "mtu") {
        int mtu;
        if (!safe_parse_int(value, mtu, "mtu", ec)) {
          return false;
        }
        config.tunnel.tun.mtu = mtu;
      }
    } else if (section == "crypto") {
      if (key == "preshared_key_file") {
        config.tunnel.key_file = value;
      }
    } else if (section == "obfuscation") {
      if (key == "profile_seed_file") {
        config.tunnel.obfuscation_seed_file = value;
      }
    } else if (section == "nat") {
      if (key == "external_interface") {
        config.nat.external_interface = value;
      } else if (key == "enable_forwarding") {
        config.nat.enable_forwarding = (value == "true" || value == "1" || value == "yes");
      } else if (key == "use_masquerade") {
        config.nat.use_masquerade = (value == "true" || value == "1" || value == "yes");
      } else if (key == "snat_source") {
        config.nat.snat_source = value;
      }
    } else if (section == "sessions") {
      if (key == "max_clients") {
        std::size_t max_clients;
        if (!safe_parse_int(value, max_clients, "max_clients", ec)) {
          return false;
        }
        config.max_clients = max_clients;
      } else if (key == "session_timeout") {
        int timeout;
        if (!safe_parse_int(value, timeout, "session_timeout", ec)) {
          return false;
        }
        config.session_timeout = std::chrono::seconds(timeout);
      } else if (key == "cleanup_interval") {
        int interval;
        if (!safe_parse_int(value, interval, "cleanup_interval", ec)) {
          return false;
        }
        config.cleanup_interval = std::chrono::seconds(interval);
      }
    } else if (section == "ip_pool") {
      if (key == "start") {
        config.ip_pool_start = value;
      } else if (key == "end") {
        config.ip_pool_end = value;
      }
    } else if (section == "daemon") {
      if (key == "pid_file") {
        config.pid_file = value;
      } else if (key == "log_file") {
        config.log_file = value;
      } else if (key == "user") {
        config.user = value;
      } else if (key == "group") {
        config.group = value;
      }
    }
  }

  LOG_DEBUG("Loaded configuration from {}", path);
  return true;
}

bool validate_config(const ServerConfig& config, std::string& error) {
  if (config.listen_port == 0) {
    error = "Invalid listen port";
    return false;
  }

  if (config.tunnel.tun.ip_address.empty()) {
    error = "TUN IP address is required";
    return false;
  }

  if (!is_valid_ipv4(config.tunnel.tun.ip_address)) {
    error = "TUN IP address is not a valid IPv4 address: " + config.tunnel.tun.ip_address;
    return false;
  }

  if (config.tunnel.tun.mtu < 576 || config.tunnel.tun.mtu > 65535) {
    error = "MTU must be between 576 and 65535";
    return false;
  }

  if (config.max_clients == 0) {
    error = "Max clients must be greater than 0";
    return false;
  }

  // Limit max_clients to prevent resource exhaustion
  constexpr std::size_t kMaxClientsLimit = 10000;
  if (config.max_clients > kMaxClientsLimit) {
    error = "Max clients cannot exceed " + std::to_string(kMaxClientsLimit);
    return false;
  }

  // Validate IP pool
  if (config.ip_pool_start.empty()) {
    error = "IP pool start address is required";
    return false;
  }

  if (config.ip_pool_end.empty()) {
    error = "IP pool end address is required";
    return false;
  }

  if (!is_valid_ipv4(config.ip_pool_start)) {
    error = "IP pool start is not a valid IPv4 address: " + config.ip_pool_start;
    return false;
  }

  if (!is_valid_ipv4(config.ip_pool_end)) {
    error = "IP pool end is not a valid IPv4 address: " + config.ip_pool_end;
    return false;
  }

  std::uint32_t pool_start = ipv4_to_uint(config.ip_pool_start);
  std::uint32_t pool_end = ipv4_to_uint(config.ip_pool_end);

  if (pool_start == 0 || pool_end == 0) {
    error = "IP pool addresses cannot be 0.0.0.0";
    return false;
  }

  if (pool_start > pool_end) {
    error = "IP pool start (" + config.ip_pool_start + ") must be <= IP pool end (" +
            config.ip_pool_end + ")";
    return false;
  }

  // Check that the IP pool has enough addresses for max_clients
  std::uint32_t pool_size = pool_end - pool_start + 1;
  if (pool_size < config.max_clients) {
    error = "IP pool size (" + std::to_string(pool_size) + ") is smaller than max_clients (" +
            std::to_string(config.max_clients) + ")";
    return false;
  }

  // Validate NAT external interface is not empty if NAT is enabled
  if (config.nat.enable_forwarding && config.nat.external_interface.empty()) {
    error = "NAT external interface is required when NAT is enabled. "
            "Use --external-interface to specify it, or enable auto-detection.";
    return false;
  }

  return true;
}

bool finalize_config(ServerConfig& config, std::error_code& ec) {
  // Auto-detect external interface if using default "eth0" or empty
  // and NAT is enabled.
  if (config.nat.enable_forwarding) {
    // Check if the interface exists, and if not, try to auto-detect.
    bool need_detection = config.nat.external_interface.empty() ||
                          config.nat.external_interface == "eth0";

    if (need_detection) {
      auto detected = tun::detect_external_interface(ec);
      if (detected) {
        // Only replace if we detected something different or if it was empty.
        if (config.nat.external_interface.empty() ||
            (config.nat.external_interface == "eth0" && *detected != "eth0")) {
          LOG_INFO("Auto-detected external interface: {} (was: {})", *detected,
                   config.nat.external_interface.empty() ? "(empty)"
                                                         : config.nat.external_interface);
          config.nat.external_interface = *detected;
        }
      } else if (config.nat.external_interface.empty()) {
        // Detection failed and no fallback specified.
        LOG_ERROR("Failed to auto-detect external interface for NAT. "
                  "Please specify --external-interface explicitly.");
        return false;
      } else {
        // Detection failed but we have a fallback (eth0), log a warning.
        LOG_WARN("Could not auto-detect external interface; using default '{}'. "
                 "If NAT doesn't work, specify --external-interface explicitly.",
                 config.nat.external_interface);
      }
    }
  }

  return true;
}

}  // namespace veil::server
