// Benchmark for RetransmitBuffer performance (Issue #96)
// This benchmark compares O(1) unordered_map vs O(log n) map operations.
//
// Build: g++ -std=c++20 -O2 -o retransmit_buffer_benchmark retransmit_buffer_benchmark.cpp
// Run: ./retransmit_buffer_benchmark

#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

// Simulated PendingPacket (simplified for benchmark)
struct PendingPacket {
  std::uint64_t sequence{0};
  std::vector<std::uint8_t> data;
  std::chrono::steady_clock::time_point first_sent;
  std::uint32_t retry_count{0};
};

// Benchmark parameters
constexpr std::size_t kNumOperations = 100000;
constexpr std::size_t kPacketSize = 1400;  // Typical MTU
constexpr std::size_t kBufferSize = 10000; // Max pending packets

template <typename MapType>
void benchmark_operations(const std::string& name) {
  MapType pending;
  std::mt19937_64 rng(42);  // Fixed seed for reproducibility
  std::vector<std::uint8_t> data(kPacketSize, 0x42);

  // Pre-populate buffer to simulate steady-state
  for (std::uint64_t seq = 0; seq < kBufferSize; ++seq) {
    PendingPacket pkt{
        .sequence = seq,
        .data = data,
        .first_sent = std::chrono::steady_clock::now(),
        .retry_count = 0,
    };
    pending.emplace(seq, std::move(pkt));
  }

  std::uint64_t next_seq = kBufferSize;
  std::uint64_t ack_seq = 0;

  // Benchmark: insert + find + erase (simulating normal operation)
  auto start = std::chrono::high_resolution_clock::now();

  for (std::size_t i = 0; i < kNumOperations; ++i) {
    // Insert new packet
    PendingPacket pkt{
        .sequence = next_seq,
        .data = data,
        .first_sent = std::chrono::steady_clock::now(),
        .retry_count = 0,
    };
    pending.emplace(next_seq, std::move(pkt));
    ++next_seq;

    // Acknowledge old packet (find + erase)
    auto it = pending.find(ack_seq);
    if (it != pending.end()) {
      pending.erase(it);
    }
    ++ack_seq;

    // Occasional random access (simulating retransmit lookup)
    if (i % 10 == 0) {
      std::uint64_t random_seq = ack_seq + (rng() % (next_seq - ack_seq));
      [[maybe_unused]] auto random_it = pending.find(random_seq);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << name << ":\n";
  std::cout << "  Total time: " << duration.count() << " us\n";
  std::cout << "  Per operation: " << (duration.count() * 1000.0 / kNumOperations) << " ns\n";
  std::cout << "  Operations/sec: " << (kNumOperations * 1000000.0 / duration.count()) << "\n";
  std::cout << "\n";
}

int main() {
  std::cout << "RetransmitBuffer Performance Benchmark (Issue #96)\n";
  std::cout << "================================================\n";
  std::cout << "Parameters:\n";
  std::cout << "  Operations: " << kNumOperations << "\n";
  std::cout << "  Packet size: " << kPacketSize << " bytes\n";
  std::cout << "  Buffer size: " << kBufferSize << " packets\n";
  std::cout << "\n";

  // Warm-up run
  benchmark_operations<std::unordered_map<std::uint64_t, PendingPacket>>("Warm-up (unordered_map)");

  // Actual benchmarks
  benchmark_operations<std::map<std::uint64_t, PendingPacket>>("std::map (O(log n))");
  benchmark_operations<std::unordered_map<std::uint64_t, PendingPacket>>("std::unordered_map (O(1))");

  // Run again for consistency
  benchmark_operations<std::map<std::uint64_t, PendingPacket>>("std::map (O(log n)) - Run 2");
  benchmark_operations<std::unordered_map<std::uint64_t, PendingPacket>>("std::unordered_map (O(1)) - Run 2");

  return 0;
}
