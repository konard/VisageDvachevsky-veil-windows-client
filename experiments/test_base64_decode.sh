#!/bin/bash

# Test script to verify the fix for base64 decoding issue

echo "Testing base64 decoding with and without newline..."

# Generate a 32-byte key and encode it
TEST_KEY=$(dd if=/dev/urandom bs=32 count=1 2>/dev/null | base64 -w0)

echo "Generated test key (base64): $TEST_KEY"

# Test with echo (old method - adds newline)
echo "--- Testing with 'echo' (old method) ---"
echo "$TEST_KEY" | base64 -d > /tmp/test_key_with_newline.bin
SIZE_WITH=$(stat -c%s /tmp/test_key_with_newline.bin 2>/dev/null || echo 0)
echo "Key size with 'echo': $SIZE_WITH bytes (expected: 32, got extra byte from newline)"

# Test with echo -n (new method - no newline)
echo "--- Testing with 'echo -n' (new method) ---"
echo -n "$TEST_KEY" | base64 -d > /tmp/test_key_without_newline.bin
SIZE_WITHOUT=$(stat -c%s /tmp/test_key_without_newline.bin 2>/dev/null || echo 0)
echo "Key size with 'echo -n': $SIZE_WITHOUT bytes (expected: 32)"

# Cleanup
rm -f /tmp/test_key_*.bin

if [[ "$SIZE_WITHOUT" == "32" ]]; then
    echo "✓ Fix verified: 'echo -n' produces correct 32-byte key"
    exit 0
else
    echo "✗ Fix failed: 'echo -n' did not produce 32-byte key"
    exit 1
fi
