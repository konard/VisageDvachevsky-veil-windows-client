#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/handshake/handshake_processor.h"
#include "common/utils/rate_limiter.h"

namespace veil::tests {

namespace {
std::vector<std::uint8_t> make_psk() { return std::vector<std::uint8_t>(32, 0xAA); }

utils::TokenBucket make_bucket(double rate = 10.0) {
  return utils::TokenBucket(rate, std::chrono::milliseconds(1000), [] {
    return std::chrono::steady_clock::now();
  });
}
}  // namespace

// =============================================================================
// 0-RTT Full Flow Tests
// =============================================================================

TEST(ZeroRttHandshakeTests, FullZeroRttFlowAccepted) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  // Step 1: Do a normal 1-RTT handshake to get session keys
  handshake::HandshakeInitiator initiator(make_psk(), std::chrono::milliseconds(1000), now_fn);
  handshake::HandshakeResponder responder(make_psk(), std::chrono::milliseconds(1000),
                                          make_bucket(), now_fn);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);
  ASSERT_TRUE(resp.has_value());

  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());

  // Step 2: Server issues a session ticket
  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);
  auto ticket = ticket_manager->issue_ticket(session->keys);

  // Step 3: Client stores the ticket and later reconnects with 0-RTT
  handshake::ZeroRttInitiator zero_rtt_initiator(make_psk(), ticket, now_fn);
  handshake::ZeroRttResponder zero_rtt_responder(make_psk(), ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(), now_fn);

  auto zero_rtt_init = zero_rtt_initiator.create_zero_rtt_init();
  auto zero_rtt_result = zero_rtt_responder.handle_zero_rtt_init(zero_rtt_init);

  ASSERT_TRUE(zero_rtt_result.has_value());
  EXPECT_TRUE(zero_rtt_result->accepted);

  // Step 4: Client processes the accept response
  auto zero_rtt_session = zero_rtt_initiator.consume_zero_rtt_response(zero_rtt_result->response);
  ASSERT_TRUE(zero_rtt_session.has_value());
  EXPECT_EQ(zero_rtt_session->session_id, zero_rtt_result->session.session_id);

  // The session keys should match the original session keys (cached in ticket)
  EXPECT_EQ(zero_rtt_session->keys.send_key, session->keys.send_key);
  EXPECT_EQ(zero_rtt_session->keys.recv_key, session->keys.recv_key);
}

TEST(ZeroRttHandshakeTests, ExpiredTicketRejected) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  // Do a normal handshake to get session keys
  handshake::HandshakeInitiator initiator(make_psk(), std::chrono::milliseconds(1000), now_fn);
  handshake::HandshakeResponder responder(make_psk(), std::chrono::milliseconds(1000),
                                          make_bucket(), now_fn);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);
  ASSERT_TRUE(resp.has_value());

  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());

  // Issue a ticket with short lifetime
  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(1000), now_fn);
  auto ticket = ticket_manager->issue_ticket(session->keys);

  // Advance time past ticket lifetime
  now += std::chrono::seconds(2);

  // Try 0-RTT with expired ticket
  handshake::ZeroRttInitiator zero_rtt_initiator(make_psk(), ticket, now_fn);
  handshake::ZeroRttResponder zero_rtt_responder(make_psk(), ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(), now_fn);

  auto zero_rtt_init = zero_rtt_initiator.create_zero_rtt_init();
  auto zero_rtt_result = zero_rtt_responder.handle_zero_rtt_init(zero_rtt_init);

  // Should get a reject response (not nullopt - nullopt means complete failure)
  ASSERT_TRUE(zero_rtt_result.has_value());
  EXPECT_FALSE(zero_rtt_result->accepted);

  // Client should detect rejection
  auto zero_rtt_session = zero_rtt_initiator.consume_zero_rtt_response(zero_rtt_result->response);
  EXPECT_FALSE(zero_rtt_session.has_value());
  EXPECT_TRUE(zero_rtt_initiator.was_rejected());
}

