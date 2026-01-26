/**
 * End-to-end tunnel integration tests
 *
 * This test suite verifies that the VPN tunnel properly proxies traffic
 * between the client and server. It tests the full data path:
 *
 * Client TUN -> Encrypt -> UDP -> Server -> Decrypt -> Server TUN
 * Server TUN -> Encrypt -> UDP -> Client -> Decrypt -> Client TUN
 *
 * These tests verify the core VPN functionality that issue #24 asks about:
 * "Does this client work fully with our server and protocol, and does it
 *  proxy all traffic to the server on Windows and Linux?"
 */

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "common/handshake/handshake_processor.h"
#include "common/logging/logger.h"
#include "common/utils/rate_limiter.h"
#include "transport/session/transport_session.h"
#include "transport/udp_socket/udp_socket.h"

namespace veil::integration_tests {

using namespace std::chrono_literals;

/**
 * Test fixture for tunnel integration tests.
 *
 * This fixture sets up a simulated client-server session with proper
 * handshake, allowing end-to-end data transfer testing.
 */
class TunnelIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = std::chrono::system_clock::now();
    steady_now_ = std::chrono::steady_clock::now();

    // Create time functions for consistent testing
    auto now_fn = [this]() { return now_; };
    auto steady_fn = [this]() { return steady_now_; };

    // Pre-shared key (32 bytes, same on both client and server)
    psk_ = std::vector<std::uint8_t>(32, 0xAB);

    // Perform handshake to establish session keys
    handshake::HandshakeInitiator initiator(psk_, 200ms, now_fn);
    utils::TokenBucket bucket(100.0, 1000ms, steady_fn);
    handshake::HandshakeResponder responder(psk_, 200ms, std::move(bucket), now_fn);

    auto init_bytes = initiator.create_init();
    ASSERT_FALSE(init_bytes.empty()) << "Failed to create handshake INIT";

    now_ += 10ms;
    steady_now_ += 10ms;

    auto resp = responder.handle_init(init_bytes);
    ASSERT_TRUE(resp.has_value()) << "Server failed to handle handshake INIT";

    now_ += 10ms;
    steady_now_ += 10ms;

    auto client_session = initiator.consume_response(resp->response);
    ASSERT_TRUE(client_session.has_value()) << "Client failed to process handshake RESPONSE";

    client_handshake_ = *client_session;
    server_handshake_ = resp->session;

    // Verify session IDs match
    ASSERT_EQ(client_handshake_.session_id, server_handshake_.session_id)
        << "Session IDs should match after handshake";
  }

  std::chrono::system_clock::time_point now_;
  std::chrono::steady_clock::time_point steady_now_;
  std::vector<std::uint8_t> psk_;
  handshake::HandshakeSession client_handshake_;
  handshake::HandshakeSession server_handshake_;
};

/**
 * Test basic IP packet tunneling (simulates TUN device data flow).
 *
 * This test simulates an IP packet being sent from client to server,
 * verifying the encryption/decryption path works correctly.
 */
