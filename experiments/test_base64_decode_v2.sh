#!/bin/bash

# Test script to verify the fix - testing with literal newline in base64

echo "Testing base64 decoding when secret contains trailing newline..."

# Generate a 32-byte key and encode it
TEST_KEY_RAW=$(dd if=/dev/urandom bs=32 count=1 2>/dev/null)
TEST_KEY_CLEAN=$(echo -n "$TEST_KEY_RAW" | base64 -w0)
# Simulate what might happen if secret is stored with trailing newline
TEST_KEY_WITH_NEWLINE="${TEST_KEY_CLEAN}
"

echo "Clean base64 key length: ${#TEST_KEY_CLEAN}"
echo "Key with newline length: ${#TEST_KEY_WITH_NEWLINE}"

# Test with echo (old method)
echo "--- Testing with 'echo' when secret has trailing newline ---"
echo "$TEST_KEY_WITH_NEWLINE" | base64 -d > /tmp/test_key_echo.bin 2>&1
SIZE_ECHO=$(stat -c%s /tmp/test_key_echo.bin 2>/dev/null || echo 0)
echo "Decoded size with 'echo': $SIZE_ECHO bytes"

# Test with echo -n (new method)
echo "--- Testing with 'echo -n' when secret has trailing newline ---"
echo -n "$TEST_KEY_WITH_NEWLINE" | base64 -d > /tmp/test_key_echo_n.bin 2>&1
SIZE_ECHO_N=$(stat -c%s /tmp/test_key_echo_n.bin 2>/dev/null || echo 0)
echo "Decoded size with 'echo -n': $SIZE_ECHO_N bytes"

# Also test if the GitHub secret itself might have embedded newline in base64
echo "--- Simulating GitHub secret with newline encoded in base64 ---"
# This would happen if someone did: echo "rawkey" | base64 (which encodes the newline too)
TEST_KEY_NEWLINE_ENCODED=$(echo "$TEST_KEY_RAW" | base64 -w0)  # This includes newline in encoding
echo -n "$TEST_KEY_NEWLINE_ENCODED" | base64 -d > /tmp/test_key_newline_encoded.bin 2>&1
SIZE_NEWLINE_ENCODED=$(stat -c%s /tmp/test_key_newline_encoded.bin 2>/dev/null || echo 0)
echo "Decoded size (newline was encoded in base64): $SIZE_NEWLINE_ENCODED bytes"

# Cleanup
rm -f /tmp/test_key_*.bin

echo ""
echo "Summary:"
echo "- If secret has trailing newline AND we use 'echo': $SIZE_ECHO bytes"
echo "- If secret has trailing newline AND we use 'echo -n': $SIZE_ECHO_N bytes"
echo "- If newline was encoded into the base64: $SIZE_NEWLINE_ENCODED bytes"
