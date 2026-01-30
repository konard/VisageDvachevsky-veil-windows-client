#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "common/auth/client_registry.h"

namespace veil::tests {

namespace {
std::vector<std::uint8_t> make_psk(std::uint8_t fill = 0xAA) {
  return std::vector<std::uint8_t>(32, fill);
}

std::vector<std::uint8_t> make_psk_64(std::uint8_t fill = 0xBB) {
  return std::vector<std::uint8_t>(64, fill);
}
}  // namespace

// Issue #87: Per-client PSK authentication tests

// ====================
// Client ID Validation
// ====================

TEST(ClientRegistryTests, ValidClientIdAccepted) {
  EXPECT_TRUE(auth::is_valid_client_id("alice"));
  EXPECT_TRUE(auth::is_valid_client_id("bob-laptop"));
  EXPECT_TRUE(auth::is_valid_client_id("user_123"));
  EXPECT_TRUE(auth::is_valid_client_id("Client-Device_01"));
  EXPECT_TRUE(auth::is_valid_client_id("a"));  // Single char
  EXPECT_TRUE(auth::is_valid_client_id(std::string(64, 'x')));  // Max length
}

TEST(ClientRegistryTests, InvalidClientIdRejected) {
  EXPECT_FALSE(auth::is_valid_client_id(""));  // Empty
  EXPECT_FALSE(auth::is_valid_client_id("user@domain"));  // Special char @
  EXPECT_FALSE(auth::is_valid_client_id("user.name"));  // Special char .
  EXPECT_FALSE(auth::is_valid_client_id("user name"));  // Space
  EXPECT_FALSE(auth::is_valid_client_id("user\ttab"));  // Tab
  EXPECT_FALSE(auth::is_valid_client_id(std::string(65, 'x')));  // Too long
}

// ====================
// PSK Size Validation
// ====================

TEST(ClientRegistryTests, ValidPskSizeAccepted) {
  EXPECT_TRUE(auth::is_valid_psk_size(32));  // Minimum (256 bits)
  EXPECT_TRUE(auth::is_valid_psk_size(48));  // 384 bits
  EXPECT_TRUE(auth::is_valid_psk_size(64));  // Maximum (512 bits)
}

TEST(ClientRegistryTests, InvalidPskSizeRejected) {
  EXPECT_FALSE(auth::is_valid_psk_size(0));
  EXPECT_FALSE(auth::is_valid_psk_size(31));  // Too small
  EXPECT_FALSE(auth::is_valid_psk_size(65));  // Too large
  EXPECT_FALSE(auth::is_valid_psk_size(128));  // Way too large
}

// ====================
// Basic Operations
// ====================

TEST(ClientRegistryTests, AddClientSuccess) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk(0xAA)));
  EXPECT_TRUE(registry.has_client("alice"));
  EXPECT_EQ(registry.client_count(), 1);
}

TEST(ClientRegistryTests, AddMultipleClients) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk(0xAA)));
  EXPECT_TRUE(registry.add_client("bob", make_psk(0xBB)));
  EXPECT_TRUE(registry.add_client("charlie", make_psk(0xCC)));

  EXPECT_EQ(registry.client_count(), 3);
  EXPECT_TRUE(registry.has_client("alice"));
  EXPECT_TRUE(registry.has_client("bob"));
  EXPECT_TRUE(registry.has_client("charlie"));
}

TEST(ClientRegistryTests, AddDuplicateClientFails) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk(0xAA)));
  EXPECT_FALSE(registry.add_client("alice", make_psk(0xBB)));  // Duplicate
  EXPECT_EQ(registry.client_count(), 1);
}

TEST(ClientRegistryTests, AddClientInvalidIdFails) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.add_client("", make_psk()));  // Empty ID
  EXPECT_FALSE(registry.add_client("user@invalid", make_psk()));  // Invalid char
  EXPECT_EQ(registry.client_count(), 0);
}

TEST(ClientRegistryTests, AddClientInvalidPskFails) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.add_client("alice", std::vector<std::uint8_t>(31, 0xAA)));  // Too small
  EXPECT_FALSE(registry.add_client("alice", std::vector<std::uint8_t>(65, 0xAA)));  // Too large
  EXPECT_EQ(registry.client_count(), 0);
}

TEST(ClientRegistryTests, RemoveClientSuccess) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk()));
  EXPECT_TRUE(registry.has_client("alice"));

  EXPECT_TRUE(registry.remove_client("alice"));
  EXPECT_FALSE(registry.has_client("alice"));
  EXPECT_EQ(registry.client_count(), 0);
}

TEST(ClientRegistryTests, RemoveNonexistentClientFails) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.remove_client("nobody"));
}

// ====================
// PSK Lookup
// ====================

