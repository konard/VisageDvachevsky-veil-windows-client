// Windows UDP socket implementation using select
// This file is only compiled on Windows platforms

#ifdef _WIN32

#include "transport/udp_socket/udp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>  // For GetBestInterface

#include <array>
#include <chrono>
#include <cstring>
#include <system_error>
#include <vector>

#include "common/logging/logger.h"

#pragma comment(lib, "iphlpapi.lib")

// IP_BOUND_IF may not be defined in older SDKs
#ifndef IP_BOUND_IF
#define IP_BOUND_IF 31
#endif

namespace {
std::error_code last_error() {
  return std::error_code(WSAGetLastError(), std::system_category());
}

bool resolve(const veil::transport::UdpEndpoint& endpoint, sockaddr_in& addr) {
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(endpoint.port);
  if (inet_pton(AF_INET, endpoint.host.c_str(), &addr.sin_addr) != 1) {
    return false;
  }
  return true;
}

void fill_endpoint(const sockaddr_in& addr, veil::transport::UdpEndpoint& endpoint) {
  std::array<char, INET_ADDRSTRLEN> buffer{};
  const char* res = inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), static_cast<socklen_t>(buffer.size()));
  endpoint.host = (res != nullptr) ? buffer.data() : "";
  endpoint.port = ntohs(addr.sin_port);
}

// Set socket to non-blocking mode on Windows.
bool set_nonblocking(SOCKET s) {
  u_long mode = 1;
  return ioctlsocket(s, FIONBIO, &mode) == 0;
}
}  // namespace

namespace veil::transport {

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
  close();
}

bool UdpSocket::configure_socket(bool reuse_port, std::error_code& ec) {
  const char enable = 1;
  if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
    ec = last_error();
    return false;
  }
  // Windows doesn't support SO_REUSEPORT.
  (void)reuse_port;
  return true;
}

bool UdpSocket::open(std::uint16_t bind_port, bool reuse_port, std::error_code& ec) {
  // Initialize Winsock if not already done.
  WSADATA wsa_data;
  int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (result != 0) {
    ec = std::error_code(result, std::system_category());
    LOG_ERROR("[UDP] WSAStartup failed: {}", ec.message());
    return false;
  }

  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    ec = last_error();
    LOG_ERROR("[UDP] socket() failed: {}", ec.message());
    return false;
  }
  fd_ = static_cast<std::uintptr_t>(s);
  LOG_DEBUG("[UDP] Created UDP socket, fd={}", static_cast<int>(s));

  // Set non-blocking mode.
  if (!set_nonblocking(s)) {
    ec = last_error();
    LOG_ERROR("[UDP] Failed to set non-blocking mode: {}", ec.message());
    close();
    return false;
  }

  if (!configure_socket(reuse_port, ec)) {
    LOG_ERROR("[UDP] Failed to configure socket: {}", ec.message());
    close();
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bind_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    LOG_ERROR("[UDP] bind() to port {} failed: {}", bind_port, ec.message());
    close();
    return false;
  }

  // Get actual bound port if bind_port was 0
  sockaddr_in bound_addr{};
  int addr_len = sizeof(bound_addr);
  if (getsockname(s, reinterpret_cast<sockaddr*>(&bound_addr), &addr_len) == 0) {
    std::uint16_t actual_port = ntohs(bound_addr.sin_port);
    LOG_INFO("[UDP] Socket bound successfully to 0.0.0.0:{}", actual_port);

    // Log local interface address for diagnostics
    std::array<char, INET_ADDRSTRLEN> local_addr_str{};
    if (inet_ntop(AF_INET, &bound_addr.sin_addr, local_addr_str.data(),
                  static_cast<socklen_t>(local_addr_str.size())) != nullptr) {
      LOG_INFO("[UDP] Socket local address: {}:{}", local_addr_str.data(), actual_port);
    }
  } else {
    LOG_INFO("[UDP] Socket bound successfully to 0.0.0.0:{}", bind_port);
  }

  return true;
}

