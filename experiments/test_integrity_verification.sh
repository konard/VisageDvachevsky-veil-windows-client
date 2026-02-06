#!/usr/bin/env bash
#
# Test script for installer integrity verification
# This script verifies that the integrity checking mechanism works correctly
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

log_test() {
    echo -e "${YELLOW}[TEST]${NC} $*"
}

# Test counter
tests_passed=0
tests_failed=0

run_test() {
    local test_name="$1"
    local test_command="$2"

    log_test "Running: $test_name"

    if eval "$test_command"; then
        log_success "✓ $test_name"
        ((tests_passed++))
        return 0
    else
        log_error "✗ $test_name"
        ((tests_failed++))
        return 1
    fi
}

# Test 1: Verify sha256sum is available
test_sha256sum_available() {
    command -v sha256sum >/dev/null 2>&1
}

# Test 2: Calculate checksum for install_veil.sh
test_calculate_veil_checksum() {
    local checksum
    checksum=$(sha256sum "$PROJECT_ROOT/install_veil.sh" | awk '{print $1}')
    [[ -n "$checksum" ]] && [[ ${#checksum} -eq 64 ]]
}

# Test 3: Calculate checksum for install_client.sh
test_calculate_client_checksum() {
    local checksum
    checksum=$(sha256sum "$PROJECT_ROOT/install_client.sh" | awk '{print $1}')
    [[ -n "$checksum" ]] && [[ ${#checksum} -eq 64 ]]
}

# Test 4: Verify install_veil.sh has integrity verification function
test_veil_has_verification() {
    grep -q "verify_script_integrity()" "$PROJECT_ROOT/install_veil.sh"
}

# Test 5: Verify install_client.sh has integrity verification function
test_client_has_verification() {
    grep -q "verify_script_integrity()" "$PROJECT_ROOT/install_client.sh"
}

# Test 6: Verify install_veil.sh calls verification in main()
test_veil_calls_verification() {
    grep -A 10 "^main()" "$PROJECT_ROOT/install_veil.sh" | grep -q "verify_script_integrity"
}

# Test 7: Verify install_client.sh calls verification in main()
test_client_calls_verification() {
    grep -A 10 "^main()" "$PROJECT_ROOT/install_client.sh" | grep -q "verify_script_integrity"
}

# Test 8: Test that SKIP_INTEGRITY_CHECK variable is respected
test_skip_integrity_variable() {
    grep -q 'SKIP_INTEGRITY_CHECK="${SKIP_INTEGRITY_CHECK:-false}"' "$PROJECT_ROOT/install_veil.sh"
}

# Test 9: Verify security warnings exist in install_veil.sh
test_veil_has_security_warnings() {
    grep -q "SECURITY WARNING" "$PROJECT_ROOT/install_veil.sh" && \
    grep -q "curl | bash" "$PROJECT_ROOT/install_veil.sh"
}

# Test 10: Verify security warnings exist in install_client.sh
test_client_has_security_warnings() {
    grep -q "SECURITY WARNING" "$PROJECT_ROOT/install_client.sh" && \
    grep -q "curl | bash" "$PROJECT_ROOT/install_client.sh"
}

# Test 11: Verify EXPECTED_SHA256 variable exists in both scripts
test_expected_sha256_variable() {
    grep -q 'EXPECTED_SHA256=""' "$PROJECT_ROOT/install_veil.sh" && \
    grep -q 'EXPECTED_SHA256=""' "$PROJECT_ROOT/install_client.sh"
}

# Test 12: Verify README has secure installation instructions
test_readme_has_secure_instructions() {
    grep -q "sha256sum -c" "$PROJECT_ROOT/README.md" && \
    grep -q "NOT RECOMMENDED" "$PROJECT_ROOT/README.md"
}

# Main test execution
main() {
    echo ""
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║         Installer Integrity Verification Test Suite           ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""

    log_info "Project root: $PROJECT_ROOT"
    echo ""

    # Run all tests
    run_test "SHA256 command available" "test_sha256sum_available"
    run_test "Calculate install_veil.sh checksum" "test_calculate_veil_checksum"
    run_test "Calculate install_client.sh checksum" "test_calculate_client_checksum"
    run_test "install_veil.sh has verify_script_integrity()" "test_veil_has_verification"
    run_test "install_client.sh has verify_script_integrity()" "test_client_has_verification"
    run_test "install_veil.sh calls verify_script_integrity()" "test_veil_calls_verification"
    run_test "install_client.sh calls verify_script_integrity()" "test_client_calls_verification"
    run_test "SKIP_INTEGRITY_CHECK variable exists" "test_skip_integrity_variable"
    run_test "install_veil.sh has security warnings" "test_veil_has_security_warnings"
    run_test "install_client.sh has security warnings" "test_client_has_security_warnings"
    run_test "EXPECTED_SHA256 variable exists" "test_expected_sha256_variable"
    run_test "README has secure installation instructions" "test_readme_has_secure_instructions"

    # Summary
    echo ""
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                        Test Summary                            ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""

    local total_tests=$((tests_passed + tests_failed))
    echo "Total tests: $total_tests"
    echo -e "${GREEN}Passed: $tests_passed${NC}"

    if [[ $tests_failed -gt 0 ]]; then
        echo -e "${RED}Failed: $tests_failed${NC}"
        echo ""
        log_error "Some tests failed!"
        exit 1
    else
        echo -e "${RED}Failed: $tests_failed${NC}"
        echo ""
        log_success "All tests passed!"

        # Display checksums
        echo ""
        echo "╔════════════════════════════════════════════════════════════════╗"
        echo "║                   Generated Checksums                          ║"
        echo "╚════════════════════════════════════════════════════════════════╝"
        echo ""
        echo "install_veil.sh:"
        sha256sum "$PROJECT_ROOT/install_veil.sh"
        echo ""
        echo "install_client.sh:"
        sha256sum "$PROJECT_ROOT/install_client.sh"
        echo ""

        exit 0
    fi
}

main "$@"
