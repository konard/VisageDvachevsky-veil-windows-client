#include "transport/udp_socket/udp_socket.h"

#ifdef _WIN32
// Windows must include winsock2.h before windows.h to avoid conflicts.
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstring>
#include <system_error>
#include <vector>

#include "common/logging/logger.h"

// Check for sendmmsg availability (Linux 3.0+, glibc 2.14+).
// On systems without sendmmsg, we fall back to multiple sendto calls.
#if defined(__linux__) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 14))
#define VEIL_HAS_SENDMMSG 1
#else
#define VEIL_HAS_SENDMMSG 0
#endif

namespace {

#ifdef _WIN32
// WinSock initialization singleton.
// Ensures WSAStartup is called before any socket operations and WSACleanup on exit.
class WinSockInit {
 public:
  WinSockInit() {
    WSADATA wsaData;
    const int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
      // WSAStartup failed - log but continue, individual socket ops will fail.
      LOG_ERROR("WSAStartup failed with error: {}", result);
      initialized_ = false;
    } else {
      initialized_ = true;
    }
  }

  ~WinSockInit() {
    if (initialized_) {
      WSACleanup();
    }
  }

  bool is_initialized() const { return initialized_; }

  // Singleton accessor.
  static WinSockInit& instance() {
    static WinSockInit instance;
    return instance;
  }

 private:
  bool initialized_{false};
};

// Helper to ensure WinSock is initialized before socket operations.
inline bool ensure_winsock() {
  return WinSockInit::instance().is_initialized();
}
#endif  // _WIN32

std::error_code last_error() {
#ifdef _WIN32
  return std::error_code(WSAGetLastError(), std::system_category());
#else
  return std::error_code(errno, std::generic_category());
#endif
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
  const char* res = inet_ntop(AF_INET, &addr.sin_addr, buffer.data(), buffer.size());
  endpoint.host = (res != nullptr) ? buffer.data() : "";
  endpoint.port = ntohs(addr.sin_port);
}
}  // namespace

namespace veil::transport {

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
#ifndef _WIN32
  close_epoll();
#endif
  close();
}

bool UdpSocket::configure_socket(bool reuse_port, std::error_code& ec) {
#ifdef _WIN32
  // On Windows, setsockopt takes const char* for the option value.
  const char enable = 1;
  if (setsockopt(static_cast<SOCKET>(fd_), SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(enable)) == SOCKET_ERROR) {
    ec = last_error();
    return false;
  }
  // Windows doesn't have SO_REUSEPORT; SO_REUSEADDR provides similar behavior.
  (void)reuse_port;
#else
  const int enable = 1;
  if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) != 0) {
    ec = last_error();
    return false;
  }
#ifdef SO_REUSEPORT
  if (reuse_port) {
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) != 0) {
      ec = last_error();
      return false;
    }
  }
#else
  (void)reuse_port;
#endif
#endif
  return true;
}

bool UdpSocket::open(std::uint16_t bind_port, bool reuse_port, std::error_code& ec) {
#ifdef _WIN32
  // Ensure WinSock is initialized.
  if (!ensure_winsock()) {
    ec = std::make_error_code(std::errc::not_enough_memory);
    return false;
  }

  // Create socket (Windows doesn't support SOCK_NONBLOCK in socket() call).
  SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    ec = last_error();
    return false;
  }
  fd_ = static_cast<std::uintptr_t>(sock);

  // Set non-blocking mode using ioctlsocket.
  u_long mode = 1;
  if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
    ec = last_error();
    close();
    return false;
  }
#else
  fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (fd_ < 0) {
    ec = last_error();
    return false;
  }
#endif

  if (!configure_socket(reuse_port, ec)) {
    close();
    return false;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bind_port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

#ifdef _WIN32
  if (::bind(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    ec = last_error();
    close();
    return false;
  }
#else
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    close();
    return false;
  }
#endif
  return true;
}

