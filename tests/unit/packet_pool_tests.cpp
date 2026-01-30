#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "common/crypto/crypto_engine.h"
#include "common/utils/packet_pool.h"

namespace veil::utils {
namespace {

class PacketPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Basic functionality tests

TEST_F(PacketPoolTest, DefaultConstruction) {
  PacketPool pool;
  EXPECT_EQ(pool.available(), 0);
  EXPECT_EQ(pool.allocations(), 0);
  EXPECT_EQ(pool.reuses(), 0);
  EXPECT_EQ(pool.releases(), 0);
}

TEST_F(PacketPoolTest, ConstructionWithPreallocation) {
  PacketPool pool(10, 1500);
  EXPECT_EQ(pool.available(), 10);
  EXPECT_EQ(pool.allocations(), 0);  // Pre-allocation doesn't count
}

TEST_F(PacketPoolTest, AcquireFromEmptyPool) {
  PacketPool pool;
  auto buffer = pool.acquire();

  EXPECT_EQ(buffer.size(), 0);  // Acquired buffer should be empty
  EXPECT_GE(buffer.capacity(), 1500);  // Default capacity
  EXPECT_EQ(pool.allocations(), 1);
  EXPECT_EQ(pool.reuses(), 0);
  EXPECT_EQ(pool.available(), 0);
}

TEST_F(PacketPoolTest, AcquireFromPreallocatedPool) {
  PacketPool pool(5, 1500);
  auto buffer = pool.acquire();

  EXPECT_EQ(buffer.size(), 0);
  EXPECT_GE(buffer.capacity(), 1500);
  EXPECT_EQ(pool.allocations(), 0);
  EXPECT_EQ(pool.reuses(), 1);
  EXPECT_EQ(pool.available(), 4);
}

TEST_F(PacketPoolTest, ReleaseBuffer) {
  PacketPool pool;
  auto buffer = pool.acquire();

  buffer.resize(100);  // Use some of the buffer
  pool.release(std::move(buffer));

  EXPECT_EQ(pool.releases(), 1);
  EXPECT_EQ(pool.available(), 1);
}

TEST_F(PacketPoolTest, ReuseReleasedBuffer) {
  PacketPool pool;

  // First acquire - allocates new buffer
  auto buffer1 = pool.acquire();
  EXPECT_EQ(pool.allocations(), 1);

  buffer1.resize(50);
  pool.release(std::move(buffer1));
  EXPECT_EQ(pool.available(), 1);

  // Second acquire - reuses released buffer
  auto buffer2 = pool.acquire();
  EXPECT_EQ(pool.allocations(), 1);  // No new allocation
  EXPECT_EQ(pool.reuses(), 1);
  EXPECT_EQ(buffer2.size(), 0);  // Buffer should be cleared
  EXPECT_GE(buffer2.capacity(), 1500);  // Capacity preserved
}

TEST_F(PacketPoolTest, MultipleAcquireRelease) {
  PacketPool pool(3, 1000);

  // Acquire all pre-allocated buffers
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(3);
  for (int i = 0; i < 3; ++i) {
    buffers.push_back(pool.acquire());
  }
  EXPECT_EQ(pool.available(), 0);
  EXPECT_EQ(pool.reuses(), 3);

  // Acquire one more - should allocate
  auto extra = pool.acquire();
  EXPECT_EQ(pool.allocations(), 1);

  // Release all
  for (auto& buf : buffers) {
    pool.release(std::move(buf));
  }
  pool.release(std::move(extra));

  EXPECT_EQ(pool.available(), 4);
  EXPECT_EQ(pool.releases(), 4);
}

TEST_F(PacketPoolTest, HitRate) {
  PacketPool pool(5, 1000);

  // Initial hit rate should be 0 (undefined, but returns 0)
  EXPECT_EQ(pool.hit_rate(), 0.0);

  // Acquire 5 buffers (all reuses from pre-allocated), then release them
  // Each acquire from preallocated pool counts as a reuse
  for (int i = 0; i < 5; ++i) {
    auto buf = pool.acquire();
    pool.release(std::move(buf));
  }

  // Each iteration: acquire (reuse) -> release -> the buffer goes back
  // So we have 5 reuses total (one per iteration, since after release
  // the buffer is available for the next iteration's acquire)
  EXPECT_EQ(pool.reuses(), 5);
  EXPECT_EQ(pool.allocations(), 0);
  EXPECT_EQ(pool.hit_rate(), 1.0);

  // Acquire 5 more without releasing -> forces allocations after pool is empty
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(5);
  for (int i = 0; i < 5; ++i) {
    buffers.push_back(pool.acquire());
  }
  EXPECT_EQ(pool.available(), 0);

  // Now acquire one more - must allocate
  auto extra = pool.acquire();
  EXPECT_EQ(pool.allocations(), 1);

  // Hit rate should decrease (10 reuses, 1 allocation -> 10/11)
  EXPECT_LT(pool.hit_rate(), 1.0);
  EXPECT_GT(pool.hit_rate(), 0.0);
}

TEST_F(PacketPoolTest, MaxPoolSize) {
  PacketPool pool(0, 1000);
  pool.set_max_pool_size(2);

  // First, acquire 3 buffers (allocates new ones since pool is empty)
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(3);
  for (int i = 0; i < 3; ++i) {
    buffers.push_back(pool.acquire());
  }
  EXPECT_EQ(pool.allocations(), 3);
  EXPECT_EQ(pool.available(), 0);

  // Now release all 3 - but pool max is 2
  for (auto& buf : buffers) {
    pool.release(std::move(buf));
  }

  // Only 2 should be stored (third one dropped)
  EXPECT_EQ(pool.available(), 2);
  EXPECT_EQ(pool.releases(), 3);  // All 3 were attempted to be released
}

TEST_F(PacketPoolTest, PreallocateAdditional) {
  PacketPool pool(2, 1000);
  EXPECT_EQ(pool.available(), 2);

  pool.preallocate(3);
  EXPECT_EQ(pool.available(), 5);
}

TEST_F(PacketPoolTest, BufferCapacityPreserved) {
  PacketPool pool(0, 2000);  // Custom capacity

  auto buffer = pool.acquire();
  EXPECT_GE(buffer.capacity(), 2000);

  // Use the buffer
  buffer.resize(1000);
  for (std::size_t i = 0; i < buffer.size(); ++i) {
    buffer[i] = static_cast<std::uint8_t>(i & 0xFF);
  }

  pool.release(std::move(buffer));

  // Re-acquire and check capacity is preserved
  auto buffer2 = pool.acquire();
  EXPECT_GE(buffer2.capacity(), 2000);
  EXPECT_EQ(buffer2.size(), 0);  // Should be cleared
}

// Thread-safe pool tests

class ThreadSafePacketPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(ThreadSafePacketPoolTest, DefaultConstruction) {
  ThreadSafePacketPool pool;
  EXPECT_EQ(pool.available(), 0);
}

TEST_F(ThreadSafePacketPoolTest, BasicAcquireRelease) {
  ThreadSafePacketPool pool(5, 1500);

  auto buffer = pool.acquire();
  EXPECT_EQ(buffer.size(), 0);
  EXPECT_GE(buffer.capacity(), 1500);
  EXPECT_EQ(pool.available(), 4);

  pool.release(std::move(buffer));
  EXPECT_EQ(pool.available(), 5);
}

TEST_F(ThreadSafePacketPoolTest, ConcurrentAccess) {
  ThreadSafePacketPool pool(100, 1000);

  constexpr int kThreads = 4;
  constexpr int kIterations = 1000;

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&pool]() {
      for (int i = 0; i < kIterations; ++i) {
        auto buffer = pool.acquire();
        // Simulate some work
        buffer.resize(100);
        for (std::size_t j = 0; j < buffer.size(); ++j) {
          buffer[j] = static_cast<std::uint8_t>(j);
        }
        pool.release(std::move(buffer));
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // After all threads complete, pool should have consistent state
  EXPECT_EQ(pool.releases(), kThreads * kIterations);
  // Some buffers should be in the pool
  EXPECT_GE(pool.available(), 0);
}

TEST_F(ThreadSafePacketPoolTest, ProducerConsumerPattern) {
  ThreadSafePacketPool pool(50, 1000);

  constexpr int kMessages = 500;
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};

  // Producer thread
  std::thread producer([&]() {
    for (int i = 0; i < kMessages; ++i) {
      auto buffer = pool.acquire();
      buffer.push_back(static_cast<std::uint8_t>(i & 0xFF));
      pool.release(std::move(buffer));
      ++produced;
    }
  });

  // Consumer thread - just acquires and releases
  std::thread consumer([&]() {
    while (consumed < kMessages) {
      auto buffer = pool.acquire();
      pool.release(std::move(buffer));
      ++consumed;
    }
  });

  producer.join();
  consumer.join();

  EXPECT_EQ(produced.load(), kMessages);
  EXPECT_GE(consumed.load(), kMessages);
}

// Crypto output buffer tests (testing the new APIs)
// These tests need to use the veil::crypto namespace

TEST(CryptoOutputBufferTest, AeadCiphertextSize) {
  EXPECT_EQ(veil::crypto::aead_ciphertext_size(0), 16);    // Tag only
  EXPECT_EQ(veil::crypto::aead_ciphertext_size(100), 116); // Plaintext + tag
  EXPECT_EQ(veil::crypto::aead_ciphertext_size(1400), 1416);
}

TEST(CryptoOutputBufferTest, AeadPlaintextSize) {
  EXPECT_EQ(veil::crypto::aead_plaintext_size(0), 0);      // Too small
  EXPECT_EQ(veil::crypto::aead_plaintext_size(15), 0);     // Still too small
  EXPECT_EQ(veil::crypto::aead_plaintext_size(16), 0);     // Exactly tag size
  EXPECT_EQ(veil::crypto::aead_plaintext_size(17), 1);     // 1 byte plaintext
  EXPECT_EQ(veil::crypto::aead_plaintext_size(116), 100);
}

TEST(CryptoOutputBufferTest, EncryptDecryptToBuffer) {
  // Generate test key and nonce
  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key{};
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};
  for (std::size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<std::uint8_t>(i);
  }
  for (std::size_t i = 0; i < nonce.size(); ++i) {
    nonce[i] = static_cast<std::uint8_t>(i + 100);
  }