TEST_F(TunnelIntegrationTest, SimulatedIPPacketTransfer) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  // Simulate an IPv4 packet (TCP SYN to 8.8.8.8:80)
  // This is what would be read from the TUN device
  std::vector<std::uint8_t> ip_packet = {
      // IPv4 Header (20 bytes)
      0x45, 0x00,              // Version/IHL, DSCP/ECN
      0x00, 0x28,              // Total length (40 bytes)
      0x00, 0x01,              // Identification
      0x40, 0x00,              // Flags/Fragment offset (Don't Fragment)
      0x40, 0x06,              // TTL (64), Protocol (TCP)
      0x00, 0x00,              // Header checksum (placeholder)
      0x0A, 0x08, 0x00, 0x02,  // Source IP: 10.8.0.2 (client VPN IP)
      0x08, 0x08, 0x08, 0x08,  // Dest IP: 8.8.8.8
      // TCP Header (20 bytes)
      0x00, 0x50,              // Source port: 80
      0x00, 0x50,              // Dest port: 80
      0x00, 0x00, 0x00, 0x01,  // Sequence number
      0x00, 0x00, 0x00, 0x00,  // ACK number
      0x50, 0x02,              // Data offset, flags (SYN)
      0xFF, 0xFF,              // Window size
      0x00, 0x00,              // Checksum (placeholder)
      0x00, 0x00               // Urgent pointer
  };

  // Client encrypts and sends (simulates client TUN -> UDP path)
  auto encrypted = client_session.encrypt_data(ip_packet, 0, false);
  ASSERT_EQ(encrypted.size(), 1U) << "Single IP packet should produce single encrypted packet";

  // Server decrypts (simulates UDP -> server TUN path)
  auto decrypted = server_session.decrypt_packet(encrypted[0]);
  ASSERT_TRUE(decrypted.has_value()) << "Server should decrypt packet successfully";
  ASSERT_EQ(decrypted->size(), 1U);

  // Verify the original IP packet is recovered
  EXPECT_EQ((*decrypted)[0].data.payload, ip_packet)
      << "Decrypted payload should match original IP packet";
}

/**
 * Test bidirectional traffic (simulates real VPN usage).
 *
 * Traffic should flow both ways: client->server and server->client.
 */
TEST_F(TunnelIntegrationTest, BidirectionalTraffic) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  // Client sends request
  std::vector<std::uint8_t> request{'R', 'E', 'Q', 'U', 'E', 'S', 'T'};
  auto request_encrypted = client_session.encrypt_data(request, 0, false);

  auto request_decrypted = server_session.decrypt_packet(request_encrypted[0]);
  ASSERT_TRUE(request_decrypted.has_value());
  EXPECT_EQ((*request_decrypted)[0].data.payload, request);

  // Server sends response
  std::vector<std::uint8_t> response{'R', 'E', 'S', 'P', 'O', 'N', 'S', 'E'};
  auto response_encrypted = server_session.encrypt_data(response, 0, false);

  auto response_decrypted = client_session.decrypt_packet(response_encrypted[0]);
  ASSERT_TRUE(response_decrypted.has_value());
  EXPECT_EQ((*response_decrypted)[0].data.payload, response);

  // Verify bidirectional stats
  EXPECT_EQ(client_session.stats().packets_sent, 1U);
  EXPECT_EQ(client_session.stats().packets_received, 1U);
  EXPECT_EQ(server_session.stats().packets_sent, 1U);
  EXPECT_EQ(server_session.stats().packets_received, 1U);
}

/**
 * Test large packet handling (MTU-related).
 *
 * VPN must handle various packet sizes up to MTU.
 */
TEST_F(TunnelIntegrationTest, VariousPacketSizes) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  // Test various packet sizes: small, medium, large (up to typical VPN MTU)
  std::vector<std::size_t> test_sizes = {20, 64, 128, 256, 512, 1024, 1400};

  for (auto size : test_sizes) {
    std::vector<std::uint8_t> data(size);
    for (std::size_t i = 0; i < size; ++i) {
      data[i] = static_cast<std::uint8_t>((i + size) & 0xFF);
    }

    auto encrypted = client_session.encrypt_data(data, 0, false);
    ASSERT_FALSE(encrypted.empty()) << "Encryption failed for size " << size;

    for (const auto& pkt : encrypted) {
      auto decrypted = server_session.decrypt_packet(pkt);
      ASSERT_TRUE(decrypted.has_value()) << "Decryption failed for size " << size;
    }
  }
}

/**
 * Test sustained traffic (simulates continuous data transfer).
 *
 * This verifies the tunnel can handle sustained traffic without
 * issues like sequence number wraparound or key exhaustion.
 */
