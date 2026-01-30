#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/auth/client_registry.h"
#include "common/handshake/handshake_processor.h"
#include "common/utils/rate_limiter.h"

namespace veil::tests {

namespace {
std::vector<std::uint8_t> make_psk(std::uint8_t fill = 0xAA) {
  return std::vector<std::uint8_t>(32, fill);
}
}  // namespace

// Issue #87: Multi-client handshake tests
// Tests for per-client PSK authentication with trial decryption

class MultiClientHandshakeTests : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = std::chrono::system_clock::now();
    now_fn_ = [this]() { return now_; };

    registry_ = std::make_shared<auth::ClientRegistry>();
  }

  std::chrono::system_clock::time_point now_;
  std::function<std::chrono::system_clock::time_point()> now_fn_;
  std::shared_ptr<auth::ClientRegistry> registry_;
};

// ====================
// Basic Multi-Client Handshake
// ====================

TEST_F(MultiClientHandshakeTests, SingleClientHandshakeSuccess) {
  auto psk = make_psk(0xAA);
  registry_->add_client("alice", psk);

  // Client initiator with client_id (psk, client_id, skew_tolerance, now_fn)
  handshake::HandshakeInitiator initiator(psk, "alice", std::chrono::milliseconds(1000), now_fn_);

  // Multi-client responder
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value()) << "Handshake should succeed";
  EXPECT_EQ(resp->session.client_id, "alice") << "Session should identify client";

  // Verify keys match
  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->keys.send_key, resp->session.keys.recv_key);
  EXPECT_EQ(session->keys.recv_key, resp->session.keys.send_key);
}

TEST_F(MultiClientHandshakeTests, MultipleClientsWithDifferentPsks) {
  auto psk_alice = make_psk(0xAA);
  auto psk_bob = make_psk(0xBB);
  auto psk_charlie = make_psk(0xCC);

  registry_->add_client("alice", psk_alice);
  registry_->add_client("bob", psk_bob);
  registry_->add_client("charlie", psk_charlie);

  // Test each client can authenticate
  for (const auto& [name, psk] :
       std::vector<std::pair<std::string, std::vector<std::uint8_t>>>{
           {"alice", psk_alice}, {"bob", psk_bob}, {"charlie", psk_charlie}}) {
    handshake::HandshakeInitiator initiator(psk, name, std::chrono::milliseconds(1000), now_fn_);
    utils::TokenBucket bucket(100.0, std::chrono::milliseconds(1000),
                              [] { return std::chrono::steady_clock::now(); });
    handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                        std::move(bucket), now_fn_);

    const auto init_bytes = initiator.create_init();
    auto resp = responder.handle_init(init_bytes);

    ASSERT_TRUE(resp.has_value()) << "Handshake should succeed for " << name;
    EXPECT_EQ(resp->session.client_id, name) << "Session should identify " << name;
  }
}

// ====================
// Unknown Client / Wrong PSK
// ====================