// Get the IP address of a network interface by its index
bool get_interface_ip(DWORD interface_index, IN_ADDR& out_addr, std::error_code& ec) {
  // Allocate buffer for adapter addresses
  ULONG buffer_size = 15000;  // Recommended initial size
  std::vector<BYTE> buffer(buffer_size);
  PIP_ADAPTER_ADDRESSES adapter_addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

  // Get adapter addresses
  ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                                      adapter_addresses, &buffer_size);

  if (result == ERROR_BUFFER_OVERFLOW) {
    // Buffer too small, resize and try again
    buffer.resize(buffer_size);
    adapter_addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                                  adapter_addresses, &buffer_size);
  }

  if (result != NO_ERROR) {
    ec = std::error_code(result, std::system_category());
    LOG_ERROR("[UDP] GetAdaptersAddresses failed: {}", ec.message());
    return false;
  }

  // Find the adapter with matching interface index
  for (PIP_ADAPTER_ADDRESSES adapter = adapter_addresses; adapter != nullptr; adapter = adapter->Next) {
    if (adapter->IfIndex == interface_index || adapter->Ipv6IfIndex == interface_index) {
      // Found the adapter, now get its first unicast IPv4 address
      for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress;
           unicast != nullptr; unicast = unicast->Next) {
        if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
          sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
          out_addr = addr_in->sin_addr;

          std::array<char, INET_ADDRSTRLEN> addr_str{};
          inet_ntop(AF_INET, &out_addr, addr_str.data(), static_cast<socklen_t>(addr_str.size()));
          LOG_INFO("[UDP] Found IP {} for interface index {}", addr_str.data(), interface_index);
          return true;
        }
      }
    }
  }

  ec = std::make_error_code(std::errc::no_such_device);
  LOG_ERROR("[UDP] No IPv4 address found for interface index {}", interface_index);
  return false;
}

bool UdpSocket::bind_to_interface(std::uint32_t interface_index, std::error_code& ec) {
  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    LOG_ERROR("[UDP] Cannot bind to interface: socket is invalid");
    return false;
  }

  // Get the IP address of the interface
  IN_ADDR interface_ip;
  if (!get_interface_ip(static_cast<DWORD>(interface_index), interface_ip, ec)) {
    LOG_ERROR("[UDP] Failed to get IP address for interface {}: {}", interface_index, ec.message());
    return false;
  }

  // Rebind the socket to the specific interface IP address
  // First, we need to get the current bound port
  sockaddr_in current_addr{};
  int addr_len = sizeof(current_addr);
  if (getsockname(s, reinterpret_cast<sockaddr*>(&current_addr), &addr_len) != 0) {
    ec = last_error();
    LOG_ERROR("[UDP] getsockname() failed: {}", ec.message());
    return false;
  }
  std::uint16_t bound_port = ntohs(current_addr.sin_port);

  // Close the current socket
  closesocket(s);

  // Create a new socket
  s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    ec = last_error();
    LOG_ERROR("[UDP] socket() failed during rebind: {}", ec.message());
    return false;
  }
  fd_ = static_cast<std::uintptr_t>(s);

  // Set non-blocking mode
  if (!set_nonblocking(s)) {
    ec = last_error();
    LOG_ERROR("[UDP] Failed to set non-blocking mode during rebind: {}", ec.message());
    closesocket(s);
    fd_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    return false;
  }

  // Configure socket options
  const char enable = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
    ec = last_error();
    LOG_ERROR("[UDP] setsockopt(SO_REUSEADDR) failed during rebind: {}", ec.message());
    closesocket(s);
    fd_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    return false;
  }

  // Bind to the specific interface IP and the same port
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(bound_port);
  bind_addr.sin_addr = interface_ip;

  if (::bind(s, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
    ec = last_error();
    std::array<char, INET_ADDRSTRLEN> addr_str{};
    inet_ntop(AF_INET, &interface_ip, addr_str.data(), static_cast<socklen_t>(addr_str.size()));
    LOG_ERROR("[UDP] bind() to {}:{} failed during interface rebind: {}",
              addr_str.data(), bound_port, ec.message());
    closesocket(s);
    fd_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    return false;
  }

  bound_interface_index_ = interface_index;
  std::array<char, INET_ADDRSTRLEN> addr_str{};
  inet_ntop(AF_INET, &interface_ip, addr_str.data(), static_cast<socklen_t>(addr_str.size()));
  LOG_INFO("[UDP] Socket rebound to {}:{} (interface index {})",
           addr_str.data(), bound_port, interface_index);
  return true;
}

