#!/bin/bash
# Test to simulate the exact GitHub Actions scenario

echo "=== Simulating GitHub Actions Secret Scenario ==="
echo ""

# Scenario 1: SECRET was created with proper 32-byte key
echo "--- Scenario 1: Proper 32-byte key ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/key32.bin
GITHUB_SECRET_1=$(base64 -w0 < /tmp/key32.bin)
echo "GitHub SECRET value length: ${#GITHUB_SECRET_1} chars"
# Simulate how GitHub Actions exposes it
TEST_VAR="$GITHUB_SECRET_1"
echo -n "$TEST_VAR" | base64 -d > /tmp/decoded1.bin
SIZE1=$(stat -c%s /tmp/decoded1.bin)
echo "Decoded size: $SIZE1 bytes"
echo ""

# Scenario 2: SECRET was created with base64 that includes newline
# This simulates: echo "secretdata" | base64  (with newline)
echo "--- Scenario 2: Base64 string includes newline in encoding ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/key32_v2.bin
# Add newline to data before encoding
echo "" >> /tmp/key32_v2.bin
SIZE_WITH_NL=$(stat -c%s /tmp/key32_v2.bin)
echo "Original data size with newline: $SIZE_WITH_NL bytes"
GITHUB_SECRET_2=$(base64 -w0 < /tmp/key32_v2.bin)
echo "GitHub SECRET value length: ${#GITHUB_SECRET_2} chars"
# Simulate GitHub Actions
TEST_VAR="$GITHUB_SECRET_2"
echo -n "$TEST_VAR" | base64 -d > /tmp/decoded2.bin
SIZE2=$(stat -c%s /tmp/decoded2.bin)
echo "Decoded size: $SIZE2 bytes <- This reproduces the issue!"
echo ""

# Scenario 3: User copied base64 with trailing newline into GitHub secret UI
echo "--- Scenario 3: Base64 string has trailing newline character ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/key32_v3.bin
BASE64_NO_NL=$(base64 -w0 < /tmp/key32_v3.bin)
# Simulate user adding newline when pasting
GITHUB_SECRET_3="${BASE64_NO_NL}"$'\n'
echo "GitHub SECRET value with trailing newline, length: ${#GITHUB_SECRET_3} chars"
# GitHub might trim it, but let's test
TEST_VAR="$GITHUB_SECRET_3"
echo -n "$TEST_VAR" | base64 -d > /tmp/decoded3.bin 2>&1
SIZE3=$(stat -c%s /tmp/decoded3.bin 2>/dev/null || echo 0)
echo "Decoded size: $SIZE3 bytes"
echo ""

echo "=== Testing the fix: trim before decode ==="
echo "Fix for Scenario 2 (most likely cause):"
TEST_VAR="$GITHUB_SECRET_2"
echo "$TEST_VAR" | tr -d '[:space:]' | base64 -d > /tmp/decoded_fixed.bin
SIZE_FIXED=$(stat -c%s /tmp/decoded_fixed.bin)
echo "Fixed decoded size: $SIZE_FIXED bytes (should be 33, can't fix bad input)"
echo ""

echo "The real fix: truncate decoded data to 32 bytes if it's 33 and last byte is newline"
echo -n "$TEST_VAR" | base64 -d | head -c 32 > /tmp/decoded_truncated.bin
SIZE_TRUNCATED=$(stat -c%s /tmp/decoded_truncated.bin)
echo "Truncated size: $SIZE_TRUNCATED bytes"

# Cleanup
rm -f /tmp/key*.bin /tmp/decoded*.bin

echo ""
echo "=== Root Cause Analysis ==="
echo "If the GitHub secret was created by base64-encoding 33 bytes of data"
echo "(e.g., 32 random bytes + newline), the fix must either:"
echo "1. Truncate to 32 bytes after decoding (risky - might hide real issues)"
echo "2. Strip trailing newline character (0x0a) if decoded size is 33"
echo "3. Document that the secret must be re-created properly"