TEST(ClientRegistryTests, GetPskReturnsCorrectKey) {
  auth::ClientRegistry registry;

  auto psk_alice = make_psk(0xAA);
  auto psk_bob = make_psk(0xBB);

  EXPECT_TRUE(registry.add_client("alice", psk_alice));
  EXPECT_TRUE(registry.add_client("bob", psk_bob));

  auto result_alice = registry.get_psk("alice");
  auto result_bob = registry.get_psk("bob");

  ASSERT_TRUE(result_alice.has_value());
  ASSERT_TRUE(result_bob.has_value());
  EXPECT_EQ(*result_alice, psk_alice);
  EXPECT_EQ(*result_bob, psk_bob);
}

TEST(ClientRegistryTests, GetPskUnknownClientReturnsNullopt) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk()));

  auto result = registry.get_psk("bob");
  EXPECT_FALSE(result.has_value());
}

TEST(ClientRegistryTests, GetPskDisabledClientReturnsNullopt) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk()));
  EXPECT_TRUE(registry.disable_client("alice"));

  auto result = registry.get_psk("alice");
  EXPECT_FALSE(result.has_value());
}

// ====================
// Enable/Disable
// ====================

TEST(ClientRegistryTests, DisableEnableClient) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk()));
  EXPECT_TRUE(registry.is_client_enabled("alice"));

  EXPECT_TRUE(registry.disable_client("alice"));
  EXPECT_FALSE(registry.is_client_enabled("alice"));

  EXPECT_TRUE(registry.enable_client("alice"));
  EXPECT_TRUE(registry.is_client_enabled("alice"));
}

TEST(ClientRegistryTests, EnableDisableNonexistentClientFails) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.enable_client("nobody"));
  EXPECT_FALSE(registry.disable_client("nobody"));
}

TEST(ClientRegistryTests, IsClientEnabledReturnsFalseForUnknown) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.is_client_enabled("nobody"));
}

// ====================
// Fallback PSK
// ====================

TEST(ClientRegistryTests, SetFallbackPsk) {
  auth::ClientRegistry registry;

  auto psk = make_psk(0xFF);
  EXPECT_TRUE(registry.set_fallback_psk(psk));
  EXPECT_TRUE(registry.has_fallback_psk());

  auto result = registry.get_fallback_psk();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, psk);
}

TEST(ClientRegistryTests, SetFallbackPskInvalidSizeFails) {
  auth::ClientRegistry registry;

  EXPECT_FALSE(registry.set_fallback_psk(std::vector<std::uint8_t>(31, 0xFF)));
  EXPECT_FALSE(registry.has_fallback_psk());
}

TEST(ClientRegistryTests, ClearFallbackPsk) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.set_fallback_psk(make_psk()));
  EXPECT_TRUE(registry.has_fallback_psk());

  registry.clear_fallback_psk();
  EXPECT_FALSE(registry.has_fallback_psk());
}

TEST(ClientRegistryTests, GetPskOrFallbackReturnsClientPsk) {
  auth::ClientRegistry registry;

  auto psk_alice = make_psk(0xAA);
  auto psk_fallback = make_psk(0xFF);

  EXPECT_TRUE(registry.add_client("alice", psk_alice));
  EXPECT_TRUE(registry.set_fallback_psk(psk_fallback));

  auto result = registry.get_psk_or_fallback("alice");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, psk_alice);  // Should return client PSK, not fallback
}

TEST(ClientRegistryTests, GetPskOrFallbackReturnsFallbackForUnknown) {
  auth::ClientRegistry registry;

  auto psk_fallback = make_psk(0xFF);
  EXPECT_TRUE(registry.set_fallback_psk(psk_fallback));

  auto result = registry.get_psk_or_fallback("unknown");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, psk_fallback);
}

TEST(ClientRegistryTests, GetPskOrFallbackReturnsFallbackForDisabled) {
  auth::ClientRegistry registry;

  auto psk_alice = make_psk(0xAA);
  auto psk_fallback = make_psk(0xFF);

  EXPECT_TRUE(registry.add_client("alice", psk_alice));
  EXPECT_TRUE(registry.set_fallback_psk(psk_fallback));
  EXPECT_TRUE(registry.disable_client("alice"));

  auto result = registry.get_psk_or_fallback("alice");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, psk_fallback);  // Disabled client falls back
}

TEST(ClientRegistryTests, GetPskOrFallbackReturnsNulloptWhenBothMissing) {
  auth::ClientRegistry registry;

  auto result = registry.get_psk_or_fallback("unknown");
  EXPECT_FALSE(result.has_value());
}

TEST(ClientRegistryTests, GetPskOrFallbackEmptyClientIdUsesFallback) {
  auth::ClientRegistry registry;

  auto psk_fallback = make_psk(0xFF);
  EXPECT_TRUE(registry.set_fallback_psk(psk_fallback));

  auto result = registry.get_psk_or_fallback("");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, psk_fallback);
}

// ====================
// Get All PSKs
// ====================

