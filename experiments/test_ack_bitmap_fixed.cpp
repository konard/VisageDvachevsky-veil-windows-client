#include <iostream>
#include <iomanip>
#include <cstdint>

// Fixed version of AckBitmap
class AckBitmap {
 public:
  AckBitmap() = default;

  void ack(std::uint64_t seq) {
    if (!initialized_) {
      head_ = seq;
      bitmap_ = 0;
      initialized_ = true;
      return;
    }
    // Use wraparound-aware comparison instead of direct comparison
    if (seq_less_than(head_, seq)) {  // seq > head_ (wraparound-aware)
      const auto shift = seq - head_;
      if (shift >= 32) {
        bitmap_ = 0;
      } else {
        // FIX: Set bit for current head before shifting
        bitmap_ = (bitmap_ << shift) | (1U << (shift - 1));
      }
      head_ = seq;
      return;
    }
    const auto diff = head_ - seq;
    if (diff == 0) {
      return;
    }
    if (diff > 32) {
      return;
    }
    bitmap_ |= (1U << (diff - 1));
  }

  bool is_acked(std::uint64_t seq) const {
    if (!initialized_) {
      return false;
    }
    if (seq == head_) {
      return true;
    }
    // Use wraparound-aware comparison instead of direct comparison
    if (seq_less_than(head_, seq)) {  // seq > head_ (wraparound-aware)
      return false;
    }
    const auto diff = head_ - seq;
    if (diff == 0) return true;
    if (diff > 32) return false;
    return ((bitmap_ >> (diff - 1)) & 1U) != 0U;
  }

  std::uint64_t head() const { return head_; }
  std::uint32_t bitmap() const { return bitmap_; }

 private:
  static bool seq_less_than(std::uint64_t seq1, std::uint64_t seq2) {
    return static_cast<std::int64_t>(seq1 - seq2) < 0;
  }

  std::uint64_t head_{0};
  std::uint32_t bitmap_{0};
  bool initialized_{false};
};

void print_bitmap_state(const AckBitmap& bm, const char* label) {
  std::cout << label << ": head=" << bm.head()
            << ", bitmap=0x" << std::hex << std::setw(8) << std::setfill('0')
            << bm.bitmap() << std::dec << std::endl;
}

void print_bitmap_bits(std::uint32_t bitmap) {
  std::cout << "  Bitmap bits (LSB to MSB): ";
  for (int i = 0; i < 32; ++i) {
    if ((bitmap >> i) & 1) {
      std::cout << i << " ";
    }
  }
  std::cout << std::endl;
}

int main() {
  std::cout << "=== Testing FIXED AckBitmap behavior ===" << std::endl << std::endl;

  // Test 1: Sequential packets (all in order)
  {
    std::cout << "Test 1: Sequential packets (100, 101, 102, 103, 104)" << std::endl;
    AckBitmap bm;
    bm.ack(100);
    print_bitmap_state(bm, "After ack(100)");
    bm.ack(101);
    print_bitmap_state(bm, "After ack(101)");
    bm.ack(102);
    print_bitmap_state(bm, "After ack(102)");
    bm.ack(103);
    print_bitmap_state(bm, "After ack(103)");
    bm.ack(104);
    print_bitmap_state(bm, "After ack(104)");
    std::cout << "Expected: bitmap should have all bits set showing contiguous sequence\n" << std::endl;
  }

  // Test 2: Out-of-order packets (100, 101, 103, 104, 102)
  {
    std::cout << "Test 2: Out-of-order - received 100, 101, 103, 104, then 102" << std::endl;
    AckBitmap bm;
    bm.ack(100);
    print_bitmap_state(bm, "After ack(100)");
    bm.ack(101);
    print_bitmap_state(bm, "After ack(101)");
    bm.ack(103);
    print_bitmap_state(bm, "After ack(103)");
    print_bitmap_bits(bm.bitmap());
    bm.ack(104);
    print_bitmap_state(bm, "After ack(104)");
    print_bitmap_bits(bm.bitmap());
    bm.ack(102);
    print_bitmap_state(bm, "After ack(102)");
    print_bitmap_bits(bm.bitmap());
    std::cout << std::endl;
  }

  // Test 3: Gap scenario from issue (received 100, 101, 103, 104, 106)
  {
    std::cout << "Test 3: Gap scenario - received 100, 101, 103, 104, 106" << std::endl;
    AckBitmap bm;
    bm.ack(100);
    print_bitmap_state(bm, "After ack(100)");
    bm.ack(101);
    print_bitmap_state(bm, "After ack(101)");
    bm.ack(103);
    print_bitmap_state(bm, "After ack(103)");
    print_bitmap_bits(bm.bitmap());
    bm.ack(104);
    print_bitmap_state(bm, "After ack(104)");
    print_bitmap_bits(bm.bitmap());
    bm.ack(106);
    print_bitmap_state(bm, "After ack(106)");
    print_bitmap_bits(bm.bitmap());

    std::cout << "Expected: After ack(106), bitmap should show bits for 104 and 103" << std::endl;
    std::cout << "  head=106, so bitmap bit 0 = seq 105 (missing)" << std::endl;
    std::cout << "             bitmap bit 1 = seq 104 (received, should be set)" << std::endl;
    std::cout << "             bitmap bit 2 = seq 103 (received, should be set)" << std::endl;
    std::cout << "             bitmap bit 3 = seq 102 (missing)" << std::endl;
    std::cout << "  So bitmap should be 0x00000006 (bits 1 and 2 set)" << std::endl;
    std::cout << std::endl;
  }

  // Test 4: Understanding bitmap encoding from header comment
  {
    std::cout << "Test 4: Verifying bitmap bit encoding per header" << std::endl;
    std::cout << "Header says: Bit N = sequence (head - 1 - N) was received" << std::endl;
    AckBitmap bm;
    bm.ack(100);
    bm.ack(102);
    print_bitmap_state(bm, "After ack(100), ack(102)");
    print_bitmap_bits(bm.bitmap());
    std::cout << "  head=102, so:" << std::endl;
    std::cout << "    bit 0 = seq (102 - 1 - 0) = 101 (missing)" << std::endl;
    std::cout << "    bit 1 = seq (102 - 1 - 1) = 100 (received, should be set)" << std::endl;
    std::cout << "  Expected bitmap: 0x00000002" << std::endl;
    std::cout << std::endl;
  }

  // Test 5: Verify is_acked works correctly
  {
    std::cout << "Test 5: Verify is_acked works correctly" << std::endl;
    AckBitmap bm;
    bm.ack(100);
    bm.ack(101);
    bm.ack(103);
    bm.ack(106);
    print_bitmap_state(bm, "After ack(100,101,103,106)");
    print_bitmap_bits(bm.bitmap());

    std::cout << "  is_acked(106) = " << bm.is_acked(106) << " (should be 1)" << std::endl;
    std::cout << "  is_acked(105) = " << bm.is_acked(105) << " (should be 0)" << std::endl;
    std::cout << "  is_acked(104) = " << bm.is_acked(104) << " (should be 0)" << std::endl;
    std::cout << "  is_acked(103) = " << bm.is_acked(103) << " (should be 1)" << std::endl;
    std::cout << "  is_acked(102) = " << bm.is_acked(102) << " (should be 0)" << std::endl;
    std::cout << "  is_acked(101) = " << bm.is_acked(101) << " (should be 1)" << std::endl;
    std::cout << "  is_acked(100) = " << bm.is_acked(100) << " (should be 1)" << std::endl;
    std::cout << std::endl;
  }

  return 0;
}
