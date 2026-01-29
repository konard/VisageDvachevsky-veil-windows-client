#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "transport/mux/ack_bitmap.h"

namespace veil::tests {

TEST(AckBitmapTests, TracksHeadAndBitmap) {
  mux::AckBitmap bitmap;
  bitmap.ack(5);
  EXPECT_TRUE(bitmap.is_acked(5));
  EXPECT_FALSE(bitmap.is_acked(4));

  bitmap.ack(4);
  EXPECT_TRUE(bitmap.is_acked(4));

  bitmap.ack(9);
  EXPECT_TRUE(bitmap.is_acked(9));
  // Issue #80 fix: After ack(9), sequence 5 is still in the 32-packet window
  // (diff = 9 - 5 = 4 < 32) and was previously acked, so it should return true
  EXPECT_TRUE(bitmap.is_acked(5));
  EXPECT_TRUE(bitmap.is_acked(4));  // Also still in window
}

// Test sequence number wraparound handling
TEST(AckBitmapTests, HandlesSequenceWraparound) {
  mux::AckBitmap bitmap;

  // Initialize near UINT64_MAX
  constexpr std::uint64_t kNearMax = std::numeric_limits<std::uint64_t>::max() - 10;
  bitmap.ack(kNearMax);
  EXPECT_TRUE(bitmap.is_acked(kNearMax));
  EXPECT_EQ(bitmap.head(), kNearMax);

  // Ack a sequence number that wraps around to small value
  constexpr std::uint64_t kWrappedSeq = 5;  // After wraparound
  bitmap.ack(kWrappedSeq);
  EXPECT_TRUE(bitmap.is_acked(kWrappedSeq));
  EXPECT_EQ(bitmap.head(), kWrappedSeq);

  // Issue #80 fix: After large forward shift (>32), old sequences are cleared
  // The shift from kNearMax to 5 is 16 positions, which clears the bitmap
  // (5 - kNearMax wraps to 16 in uint64 arithmetic)
  // Actually: (5 - (UINT64_MAX - 10)) = 16, so shift by 16, bitmap cleared and bit 15 set
  // So kNearMax (which is now 16 positions back) should still be marked
  EXPECT_TRUE(bitmap.is_acked(kNearMax));
}

TEST(AckBitmapTests, WraparoundWithinBitmapWindow) {
  mux::AckBitmap bitmap;

  // Initialize near UINT64_MAX
  constexpr std::uint64_t kNearMax = std::numeric_limits<std::uint64_t>::max() - 2;
  bitmap.ack(kNearMax);
  EXPECT_TRUE(bitmap.is_acked(kNearMax));

  // Ack UINT64_MAX (shift by 2, bitmap window moves)
  bitmap.ack(std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(bitmap.is_acked(std::numeric_limits<std::uint64_t>::max()));

  // Ack after wraparound (shift by 1)
  bitmap.ack(0);
  EXPECT_TRUE(bitmap.is_acked(0));

  // Now explicitly ack UINT64_MAX again (backward ack within window)
  // This tests that wraparound-aware comparison allows backward ack
  bitmap.ack(std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(bitmap.is_acked(std::numeric_limits<std::uint64_t>::max()));

  // Ack kNearMax explicitly (backward ack within window)
  bitmap.ack(kNearMax);
  EXPECT_TRUE(bitmap.is_acked(kNearMax));
}

TEST(AckBitmapTests, WraparoundBackwardAck) {
  mux::AckBitmap bitmap;

  // Start after wraparound
  bitmap.ack(10);
  EXPECT_TRUE(bitmap.is_acked(10));

  // Ack older sequences within window (after wraparound)
  bitmap.ack(9);
  EXPECT_TRUE(bitmap.is_acked(9));

  bitmap.ack(5);
  EXPECT_TRUE(bitmap.is_acked(5));

  // Try to ack a very old sequence from before wraparound
  // With head=10 and kBeforeWrap near UINT64_MAX, the difference is huge
  constexpr std::uint64_t kBeforeWrap = std::numeric_limits<std::uint64_t>::max() - 5;
  bitmap.ack(kBeforeWrap);

  // With wraparound-aware comparison, seq_less_than(10, kBeforeWrap) = false
  // because kBeforeWrap is actually "less than" 10 in wraparound semantics
  // But the diff = 10 - kBeforeWrap wraps to a large number > 32
  // Actually: 10 - (UINT64_MAX - 5) = 10 - UINT64_MAX + 5 = 16 (wraps around)
  // So it's within the 32-packet window!
  // But since we never explicitly acked it before, and we just tried to ack it,
  // let's check if it gets added
  EXPECT_TRUE(bitmap.is_acked(kBeforeWrap));
}

TEST(AckBitmapTests, LargeSequenceJump) {
  mux::AckBitmap bitmap;

  // Start at a normal sequence
  bitmap.ack(1000);
  EXPECT_TRUE(bitmap.is_acked(1000));

  // Large jump forward (> 32)
  bitmap.ack(1100);
  EXPECT_TRUE(bitmap.is_acked(1100));
  EXPECT_FALSE(bitmap.is_acked(1000));  // Outside window now

  // Ack old sequence (too old)
  bitmap.ack(1000);
  EXPECT_FALSE(bitmap.is_acked(1000));  // Still outside window
}

TEST(AckBitmapTests, ExactWraparoundBoundary) {
  mux::AckBitmap bitmap;

  // Test exactly at UINT64_MAX
  bitmap.ack(std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(bitmap.is_acked(std::numeric_limits<std::uint64_t>::max()));

  // Next sequence wraps to 0
  bitmap.ack(0);
  EXPECT_TRUE(bitmap.is_acked(0));
  EXPECT_EQ(bitmap.head(), 0);

  // Explicitly ack UINT64_MAX again (backward ack, diff = 1)
  // This tests that the wraparound calculation works correctly
  bitmap.ack(std::numeric_limits<std::uint64_t>::max());
  EXPECT_TRUE(bitmap.is_acked(std::numeric_limits<std::uint64_t>::max()));
}

// Issue #80: Test SACK bitmap with out-of-order packets
TEST(AckBitmapTests, OutOfOrderPacketsPopulateBitmap) {
  mux::AckBitmap bitmap;

  // Sequential packets should build up bitmap
  bitmap.ack(100);
  EXPECT_EQ(bitmap.head(), 100);
  EXPECT_EQ(bitmap.bitmap(), 0x00000000);  // No previous packets

  bitmap.ack(101);
  EXPECT_EQ(bitmap.head(), 101);
  EXPECT_EQ(bitmap.bitmap(), 0x00000001);  // Bit 0 represents seq 100

  bitmap.ack(102);
  EXPECT_EQ(bitmap.head(), 102);
  EXPECT_EQ(bitmap.bitmap(), 0x00000003);  // Bits 0,1 represent seq 101,100

  // Out-of-order: skip 103, receive 104
  bitmap.ack(104);
  EXPECT_EQ(bitmap.head(), 104);
  // After shift by 2: old bits shifted left, new bit set
  // Bit 0: seq 103 (NOT received, should be 0)
  // Bit 1: seq 102 (received, should be 1)
  // Bit 2: seq 101 (received, should be 1)
  // Bit 3: seq 100 (received, should be 1)
  EXPECT_EQ(bitmap.bitmap(), 0x0000000E);  // Binary: 1110

  // Verify individual sequences
  EXPECT_TRUE(bitmap.is_acked(104));   // head
  EXPECT_FALSE(bitmap.is_acked(103));  // gap
  EXPECT_TRUE(bitmap.is_acked(102));
  EXPECT_TRUE(bitmap.is_acked(101));
  EXPECT_TRUE(bitmap.is_acked(100));

  // Fill the gap
  bitmap.ack(103);
  EXPECT_EQ(bitmap.head(), 104);
  EXPECT_EQ(bitmap.bitmap(), 0x0000000F);  // All bits set (no gaps)
  EXPECT_TRUE(bitmap.is_acked(103));
}

// Issue #80: Test SACK bitmap with multiple gaps
TEST(AckBitmapTests, MultipleGapsInBitmap) {
  mux::AckBitmap bitmap;

  // Received: 100, 101, 103, 104, 106
  // Missing: 102, 105
  bitmap.ack(100);
  bitmap.ack(101);
  bitmap.ack(103);
  bitmap.ack(104);
  bitmap.ack(106);

  EXPECT_EQ(bitmap.head(), 106);
  // Bit encoding (per header: Bit N = sequence (head - 1 - N) was received)
  // Bit 0: seq 105 (missing)
  // Bit 1: seq 104 (received)
  // Bit 2: seq 103 (received)
  // Bit 3: seq 102 (missing)
  // Bit 4: seq 101 (received)
  // Bit 5: seq 100 (received)
  // Binary: 110110 = 0x36
  EXPECT_EQ(bitmap.bitmap(), 0x00000036);

  EXPECT_TRUE(bitmap.is_acked(106));
  EXPECT_FALSE(bitmap.is_acked(105));
  EXPECT_TRUE(bitmap.is_acked(104));
  EXPECT_TRUE(bitmap.is_acked(103));
  EXPECT_FALSE(bitmap.is_acked(102));
  EXPECT_TRUE(bitmap.is_acked(101));
  EXPECT_TRUE(bitmap.is_acked(100));
}

}  // namespace veil::tests
