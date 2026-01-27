#!/bin/bash

# Simulate the exact scenario from GitHub Actions

# Generate a proper 32-byte key
RAW_KEY=$(dd if=/dev/urandom bs=32 count=1 2>/dev/null)

# Scenario 1: Secret was created correctly (base64 without newline)
# This is how it SHOULD be: base64 -w0 to avoid line wrapping
CORRECT_SECRET=$(echo -n "$RAW_KEY" | base64 -w0)
echo "Scenario 1: Correctly encoded secret (no newline)"
echo "  Secret value: ${CORRECT_SECRET:0:40}..."
echo "  Using echo: $(echo "$CORRECT_SECRET" | base64 -d | wc -c) bytes"
echo "  Using echo -n: $(echo -n "$CORRECT_SECRET" | base64 -d | wc -c) bytes"

# Scenario 2: Secret was created with newline (common mistake)
# This happens when someone does: echo "base64string" > file or copies with newline
INCORRECT_SECRET="${CORRECT_SECRET}
"
echo ""
echo "Scenario 2: Secret stored WITH trailing newline (common mistake)"
echo "  Secret value has newline at end"
echo "  Using echo: $(echo "$INCORRECT_SECRET" | base64 -d 2>&1 | wc -c) bytes"
echo "  Using echo -n: $(echo -n "$INCORRECT_SECRET" | base64 -d 2>&1 | wc -c) bytes"

# Scenario 3: The key itself was base64-encoded with newline included
# This happens if someone did: echo RAW_KEY | base64 instead of echo -n RAW_KEY | base64
WRONG_ENCODING=$(echo "$RAW_KEY" | base64 -w0)
echo ""
echo "Scenario 3: Newline was INCLUDED in the base64 encoding"
echo "  Secret value: ${WRONG_ENCODING:0:40}..."
echo "  Using echo: $(echo "$WRONG_ENCODING" | base64 -d | wc -c) bytes"
echo "  Using echo -n: $(echo -n "$WRONG_ENCODING" | base64 -d | wc -c) bytes"

