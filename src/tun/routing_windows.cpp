// Windows routing implementation using IP Helper API
// This file is only compiled on Windows platforms

#ifdef _WIN32

#include "tun/routing.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <array>
#include <ranges>
#include <sstream>
#include <system_error>

#include "common/logging/logger.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace {

std::error_code last_error() {
  return std::error_code(static_cast<int>(GetLastError()), std::system_category());
}

// Convert IPv4 address string to DWORD (network byte order)
bool parse_ipv4(const std::string& addr, DWORD& result) {
  IN_ADDR in_addr;
  if (InetPtonA(AF_INET, addr.c_str(), &in_addr) != 1) {
    return false;
  }
  result = in_addr.S_un.S_addr;
  return true;
}

// Convert netmask to prefix length
int netmask_to_prefix(const std::string& netmask) {
  IN_ADDR mask;
  if (InetPtonA(AF_INET, netmask.c_str(), &mask) != 1) {
    return 32;  // Default to /32 on error
  }

  ULONG mask_bits = ntohl(mask.S_un.S_addr);
  int prefix = 0;
  while (mask_bits & 0x80000000) {
    prefix++;
    mask_bits <<= 1;
  }
  return prefix;
}

// Get interface LUID from interface name
bool get_interface_luid(const std::string& interface_name, NET_LUID& luid) {
  // Try to convert interface name to index first
  std::wstring wide_name(interface_name.begin(), interface_name.end());
  NET_IFINDEX index = 0;

  // Try GetAdapterIndex
  IP_ADAPTER_INDEX_MAP adapter_map;
  adapter_map.Index = 0;
  wcscpy_s(adapter_map.Name, wide_name.c_str());

  DWORD result = GetAdapterIndex(adapter_map.Name, &index);
  if (result == NO_ERROR) {
    if (ConvertInterfaceIndexToLuid(index, &luid) == NO_ERROR) {
      return true;
    }
  }

  // Try to find by alias
  ULONG buf_size = 0;
  GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &buf_size);

  std::vector<BYTE> buffer(buf_size);
  auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

  result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, addresses, &buf_size);
  if (result != NO_ERROR) {
    return false;
  }

  for (auto* adapter = addresses; adapter; adapter = adapter->Next) {
    std::string friendly_name;
    // Convert wide string to narrow
    int len = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1,
                                   nullptr, 0, nullptr, nullptr);
    if (len > 0) {
      friendly_name.resize(len - 1);
      WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1,
                          &friendly_name[0], len, nullptr, nullptr);
    }

    std::string adapter_name(adapter->AdapterName);

    if (friendly_name == interface_name || adapter_name == interface_name) {
      luid = adapter->Luid;
      return true;
    }
  }

  return false;
}

// Get default gateway info
bool get_default_route(std::string& gateway, std::string& interface_name) {
  ULONG buf_size = 0;
  GetIpForwardTable(nullptr, &buf_size, FALSE);

  std::vector<BYTE> buffer(buf_size);
  auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());

  if (GetIpForwardTable(table, &buf_size, FALSE) != NO_ERROR) {
    return false;
  }

  for (DWORD i = 0; i < table->dwNumEntries; i++) {
    const auto& row = table->table[i];
    if (row.dwForwardDest == 0 && row.dwForwardMask == 0) {
      // This is a default route
      char gw_str[INET_ADDRSTRLEN];
      IN_ADDR gw_addr;
      gw_addr.S_un.S_addr = row.dwForwardNextHop;
      InetNtopA(AF_INET, &gw_addr, gw_str, INET_ADDRSTRLEN);
      gateway = gw_str;

      // Get interface name from index
      NET_LUID luid;
      if (ConvertInterfaceIndexToLuid(row.dwForwardIfIndex, &luid) == NO_ERROR) {
        wchar_t alias[NDIS_IF_MAX_STRING_SIZE + 1];
        if (ConvertInterfaceLuidToAlias(&luid, alias, NDIS_IF_MAX_STRING_SIZE + 1) == NO_ERROR) {
          int len = WideCharToMultiByte(CP_UTF8, 0, alias, -1, nullptr, 0, nullptr, nullptr);
          if (len > 0) {
            interface_name.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, alias, -1, &interface_name[0], len, nullptr, nullptr);
          }
        }
      }
      return true;
    }
  }

  return false;
}

}  // namespace