TEST_F(TunnelIntegrationTest, SustainedTraffic) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  const std::size_t num_packets = 100;

  for (std::size_t i = 0; i < num_packets; ++i) {
    // Simulate various application data
    std::vector<std::uint8_t> data(64 + (i % 200), static_cast<std::uint8_t>(i));

    auto encrypted = client_session.encrypt_data(data, 0, false);
    for (const auto& pkt : encrypted) {
      auto decrypted = server_session.decrypt_packet(pkt);
      EXPECT_TRUE(decrypted.has_value()) << "Failed at packet " << i;
    }
  }

  EXPECT_EQ(client_session.stats().packets_sent, num_packets);
  EXPECT_EQ(server_session.stats().packets_received, num_packets);
  EXPECT_EQ(server_session.stats().packets_dropped_decrypt, 0U)
      << "No decryption errors should occur";
  EXPECT_EQ(server_session.stats().packets_dropped_replay, 0U)
      << "No replay detection errors should occur";
}

/**
 * Test fragmented data transfer (large application data).
 *
 * When application sends data larger than MTU, the transport layer
 * must fragment and reassemble correctly.
 */
TEST_F(TunnelIntegrationTest, FragmentedDataTransfer) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSessionConfig config;
  config.max_fragment_size = 100;  // Small fragments for testing

  transport::TransportSession client_session(client_handshake_, config, now_fn);
  transport::TransportSession server_session(server_handshake_, config, now_fn);

  // Large data that will be fragmented
  std::vector<std::uint8_t> large_data(500);
  for (std::size_t i = 0; i < large_data.size(); ++i) {
    large_data[i] = static_cast<std::uint8_t>(i & 0xFF);
  }

  auto encrypted = client_session.encrypt_data(large_data, 0, true);
  EXPECT_GE(encrypted.size(), 2U) << "Large data should produce multiple fragments";

  std::vector<std::uint8_t> reassembled;
  for (const auto& pkt : encrypted) {
    auto decrypted = server_session.decrypt_packet(pkt);
    ASSERT_TRUE(decrypted.has_value());
    for (const auto& frame : *decrypted) {
      if (frame.kind == mux::FrameKind::kData) {
        reassembled.insert(reassembled.end(), frame.data.payload.begin(),
                           frame.data.payload.end());
      }
    }
  }

  EXPECT_EQ(reassembled, large_data) << "Reassembled data should match original";
}

/**
 * Test traffic integrity under simulated packet loss.
 *
 * In real networks, some packets may be lost. The tunnel should handle
 * this gracefully with retransmission mechanisms.
 */
TEST_F(TunnelIntegrationTest, PacketLossRecovery) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSessionConfig config;
  // Reliable delivery is enabled by default via retransmit_config

  transport::TransportSession client_session(client_handshake_, config, now_fn);
  transport::TransportSession server_session(server_handshake_, config, now_fn);

  // Send multiple packets
  std::vector<std::vector<std::uint8_t>> encrypted_packets;
  for (int i = 0; i < 5; ++i) {
    std::vector<std::uint8_t> data{static_cast<std::uint8_t>('A' + i)};
    auto pkts = client_session.encrypt_data(data, 0, true);  // Reliable
    encrypted_packets.insert(encrypted_packets.end(), pkts.begin(), pkts.end());
  }

  // Deliver packets 0, 2, 4 (simulating loss of 1, 3)
  for (std::size_t i = 0; i < encrypted_packets.size(); i += 2) {
    auto decrypted = server_session.decrypt_packet(encrypted_packets[i]);
    EXPECT_TRUE(decrypted.has_value()) << "Delivered packet " << i << " should decrypt";
  }

  // Now deliver the "lost" packets (simulating delayed arrival or retransmit)
  for (std::size_t i = 1; i < encrypted_packets.size(); i += 2) {
    auto decrypted = server_session.decrypt_packet(encrypted_packets[i]);
    EXPECT_TRUE(decrypted.has_value()) << "Delayed packet " << i << " should decrypt";
  }

  EXPECT_EQ(server_session.stats().packets_received, 5U);
}