TEST(ZeroRttHandshakeTests, WrongPskFailsDecryption) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  // Create a ticket with one PSK
  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);

  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  keys.recv_key.fill(0x22);
  auto ticket = ticket_manager->issue_ticket(keys);

  // Client uses PSK1
  std::vector<std::uint8_t> psk1(32, 0xAA);
  // Server uses PSK2
  std::vector<std::uint8_t> psk2(32, 0xBB);

  handshake::ZeroRttInitiator zero_rtt_initiator(psk1, ticket, now_fn);
  handshake::ZeroRttResponder zero_rtt_responder(psk2, ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(), now_fn);

  auto zero_rtt_init = zero_rtt_initiator.create_zero_rtt_init();
  auto zero_rtt_result = zero_rtt_responder.handle_zero_rtt_init(zero_rtt_init);

  // Should fail completely (decryption failure, not just rejection)
  EXPECT_FALSE(zero_rtt_result.has_value());
}

TEST(ZeroRttHandshakeTests, RateLimiterDropsExcessZeroRtt) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);

  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  auto ticket = ticket_manager->issue_ticket(keys);

  // Create responder with very low rate limit
  handshake::ZeroRttResponder zero_rtt_responder(make_psk(), ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(1.0), now_fn);

  // First request should succeed
  handshake::ZeroRttInitiator initiator1(make_psk(), ticket, now_fn);
  auto init1 = initiator1.create_zero_rtt_init();
  auto result1 = zero_rtt_responder.handle_zero_rtt_init(init1);
  EXPECT_TRUE(result1.has_value());

  // Second request should be rate-limited
  handshake::ZeroRttInitiator initiator2(make_psk(), ticket, now_fn);
  auto init2 = initiator2.create_zero_rtt_init();
  auto result2 = zero_rtt_responder.handle_zero_rtt_init(init2);
  EXPECT_FALSE(result2.has_value());
}

TEST(ZeroRttHandshakeTests, CorruptedZeroRttInitDropped) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);

  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  auto ticket = ticket_manager->issue_ticket(keys);

  handshake::ZeroRttInitiator zero_rtt_initiator(make_psk(), ticket, now_fn);
  handshake::ZeroRttResponder zero_rtt_responder(make_psk(), ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(), now_fn);

  auto zero_rtt_init = zero_rtt_initiator.create_zero_rtt_init();

  // Corrupt the packet
  if (!zero_rtt_init.empty()) {
    zero_rtt_init[zero_rtt_init.size() / 2] ^= 0xFF;
  }

  auto zero_rtt_result = zero_rtt_responder.handle_zero_rtt_init(zero_rtt_init);
  EXPECT_FALSE(zero_rtt_result.has_value());
}

TEST(ZeroRttHandshakeTests, TimestampOutsideWindowDropped) {
  auto now = std::chrono::system_clock::now();
  auto now_fn_future = [&]() { return now + std::chrono::seconds(10); };
  auto now_fn_past = [&]() { return now; };

  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn_past);

  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  auto ticket = ticket_manager->issue_ticket(keys);

  // Client creates INIT with future timestamp
  handshake::ZeroRttInitiator zero_rtt_initiator(make_psk(), ticket, now_fn_future);
  // Server uses current time
  handshake::ZeroRttResponder zero_rtt_responder(make_psk(), ticket_manager,
                                                  std::chrono::milliseconds(1000),
                                                  make_bucket(), now_fn_past);

  auto zero_rtt_init = zero_rtt_initiator.create_zero_rtt_init();
  auto zero_rtt_result = zero_rtt_responder.handle_zero_rtt_init(zero_rtt_init);

  // Should fail due to timestamp out of window
  EXPECT_FALSE(zero_rtt_result.has_value());
}

// =============================================================================
// 0-RTT Constructor Validation Tests
// =============================================================================

