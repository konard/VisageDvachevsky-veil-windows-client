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

  int fd() const { return fd_; }

 private:
  int fd_{-1};
  int epoll_fd_{-1};  // Persistent epoll FD to avoid creating/destroying on every poll() call.
  UdpEndpoint connected_;

  bool configure_socket(bool reuse_port, std::error_code& ec);
  bool ensure_epoll(std::error_code& ec);  // Lazy initialization of epoll FD.
  void close_epoll();  // Helper to close epoll FD.
};

}  // namespace veil::transport
