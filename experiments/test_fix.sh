#!/bin/bash
# Test the fix for the 33-byte key issue

echo "=== Testing the CI workflow fix ==="
echo ""

# Create test scenario: 32-byte key + newline (33 bytes total)
echo "--- Creating 33-byte test data (32 bytes + newline) ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/test_key_32.bin
echo -n -e '\x0a' >> /tmp/test_key_32.bin
SIZE_ORIGINAL=$(stat -c%s /tmp/test_key_32.bin)
echo "Original size: $SIZE_ORIGINAL bytes"

# Encode to base64 (simulating GitHub secret)
VEIL_CLIENT_KEY=$(base64 -w0 < /tmp/test_key_32.bin)
echo "Base64 encoded"
echo ""

# Simulate the CI workflow fix
echo "--- Simulating CI workflow steps ---"
mkdir -p /tmp/veil-test

# Step 1: Decode
echo -n "$VEIL_CLIENT_KEY" | base64 -d > /tmp/veil-test/client.key.tmp

# Step 2: Check size
KEY_SIZE=$(stat -c%s /tmp/veil-test/client.key.tmp 2>/dev/null || echo 0)
echo "Decoded key size: $KEY_SIZE bytes"

# Step 3: Apply fix
if [[ "$KEY_SIZE" == "33" ]]; then
  # Check if last byte is newline (0x0a)
  LAST_BYTE=$(tail -c 1 /tmp/veil-test/client.key.tmp | od -An -tx1 | tr -d ' ')
  echo "Last byte: 0x$LAST_BYTE"
  if [[ "$LAST_BYTE" == "0a" ]]; then
    echo "Stripping trailing newline..."
    head -c 32 /tmp/veil-test/client.key.tmp > /tmp/veil-test/client.key
    rm /tmp/veil-test/client.key.tmp
    KEY_SIZE=32
  else
    mv /tmp/veil-test/client.key.tmp /tmp/veil-test/client.key
  fi
elif [[ "$KEY_SIZE" == "32" ]]; then
  mv /tmp/veil-test/client.key.tmp /tmp/veil-test/client.key
else
  mv /tmp/veil-test/client.key.tmp /tmp/veil-test/client.key
fi

# Step 4: Verify
FINAL_KEY_SIZE=$(stat -c%s /tmp/veil-test/client.key 2>/dev/null || echo 0)
echo "Final key file size: $FINAL_KEY_SIZE bytes"

if [[ "$FINAL_KEY_SIZE" != "32" ]]; then
  echo "ERROR: Client key must be exactly 32 bytes (got $FINAL_KEY_SIZE)"
  exit 1
else
  echo "SUCCESS: Key is exactly 32 bytes"
fi

# Cleanup
rm -rf /tmp/veil-test /tmp/test_key_32.bin

echo ""
echo "=== Test passed! The fix works correctly. ==="
