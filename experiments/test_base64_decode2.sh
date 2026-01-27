#!/bin/bash
# Test base64 decoding with various edge cases

echo "=== Testing base64 decoding edge cases ==="
echo ""

# Create a proper 32-byte key
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/test_key.bin
ACTUAL_SIZE=$(stat -c%s /tmp/test_key.bin)
echo "Generated test key size: $ACTUAL_SIZE bytes"

# Encode it to base64
BASE64_KEY=$(base64 -w0 < /tmp/test_key.bin)
echo "Base64 encoded key length: ${#BASE64_KEY} characters"
echo ""

# Test what happens if the secret has trailing whitespace or padding issues
echo "--- Test 1: Secret with trailing space ---"
TEST_SECRET="${BASE64_KEY} "
echo "Secret length: ${#TEST_SECRET} chars"
echo -n "$TEST_SECRET" | base64 -d > /tmp/test1.key 2>&1
SIZE1=$(stat -c%s /tmp/test1.key 2>/dev/null || echo 0)
echo "Result size: $SIZE1 bytes"
echo ""

echo "--- Test 2: Secret with trailing newline (decoded as-is) ---"
TEST_SECRET="${BASE64_KEY}"$'\n'
echo "Secret length: ${#TEST_SECRET} chars"
# The newline itself becomes part of the base64 string to decode
echo "$TEST_SECRET" | base64 -d > /tmp/test2.key 2>&1
SIZE2=$(stat -c%s /tmp/test2.key 2>/dev/null || echo 0)
echo "Result size: $SIZE2 bytes"
echo ""

echo "--- Test 3: What if secret ends with 'Cg==' (newline character encoded) ---"
# Create 31 bytes + newline (0x0a)
dd if=/dev/urandom bs=31 count=1 2>/dev/null > /tmp/test_key_31.bin
echo -n -e '\x0a' >> /tmp/test_key_31.bin
SIZE_BEFORE=$(stat -c%s /tmp/test_key_31.bin)
echo "Created test file with size: $SIZE_BEFORE bytes"
BASE64_KEY_WITH_NL=$(base64 -w0 < /tmp/test_key_31.bin)
echo "Base64 key: ${BASE64_KEY_WITH_NL:(-10)}"
echo -n "$BASE64_KEY_WITH_NL" | base64 -d > /tmp/test3.key
SIZE3=$(stat -c%s /tmp/test3.key)
echo "Decoded size: $SIZE3 bytes"
echo ""

echo "--- Test 4: 32 bytes + newline character (33 bytes total) ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/test_key_32.bin
echo -n -e '\x0a' >> /tmp/test_key_32.bin
SIZE_BEFORE=$(stat -c%s /tmp/test_key_32.bin)
echo "Created test file with size: $SIZE_BEFORE bytes"
BASE64_KEY_33=$(base64 -w0 < /tmp/test_key_32.bin)
echo "Base64 encoding of 33-byte data"
echo -n "$BASE64_KEY_33" | base64 -d > /tmp/test4.key
SIZE4=$(stat -c%s /tmp/test4.key)
echo "Decoded size: $SIZE4 bytes <- This is the actual problem!"
echo ""

# Cleanup
rm -f /tmp/test*.key /tmp/test_key*.bin

echo "=== Summary ==="
echo "The issue occurs when the CLIENT_KEY secret contains base64-encoded data"
echo "that represents 33 bytes (32 bytes + newline) instead of just 32 bytes."
echo "This means the secret itself was created incorrectly on GitHub."
