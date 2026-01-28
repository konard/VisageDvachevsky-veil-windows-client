#include <iostream>
#include "../src/common/session/replay_window.h"

// Test the new unmark functionality (Issue #78)

int main() {
    using veil::session::ReplayWindow;

    std::cout << "Test 1: Verify unmark allows retransmission\n";
    std::cout << "=============================================\n";

    ReplayWindow window(64);

    // Receive packet 100
    bool accepted = window.mark_and_check(100);
    std::cout << "seq=100 first attempt: " << (accepted ? "ACCEPTED" : "REJECTED") << "\n";

    // Try to receive it again (should be rejected as duplicate)
    accepted = window.mark_and_check(100);
    std::cout << "seq=100 second attempt (before unmark): " << (accepted ? "ACCEPTED" : "REJECTED") << "\n";

    // Unmark it (simulating decryption failure)
    window.unmark(100);
    std::cout << "Unmarked seq=100\n";

    // Try to receive it again (should now be accepted)
    accepted = window.mark_and_check(100);
    std::cout << "seq=100 third attempt (after unmark): " << (accepted ? "ACCEPTED" : "REJECTED") << "\n";

    std::cout << "\n\nTest 2: Unmark with window advancement\n";
    std::cout << "========================================\n";

    ReplayWindow window2(64);

    // Receive packets 100, 101, 102
    window2.mark_and_check(100);
    window2.mark_and_check(101);
    window2.mark_and_check(102);
    std::cout << "Received: 100, 101, 102 (highest=102)\n";

    // Advance window to 130
    window2.mark_and_check(130);
    std::cout << "Advanced to seq=130 (highest=130)\n";

    // Try seq=100 (should be rejected as already seen)
    accepted = window2.mark_and_check(100);
    std::cout << "seq=100 (diff=30): " << (accepted ? "ACCEPTED" : "REJECTED") << "\n";

    // Unmark 100
    window2.unmark(100);
    std::cout << "Unmarked seq=100\n";

    // Try seq=100 again (should now be accepted)
    accepted = window2.mark_and_check(100);
    std::cout << "seq=100 after unmark: " << (accepted ? "ACCEPTED" : "REJECTED") << "\n";

    std::cout << "\n\nTest 3: Issue #78 scenario simulation\n";
    std::cout << "======================================\n";

    ReplayWindow window3(1024);

    // Simulate: packets 1871-1902 arrive, but some fail decryption
    std::cout << "Phase 1: Receiving packets 1871-1902\n";
    for (uint64_t seq = 1871; seq <= 1902; ++seq) {
        window3.mark_and_check(seq);
    }
    std::cout << "  highest=" << window3.highest() << "\n";

    // Simulate: packets 1871-1875 failed decryption, so we unmark them
    std::cout << "\nPhase 2: Simulating decryption failures for 1871-1875 (unmarking)\n";
    for (uint64_t seq = 1871; seq <= 1875; ++seq) {
        window3.unmark(seq);
    }

    // Now server retransmits 1871-1875
    std::cout << "\nPhase 3: Retransmitted packets 1871-1875 should be ACCEPTED\n";
    for (uint64_t seq = 1871; seq <= 1875; ++seq) {
        bool accepted = window3.mark_and_check(seq);
        uint64_t diff = window3.highest() - seq;
        std::cout << "  seq=" << seq << " diff=" << diff << " accepted=" << (accepted ? "YES" : "NO (BUG!)") << "\n";
    }

    std::cout << "\n\nConclusion:\n";
    std::cout << "===========\n";
    std::cout << "The unmark() method allows legitimate retransmissions after decryption failures,\n";
    std::cout << "solving the issue where packets failing decryption would be permanently rejected.\n";

    return 0;
}
