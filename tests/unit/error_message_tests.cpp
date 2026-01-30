// VEIL Error Message System Tests
//
// Tests for:
// - Error message structure and formatting
// - Error categorization
// - Pre-defined error message functions
// - String representation methods

#include <gtest/gtest.h>

#include "common/gui/error_message.h"

using namespace veil::gui;

// =============================================================================
// ErrorMessage Structure Tests
// =============================================================================

TEST(ErrorMessageTest, DefaultConstructor) {
  ErrorMessage error;
  EXPECT_EQ(error.category, ErrorCategory::kUnknown);
  EXPECT_TRUE(error.title.empty());
  EXPECT_TRUE(error.description.empty());
  EXPECT_TRUE(error.action.empty());
  EXPECT_TRUE(error.technical_details.empty());
}

TEST(ErrorMessageTest, ParameterizedConstructor) {
  ErrorMessage error(ErrorCategory::kNetwork, "Test Title", "Test Description",
                     "Test Action", "Test Details");

  EXPECT_EQ(error.category, ErrorCategory::kNetwork);
  EXPECT_EQ(error.title, "Test Title");
  EXPECT_EQ(error.description, "Test Description");
  EXPECT_EQ(error.action, "Test Action");
  EXPECT_EQ(error.technical_details, "Test Details");
}

TEST(ErrorMessageTest, ConstructorWithoutTechnicalDetails) {
  ErrorMessage error(ErrorCategory::kConfiguration, "Title", "Desc", "Action");

  EXPECT_EQ(error.category, ErrorCategory::kConfiguration);
  EXPECT_EQ(error.title, "Title");
  EXPECT_EQ(error.description, "Desc");
  EXPECT_EQ(error.action, "Action");
  EXPECT_TRUE(error.technical_details.empty());
}

// =============================================================================
// Category Name Tests
// =============================================================================

TEST(ErrorMessageTest, CategoryNameNetwork) {
  ErrorMessage error(ErrorCategory::kNetwork, "", "", "");
  EXPECT_EQ(error.category_name(), "Network Error");
}

TEST(ErrorMessageTest, CategoryNameConfiguration) {
  ErrorMessage error(ErrorCategory::kConfiguration, "", "", "");
  EXPECT_EQ(error.category_name(), "Configuration Error");
}

TEST(ErrorMessageTest, CategoryNamePermission) {
  ErrorMessage error(ErrorCategory::kPermission, "", "", "");
  EXPECT_EQ(error.category_name(), "Permission Error");
}

TEST(ErrorMessageTest, CategoryNameDaemon) {
  ErrorMessage error(ErrorCategory::kDaemon, "", "", "");
  EXPECT_EQ(error.category_name(), "Service Error");
}

TEST(ErrorMessageTest, CategoryNameUnknown) {
  ErrorMessage error(ErrorCategory::kUnknown, "", "", "");
  EXPECT_EQ(error.category_name(), "Error");
}

// =============================================================================
// String Representation Tests
// =============================================================================

TEST(ErrorMessageTest, ToUserStringOnlyTitle) {
  ErrorMessage error(ErrorCategory::kNetwork, "Connection Failed", "", "");
  std::string result = error.to_user_string();
  EXPECT_EQ(result, "Connection Failed");
}

TEST(ErrorMessageTest, ToUserStringWithDescription) {
  ErrorMessage error(ErrorCategory::kNetwork, "Connection Failed",
                     "The server is not responding.", "");
  std::string result = error.to_user_string();
  EXPECT_EQ(result, "Connection Failed\n\nThe server is not responding.");
}

TEST(ErrorMessageTest, ToUserStringWithAction) {
  ErrorMessage error(ErrorCategory::kNetwork, "Connection Failed", "",
                     "Check your internet connection");
  std::string result = error.to_user_string();
  EXPECT_EQ(result, "Connection Failed\n\nCheck your internet connection");
}

TEST(ErrorMessageTest, ToUserStringComplete) {
  ErrorMessage error(ErrorCategory::kNetwork, "Connection Failed",
                     "The server is not responding.",
                     "Check your internet connection");
  std::string result = error.to_user_string();
  EXPECT_EQ(result, "Connection Failed\n\nThe server is not responding.\n\nCheck your internet connection");
}

TEST(ErrorMessageTest, ToDetailedStringWithoutTechnicalDetails) {
  ErrorMessage error(ErrorCategory::kNetwork, "Error", "Description", "Action");
  std::string detailed = error.to_detailed_string();
  std::string user = error.to_user_string();
  EXPECT_EQ(detailed, user);
}

TEST(ErrorMessageTest, ToDetailedStringWithTechnicalDetails) {
  ErrorMessage error(ErrorCategory::kNetwork, "Error", "Description", "Action",
                     "errno: 110 (Connection timed out)");
  std::string result = error.to_detailed_string();
  EXPECT_TRUE(result.find("Error") != std::string::npos);
  EXPECT_TRUE(result.find("Description") != std::string::npos);
  EXPECT_TRUE(result.find("Action") != std::string::npos);
  EXPECT_TRUE(result.find("Technical Details:") != std::string::npos);
  EXPECT_TRUE(result.find("errno: 110") != std::string::npos);
}