TEST(ZeroRttHandshakeTests, ZeroRttInitiatorRequiresPsk) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicket ticket{
      .ticket_data = {1, 2, 3},
      .issued_at_ms = 1000,
      .lifetime_ms = 60000,
      .cached_keys = {},
      .client_id = {},
  };

  EXPECT_THROW(
      handshake::ZeroRttInitiator({}, ticket, now_fn),
      std::invalid_argument);
}

TEST(ZeroRttHandshakeTests, ZeroRttInitiatorRequiresTicketData) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicket ticket{
      .ticket_data = {},  // Empty ticket data
      .issued_at_ms = 1000,
      .lifetime_ms = 60000,
      .cached_keys = {},
      .client_id = {},
  };

  EXPECT_THROW(
      handshake::ZeroRttInitiator(make_psk(), ticket, now_fn),
      std::invalid_argument);
}

TEST(ZeroRttHandshakeTests, ZeroRttResponderRequiresPsk) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };
  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);

  EXPECT_THROW(
      handshake::ZeroRttResponder({}, ticket_manager, std::chrono::milliseconds(1000),
                                   make_bucket(), now_fn),
      std::invalid_argument);
}

TEST(ZeroRttHandshakeTests, ZeroRttResponderRequiresTicketManager) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  EXPECT_THROW(
      handshake::ZeroRttResponder(make_psk(), nullptr, std::chrono::milliseconds(1000),
                                   make_bucket(), now_fn),
      std::invalid_argument);
}

// =============================================================================
// 0-RTT DPI Resistance Tests
// =============================================================================

TEST(ZeroRttHandshakeTests, ZeroRttPacketDoesNotContainPlaintextMagicBytes) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);
  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  auto ticket = ticket_manager->issue_ticket(keys);

  handshake::ZeroRttInitiator initiator(make_psk(), ticket, now_fn);

  auto zero_rtt_init = initiator.create_zero_rtt_init();

  // Should start with random nonce, not magic bytes
  ASSERT_GE(zero_rtt_init.size(), 2u);
  bool magic_at_start = (zero_rtt_init[0] == 0x48 && zero_rtt_init[1] == 0x53);
  EXPECT_FALSE(magic_at_start) << "0-RTT packet should not start with plaintext magic bytes";
}

TEST(ZeroRttHandshakeTests, TwoZeroRttPacketsAreDifferent) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);
  crypto::SessionKeys keys{};
  keys.send_key.fill(0x11);
  auto ticket = ticket_manager->issue_ticket(keys);

  handshake::ZeroRttInitiator initiator1(make_psk(), ticket, now_fn);
  handshake::ZeroRttInitiator initiator2(make_psk(), ticket, now_fn);

  auto init1 = initiator1.create_zero_rtt_init();
  auto init2 = initiator2.create_zero_rtt_init();

  // Should be different due to random nonce, ephemeral keys, and anti-replay nonce
  EXPECT_NE(init1, init2);
}

// =============================================================================
// 0-RTT Ticket Issuance After Handshake Test
// =============================================================================

TEST(ZeroRttHandshakeTests, TicketIssuedAfterHandshakeContainsCorrectKeys) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  // Complete a full handshake
  handshake::HandshakeInitiator initiator(make_psk(), std::chrono::milliseconds(1000), now_fn);
  handshake::HandshakeResponder responder(make_psk(), std::chrono::milliseconds(1000),
                                          make_bucket(), now_fn);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);
  ASSERT_TRUE(resp.has_value());

  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());

  // Issue ticket from responder's session
  auto ticket_manager = std::make_shared<handshake::SessionTicketManager>(
      std::chrono::milliseconds(60000), now_fn);
  auto ticket = ticket_manager->issue_ticket(resp->session.keys, "test-client");

  // Validate ticket contains correct keys
  auto payload = ticket_manager->validate_ticket(ticket.ticket_data);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->send_key, resp->session.keys.send_key);
  EXPECT_EQ(payload->recv_key, resp->session.keys.recv_key);
}

}  // namespace veil::tests
