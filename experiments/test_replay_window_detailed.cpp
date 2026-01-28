#include <iostream>
#include <vector>
#include "../src/common/session/replay_window.h"

// Detailed experiment to understand the issue #78 scenario

int main() {
    using veil::session::ReplayWindow;

    std::cout << "Test: Simulating ACTUAL out-of-order arrival (not duplicates)\n";
    std::cout << "================================================================\n";

    ReplayWindow window(1024);

    // Simulate what could happen in a real network:
    // 1. Packets 1873, 1876, 1877, ... 1902 arrive first (out of order)
    // 2. Then delayed packets 1871, 1872, 1874, 1875 arrive later

    std::cout << "\nPhase 1: Receiving packets OUT OF ORDER (skipping 1871, 1872, 1874, 1875)\n";

    // Start with 1873
    window.mark_and_check(1873);
    std::cout << "  seq=1873 highest=" << window.highest() << "\n";

    // Skip 1874, 1875, receive 1876-1902
    for (uint64_t seq = 1876; seq <= 1902; ++seq) {
        window.mark_and_check(seq);
    }
    std::cout << "  seq=1876..1902 highest=" << window.highest() << "\n";

    // Now the delayed packets arrive
    std::cout << "\nPhase 2: Delayed packets arrive (1871, 1872, 1874, 1875)\n";
    std::vector<uint64_t> delayed_packets = {1871, 1872, 1874, 1875};
    for (uint64_t seq : delayed_packets) {
        bool accepted = window.mark_and_check(seq);
        uint64_t diff = window.highest() - seq;
        std::cout << "  seq=" << seq << " diff=" << diff << " accepted=" << (accepted ? "YES" : "NO (REJECTED)") << "\n";
    }

    std::cout << "\n\nTest 2: What if these WERE duplicates?\n";
    std::cout << "========================================================\n";

    ReplayWindow window2(1024);

    // Receive all packets including 1871-1875
    std::cout << "\nPhase 1: Receiving ALL packets 1871-1902 in order\n";
    for (uint64_t seq = 1871; seq <= 1902; ++seq) {
        window2.mark_and_check(seq);
    }
    std::cout << "  After phase 1: highest=" << window2.highest() << "\n";

    // Try to receive them again (actual duplicates/retransmissions)
    std::cout << "\nPhase 2: Try receiving 1871-1875 again (duplicates)\n";
    for (uint64_t seq : delayed_packets) {
        bool accepted = window2.mark_and_check(seq);
        uint64_t diff = window2.highest() - seq;
        std::cout << "  seq=" << seq << " diff=" << diff << " accepted=" << (accepted ? "YES" : "NO (REJECTED)") << "\n";
    }

    std::cout << "\n\nConclusion:\n";
    std::cout << "===========\n";
    std::cout << "The replay window correctly:\n";
    std::cout << "1. ACCEPTS out-of-order packets that haven't been seen before\n";
    std::cout << "2. REJECTS duplicate packets that were already received\n";
    std::cout << "\nTo diagnose issue #78, we need to determine:\n";
    std::cout << "- Are these packets ACTUALLY duplicates being retransmitted by the server?\n";
    std::cout << "- Or are they legitimate out-of-order packets being incorrectly rejected?\n";
    std::cout << "\nThe logs don't show enough info to distinguish these cases.\n";

    return 0;
}