// =============================================================================
// Pre-defined Error Message Tests
// =============================================================================

TEST(ErrorMessageTest, ConnectionTimeout) {
  auto error = errors::connection_timeout();

  EXPECT_EQ(error.category, ErrorCategory::kDaemon);
  EXPECT_EQ(error.title, "Connection Timeout");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());

  // Verify actionable guidance is present
  std::string action = error.action;
  EXPECT_TRUE(action.find("service") != std::string::npos ||
              action.find("Service") != std::string::npos);
  EXPECT_TRUE(action.find("firewall") != std::string::npos ||
              action.find("Firewall") != std::string::npos);
}

TEST(ErrorMessageTest, DaemonNotRunning) {
  auto error = errors::daemon_not_running();

  EXPECT_EQ(error.category, ErrorCategory::kDaemon);
  EXPECT_EQ(error.title, "Service Not Running");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());

  // Verify actionable guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("Administrator") != std::string::npos ||
              action.find("Services") != std::string::npos);
}

TEST(ErrorMessageTest, NetworkUnreachable) {
  auto error = errors::network_unreachable();

  EXPECT_EQ(error.category, ErrorCategory::kNetwork);
  EXPECT_EQ(error.title, "Network Unreachable");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());

  // Verify network-specific guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("internet") != std::string::npos ||
              action.find("Internet") != std::string::npos ||
              action.find("connection") != std::string::npos);
}

TEST(ErrorMessageTest, MissingKeyFile) {
  std::string test_path = "/path/to/missing.key";
  auto error = errors::missing_key_file(test_path);

  EXPECT_EQ(error.category, ErrorCategory::kConfiguration);
  EXPECT_EQ(error.title, "Pre-shared Key Not Found");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());
  EXPECT_FALSE(error.technical_details.empty());

  // Verify path is in technical details
  EXPECT_TRUE(error.technical_details.find(test_path) != std::string::npos);

  // Verify Settings guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("Settings") != std::string::npos);
}

TEST(ErrorMessageTest, InvalidServerAddress) {
  std::string test_address = "invalid::address::123";
  auto error = errors::invalid_server_address(test_address);

  EXPECT_EQ(error.category, ErrorCategory::kConfiguration);
  EXPECT_EQ(error.title, "Invalid Server Address");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());
  EXPECT_FALSE(error.technical_details.empty());

  // Verify address is in technical details
  EXPECT_TRUE(error.technical_details.find(test_address) != std::string::npos);

  // Verify Settings guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("Settings") != std::string::npos);
}

TEST(ErrorMessageTest, PermissionDeniedServiceInstall) {
  auto error = errors::permission_denied_service_install();

  EXPECT_EQ(error.category, ErrorCategory::kPermission);
  EXPECT_EQ(error.title, "Administrator Rights Required");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());

  // Verify Administrator guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("Administrator") != std::string::npos);
}

TEST(ErrorMessageTest, PermissionDeniedServiceStart) {
  auto error = errors::permission_denied_service_start();

  EXPECT_EQ(error.category, ErrorCategory::kPermission);
  EXPECT_EQ(error.title, "Administrator Rights Required");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());

  // Verify Administrator guidance
  std::string action = error.action;
  EXPECT_TRUE(action.find("Administrator") != std::string::npos);
}

TEST(ErrorMessageTest, ServiceStartFailed) {
  std::string details = "Error code: 1053";
  auto error = errors::service_start_failed(details);

  EXPECT_EQ(error.category, ErrorCategory::kDaemon);
  EXPECT_EQ(error.title, "Service Start Failed");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());
  EXPECT_FALSE(error.technical_details.empty());

  // Verify error details are preserved
  EXPECT_TRUE(error.technical_details.find(details) != std::string::npos);
}

TEST(ErrorMessageTest, IpcError) {
  std::string details = "Named pipe connection failed";
  auto error = errors::ipc_error(details);

  EXPECT_EQ(error.category, ErrorCategory::kDaemon);
  EXPECT_EQ(error.title, "Communication Error");
  EXPECT_FALSE(error.description.empty());
  EXPECT_FALSE(error.action.empty());
  EXPECT_FALSE(error.technical_details.empty());

  // Verify error details are preserved
  EXPECT_TRUE(error.technical_details.find(details) != std::string::npos);
}

TEST(ErrorMessageTest, GenericError) {
  std::string message = "Something unexpected happened";
  auto error = errors::generic(message);

  EXPECT_EQ(error.category, ErrorCategory::kUnknown);
  EXPECT_EQ(error.title, "Error");
  EXPECT_EQ(error.description, message);
  EXPECT_FALSE(error.action.empty());
}

// =============================================================================
// Edge Cases and Robustness Tests
// =============================================================================

TEST(ErrorMessageTest, EmptyStringsInConstructor) {
  ErrorMessage error(ErrorCategory::kNetwork, "", "", "");

  std::string user_str = error.to_user_string();
  EXPECT_TRUE(user_str.empty());

  std::string detailed_str = error.to_detailed_string();
  EXPECT_TRUE(detailed_str.empty());
}