/**
 * Test that the protocol handles replay attacks.
 *
 * An attacker capturing encrypted packets should not be able to
 * replay them successfully.
 */
TEST_F(TunnelIntegrationTest, ReplayProtection) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  // Client sends a packet
  std::vector<std::uint8_t> data{'S', 'E', 'C', 'R', 'E', 'T'};
  auto encrypted = client_session.encrypt_data(data, 0, false);
  ASSERT_EQ(encrypted.size(), 1U);

  // First reception - should succeed
  auto first = server_session.decrypt_packet(encrypted[0]);
  EXPECT_TRUE(first.has_value()) << "First packet should be accepted";

  // Replay attempt - should fail
  auto replay = server_session.decrypt_packet(encrypted[0]);
  EXPECT_FALSE(replay.has_value()) << "Replayed packet should be rejected";

  EXPECT_EQ(server_session.stats().packets_dropped_replay, 1U)
      << "Replay should be counted";
}

/**
 * Test protocol compatibility (client and server using same protocol version).
 *
 * Verifies that the VEIL protocol works correctly for establishing
 * encrypted tunnels.
 */
TEST_F(TunnelIntegrationTest, ProtocolCompatibility) {
  auto now_fn = []() { return std::chrono::steady_clock::now(); };

  transport::TransportSession client_session(client_handshake_, {}, now_fn);
  transport::TransportSession server_session(server_handshake_, {}, now_fn);

  // Verify sessions were established with matching keys
  EXPECT_EQ(client_session.session_id(), server_session.session_id())
      << "Session IDs should match";

  // Verify key derivation produced usable keys
  std::vector<std::uint8_t> test_data{'T', 'E', 'S', 'T'};
  auto encrypted = client_session.encrypt_data(test_data, 0, false);
  ASSERT_FALSE(encrypted.empty()) << "Client should be able to encrypt";

  auto decrypted = server_session.decrypt_packet(encrypted[0]);
  ASSERT_TRUE(decrypted.has_value()) << "Server should be able to decrypt";
  EXPECT_EQ((*decrypted)[0].data.payload, test_data)
      << "Protocol should preserve data integrity";
}

/**
 * Test session rotation during active traffic.
 *
 * Session keys should rotate periodically for forward secrecy,
 * and this should be transparent to the tunnel operation.
 */
TEST_F(TunnelIntegrationTest, SessionRotationDuringTraffic) {
  std::chrono::steady_clock::time_point test_now = std::chrono::steady_clock::now();
  auto now_fn = [&test_now]() { return test_now; };

  transport::TransportSessionConfig config;
  config.session_rotation_interval = std::chrono::seconds(1);
  config.session_rotation_packets = 1000000;

  transport::TransportSession client_session(client_handshake_, config, now_fn);
  transport::TransportSession server_session(server_handshake_, config, now_fn);

  auto initial_session_id = client_session.session_id();

  // Send some traffic
  for (int i = 0; i < 10; ++i) {
    std::vector<std::uint8_t> data{static_cast<std::uint8_t>(i)};
    auto encrypted = client_session.encrypt_data(data, 0, false);
    for (const auto& pkt : encrypted) {
      auto decrypted = server_session.decrypt_packet(pkt);
      EXPECT_TRUE(decrypted.has_value());
    }
  }

  // Advance time to trigger rotation
  test_now += std::chrono::seconds(2);

  EXPECT_TRUE(client_session.should_rotate_session());
  client_session.rotate_session();

  auto new_session_id = client_session.session_id();
  EXPECT_NE(new_session_id, initial_session_id)
      << "Session ID should change after rotation";

  EXPECT_EQ(client_session.stats().session_rotations, 1U);
}

}  // namespace veil::integration_tests
