#!/usr/bin/env bash
#
# Test script for generate_client_config.sh
# Verifies that the .veil config file is generated correctly.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GENERATE_SCRIPT="$REPO_DIR/scripts/generate_client_config.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "${GREEN}PASS${NC}: $1"
}

fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "${RED}FAIL${NC}: $1"
}

run_test() {
    TESTS_RUN=$((TESTS_RUN + 1))
}

# Setup test environment
TEST_DIR=$(mktemp -d)
TEST_CONFIG_DIR="$TEST_DIR/veil"
mkdir -p "$TEST_CONFIG_DIR"

# Generate fake 32-byte keys
head -c 32 /dev/urandom > "$TEST_CONFIG_DIR/server.key"
head -c 32 /dev/urandom > "$TEST_CONFIG_DIR/obfuscation.seed"

# Create a minimal server.conf
cat > "$TEST_CONFIG_DIR/server.conf" <<EOF
[server]
listen_address = 0.0.0.0
listen_port = 4433
EOF

cleanup() {
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

echo "Running generate_client_config.sh tests..."
echo ""

# Test 1: Help flag
run_test
if bash "$GENERATE_SCRIPT" --help > /dev/null 2>&1; then
    pass "Help flag works"
else
    fail "Help flag failed"
fi

# Test 2: Generate config with explicit server address
run_test
OUTPUT="$TEST_DIR/test1.veil"
if bash "$GENERATE_SCRIPT" -c "$TEST_CONFIG_DIR" -s "test.example.com" -o "$OUTPUT" > /dev/null 2>&1; then
    if [[ -f "$OUTPUT" ]]; then
        pass "Config file generated"
    else
        fail "Config file not created"
    fi
else
    fail "Script exited with error"
fi

# Test 3: Generated config is valid JSON
run_test
if python3 -c "import json; json.load(open('$OUTPUT'))" 2>/dev/null; then
    pass "Generated config is valid JSON"
elif command -v jq >/dev/null 2>&1 && jq . "$OUTPUT" > /dev/null 2>&1; then
    pass "Generated config is valid JSON"
else
    fail "Generated config is not valid JSON"
fi

# Test 4: Config contains required fields
run_test
if python3 -c "
import json
with open('$OUTPUT') as f:
    cfg = json.load(f)
assert 'server' in cfg
assert 'address' in cfg['server']
assert 'port' in cfg['server']
assert 'crypto' in cfg
assert 'presharedKey' in cfg['crypto']
assert 'obfuscationSeed' in cfg['crypto']
print('OK')
" 2>/dev/null; then
    pass "Config contains all required fields"
else
    fail "Config missing required fields"
fi

# Test 5: Server address matches specified value
run_test
if python3 -c "
import json
with open('$OUTPUT') as f:
    cfg = json.load(f)
assert cfg['server']['address'] == 'test.example.com'
print('OK')
" 2>/dev/null; then
    pass "Server address matches"
else
    fail "Server address mismatch"
fi

# Test 6: Port from config file is used
run_test
if python3 -c "
import json
with open('$OUTPUT') as f:
    cfg = json.load(f)
assert cfg['server']['port'] == 4433
print('OK')
" 2>/dev/null; then
    pass "Port matches server.conf"
else
    fail "Port mismatch"
fi

# Test 7: Custom port override
run_test
OUTPUT2="$TEST_DIR/test2.veil"
bash "$GENERATE_SCRIPT" -c "$TEST_CONFIG_DIR" -s "test.example.com" -p 5555 -o "$OUTPUT2" > /dev/null 2>&1
if python3 -c "
import json
with open('$OUTPUT2') as f:
    cfg = json.load(f)
assert cfg['server']['port'] == 5555
print('OK')
" 2>/dev/null; then
    pass "Custom port override works"
else
    fail "Custom port override failed"
fi

# Test 8: Base64 key decodes to 32 bytes
run_test
if python3 -c "
import json, base64
with open('$OUTPUT') as f:
    cfg = json.load(f)
key_bytes = base64.b64decode(cfg['crypto']['presharedKey'])
seed_bytes = base64.b64decode(cfg['crypto']['obfuscationSeed'])
assert len(key_bytes) == 32, f'key is {len(key_bytes)} bytes'
assert len(seed_bytes) == 32, f'seed is {len(seed_bytes)} bytes'
print('OK')
" 2>/dev/null; then
    pass "Embedded keys decode to 32 bytes"
else
    fail "Embedded keys have wrong size"
fi

# Test 9: Missing key file should fail
run_test
EMPTY_DIR="$TEST_DIR/empty"
mkdir -p "$EMPTY_DIR"
if bash "$GENERATE_SCRIPT" -c "$EMPTY_DIR" -s "test.example.com" -o "$TEST_DIR/fail.veil" > /dev/null 2>&1; then
    fail "Should fail when key files are missing"
else
    pass "Correctly fails when key files are missing"
fi

# Test 10: File permissions are restrictive
run_test
PERMS=$(stat -c "%a" "$OUTPUT" 2>/dev/null || stat -f "%Lp" "$OUTPUT" 2>/dev/null)
if [[ "$PERMS" == "600" ]]; then
    pass "Config file has 600 permissions"
else
    fail "Config file has $PERMS permissions (expected 600)"
fi

echo ""
echo "================================"
echo "Tests run: $TESTS_RUN"
echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
echo "================================"

if [[ "$TESTS_FAILED" -gt 0 ]]; then
    exit 1
fi