TEST(ClientRegistryTests, GetAllEnabledPsks) {
  auth::ClientRegistry registry;

  auto psk_alice = make_psk(0xAA);
  auto psk_bob = make_psk(0xBB);
  auto psk_charlie = make_psk(0xCC);

  EXPECT_TRUE(registry.add_client("alice", psk_alice));
  EXPECT_TRUE(registry.add_client("bob", psk_bob));
  EXPECT_TRUE(registry.add_client("charlie", psk_charlie));
  EXPECT_TRUE(registry.disable_client("bob"));

  auto all_psks = registry.get_all_enabled_psks();
  EXPECT_EQ(all_psks.size(), 2);  // Only alice and charlie (bob is disabled)

  // Check that alice and charlie are in the result
  bool found_alice = false;
  bool found_charlie = false;
  bool found_bob = false;
  for (const auto& [id, psk] : all_psks) {
    if (id == "alice") {
      found_alice = true;
      EXPECT_EQ(psk, psk_alice);
    } else if (id == "charlie") {
      found_charlie = true;
      EXPECT_EQ(psk, psk_charlie);
    } else if (id == "bob") {
      found_bob = true;
    }
  }
  EXPECT_TRUE(found_alice);
  EXPECT_TRUE(found_charlie);
  EXPECT_FALSE(found_bob);
}

TEST(ClientRegistryTests, GetClientIds) {
  auth::ClientRegistry registry;

  EXPECT_TRUE(registry.add_client("alice", make_psk()));
  EXPECT_TRUE(registry.add_client("bob", make_psk()));
  EXPECT_TRUE(registry.add_client("charlie", make_psk()));

  auto ids = registry.get_client_ids();
  EXPECT_EQ(ids.size(), 3);

  // Sort for consistent comparison
  std::sort(ids.begin(), ids.end());
  EXPECT_EQ(ids[0], "alice");
  EXPECT_EQ(ids[1], "bob");
  EXPECT_EQ(ids[2], "charlie");
}

// ====================
// Move Operations
// ====================

TEST(ClientRegistryTests, MoveConstructor) {
  auth::ClientRegistry registry1;
  EXPECT_TRUE(registry1.add_client("alice", make_psk(0xAA)));
  EXPECT_TRUE(registry1.set_fallback_psk(make_psk(0xFF)));

  auth::ClientRegistry registry2(std::move(registry1));

  EXPECT_TRUE(registry2.has_client("alice"));
  EXPECT_TRUE(registry2.has_fallback_psk());
  EXPECT_EQ(registry2.client_count(), 1);
}

TEST(ClientRegistryTests, MoveAssignment) {
  auth::ClientRegistry registry1;
  EXPECT_TRUE(registry1.add_client("alice", make_psk(0xAA)));

  auth::ClientRegistry registry2;
  EXPECT_TRUE(registry2.add_client("bob", make_psk(0xBB)));

  registry2 = std::move(registry1);

  EXPECT_TRUE(registry2.has_client("alice"));
  EXPECT_FALSE(registry2.has_client("bob"));
}

// ====================
// Thread Safety
// ====================

TEST(ClientRegistryTests, ConcurrentReads) {
  auth::ClientRegistry registry;

  // Add some clients
  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(registry.add_client("client_" + std::to_string(i), make_psk(static_cast<std::uint8_t>(i))));
  }

  // Concurrent reads should not crash
  std::vector<std::thread> threads;
  threads.reserve(4);
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&registry]() {
      for (int i = 0; i < 1000; ++i) {
        auto ids = registry.get_client_ids();
        (void)ids;  // Just access it
        auto psk = registry.get_psk("client_5");
        (void)psk;
        bool enabled = registry.is_client_enabled("client_5");
        (void)enabled;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Verify registry is still intact
  EXPECT_EQ(registry.client_count(), 10);
}

TEST(ClientRegistryTests, ConcurrentReadsAndWrites) {
  auth::ClientRegistry registry;

  // Add initial clients
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(registry.add_client("client_" + std::to_string(i), make_psk(static_cast<std::uint8_t>(i))));
  }

  std::atomic<bool> stop{false};

  // Reader thread
  std::thread reader([&registry, &stop]() {
    while (!stop) {
      auto ids = registry.get_client_ids();
      (void)ids;
      auto psk = registry.get_psk("client_0");
      (void)psk;
    }
  });

  // Writer thread (enable/disable)
  std::thread writer([&registry, &stop]() {
    int count = 0;
    while (!stop && count < 100) {
      registry.disable_client("client_0");
      registry.enable_client("client_0");
      ++count;
    }
    stop = true;
  });

  writer.join();
  reader.join();

  // Verify registry is still functional
  EXPECT_TRUE(registry.has_client("client_0"));
}

// ====================
// Large PSK (64 bytes)
// ====================

TEST(ClientRegistryTests, Add64BytePsk) {
  auth::ClientRegistry registry;

  auto large_psk = make_psk_64(0xDD);
  EXPECT_TRUE(registry.add_client("alice", large_psk));

  auto result = registry.get_psk("alice");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 64);
  EXPECT_EQ(*result, large_psk);
}

}  // namespace veil::tests