bool UdpSocket::connect(const UdpEndpoint& remote, std::error_code& ec) {
  sockaddr_in addr{};
  if (!resolve(remote, addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("[UDP] Failed to resolve endpoint for connect: {}:{}", remote.host, remote.port);
    return false;
  }
  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    LOG_ERROR("[UDP] Cannot connect: socket is invalid");
    return false;
  }

  // Before connecting, determine the best interface to reach the remote host
  // and bind the socket to that interface. This is CRITICAL for VPN clients
  // because once VPN routes are configured, we need to ensure that UDP packets
  // to the VPN server continue to use the physical interface, not the VPN tunnel.
  DWORD best_interface = 0;
  DWORD result = GetBestInterface(addr.sin_addr.s_addr, &best_interface);
  if (result == NO_ERROR && best_interface != 0) {
    LOG_INFO("[UDP] Best interface for {}:{} is index {}", remote.host, remote.port, best_interface);

    // Bind the socket to this interface so it continues to use it even after
    // VPN routing is configured. This prevents the "routing loop" issue where
    // VPN packets get sent through the VPN tunnel instead of the physical interface.
    std::error_code bind_ec;
    if (!bind_to_interface(best_interface, bind_ec)) {
      // Log warning but don't fail - the connection may still work without interface binding
      LOG_WARN("[UDP] Failed to bind to interface {}: {}. Continuing without interface binding.",
               best_interface, bind_ec.message());
    }
  } else {
    LOG_WARN("[UDP] GetBestInterface() failed or returned 0: error {}. Continuing without interface binding.", result);
  }

  // After bind_to_interface(), the socket handle may have changed (due to rebinding)
  // Get the updated socket handle
  s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    LOG_ERROR("[UDP] Socket is invalid after interface binding");
    return false;
  }

  LOG_DEBUG("[UDP] Connecting UDP socket to {}:{}", remote.host, remote.port);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    int wsa_error = WSAGetLastError();
    LOG_ERROR("[UDP] connect() failed: WSA error {}, message: {}", wsa_error, ec.message());
    return false;
  }

  connected_ = remote;
  LOG_INFO("[UDP] UDP socket connected to {}:{}", remote.host, remote.port);

  // Log the local address after connect to verify interface binding worked
  sockaddr_in local_addr{};
  int local_addr_len = sizeof(local_addr);
  if (getsockname(s, reinterpret_cast<sockaddr*>(&local_addr), &local_addr_len) == 0) {
    std::array<char, INET_ADDRSTRLEN> local_addr_str{};
    if (inet_ntop(AF_INET, &local_addr.sin_addr, local_addr_str.data(),
                  static_cast<socklen_t>(local_addr_str.size())) != nullptr) {
      LOG_INFO("[UDP] Local address after connect: {}:{}", local_addr_str.data(), ntohs(local_addr.sin_port));
    }
  }

  return true;
}

bool UdpSocket::send(std::span<const std::uint8_t> data, const UdpEndpoint& remote,
                     std::error_code& ec) {
  sockaddr_in addr{};
  if (!resolve(remote, addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    LOG_ERROR("[UDP] Failed to resolve endpoint {}:{}", remote.host, remote.port);
    return false;
  }

  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    LOG_ERROR("[UDP] Socket is invalid (not opened or already closed)");
    return false;
  }

  LOG_INFO("[UDP] Sending {} bytes to {}:{}", data.size(), remote.host, remote.port);
  LOG_DEBUG("[UDP] Socket handle: {}, target addr: {:08x}:{}",
            static_cast<unsigned long long>(s),
            ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));

  const int sent = ::sendto(s, reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(data.size()), 0,
                             reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

  if (sent == SOCKET_ERROR) {
    ec = last_error();
    int wsa_error = WSAGetLastError();
    LOG_ERROR("[UDP] sendto() failed: WSA error {}, message: {}", wsa_error, ec.message());
    // Log additional Windows-specific error context
    if (wsa_error == WSAENETUNREACH) {
      LOG_ERROR("[UDP] Network is unreachable - check routing and firewall");
    } else if (wsa_error == WSAEHOSTUNREACH) {
      LOG_ERROR("[UDP] Host is unreachable - check if server is reachable");
    } else if (wsa_error == WSAEACCES) {
      LOG_ERROR("[UDP] Permission denied - firewall may be blocking outgoing UDP");
    } else if (wsa_error == WSAEINVAL) {
      LOG_ERROR("[UDP] Invalid argument - socket may not be properly configured");
    }
    return false;
  }

  if (static_cast<std::size_t>(sent) != data.size()) {
    LOG_ERROR("[UDP] Partial send: {} bytes sent, {} bytes requested", sent, data.size());
    ec = std::make_error_code(std::errc::io_error);
    return false;
  }

  LOG_INFO("[UDP] Successfully sent {} bytes to {}:{}", sent, remote.host, remote.port);
  return true;
}

