#!/bin/bash
# Test script for IP pool size calculation

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

# Test cases
echo "Testing IP pool size calculation..."
echo ""

# Test 1: Default VEIL configuration (10.8.0.2 to 10.8.0.254)
result=$(calculate_ip_pool_size "10.8.0.2" "10.8.0.254")
echo "Test 1: 10.8.0.2 to 10.8.0.254"
echo "  Expected: 253"
echo "  Got: $result"
if [ "$result" -eq 253 ]; then
    echo "  ✅ PASS"
else
    echo "  ❌ FAIL"
fi
echo ""

# Test 2: Full /24 subnet (10.8.0.1 to 10.8.0.254)
result=$(calculate_ip_pool_size "10.8.0.1" "10.8.0.254")
echo "Test 2: 10.8.0.1 to 10.8.0.254"
echo "  Expected: 254"
echo "  Got: $result"
if [ "$result" -eq 254 ]; then
    echo "  ✅ PASS"
else
    echo "  ❌ FAIL"
fi
echo ""

# Test 3: Small range (10.8.0.10 to 10.8.0.20)
result=$(calculate_ip_pool_size "10.8.0.10" "10.8.0.20")
echo "Test 3: 10.8.0.10 to 10.8.0.20"
echo "  Expected: 11"
echo "  Got: $result"
if [ "$result" -eq 11 ]; then
    echo "  ✅ PASS"
else
    echo "  ❌ FAIL"
fi
echo ""

# Test 4: Single IP (10.8.0.5 to 10.8.0.5)
result=$(calculate_ip_pool_size "10.8.0.5" "10.8.0.5")
echo "Test 4: 10.8.0.5 to 10.8.0.5"
echo "  Expected: 1"
echo "  Got: $result"
if [ "$result" -eq 1 ]; then
    echo "  ✅ PASS"
else
    echo "  ❌ FAIL"
fi
echo ""

# Test 5: Cross subnet boundary (10.8.0.250 to 10.8.1.5)
result=$(calculate_ip_pool_size "10.8.0.250" "10.8.1.5")
echo "Test 5: 10.8.0.250 to 10.8.1.5"
echo "  Expected: 12"
echo "  Got: $result"
if [ "$result" -eq 12 ]; then
    echo "  ✅ PASS"
else
    echo "  ❌ FAIL"
fi
echo ""

echo "All tests completed!"