bool UdpSocket::connect(const UdpEndpoint& remote, std::error_code& ec) {
  sockaddr_in addr{};
  if (!resolve(remote, addr)) {
    ec = std::make_error_code(std::errc::invalid_argument);
    return false;
  }

#ifdef _WIN32
  if (::connect(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    ec = last_error();
    return false;
  }
#else
  if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ec = last_error();
    return false;
  }
#endif
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

#ifdef _WIN32
  const int sent = ::sendto(static_cast<SOCKET>(fd_),
                            reinterpret_cast<const char*>(data.data()),
                            static_cast<int>(data.size()), 0,
                            reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (sent == SOCKET_ERROR || static_cast<std::size_t>(sent) != data.size()) {
    ec = last_error();
    return false;
  }
#else
  const auto sent =
      ::sendto(fd_, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (sent < 0 || static_cast<std::size_t>(sent) != data.size()) {
    ec = last_error();
    return false;
  }
#endif
  return true;
}

bool UdpSocket::send_batch(std::span<const UdpPacket> packets, std::error_code& ec) {
  if (packets.empty()) {
    return true;
  }

#if VEIL_HAS_SENDMMSG
  // Use sendmmsg for better performance when available (Linux only).
  std::vector<mmsghdr> messages(packets.size());
  std::vector<sockaddr_in> addrs(packets.size());
  std::vector<iovec> iovecs(packets.size());
  for (std::size_t i = 0; i < packets.size(); ++i) {
    if (!resolve(packets[i].remote, addrs[i])) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return false;
    }
    iovecs[i].iov_base = const_cast<std::uint8_t*>(packets[i].data.data());
    iovecs[i].iov_len = packets[i].data.size();
    messages[i].msg_hdr.msg_name = &addrs[i];
    messages[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
    messages[i].msg_hdr.msg_iov = &iovecs[i];
    messages[i].msg_hdr.msg_iovlen = 1;
    messages[i].msg_hdr.msg_control = nullptr;
    messages[i].msg_hdr.msg_controllen = 0;
    messages[i].msg_hdr.msg_flags = 0;
    messages[i].msg_len = 0;
  }
  const auto sent =
      ::sendmmsg(fd_, messages.data(), static_cast<unsigned int>(messages.size()), 0);
  if (sent < 0) {
    // If sendmmsg fails with EPERM (sandbox/container), fall back to sendto.
    if (errno == EPERM || errno == ENOSYS) {
      LOG_DEBUG("sendmmsg failed with {}, falling back to sendto", errno);
      goto fallback;
    }
    ec = last_error();
    return false;
  }
  if (static_cast<std::size_t>(sent) != packets.size()) {
    ec = last_error();
    return false;
  }
  return true;

fallback:
#endif
  // Fallback: send each packet individually with sendto (Windows and non-sendmmsg systems).
  for (const auto& pkt : packets) {
    if (!send(pkt.data, pkt.remote, ec)) {
      return false;
    }
  }
  return true;
}

#ifndef _WIN32
bool UdpSocket::ensure_epoll(std::error_code& ec) {
  if (epoll_fd_ >= 0) {
    return true;  // Already initialized.
  }

  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0) {
    ec = last_error();
    return false;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = fd_;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev) != 0) {
    ec = last_error();
    ::close(epoll_fd_);
    epoll_fd_ = -1;
    return false;
  }

  return true;
}

void UdpSocket::close_epoll() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
    epoll_fd_ = -1;
  }
}
#endif  // !_WIN32

bool UdpSocket::poll(const ReceiveHandler& handler, int timeout_ms, std::error_code& ec) {
#ifdef _WIN32
  // Use WSAPoll for Windows (available since Windows Vista).
  WSAPOLLFD pfd{};
  pfd.fd = static_cast<SOCKET>(fd_);
  pfd.events = POLLIN;
  pfd.revents = 0;

  const int result = WSAPoll(&pfd, 1, timeout_ms);
  if (result == SOCKET_ERROR) {
    ec = last_error();
    return false;
  }

  if (result == 0) {
    return true;  // Timeout, no data.
  }

  // Check for errors on the socket.
  if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    ec = std::make_error_code(std::errc::connection_reset);
    return false;
  }

  // Read available data.
  if ((pfd.revents & POLLIN) != 0) {
    std::array<char, 65535> buffer{};
    sockaddr_in src{};
    int src_len = sizeof(src);

    // Read in a loop to drain all available packets (non-blocking socket).
    while (true) {
      const int read_bytes = ::recvfrom(static_cast<SOCKET>(fd_), buffer.data(),
                                        static_cast<int>(buffer.size()), 0,
                                        reinterpret_cast<sockaddr*>(&src), &src_len);
      if (read_bytes == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
          // No more data available.
          break;
        }
        // On other errors, continue (might be WSAECONNRESET for ICMP unreachable).
        break;
      }
      if (read_bytes <= 0) {
        break;
      }

      UdpEndpoint remote{};
      fill_endpoint(src, remote);
      handler(UdpPacket{std::vector<std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(buffer.data()),
                            reinterpret_cast<const std::uint8_t*>(buffer.data()) + read_bytes),
                        remote});
    }
  }

  return true;

#else
  // Use epoll for Linux (more efficient than poll for multiple sockets).
  // Ensure epoll FD is initialized (lazy initialization).
  // This reuses the same epoll FD across all poll() calls to avoid resource leaks.
  if (!ensure_epoll(ec)) {
    return false;
  }

  std::array<epoll_event, 4> events{};
  const int n = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), timeout_ms);
  if (n < 0) {
    // On EINTR, just return true (no data, but not an error).
    if (errno == EINTR) {
      return true;
    }
    ec = last_error();
    return false;
  }

  if (n == 0) {
    return true;  // Timeout, no data.
  }

  std::array<std::uint8_t, 65535> buffer{};
  for (int i = 0; i < n; ++i) {
    if ((events[static_cast<std::size_t>(i)].events & EPOLLIN) == 0U) {
      continue;
    }
    sockaddr_in src{};
    socklen_t src_len = sizeof(src);
    const auto read =
        ::recvfrom(fd_, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&src), &src_len);
    if (read <= 0) {
      continue;
    }
    UdpEndpoint remote{};
    fill_endpoint(src, remote);
    handler(UdpPacket{std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + read), remote});
  }

  return true;
#endif  // _WIN32
}

void UdpSocket::close() {
#ifdef _WIN32
  constexpr std::uintptr_t invalid_socket = static_cast<std::uintptr_t>(~0ULL);
  if (fd_ != invalid_socket) {
    ::closesocket(static_cast<SOCKET>(fd_));
    fd_ = invalid_socket;
  }
#else
  // Close epoll FD first (it references the socket FD).
  close_epoll();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#endif
}

}  // namespace veil::transport