TEST(ErrorMessageTest, VeryLongStrings) {
  std::string long_title(1000, 'A');
  std::string long_desc(2000, 'B');
  std::string long_action(1500, 'C');
  std::string long_details(3000, 'D');

  ErrorMessage error(ErrorCategory::kNetwork, long_title, long_desc,
                     long_action, long_details);

  EXPECT_EQ(error.title.size(), 1000);
  EXPECT_EQ(error.description.size(), 2000);
  EXPECT_EQ(error.action.size(), 1500);
  EXPECT_EQ(error.technical_details.size(), 3000);

  // Should not crash when generating string representation
  std::string result = error.to_detailed_string();
  EXPECT_FALSE(result.empty());
}

TEST(ErrorMessageTest, SpecialCharactersInStrings) {
  std::string special_chars = "Test\n\r\t\\\"\'<>&";
  ErrorMessage error(ErrorCategory::kConfiguration, special_chars, special_chars,
                     special_chars, special_chars);

  EXPECT_EQ(error.title, special_chars);
  EXPECT_EQ(error.description, special_chars);
  EXPECT_EQ(error.action, special_chars);
  EXPECT_EQ(error.technical_details, special_chars);

  // Should handle special characters in string representation
  std::string result = error.to_detailed_string();
  EXPECT_TRUE(result.find(special_chars) != std::string::npos);
}

TEST(ErrorMessageTest, MultilineActionGuidance) {
  std::string multiline_action = "Step 1: Do this\n• Step 2: Do that\n• Step 3: Complete";
  ErrorMessage error(ErrorCategory::kDaemon, "Error", "Description",
                     multiline_action);

  std::string result = error.to_user_string();
  EXPECT_TRUE(result.find("Step 1") != std::string::npos);
  EXPECT_TRUE(result.find("Step 2") != std::string::npos);
  EXPECT_TRUE(result.find("Step 3") != std::string::npos);
}

// =============================================================================
// Error Message Copy and Assignment Tests
// =============================================================================

TEST(ErrorMessageTest, CopyConstructor) {
  ErrorMessage original(ErrorCategory::kNetwork, "Title", "Desc", "Action", "Details");
  ErrorMessage copy = original;  // NOLINT(performance-unnecessary-copy-initialization) - Testing copy constructor

  EXPECT_EQ(copy.category, original.category);
  EXPECT_EQ(copy.title, original.title);
  EXPECT_EQ(copy.description, original.description);
  EXPECT_EQ(copy.action, original.action);
  EXPECT_EQ(copy.technical_details, original.technical_details);
}

TEST(ErrorMessageTest, AssignmentOperator) {
  ErrorMessage original(ErrorCategory::kPermission, "Title", "Desc", "Action", "Details");
  ErrorMessage assigned;

  assigned = original;

  EXPECT_EQ(assigned.category, original.category);
  EXPECT_EQ(assigned.title, original.title);
  EXPECT_EQ(assigned.description, original.description);
  EXPECT_EQ(assigned.action, original.action);
  EXPECT_EQ(assigned.technical_details, original.technical_details);
}

// =============================================================================
// Integration with Error Workflow Tests
// =============================================================================

TEST(ErrorMessageTest, TypicalUserWorkflow) {
  // Simulate typical error workflow: create error, show to user, copy details

  // 1. Create error
  auto error = errors::connection_timeout();

  // 2. Display category and title to user (e.g., in status bar)
  std::string status = error.category_name() + ": " + error.title;
  EXPECT_FALSE(status.empty());

  // 3. Show full user message in error widget
  std::string user_message = error.to_user_string();
  EXPECT_TRUE(user_message.find("Connection Timeout") != std::string::npos);
  EXPECT_TRUE(user_message.find("service") != std::string::npos ||
              user_message.find("Service") != std::string::npos);

  // 4. Prepare detailed message for clipboard (copy button)
  std::string clipboard_content = error.to_detailed_string();
  EXPECT_FALSE(clipboard_content.empty());
  EXPECT_TRUE(clipboard_content.size() >= user_message.size());
}

TEST(ErrorMessageTest, ErrorCategorization) {
  // Test that all pre-defined errors use appropriate categories

  EXPECT_EQ(errors::connection_timeout().category, ErrorCategory::kDaemon);
  EXPECT_EQ(errors::daemon_not_running().category, ErrorCategory::kDaemon);
  EXPECT_EQ(errors::network_unreachable().category, ErrorCategory::kNetwork);
  EXPECT_EQ(errors::missing_key_file("").category, ErrorCategory::kConfiguration);
  EXPECT_EQ(errors::invalid_server_address("").category, ErrorCategory::kConfiguration);
  EXPECT_EQ(errors::permission_denied_service_install().category, ErrorCategory::kPermission);
  EXPECT_EQ(errors::permission_denied_service_start().category, ErrorCategory::kPermission);
  EXPECT_EQ(errors::service_start_failed("").category, ErrorCategory::kDaemon);
  EXPECT_EQ(errors::ipc_error("").category, ErrorCategory::kDaemon);
  EXPECT_EQ(errors::generic("").category, ErrorCategory::kUnknown);
}
