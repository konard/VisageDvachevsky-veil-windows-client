#include <chrono>
#include <iostream>
#include "../src/common/session/replay_window.h"

// Experiment: Demonstrate DoS mitigation for issue #233
// Before fix: Attacker can cause infinite mark->unmark->mark cycles
// After fix: Sequence is blacklisted after MAX_UNMARK_RETRIES attempts

int main() {
    using veil::session::ReplayWindow;

    std::cout << "=============================================================\n";
    std::cout << "Issue #233 DoS Mitigation Experiment\n";
    std::cout << "=============================================================\n\n";

    std::cout << "ATTACK SCENARIO:\n";
    std::cout << "Attacker sends malformed packet with sequence N repeatedly.\n";
    std::cout << "Each iteration: mark(N) -> decrypt fails -> unmark(N) -> repeat\n";
    std::cout << "This causes CPU exhaustion via infinite mark/unmark cycles.\n\n";

    ReplayWindow window(1024);
    const std::uint64_t attack_seq = 1337;

    std::cout << "Simulating DoS attack on sequence " << attack_seq << ":\n";
    std::cout << "-----------------------------------------------\n";

    int successful_cycles = 0;
    bool attack_mitigated = false;

    // Simulate attacker repeatedly sending same malformed packet
    for (int attempt = 1; attempt <= 10; ++attempt) {
        std::cout << "Attempt " << attempt << ": ";

        // Step 1: mark_and_check
        bool marked = window.mark_and_check(attack_seq);
        std::cout << "mark=" << (marked ? "✓" : "✗") << " ";

        if (!marked && attempt == 1) {
            std::cout << " [ERROR: First mark should succeed]\n";
            return 1;
        }

        // Step 2: Simulate decryption failure -> unmark
        bool unmarked = window.unmark(attack_seq);
        std::cout << "unmark=" << (unmarked ? "✓" : "✗");

        if (unmarked) {
            ++successful_cycles;
            std::cout << " → Attack cycle continues\n";
        } else {
            attack_mitigated = true;
            std::cout << " → BLACKLISTED! Attack mitigated.\n";

            // Verify further unmark attempts also fail
            std::cout << "\nVerifying blacklist persists:\n";
            for (int i = 0; i < 3; ++i) {
                bool retry = window.unmark(attack_seq);
                std::cout << "  Unmark retry " << (i+1) << ": "
                          << (retry ? "✓ (BAD)" : "✗ (Good)") << "\n";
                if (retry) {
                    std::cout << "ERROR: Blacklist was bypassed!\n";
                    return 1;
                }
            }
            break;
        }
    }

    std::cout << "\n=============================================================\n";
    std::cout << "RESULTS:\n";
    std::cout << "=============================================================\n";
    std::cout << "Successful attack cycles: " << successful_cycles << "\n";
    std::cout << "Attack mitigated: " << (attack_mitigated ? "YES ✓" : "NO ✗") << "\n";
    std::cout << "Expected cycles before mitigation: 3 (MAX_UNMARK_RETRIES)\n";

    if (attack_mitigated && successful_cycles == 3) {
        std::cout << "\n✓ SUCCESS: DoS attack mitigated after 3 retries.\n";
        std::cout << "  The sequence is now blacklisted and will not consume further CPU.\n";
        return 0;
    } else if (!attack_mitigated) {
        std::cout << "\n✗ FAILURE: Attack was NOT mitigated after 10 attempts.\n";
        std::cout << "  This would allow infinite CPU exhaustion.\n";
        return 1;
    } else {
        std::cout << "\n✗ FAILURE: Mitigation happened but at wrong threshold.\n";
        std::cout << "  Expected exactly 3 successful cycles.\n";
        return 1;
    }
}
