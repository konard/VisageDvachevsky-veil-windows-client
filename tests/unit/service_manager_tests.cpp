#include <gtest/gtest.h>

#ifdef _WIN32

#include "windows/service_manager.h"

namespace veil::tests {

class ServiceManagerTest : public ::testing::Test {};

TEST_F(ServiceManagerTest, ServiceNameConstants) {
  EXPECT_STREQ(windows::ServiceManager::kServiceName, "VeilVPN");
  EXPECT_STREQ(windows::ServiceManager::kServiceDisplayName,
               "VEIL VPN Service");
  EXPECT_NE(windows::ServiceManager::kServiceDescription, nullptr);
}

TEST_F(ServiceManagerTest, StatusStringForUnknownState) {
  // When service is not accessible (e.g., not installed), status returns 0
  // which maps to "Unknown"
  auto status_str = windows::ServiceManager::get_status_string();
  // On a test machine without the service installed, this should return
  // either a valid state string or "Unknown"
  EXPECT_FALSE(status_str.empty());
}

}  // namespace veil::tests

#else

// Non-Windows stub test
namespace veil::tests {
TEST(ServiceManagerTest, NotAvailableOnNonWindows) {
  GTEST_SKIP() << "Service manager is Windows-only";
}
}  // namespace veil::tests

#endif  // _WIN32
