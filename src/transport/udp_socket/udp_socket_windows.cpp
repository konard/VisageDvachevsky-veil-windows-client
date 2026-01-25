// Windows UDP socket implementation using select
// This file is only compiled on Windows platforms

#ifdef _WIN32

#include "transport/udp_socket/udp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <chrono>
#include <cstring>
#include <system_error>
#include <vector>

#include "common/logging/logger.h"

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
  close_epoll();
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
    return false;
  }

  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) {
    ec = last_error();
    return false;
  }
  fd_ = static_cast<int>(s);

  // Set non-blocking mode.
  if (!set_nonblocking(s)) {
    ec = last_error();
    close();
    return false;
  }

  if (!configure_socket(reuse_port, ec)) {
    close();
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bind_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    close();
    return false;
  }
  return true;
}

bool UdpSocket::connect(const UdpEndpoint& remote, std::error_code& ec) {
  sockaddr_in addr{};
  if (!resolve(remote, addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  SOCKET s = static_cast<SOCKET>(fd_);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    return false;
  }
  connected_ = remote;
  return true;
}

bool UdpSocket::send(std::span<const std::uint8_t> data, const UdpEndpoint& remote,
                     std::error_code& ec) {
  sockaddr_in addr{};
  if (!resolve(remote, addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }
  SOCKET s = static_cast<SOCKET>(fd_);
  const int sent = ::sendto(s, reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(data.size()), 0,
                             reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (sent == SOCKET_ERROR || static_cast<std::size_t>(sent) != data.size()) {
    ec = last_error();
    return false;
  }
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

bool UdpSocket::ensure_epoll(std::error_code& /* ec */) {
  // On Windows, we don't use epoll. The poll() method uses select directly.
  // Just set the dummy flag to indicate initialization.
  epoll_fd_ = 0;
  return true;
}

void UdpSocket::close_epoll() {
  // Nothing to close on Windows (no epoll FD).
  epoll_fd_ = -1;
}

bool UdpSocket::poll(const ReceiveHandler& handler, int timeout_ms, std::error_code& ec) {
  SOCKET s = static_cast<SOCKET>(fd_);
  if (s == INVALID_SOCKET) {
    ec = std::make_error_code(std::errc::bad_file_descriptor);
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
    return false;
  }

  if (n == 0) {
    return true;  // Timeout, no data.
  }

  // Read available data.
  std::array<char, 65535> buffer{};
  sockaddr_in src{};
  int src_len = sizeof(src);
  const int read = ::recvfrom(s, buffer.data(), static_cast<int>(buffer.size()), 0,
                               reinterpret_cast<sockaddr*>(&src), &src_len);
  if (read == SOCKET_ERROR) {
    int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
      return true;  // No data available right now.
    }
    ec = std::error_code(err, std::system_category());
    return false;
  }

  if (read > 0) {
    UdpEndpoint remote{};
    fill_endpoint(src, remote);
    handler(UdpPacket{
        std::vector<std::uint8_t>(reinterpret_cast<std::uint8_t*>(buffer.data()),
                                   reinterpret_cast<std::uint8_t*>(buffer.data()) + read),
        remote});
  }

  return true;
}

void UdpSocket::close() {
  // Close epoll FD first (no-op on Windows).
  close_epoll();
  if (fd_ >= 0) {
    ::closesocket(static_cast<SOCKET>(fd_));
    fd_ = -1;
    WSACleanup();
  }
}

}  // namespace veil::transport

#endif  // _WIN32