TEST_F(MultiClientHandshakeTests, UnknownClientRejected) {
  auto psk_alice = make_psk(0xAA);
  auto psk_unknown = make_psk(0xFF);

  registry_->add_client("alice", psk_alice);

  // Client with unknown PSK
  handshake::HandshakeInitiator initiator(psk_unknown, "eve", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  EXPECT_FALSE(resp.has_value()) << "Handshake should fail for unknown client";
}

TEST_F(MultiClientHandshakeTests, ClientWithWrongPskRejected) {
  auto psk_alice = make_psk(0xAA);
  auto psk_wrong = make_psk(0xFF);

  registry_->add_client("alice", psk_alice);

  // Client claiming to be alice but with wrong PSK
  handshake::HandshakeInitiator initiator(psk_wrong, "alice", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  EXPECT_FALSE(resp.has_value()) << "Handshake should fail with wrong PSK";
}

// ====================
// Disabled Client
// ====================

TEST_F(MultiClientHandshakeTests, DisabledClientRejected) {
  auto psk_alice = make_psk(0xAA);
  registry_->add_client("alice", psk_alice);
  registry_->disable_client("alice");

  handshake::HandshakeInitiator initiator(psk_alice, "alice", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  EXPECT_FALSE(resp.has_value()) << "Handshake should fail for disabled client";
}

TEST_F(MultiClientHandshakeTests, ReenabledClientAccepted) {
  auto psk_alice = make_psk(0xAA);
  registry_->add_client("alice", psk_alice);
  registry_->disable_client("alice");
  registry_->enable_client("alice");

  handshake::HandshakeInitiator initiator(psk_alice, "alice", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value()) << "Handshake should succeed for re-enabled client";
  EXPECT_EQ(resp->session.client_id, "alice");
}

// ====================
// Fallback PSK
// ====================

TEST_F(MultiClientHandshakeTests, FallbackPskWorksForUnknownClient) {
  auto psk_fallback = make_psk(0xFF);
  registry_->set_fallback_psk(psk_fallback);

  // Client using fallback PSK without being in registry (no client_id)
  handshake::HandshakeInitiator initiator(psk_fallback, std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value()) << "Handshake should succeed with fallback PSK";
  EXPECT_TRUE(resp->session.client_id.empty()) << "Session should have empty client_id for fallback";
}

TEST_F(MultiClientHandshakeTests, RegisteredClientPreferredOverFallback) {
  auto psk_alice = make_psk(0xAA);
  auto psk_fallback = make_psk(0xFF);

  registry_->add_client("alice", psk_alice);
  registry_->set_fallback_psk(psk_fallback);

  // Client alice with her own PSK
  handshake::HandshakeInitiator initiator(psk_alice, "alice", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->session.client_id, "alice") << "Alice should be identified by her PSK, not fallback";
}

// ====================
// Client ID in Handshake
// ====================

TEST_F(MultiClientHandshakeTests, ClientIdIncludedInSession) {
  auto psk = make_psk(0xAA);
  registry_->add_client("alice-laptop-01", psk);

  handshake::HandshakeInitiator initiator(psk, "alice-laptop-01", std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->session.client_id, "alice-laptop-01");

  // Client side should also have client_id
  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->client_id, "alice-laptop-01");
}

TEST_F(MultiClientHandshakeTests, EmptyClientIdInitiator) {
  // Test that initiator without client_id works (for legacy compatibility)
  auto psk = make_psk(0xAA);
  registry_->add_client("alice", psk);

  // Initiator without client_id (using the 3-arg constructor)
  handshake::HandshakeInitiator initiator(psk, std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value()) << "Handshake should succeed even without client_id";
  EXPECT_EQ(resp->session.client_id, "alice") << "Server should identify client by PSK";
}

// ====================
// Rate Limiting
// ====================

TEST_F(MultiClientHandshakeTests, RateLimiterApplies) {
  auto psk = make_psk(0xAA);
  registry_->add_client("alice", psk);

  handshake::HandshakeInitiator initiator(psk, "alice", std::chrono::milliseconds(1000), now_fn_);

  // Tight rate limit: 1 request
  utils::TokenBucket bucket(1.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();

  auto first = responder.handle_init(init_bytes);
  auto second = responder.handle_init(init_bytes);

  EXPECT_TRUE(first.has_value()) << "First request should succeed";
  EXPECT_FALSE(second.has_value()) << "Second request should be rate limited";
}

// ====================
// Timestamp Validation
// ====================

TEST_F(MultiClientHandshakeTests, ExpiredTimestampRejected) {
  auto psk = make_psk(0xAA);
  registry_->add_client("alice", psk);

  auto old_time = now_ - std::chrono::seconds(10);
  auto old_now_fn = [old_time]() { return old_time; };

  // Initiator creates packet with old timestamp
  handshake::HandshakeInitiator initiator(psk, "alice", std::chrono::milliseconds(1000), old_now_fn);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  EXPECT_FALSE(resp.has_value()) << "Handshake with expired timestamp should fail";
}

// ====================
// Registry Access
// ====================

TEST_F(MultiClientHandshakeTests, ResponderProvidesRegistryAccess) {
  registry_->add_client("alice", make_psk(0xAA));

  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::MultiClientHandshakeResponder responder(registry_, std::chrono::milliseconds(1000),
                                                      std::move(bucket), now_fn_);

  auto reg = responder.registry();
  ASSERT_NE(reg, nullptr);
  EXPECT_TRUE(reg->has_client("alice"));
}

// ====================
// Backward Compatibility
// ====================

TEST_F(MultiClientHandshakeTests, SinglePskResponderStillWorks) {
  // Ensure the original HandshakeResponder still works for non-multi-client use
  auto psk = make_psk(0xAA);

  handshake::HandshakeInitiator initiator(psk, std::chrono::milliseconds(1000), now_fn_);
  utils::TokenBucket bucket(10.0, std::chrono::milliseconds(1000),
                            [] { return std::chrono::steady_clock::now(); });
  handshake::HandshakeResponder responder(psk, std::chrono::milliseconds(1000), std::move(bucket),
                                           now_fn_);

  const auto init_bytes = initiator.create_init();
  auto resp = responder.handle_init(init_bytes);

  ASSERT_TRUE(resp.has_value());

  auto session = initiator.consume_response(resp->response);
  ASSERT_TRUE(session.has_value());
  EXPECT_EQ(session->keys.send_key, resp->session.keys.recv_key);
}

}  // namespace veil::tests
