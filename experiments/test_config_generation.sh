#!/bin/bash
# Test script for config generation with dynamic max_clients

# Calculate IP pool size from start and end IP addresses
calculate_ip_pool_size() {
    local start_ip="$1"
    local end_ip="$2"

    # Convert IP address to integer
    ip_to_int() {
        local ip=$1
        local a b c d
        IFS=. read -r a b c d <<< "$ip"
        echo "$((a * 256 ** 3 + b * 256 ** 2 + c * 256 + d))"
    }

    local start_int=$(ip_to_int "$start_ip")
    local end_int=$(ip_to_int "$end_ip")

    # Calculate pool size (inclusive)
    echo "$((end_int - start_int + 1))"
}

# Define IP pool range
IP_POOL_START="10.8.0.2"
IP_POOL_END="10.8.0.254"

# Calculate max_clients dynamically based on IP pool size
MAX_CLIENTS=$(calculate_ip_pool_size "$IP_POOL_START" "$IP_POOL_END")

echo "Configuration Generation Test"
echo "=============================="
echo ""
echo "IP pool range: $IP_POOL_START to $IP_POOL_END"
echo "Calculated max_clients: $MAX_CLIENTS"
echo ""
echo "Generated configuration snippet:"
echo ""
echo "[sessions]"
echo "max_clients = $MAX_CLIENTS"
echo "session_timeout = 300"
echo ""
echo "[ip_pool]"
echo "start = $IP_POOL_START"
echo "end = $IP_POOL_END"
echo ""

# Verify the calculation
if [ "$MAX_CLIENTS" -eq 253 ]; then
    echo "✅ Configuration generation successful!"
    echo "✅ max_clients ($MAX_CLIENTS) matches expected value (253)"
else
    echo "❌ Configuration generation failed!"
    echo "❌ max_clients ($MAX_CLIENTS) does not match expected value (253)"
    exit 1
fi