  // Test plaintext
  std::vector<std::uint8_t> plaintext(100);
  for (std::size_t i = 0; i < plaintext.size(); ++i) {
    plaintext[i] = static_cast<std::uint8_t>(i * 2);
  }

  // Encrypt to buffer
  std::vector<std::uint8_t> ciphertext(veil::crypto::aead_ciphertext_size(plaintext.size()));
  std::size_t ct_len = veil::crypto::aead_encrypt_to(key, nonce, {}, plaintext, ciphertext);
  ASSERT_GT(ct_len, 0);
  EXPECT_EQ(ct_len, plaintext.size() + veil::crypto::kAeadTagLen);

  // Decrypt to buffer
  std::vector<std::uint8_t> decrypted(veil::crypto::aead_plaintext_size(ct_len));
  std::size_t pt_len = veil::crypto::aead_decrypt_to(key, nonce, {},
                                                std::span<const std::uint8_t>(ciphertext.data(), ct_len),
                                                decrypted);
  ASSERT_GT(pt_len, 0);
  EXPECT_EQ(pt_len, plaintext.size());

  // Compare
  EXPECT_EQ(decrypted, plaintext);
}

TEST(CryptoOutputBufferTest, EncryptToBufferTooSmall) {
  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key{};
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};
  std::vector<std::uint8_t> plaintext(100);
  std::vector<std::uint8_t> ciphertext(50);  // Too small

