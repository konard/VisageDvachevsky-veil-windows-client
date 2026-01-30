#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/handshake/session_ticket.h"

namespace veil::tests {

namespace {

crypto::SessionKeys make_test_keys() {
  crypto::SessionKeys keys{};
  for (std::size_t i = 0; i < keys.send_key.size(); ++i) {
    keys.send_key[i] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < keys.recv_key.size(); ++i) {
    keys.recv_key[i] = static_cast<std::uint8_t>(i + 0x80);
  }
  for (std::size_t i = 0; i < keys.send_nonce.size(); ++i) {
    keys.send_nonce[i] = static_cast<std::uint8_t>(i + 0x40);
  }
  for (std::size_t i = 0; i < keys.recv_nonce.size(); ++i) {
    keys.recv_nonce[i] = static_cast<std::uint8_t>(i + 0xC0);
  }
  return keys;
}

}  // namespace

// =============================================================================
// SessionTicketManager Tests
// =============================================================================

TEST(SessionTicketManagerTests, IssueAndValidateTicket) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager.issue_ticket(keys, "test-client");

  EXPECT_FALSE(ticket.ticket_data.empty());
  EXPECT_GT(ticket.issued_at_ms, 0u);
  EXPECT_EQ(ticket.lifetime_ms, 60000u);
  EXPECT_EQ(ticket.client_id, "test-client");
  EXPECT_EQ(ticket.cached_keys.send_key, keys.send_key);
  EXPECT_EQ(ticket.cached_keys.recv_key, keys.recv_key);

  // Validate the ticket
  auto payload = manager.validate_ticket(ticket.ticket_data);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->send_key, keys.send_key);
  EXPECT_EQ(payload->recv_key, keys.recv_key);
  EXPECT_EQ(payload->send_nonce, keys.send_nonce);
  EXPECT_EQ(payload->recv_nonce, keys.recv_nonce);
}

TEST(SessionTicketManagerTests, ExpiredTicketRejected) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(1000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager.issue_ticket(keys);

  // Advance time past ticket lifetime
  now += std::chrono::seconds(2);

  auto payload = manager.validate_ticket(ticket.ticket_data);
  EXPECT_FALSE(payload.has_value());
}

TEST(SessionTicketManagerTests, CorruptedTicketRejected) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager.issue_ticket(keys);

  // Corrupt the ticket data
  auto corrupted = ticket.ticket_data;
  if (!corrupted.empty()) {
    corrupted[corrupted.size() / 2] ^= 0xFF;
  }

  auto payload = manager.validate_ticket(corrupted);
  EXPECT_FALSE(payload.has_value());
}

TEST(SessionTicketManagerTests, EmptyTicketRejected) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  std::vector<std::uint8_t> empty_ticket;
  auto payload = manager.validate_ticket(empty_ticket);
  EXPECT_FALSE(payload.has_value());
}

TEST(SessionTicketManagerTests, TruncatedTicketRejected) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager.issue_ticket(keys);

  // Truncate the ticket
  std::vector<std::uint8_t> truncated(ticket.ticket_data.begin(),
                                       ticket.ticket_data.begin() + 10);

  auto payload = manager.validate_ticket(truncated);
  EXPECT_FALSE(payload.has_value());
}

TEST(SessionTicketManagerTests, DifferentManagerCannotValidate) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager1(std::chrono::milliseconds(60000), now_fn);
  handshake::SessionTicketManager manager2(std::chrono::milliseconds(60000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager1.issue_ticket(keys);

  // Different manager has different key, cannot validate
  auto payload = manager2.validate_ticket(ticket.ticket_data);
  EXPECT_FALSE(payload.has_value());
}

TEST(SessionTicketManagerTests, IssueTicketWithEmptyClientId) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  auto keys = make_test_keys();
  auto ticket = manager.issue_ticket(keys);

  EXPECT_TRUE(ticket.client_id.empty());

  auto payload = manager.validate_ticket(ticket.ticket_data);
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(payload->send_key, keys.send_key);
}

// =============================================================================
// Anti-Replay Nonce Tests
// =============================================================================

TEST(SessionTicketManagerTests, AntiReplayNonceDetectsReplay) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  std::array<std::uint8_t, handshake::kAntiReplayNonceSize> nonce{};
  for (std::size_t i = 0; i < nonce.size(); ++i) {
    nonce[i] = static_cast<std::uint8_t>(i + 1);
  }

  // First use should succeed
  EXPECT_FALSE(manager.check_and_mark_nonce(nonce));

  // Second use should detect replay
  EXPECT_TRUE(manager.check_and_mark_nonce(nonce));
}

TEST(SessionTicketManagerTests, DifferentNoncesNotDetectedAsReplay) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketManager manager(std::chrono::milliseconds(60000), now_fn);

  std::array<std::uint8_t, handshake::kAntiReplayNonceSize> nonce1{};
  std::array<std::uint8_t, handshake::kAntiReplayNonceSize> nonce2{};
  nonce1.fill(0x01);
  nonce2.fill(0x02);

  EXPECT_FALSE(manager.check_and_mark_nonce(nonce1));
  EXPECT_FALSE(manager.check_and_mark_nonce(nonce2));
}

