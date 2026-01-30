# Experiments CMake configuration
# These are standalone test programs for debugging and investigation

add_executable(cross_platform_crypto_test cross_platform_crypto_test.cpp)
target_link_libraries(cross_platform_crypto_test PRIVATE veil_common)

# Issue #96: RetransmitBuffer performance benchmark
add_executable(retransmit_buffer_benchmark retransmit_buffer_benchmark.cpp)
