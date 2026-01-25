#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "transport/udp_socket/udp_socket.h"

namespace veil::tests {

TEST(UdpSocketTests, SendAndReceiveLoopback) {
  transport::UdpSocket server;
  std::error_code ec;
  if (!server.open(0, false, ec)) {
    if (ec == std::errc::operation_not_permitted) {
      GTEST_SKIP() << "UDP sockets not permitted in this environment";
    }
    FAIL() << ec.message();
  }

  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
#ifdef _WIN32
  ASSERT_EQ(::getsockname(static_cast<SOCKET>(server.native_handle()),
                          reinterpret_cast<sockaddr*>(&addr), &len), 0);
#else
  ASSERT_EQ(::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&addr), &len), 0);
#endif
  const auto port = ntohs(addr.sin_port);

  transport::UdpSocket client;
  if (!client.open(0, false, ec)) {
    if (ec == std::errc::operation_not_permitted) {
      GTEST_SKIP() << "UDP sockets not permitted in this environment";
    }
    FAIL() << ec.message();
  }

  transport::UdpEndpoint server_ep{"127.0.0.1", port};
  std::vector<std::uint8_t> payload{1, 2, 3};
  ASSERT_TRUE(client.send(payload, server_ep, ec)) << ec.message();

  bool received = false;
  server.poll(
      [&](const transport::UdpPacket& pkt) {
        received = true;
        EXPECT_EQ(pkt.data, payload);
      },
      100, ec);
  EXPECT_TRUE(received);
}

TEST(UdpSocketTests, PollTimeout) {
  transport::UdpSocket socket;
  std::error_code ec;
  if (!socket.open(0, false, ec)) {
    if (ec == std::errc::operation_not_permitted) {
      GTEST_SKIP() << "UDP sockets not permitted in this environment";
    }
    FAIL() << ec.message();
  }

  // Poll with a short timeout - should return true (no error) but no data received.
  bool received = false;
  auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(socket.poll(
      [&](const transport::UdpPacket&) { received = true; }, 50, ec))
      << ec.message();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  EXPECT_FALSE(received);
  // Allow some tolerance for timing.
  EXPECT_GE(elapsed.count(), 40);
}

TEST(UdpSocketTests, MultiplePackets) {
  transport::UdpSocket server;
  std::error_code ec;
  if (!server.open(0, false, ec)) {
    if (ec == std::errc::operation_not_permitted) {
      GTEST_SKIP() << "UDP sockets not permitted in this environment";
    }
    FAIL() << ec.message();
  }

  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
#ifdef _WIN32
  ASSERT_EQ(::getsockname(static_cast<SOCKET>(server.native_handle()),
                          reinterpret_cast<sockaddr*>(&addr), &len), 0);
#else
  ASSERT_EQ(::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&addr), &len), 0);
#endif
  const auto port = ntohs(addr.sin_port);

  transport::UdpSocket client;
  if (!client.open(0, false, ec)) {
    if (ec == std::errc::operation_not_permitted) {
      GTEST_SKIP() << "UDP sockets not permitted in this environment";
    }
    FAIL() << ec.message();
  }

  transport::UdpEndpoint server_ep{"127.0.0.1", port};

  // Send multiple packets.
  const int num_packets = 5;
  for (int i = 0; i < num_packets; ++i) {
    std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(i)};
    ASSERT_TRUE(client.send(payload, server_ep, ec)) << ec.message();
  }

  // Give packets time to arrive.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Receive all packets.
  int received_count = 0;
  for (int attempt = 0; attempt < 10 && received_count < num_packets; ++attempt) {
    server.poll(
        [&](const transport::UdpPacket&) { ++received_count; }, 50, ec);
  }

  EXPECT_EQ(received_count, num_packets);
}

}  // namespace veil::tests