// =============================================================================
// SessionTicketStore Tests
// =============================================================================

TEST(SessionTicketStoreTests, StoreAndRetrieveTicket) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  handshake::SessionTicket ticket{
      .ticket_data = {1, 2, 3, 4},
      .issued_at_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
      .lifetime_ms = 60000,
      .cached_keys = make_test_keys(),
      .client_id = "test",
  };

  store.store_ticket("server1:4430", ticket);
  EXPECT_EQ(store.size(), 1u);

  auto retrieved = store.get_ticket("server1:4430");
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(retrieved->ticket_data, ticket.ticket_data);
  EXPECT_EQ(retrieved->client_id, "test");
}

TEST(SessionTicketStoreTests, NonExistentServerReturnsNullopt) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  auto retrieved = store.get_ticket("unknown:4430");
  EXPECT_FALSE(retrieved.has_value());
}

TEST(SessionTicketStoreTests, ExpiredTicketRemovedOnGet) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  handshake::SessionTicket ticket{
      .ticket_data = {1, 2, 3},
      .issued_at_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
      .lifetime_ms = 1000,
      .cached_keys = make_test_keys(),
      .client_id = {},
  };

  store.store_ticket("server1:4430", ticket);
  EXPECT_EQ(store.size(), 1u);

  // Advance time past ticket lifetime
  now += std::chrono::seconds(2);

  auto retrieved = store.get_ticket("server1:4430");
  EXPECT_FALSE(retrieved.has_value());
  EXPECT_EQ(store.size(), 0u);
}

TEST(SessionTicketStoreTests, RemoveTicket) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  handshake::SessionTicket ticket{
      .ticket_data = {1, 2, 3},
      .issued_at_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
      .lifetime_ms = 60000,
      .cached_keys = make_test_keys(),
      .client_id = {},
  };

  store.store_ticket("server1:4430", ticket);
  EXPECT_EQ(store.size(), 1u);

  store.remove_ticket("server1:4430");
  EXPECT_EQ(store.size(), 0u);

  auto retrieved = store.get_ticket("server1:4430");
  EXPECT_FALSE(retrieved.has_value());
}

TEST(SessionTicketStoreTests, CleanupExpiredTickets) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());

  handshake::SessionTicket short_lived{
      .ticket_data = {1},
      .issued_at_ms = now_ms,
      .lifetime_ms = 1000,
      .cached_keys = make_test_keys(),
      .client_id = {},
  };
  handshake::SessionTicket long_lived{
      .ticket_data = {2},
      .issued_at_ms = now_ms,
      .lifetime_ms = 60000,
      .cached_keys = make_test_keys(),
      .client_id = {},
  };

  store.store_ticket("short", short_lived);
  store.store_ticket("long", long_lived);
  EXPECT_EQ(store.size(), 2u);

  // Advance time past short ticket lifetime
  now += std::chrono::seconds(2);
  store.cleanup_expired();

  EXPECT_EQ(store.size(), 1u);
  EXPECT_FALSE(store.get_ticket("short").has_value());
  EXPECT_TRUE(store.get_ticket("long").has_value());
}

TEST(SessionTicketStoreTests, OverwriteExistingTicket) {
  auto now = std::chrono::system_clock::now();
  auto now_fn = [&]() { return now; };

  handshake::SessionTicketStore store(now_fn);

  auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());

  handshake::SessionTicket ticket1{
      .ticket_data = {1, 1, 1},
      .issued_at_ms = now_ms,
      .lifetime_ms = 60000,
      .cached_keys = make_test_keys(),
      .client_id = "first",
  };
  handshake::SessionTicket ticket2{
      .ticket_data = {2, 2, 2},
      .issued_at_ms = now_ms,
      .lifetime_ms = 60000,
      .cached_keys = make_test_keys(),
      .client_id = "second",
  };

  store.store_ticket("server1:4430", ticket1);
  store.store_ticket("server1:4430", ticket2);
  EXPECT_EQ(store.size(), 1u);

  auto retrieved = store.get_ticket("server1:4430");
  ASSERT_TRUE(retrieved.has_value());
  EXPECT_EQ(retrieved->ticket_data, ticket2.ticket_data);
  EXPECT_EQ(retrieved->client_id, "second");
}

// =============================================================================
// SessionTicket Expiry Tests
// =============================================================================

TEST(SessionTicketTests, IsExpiredReturnsFalseWhenValid) {
  handshake::SessionTicket ticket{
      .ticket_data = {},
      .issued_at_ms = 1000,
      .lifetime_ms = 5000,
      .cached_keys = {},
      .client_id = {},
  };

  EXPECT_FALSE(ticket.is_expired(2000));  // Within lifetime
  EXPECT_FALSE(ticket.is_expired(5999));  // Just before expiry
}

TEST(SessionTicketTests, IsExpiredReturnsTrueWhenExpired) {
  handshake::SessionTicket ticket{
      .ticket_data = {},
      .issued_at_ms = 1000,
      .lifetime_ms = 5000,
      .cached_keys = {},
      .client_id = {},
  };

  EXPECT_TRUE(ticket.is_expired(6001));   // Just after expiry
  EXPECT_TRUE(ticket.is_expired(10000));  // Well after expiry
}

}  // namespace veil::tests
