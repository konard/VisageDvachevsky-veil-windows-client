#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace veil::transport {

struct UdpEndpoint {
  std::string host;
  std::uint16_t port{0};
};

struct UdpPacket {
  std::vector<std::uint8_t> data;
  UdpEndpoint remote;
};

class UdpSocket {
 public:
  using ReceiveHandler = std::function<void(const UdpPacket&)>;

  UdpSocket();
  ~UdpSocket();

  // Non-copyable, non-movable to prevent epoll_fd_ resource issues.
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  UdpSocket(UdpSocket&&) = delete;
  UdpSocket& operator=(UdpSocket&&) = delete;

  bool open(std::uint16_t bind_port, bool reuse_port, std::error_code& ec);
  bool connect(const UdpEndpoint& remote, std::error_code& ec);
  bool send(std::span<const std::uint8_t> data, const UdpEndpoint& remote, std::error_code& ec);
  bool send_batch(std::span<const UdpPacket> packets, std::error_code& ec);
  bool poll(const ReceiveHandler& handler, int timeout_ms, std::error_code& ec);
  void close();

#ifdef _WIN32
  // On Windows, returns the socket handle cast to int for compatibility.
  // Note: Use native_handle() for Windows-specific operations.
  int fd() const { return static_cast<int>(fd_); }
  std::uintptr_t native_handle() const { return fd_; }
#else
  int fd() const { return fd_; }
#endif

  // Get the actual local port the socket is bound to.
  // Returns 0 if the socket is not open or on error.
  std::uint16_t local_port() const;

#ifdef _WIN32
  // Bind the socket to a specific network interface by index.
  // This ensures packets are sent through the physical interface even when VPN routes are active.
  // interface_index should be obtained from GetBestInterface() or GetAdaptersAddresses().
  bool bind_to_interface(std::uint32_t interface_index, std::error_code& ec);
#endif

 private:
#ifdef _WIN32
  // On Windows, we use SOCKET type (unsigned __int64 on 64-bit, unsigned int on 32-bit).
  // Using uintptr_t provides a portable representation that works with INVALID_SOCKET.
  std::uintptr_t fd_{static_cast<std::uintptr_t>(~0ULL)};  // INVALID_SOCKET
  std::uint32_t bound_interface_index_{0};  // Interface index if bound via IP_BOUND_IF.
#else
  int fd_{-1};
  int epoll_fd_{-1};  // Persistent epoll FD to avoid creating/destroying on every poll() call.
#endif
  UdpEndpoint connected_;

  bool configure_socket(bool reuse_port, std::error_code& ec);
#ifndef _WIN32
  bool ensure_epoll(std::error_code& ec);  // Lazy initialization of epoll FD (Linux only).
  void close_epoll();  // Helper to close epoll FD (Linux only).
#endif
};

}  // namespace veil::transport
