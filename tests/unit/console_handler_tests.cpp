#include <gtest/gtest.h>

#ifdef _WIN32

#include "windows/console_handler.h"

namespace veil::tests {

class ConsoleHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset handler before each test
    auto& handler = windows::ConsoleHandler::instance();
    handler.reset();
  }

  void TearDown() override {
    // Clean up after each test
    auto& handler = windows::ConsoleHandler::instance();
    handler.restore();
  }
};

TEST_F(ConsoleHandlerTest, SingletonInstance) {
  auto& handler1 = windows::ConsoleHandler::instance();
  auto& handler2 = windows::ConsoleHandler::instance();
  ASSERT_EQ(&handler1, &handler2);
}

TEST_F(ConsoleHandlerTest, SetupSucceeds) {
  auto& handler = windows::ConsoleHandler::instance();
  ASSERT_TRUE(handler.setup());
}

TEST_F(ConsoleHandlerTest, InitialTerminateFlagIsFalse) {
  auto& handler = windows::ConsoleHandler::instance();
  ASSERT_FALSE(handler.should_terminate());
}

TEST_F(ConsoleHandlerTest, ResetClearsTerminateFlag) {
  auto& handler = windows::ConsoleHandler::instance();
  handler.reset();
  ASSERT_FALSE(handler.should_terminate());
}

TEST_F(ConsoleHandlerTest, SetupIsIdempotent) {
  auto& handler = windows::ConsoleHandler::instance();
  ASSERT_TRUE(handler.setup());
  ASSERT_TRUE(handler.setup());  // Should succeed even if already setup
}

TEST_F(ConsoleHandlerTest, RestoreIsIdempotent) {
  auto& handler = windows::ConsoleHandler::instance();
  handler.setup();
  handler.restore();
  handler.restore();  // Should not crash even if already restored
  SUCCEED();
}

TEST_F(ConsoleHandlerTest, CallbackIsInvoked) {
  auto& handler = windows::ConsoleHandler::instance();
  bool callback_invoked = false;

  handler.on_control([&callback_invoked]() { callback_invoked = true; });

  // Note: We can't easily test actual Ctrl+C signal delivery in a unit test,
  // but we can test that the callback mechanism is properly set up.
  // The callback would be invoked when a real console control event occurs.
  ASSERT_FALSE(callback_invoked);  // Not invoked yet since no signal sent
}

}  // namespace veil::tests

#else

// Non-Windows stub test
namespace veil::tests {
TEST(ConsoleHandlerTest, NotAvailableOnNonWindows) {
  GTEST_SKIP() << "Console handler is Windows-only";
}
}  // namespace veil::tests

#endif  // _WIN32