  std::size_t len = veil::crypto::aead_encrypt_to(key, nonce, {}, plaintext, ciphertext);
  EXPECT_EQ(len, 0);  // Should fail
}

TEST(CryptoOutputBufferTest, DecryptToBufferTooSmall) {
  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key{};
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};

  // First encrypt something
  std::vector<std::uint8_t> plaintext(100);
  std::vector<std::uint8_t> ciphertext(veil::crypto::aead_ciphertext_size(plaintext.size()));
  veil::crypto::aead_encrypt_to(key, nonce, {}, plaintext, ciphertext);

  // Try to decrypt with too small buffer
  std::vector<std::uint8_t> decrypted(50);  // Too small
  std::size_t len = veil::crypto::aead_decrypt_to(key, nonce, {}, ciphertext, decrypted);
  EXPECT_EQ(len, 0);  // Should fail
}

TEST(CryptoOutputBufferTest, DecryptWithWrongKey) {
  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key1{};
  std::array<std::uint8_t, veil::crypto::kAeadKeyLen> key2{};
  std::array<std::uint8_t, veil::crypto::kNonceLen> nonce{};

  key1[0] = 1;
  key2[0] = 2;  // Different key

  std::vector<std::uint8_t> plaintext(100);
  std::vector<std::uint8_t> ciphertext(veil::crypto::aead_ciphertext_size(plaintext.size()));
  veil::crypto::aead_encrypt_to(key1, nonce, {}, plaintext, ciphertext);

  std::vector<std::uint8_t> decrypted(veil::crypto::aead_plaintext_size(ciphertext.size()));
  std::size_t len = veil::crypto::aead_decrypt_to(key2, nonce, {}, ciphertext, decrypted);
  EXPECT_EQ(len, 0);  // Authentication should fail
}

}  // namespace
}  // namespace veil::utils
