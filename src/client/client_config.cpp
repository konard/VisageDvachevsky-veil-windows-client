#include "client/client_config.h"

#include <arpa/inet.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include <CLI/CLI.hpp>

#include "common/logging/logger.h"

namespace veil::client {

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

// Simple INI parser for configuration files.
bool parse_ini_value(const std::string& line, std::string& key, std::string& value) {
  // Skip comments and empty lines.
  if (line.empty() || line[0] == '#' || line[0] == ';') {
    return false;
  }

  // Skip section headers.
  if (line[0] == '[') {
    return false;
  }

  // Find '=' delimiter.
  auto pos = line.find('=');
  if (pos == std::string::npos) {
    return false;
  }

  // Extract key and value.
  key = line.substr(0, pos);
  value = line.substr(pos + 1);

  // Trim whitespace.
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
}  // namespace

bool parse_args(int argc, char* argv[], ClientConfig& config, std::error_code& ec) {
  CLI::App app{"VEIL VPN Client"};

  // General options.
  app.add_option("-c,--config", config.config_file, "Configuration file path");
  app.add_flag("-d,--daemon", config.daemon_mode, "Run as daemon");
  app.add_flag("-v,--verbose", config.verbose, "Enable verbose logging");

  // Server connection.
  app.add_option("-s,--server", config.tunnel.server_address, "Server address");
  app.add_option("-p,--port", config.tunnel.server_port, "Server port")->default_val(4433);

  // TUN device.
  app.add_option("--tun-name", config.tunnel.tun.device_name, "TUN device name")->default_val("veil0");
  app.add_option("--tun-ip", config.tunnel.tun.ip_address, "TUN device IP address")
      ->default_val("10.8.0.2");
  app.add_option("--tun-netmask", config.tunnel.tun.netmask, "TUN device netmask")
      ->default_val("255.255.255.0");
  app.add_option("--mtu", config.tunnel.tun.mtu, "MTU size")->default_val(1400);

  // Crypto.
  app.add_option("-k,--key", config.tunnel.key_file, "Pre-shared key file");
  app.add_option("--obfuscation-seed", config.tunnel.obfuscation_seed_file, "Obfuscation seed file");

  // Routing.
  app.add_flag("--default-route", config.set_default_route, "Set as default route");
  app.add_option("--route", config.routes, "Additional routes to add (CIDR notation)");

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

  // Copy verbose flag to tunnel config.
  config.tunnel.verbose = config.verbose;

  return true;
}

bool load_config_file(const std::string& path, ClientConfig& config, std::error_code& ec) {
  std::ifstream file(path);
  if (!file) {
    ec = std::error_code(errno, std::generic_category());
    LOG_ERROR("Failed to open config file: {}", path);
    return false;
  }

  std::string line;
  std::string section;

  while (std::getline(file, line)) {
    // Check for section header.
    std::string new_section = get_current_section(line);
    if (!new_section.empty()) {
      section = new_section;
      continue;
    }

    // Parse key-value pair.
    std::string key;
    std::string value;
    if (!parse_ini_value(line, key, value)) {
      continue;
    }

    // Apply value based on section and key.
    if (section == "client" || section.empty()) {
      if (key == "server_address") {
        config.tunnel.server_address = value;
      } else if (key == "server_port") {
        std::uint16_t port;
        if (!safe_parse_int(value, port, "server_port", ec)) {
          return false;
        }
        config.tunnel.server_port = port;
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
    } else if (section == "routing") {
      if (key == "default_route") {
        config.set_default_route = (value == "true" || value == "1" || value == "yes");
      } else if (key == "routes") {
        // Parse comma-separated routes.
        std::istringstream iss(value);
        std::string route;
        while (std::getline(iss, route, ',')) {
          while (!route.empty() && route.front() == ' ') {
            route.erase(0, 1);
          }
          while (!route.empty() && route.back() == ' ') {
            route.pop_back();
          }
          if (!route.empty()) {
            config.routes.push_back(route);
          }
        }
      }
    } else if (section == "connection") {
      if (key == "reconnect_interval_ms") {
        int interval;
        if (!safe_parse_int(value, interval, "reconnect_interval_ms", ec)) {
          return false;
        }
        config.tunnel.reconnect_delay = std::chrono::milliseconds(interval);
      } else if (key == "auto_reconnect") {
        config.tunnel.auto_reconnect = (value == "true" || value == "1" || value == "yes");
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

bool validate_config(const ClientConfig& config, std::string& error) {
  if (config.tunnel.server_address.empty()) {
    error = "Server address is required";
    return false;
  }

  if (config.tunnel.server_port == 0) {
    error = "Invalid server port";
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

  return true;
}

}  // namespace veil::client
