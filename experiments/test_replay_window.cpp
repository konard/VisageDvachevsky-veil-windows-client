#include <iostream>
#include <vector>
#include "../src/common/session/replay_window.h"

// Experiment to understand replay window behavior with out-of-order packets

int main() {
    using veil::session::ReplayWindow;

    // Test scenario from issue #78
    // Window size 1024, simulating packets arriving out of order
    ReplayWindow window(1024);

    std::cout << "Test 1: Simulating the exact scenario from issue #78\n";
    std::cout << "========================================================\n";

    // First, receive packets 1871-1902 to advance the highest to 1902
    std::cout << "\nPhase 1: Receiving packets to advance highest to 1902\n";
    for (uint64_t seq = 1871; seq <= 1902; ++seq) {
        bool accepted = window.mark_and_check(seq);
        if (!accepted) {
            std::cout << "  REJECTED: seq=" << seq << " (highest=" << window.highest() << ")\n";
        }
    }
    std::cout << "  After phase 1: highest=" << window.highest() << "\n";

    // Now try to receive packets 1871-1875 again (as if they were retransmitted/delayed)
    std::cout << "\nPhase 2: Try receiving delayed packets 1871-1875\n";
    std::vector<uint64_t> delayed_packets = {1871, 1872, 1874, 1875};
    for (uint64_t seq : delayed_packets) {
        bool accepted = window.mark_and_check(seq);
        uint64_t diff = window.highest() - seq;
        std::cout << "  seq=" << seq << " diff=" << diff << " accepted=" << (accepted ? "YES" : "NO (REJECTED)") << "\n";
    }

    std::cout << "\n\nTest 2: Receiving packets out-of-order (not duplicates)\n";
    std::cout << "========================================================\n";

    ReplayWindow window2(1024);

    // Simulate receiving packets: 100, 105, 110, then 102, 103, 104
    std::cout << "\nReceiving: 100, 105, 110\n";
    window2.mark_and_check(100);
    std::cout << "  seq=100 highest=" << window2.highest() << "\n";
    window2.mark_and_check(105);
    std::cout << "  seq=105 highest=" << window2.highest() << "\n";
    window2.mark_and_check(110);
    std::cout << "  seq=110 highest=" << window2.highest() << "\n";

    std::cout << "\nNow receiving delayed packets: 102, 103, 104\n";
    for (uint64_t seq : {102, 103, 104}) {
        bool accepted = window2.mark_and_check(seq);
        uint64_t diff = window2.highest() - seq;
        std::cout << "  seq=" << seq << " diff=" << diff << " accepted=" << (accepted ? "YES" : "NO") << "\n";
    }

    // Try duplicates
    std::cout << "\nNow trying duplicates: 102, 103, 104\n";
    for (uint64_t seq : {102, 103, 104}) {
        bool accepted = window2.mark_and_check(seq);
        std::cout << "  seq=" << seq << " accepted=" << (accepted ? "YES" : "NO (DUPLICATE)") << "\n";
    }

    std::cout << "\n\nTest 3: Edge case - packet exactly at window boundary\n";
    std::cout << "========================================================\n";

    ReplayWindow window3(64);
    window3.mark_and_check(100);
    std::cout << "  seq=100 highest=" << window3.highest() << "\n";

    // Packet 36 is 64 behind (exactly at window boundary)
    bool accepted = window3.mark_and_check(36);
    std::cout << "  seq=36 diff=64 (exactly window size) accepted=" << (accepted ? "YES" : "NO") << "\n";

    // Packet 37 is 63 behind (just inside window)
    accepted = window3.mark_and_check(37);
    std::cout << "  seq=37 diff=63 (just inside window) accepted=" << (accepted ? "YES" : "NO") << "\n";

    return 0;
}