namespace veil::tun {

RouteManager::RouteManager() = default;

RouteManager::~RouteManager() { cleanup(); }

bool RouteManager::add_route(const Route& route, std::error_code& ec) {
  MIB_IPFORWARD_ROW2 row;
  InitializeIpForwardEntry(&row);

  // Get interface LUID
  if (!route.interface.empty()) {
    if (!get_interface_luid(route.interface, row.InterfaceLuid)) {
      ec = std::make_error_code(std::errc::no_such_device);
      LOG_ERROR("Interface not found: {}", route.interface);
      return false;
    }
  }

  // Set destination
  row.DestinationPrefix.Prefix.si_family = AF_INET;
  if (!parse_ipv4(route.destination, row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("Invalid destination address: {}", route.destination);
    return false;
  }

  // Set prefix length from netmask
  if (route.netmask.empty() || route.netmask == "0.0.0.0") {
    row.DestinationPrefix.PrefixLength = 0;
  } else {
    row.DestinationPrefix.PrefixLength = static_cast<UINT8>(netmask_to_prefix(route.netmask));
  }

  // Set gateway (next hop)
  if (!route.gateway.empty()) {
    row.NextHop.si_family = AF_INET;
    if (!parse_ipv4(route.gateway, row.NextHop.Ipv4.sin_addr.S_un.S_addr)) {
      ec = std::make_error_code(std::errc::invalid_argument);
      LOG_ERROR("Invalid gateway address: {}", route.gateway);
      return false;
    }
  }

  // Set metric
  row.Metric = (route.metric > 0) ? static_cast<ULONG>(route.metric) : 0;
  row.Protocol = MIB_IPPROTO_NETMGMT;
  row.Origin = NlroManual;
  row.ValidLifetime = 0xFFFFFFFF;  // Infinite
  row.PreferredLifetime = 0xFFFFFFFF;

  DWORD result = CreateIpForwardEntry2(&row);
  if (result != NO_ERROR && result != ERROR_OBJECT_ALREADY_EXISTS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to add route to {}: {}", route.destination, ec.message());
    return false;
  }

  added_routes_.push_back(route);
  LOG_INFO("Added route: {} via {} dev {}",
           route.destination,
           route.gateway.empty() ? "(direct)" : route.gateway,
           route.interface);
  return true;
}

bool RouteManager::remove_route(const Route& route, std::error_code& ec) {
  MIB_IPFORWARD_ROW2 row;
  InitializeIpForwardEntry(&row);

  // Get interface LUID
  if (!route.interface.empty()) {
    if (!get_interface_luid(route.interface, row.InterfaceLuid)) {
      LOG_WARN("Interface not found for route removal: {}", route.interface);
    }
  }

  // Set destination
  row.DestinationPrefix.Prefix.si_family = AF_INET;
  if (!parse_ipv4(route.destination, row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

  if (route.netmask.empty() || route.netmask == "0.0.0.0") {
    row.DestinationPrefix.PrefixLength = 0;
  } else {
    row.DestinationPrefix.PrefixLength = static_cast<UINT8>(netmask_to_prefix(route.netmask));
  }

  // Set gateway
  if (!route.gateway.empty()) {
    row.NextHop.si_family = AF_INET;
    parse_ipv4(route.gateway, row.NextHop.Ipv4.sin_addr.S_un.S_addr);
  }

  DWORD result = DeleteIpForwardEntry2(&row);
  if (result != NO_ERROR && result != ERROR_NOT_FOUND) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to remove route to {}: {}", route.destination, ec.message());
    return false;
  }

  LOG_INFO("Removed route: {}", route.destination);
  return true;
}

bool RouteManager::add_default_route(const std::string& interface, const std::string& gateway,
                                      int metric, std::error_code& ec) {
  Route route;
  route.destination = "0.0.0.0";
  route.netmask = "0.0.0.0";
  route.gateway = gateway;
  route.interface = interface;
  route.metric = metric;
  return add_route(route, ec);
}

bool RouteManager::remove_default_route(const std::string& interface, std::error_code& ec) {
  Route route;
  route.destination = "0.0.0.0";
  route.netmask = "0.0.0.0";
  route.interface = interface;
  return remove_route(route, ec);
}

bool RouteManager::set_ip_forwarding(bool enable, std::error_code& ec) {
  // Save original state if not already saved
  if (!forwarding_state_saved_) {
    std::error_code ignored;
    original_forwarding_state_ = is_ip_forwarding_enabled(ignored);
    forwarding_state_saved_ = true;
  }

  // On Windows, IP forwarding is controlled via registry or netsh
  // Using registry approach
  HKEY key;
  LONG result = RegOpenKeyExA(
      HKEY_LOCAL_MACHINE,
      "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
      0, KEY_SET_VALUE, &key);

  if (result != ERROR_SUCCESS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to open registry key for IP forwarding: {}", ec.message());
    return false;
  }

  DWORD value = enable ? 1 : 0;
  result = RegSetValueExA(key, "IPEnableRouter", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
  RegCloseKey(key);

  if (result != ERROR_SUCCESS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    LOG_ERROR("Failed to set IP forwarding: {}", ec.message());
    return false;
  }

  LOG_INFO("IP forwarding {}", enable ? "enabled" : "disabled");
  return true;
}

bool RouteManager::is_ip_forwarding_enabled(std::error_code& ec) {
  HKEY key;
  LONG result = RegOpenKeyExA(
      HKEY_LOCAL_MACHINE,
      "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
      0, KEY_READ, &key);

  if (result != ERROR_SUCCESS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    return false;
  }

  DWORD value = 0;
  DWORD size = sizeof(value);
  result = RegQueryValueExA(key, "IPEnableRouter", nullptr, nullptr,
                            reinterpret_cast<BYTE*>(&value), &size);
  RegCloseKey(key);

  if (result != ERROR_SUCCESS) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    return false;
  }

  return value != 0;
}

bool RouteManager::configure_nat(const NatConfig& config, std::error_code& ec) {
  // Windows NAT configuration requires Windows Routing and Remote Access Service (RRAS)
  // or Internet Connection Sharing (ICS), or Windows Firewall with NAT rules
  //
  // For a VPN client, we typically don't need NAT (that's server-side)
  // But for completeness, we can use netsh to configure NAT

  LOG_WARN("Windows NAT configuration requires elevated privileges and RRAS or ICS");

  // Enable IP forwarding first
  if (config.enable_forwarding) {
    if (!set_ip_forwarding(true, ec)) {
      return false;
    }
  }

  // On Windows, NAT is typically configured via:
  // 1. Internet Connection Sharing (ICS) - UI based
  // 2. Routing and Remote Access Service (RRAS) - server feature
  // 3. netsh routing ip nat commands (requires RRAS)
  // 4. Windows Firewall NAT rules (newer approach)

  // For a proper implementation, we would use the Windows Filtering Platform (WFP)
  // or netsh commands. This is a simplified stub that logs the request.

  LOG_INFO("NAT configuration requested: {} -> {} (subnet: {})",
           config.internal_interface, config.external_interface, config.vpn_subnet);

  nat_configured_ = true;
  current_nat_config_ = config;

  return true;
}

bool RouteManager::remove_nat(const NatConfig& config, std::error_code& ec) {
  // Restore IP forwarding state if we changed it
  if (forwarding_state_saved_) {
    set_ip_forwarding(original_forwarding_state_, ec);
    forwarding_state_saved_ = false;
  }

  nat_configured_ = false;
  LOG_INFO("NAT removed");
  return true;
}

std::optional<SystemState> RouteManager::get_system_state(std::error_code& ec) {
  SystemState state;
  state.ip_forwarding_enabled = is_ip_forwarding_enabled(ec);

  // Get default route info
  get_default_route(state.default_gateway, state.default_interface);

  return state;
}

bool RouteManager::save_routes(std::error_code& ec) {
  // On Windows, we just track the routes we add in added_routes_
  LOG_DEBUG("Routes saved to memory");
  return true;
}

bool RouteManager::restore_routes(std::error_code& ec) {
  // Remove added routes in reverse order
  for (auto& route : std::ranges::reverse_view(added_routes_)) {
    std::error_code ignored;
    remove_route(route, ignored);
  }
  added_routes_.clear();

  // Restore IP forwarding state
  if (forwarding_state_saved_) {
    set_ip_forwarding(original_forwarding_state_, ec);
    forwarding_state_saved_ = false;
  }

  return true;
}

bool RouteManager::route_exists(const Route& route, std::error_code& ec) {
  ULONG buf_size = 0;
  GetIpForwardTable(nullptr, &buf_size, FALSE);

  std::vector<BYTE> buffer(buf_size);
  auto* table = reinterpret_cast<MIB_IPFORWARDTABLE*>(buffer.data());

  DWORD result = GetIpForwardTable(table, &buf_size, FALSE);
  if (result != NO_ERROR) {
    ec = std::error_code(static_cast<int>(result), std::system_category());
    return false;
  }

  DWORD dest_addr = 0;
  if (!parse_ipv4(route.destination, dest_addr)) {
    return false;
  }

  for (DWORD i = 0; i < table->dwNumEntries; i++) {
    if (table->table[i].dwForwardDest == dest_addr) {
      return true;
    }
  }

  return false;
}

void RouteManager::cleanup() {
  std::error_code ec;

  // Remove NAT if configured
  if (nat_configured_) {
    remove_nat(current_nat_config_, ec);
  }

  // Restore routes
  restore_routes(ec);
}

// Stubs for methods used in Linux but not needed in Windows implementation
std::optional<std::string> RouteManager::execute_command(const std::string& command,
                                                          std::error_code& ec) {
  // Not used in Windows implementation
  return std::nullopt;
}

bool RouteManager::execute_command_check(const std::string& command, std::error_code& ec) {
  // Not used in Windows implementation
  return false;
}

bool RouteManager::is_tool_available(const std::string& tool, std::error_code& ec) {
  // Not used in Windows implementation - we use API calls directly
  return false;
}

bool RouteManager::check_firewall_availability(std::error_code& ec) {
  // Windows Firewall is always available on modern Windows
  return true;
}

void RouteManager::log_iptables_state(const std::string& phase) {
  // Not applicable to Windows
}

std::string RouteManager::build_nat_command(const NatConfig& config, bool add) {
  // Not used in Windows implementation
  return "";
}

std::optional<std::string> detect_external_interface(std::error_code& ec) {
  // Use the helper function to get the default route info
  std::string gateway;
  std::string interface_name;

  if (!get_default_route(gateway, interface_name)) {
    ec = std::make_error_code(std::errc::no_such_device_or_address);
    LOG_ERROR("No default route found. Is the network configured?");
    return std::nullopt;
  }

  if (interface_name.empty()) {
    ec = std::make_error_code(std::errc::no_such_device);
    LOG_ERROR("Default route found but interface name is empty");
    return std::nullopt;
  }

  LOG_INFO("Auto-detected external interface: {}", interface_name);
  return interface_name;
}

}  // namespace veil::tun

#endif  // _WIN32