bool UdpSocket::send_batch(std::span<const UdpPacket> packets, std::error_code& ec) {
  if (packets.empty()) {
    return true;
  }

  // Windows doesn't have sendmmsg, so send each packet individually.
  for (const auto& pkt : packets) {
    if (!send(pkt.data, pkt.remote, ec)) {
      return false;
    }
  }
  return true;
}

bool UdpSocket::poll(const ReceiveHandler& handler, int timeout_ms, std::error_code& ec) {
  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
    LOG_ERROR("[UDP] poll() called on invalid socket");
    return false;
  }

  // Use select to wait for data.
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(s, &read_fds);

  timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;

  // On Windows, the first argument to select is ignored.
  int n = ::select(0, &read_fds, nullptr, nullptr, &tv);
  if (n == SOCKET_ERROR) {
    int err = WSAGetLastError();
    if (err == WSAEINTR) {
      return true;  // Interrupted, but not an error.
    }
    ec = std::error_code(err, std::system_category());
    LOG_ERROR("[UDP] select() failed: WSA error {}, message: {}", err, ec.message());
    return false;
  }

  if (n == 0) {
    return true;  // Timeout, no data.
  }

  // Read available data - may have multiple packets pending
  int packets_read = 0;
  while (true) {
    std::array<char, 65535> buffer{};
    sockaddr_in src{};
    int src_len = sizeof(src);
    const int read = ::recvfrom(s, buffer.data(), static_cast<int>(buffer.size()), 0,
                                 reinterpret_cast<sockaddr*>(&src), &src_len);
    if (read == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
        break;  // No more data available right now.
      }
      // Log other errors but don't fail if we already read some packets
      if (packets_read == 0) {
        ec = std::error_code(err, std::system_category());
        LOG_ERROR("[UDP] recvfrom() failed: WSA error {}, message: {}", err, ec.message());
        return false;
      }
      LOG_WARN("[UDP] recvfrom() error after reading {} packets: WSA error {}", packets_read, err);
      break;
    }

    if (read > 0) {
      UdpEndpoint remote{};
      fill_endpoint(src, remote);
      LOG_INFO("[UDP] Received {} bytes from {}:{}", read, remote.host, remote.port);
      handler(UdpPacket{
          std::vector<std::uint8_t>(reinterpret_cast<std::uint8_t*>(buffer.data()),
                                     reinterpret_cast<std::uint8_t*>(buffer.data()) + read),
          remote});
      packets_read++;
    } else {
      break;  // No data read
    }
  }

  return true;
}

void UdpSocket::close() {
  if (fd_ != static_cast<std::uintptr_t>(~0ULL)) {  // Check if not INVALID_SOCKET
    ::closesocket(static_cast<SOCKET>(fd_));
    fd_ = static_cast<std::uintptr_t>(~0ULL);  // Set to INVALID_SOCKET
    WSACleanup();
  }
}

std::uint16_t UdpSocket::local_port() const {
  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    return 0;
  }

  sockaddr_in addr{};
  int addr_len = sizeof(addr);
  if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
    LOG_WARN("[UDP] getsockname() failed: WSA error {}", WSAGetLastError());
    return 0;
  }

  return ntohs(addr.sin_port);
}

}  // namespace veil::transport

#endif  // _WIN32
