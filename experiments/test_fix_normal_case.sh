#!/bin/bash
# Test that the fix doesn't break normal 32-byte keys

echo "=== Testing fix with normal 32-byte key ==="
echo ""

# Create normal 32-byte key
echo "--- Creating proper 32-byte key ---"
dd if=/dev/urandom bs=32 count=1 2>/dev/null > /tmp/test_key_32_proper.bin
SIZE_ORIGINAL=$(stat -c%s /tmp/test_key_32_proper.bin)
echo "Original size: $SIZE_ORIGINAL bytes"

# Encode to base64
VEIL_CLIENT_KEY=$(base64 -w0 < /tmp/test_key_32_proper.bin)
echo "Base64 encoded"
echo ""

# Simulate the CI workflow
echo "--- Simulating CI workflow steps ---"
mkdir -p /tmp/veil-test

echo -n "$VEIL_CLIENT_KEY" | base64 -d > /tmp/veil-test/client.key.tmp

KEY_SIZE=$(stat -c%s /tmp/veil-test/client.key.tmp 2>/dev/null || echo 0)
echo "Decoded key size: $KEY_SIZE bytes"

if [[ "$KEY_SIZE" == "33" ]]; then
  LAST_BYTE=$(tail -c 1 /tmp/veil-test/client.key.tmp | od -An -tx1 | tr -d ' ')
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

FINAL_KEY_SIZE=$(stat -c%s /tmp/veil-test/client.key 2>/dev/null || echo 0)
echo "Final key file size: $FINAL_KEY_SIZE bytes"

if [[ "$FINAL_KEY_SIZE" != "32" ]]; then
  echo "ERROR: Client key must be exactly 32 bytes (got $FINAL_KEY_SIZE)"
  exit 1
else
  echo "SUCCESS: Key is exactly 32 bytes"
fi

# Verify content is unchanged
if cmp -s /tmp/test_key_32_proper.bin /tmp/veil-test/client.key; then
  echo "SUCCESS: Key content is identical"
else
  echo "ERROR: Key content was modified!"
  exit 1
fi

# Cleanup
rm -rf /tmp/veil-test /tmp/test_key_32_proper.bin

echo ""
echo "=== Test passed! Normal case still works. ==="
