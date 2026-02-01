#include "shortcut_manager.h"

#ifdef _WIN32

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>

#include <filesystem>
#include <iostream>

namespace veil::windows {

namespace {

// Convert std::string to wide string
std::wstring toWideString(const std::string& str) {
  if (str.empty()) {
    return std::wstring();
  }

  int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                       static_cast<int>(str.size()), nullptr, 0);
  std::wstring wide_string(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                     &wide_string[0], size_needed);
  return wide_string;
}

// Convert wide string to std::string
std::string toNarrowString(const std::wstring& wide_str) {
  if (wide_str.empty()) {
    return std::string();
  }

  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(),
                                       static_cast<int>(wide_str.size()),
                                       nullptr, 0, nullptr, nullptr);
  std::string narrow_string(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(),
                     static_cast<int>(wide_str.size()), &narrow_string[0],
                     size_needed, nullptr, nullptr);
  return narrow_string;
}

}  // namespace

bool ShortcutManager::createShortcut(Location location,
                                    const std::string& shortcut_name,
                                    const std::string& target_path,
                                    std::string& error,
                                    const std::string& arguments,
                                    const std::string& description,
                                    const std::string& icon_path,
                                    int icon_index,
                                    const std::string& working_dir) {
  // Get the shortcut file path
  std::string shortcut_path = getShortcutPath(location, shortcut_name, error);
  if (shortcut_path.empty()) {
    return false;
  }

  // Initialize COM
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool com_initialized = SUCCEEDED(hr);
  if (!com_initialized && hr != RPC_E_CHANGED_MODE) {
    error = "Failed to initialize COM: " + std::to_string(hr);
    return false;
  }

  bool success = false;
  IShellLinkW* shell_link = nullptr;
  IPersistFile* persist_file = nullptr;

  do {
    // Create IShellLink instance
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW, reinterpret_cast<void**>(&shell_link));
    if (FAILED(hr)) {
      error = "Failed to create IShellLink instance: " + std::to_string(hr);
      break;
    }

    // Set the target path
    std::wstring target_wide = toWideString(target_path);
    hr = shell_link->SetPath(target_wide.c_str());
    if (FAILED(hr)) {
      error = "Failed to set target path: " + std::to_string(hr);
      break;
    }

    // Set arguments if provided
    if (!arguments.empty()) {
      std::wstring args_wide = toWideString(arguments);
      hr = shell_link->SetArguments(args_wide.c_str());
      if (FAILED(hr)) {
        error = "Failed to set arguments: " + std::to_string(hr);
        break;
      }
    }

    // Set description if provided
    if (!description.empty()) {
      std::wstring desc_wide = toWideString(description);
      hr = shell_link->SetDescription(desc_wide.c_str());
      if (FAILED(hr)) {
        error = "Failed to set description: " + std::to_string(hr);
        break;
      }
    }

    // Set icon
    std::string icon_file = icon_path.empty() ? target_path : icon_path;
    std::wstring icon_wide = toWideString(icon_file);
    hr = shell_link->SetIconLocation(icon_wide.c_str(), icon_index);
    if (FAILED(hr)) {
      error = "Failed to set icon: " + std::to_string(hr);
      break;
    }

    // Set working directory
    std::string work_dir = working_dir;
    if (work_dir.empty()) {
      // Use the directory of the target executable
      std::filesystem::path target_fs_path(target_path);
      work_dir = target_fs_path.parent_path().string();
    }
    std::wstring work_dir_wide = toWideString(work_dir);
    hr = shell_link->SetWorkingDirectory(work_dir_wide.c_str());
    if (FAILED(hr)) {
      error = "Failed to set working directory: " + std::to_string(hr);
      break;
    }

    // Get IPersistFile interface
    hr = shell_link->QueryInterface(IID_IPersistFile,
                                   reinterpret_cast<void**>(&persist_file));
    if (FAILED(hr)) {
      error = "Failed to get IPersistFile interface: " + std::to_string(hr);
      break;
    }

    // Save the shortcut
    std::wstring shortcut_wide = toWideString(shortcut_path);
    hr = persist_file->Save(shortcut_wide.c_str(), TRUE);
    if (FAILED(hr)) {
      error = "Failed to save shortcut file: " + std::to_string(hr);
      break;
    }

    success = true;
  } while (false);

  // Clean up
  if (persist_file) {
    persist_file->Release();
  }
  if (shell_link) {
    shell_link->Release();
  }
  if (com_initialized) {
    CoUninitialize();
  }

  return success;
}

bool ShortcutManager::removeShortcut(Location location,
                                    const std::string& shortcut_name,
                                    std::string& error) {
  std::string shortcut_path = getShortcutPath(location, shortcut_name, error);
  if (shortcut_path.empty()) {
    return false;
  }

  // Check if the shortcut exists
  std::error_code exists_ec;
  if (!std::filesystem::exists(shortcut_path, exists_ec) || exists_ec) {
    // Not an error - shortcut doesn't exist (or path is inaccessible)
    return true;
  }

  // Delete the shortcut file
  std::error_code ec;
  if (!std::filesystem::remove(shortcut_path, ec)) {
    if (ec) {
      error = "Failed to delete shortcut: " + ec.message();
      return false;
    }
  }

  return true;
}

bool ShortcutManager::shortcutExists(Location location,
                                    const std::string& shortcut_name) {
  std::string error;
  std::string shortcut_path = getShortcutPath(location, shortcut_name, error);
  if (shortcut_path.empty()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::exists(shortcut_path, ec) && !ec;
}

std::string ShortcutManager::getLocationPath(Location location, std::string& error) {
  KNOWNFOLDERID folder_id;

  switch (location) {
    case Location::kDesktop:
      folder_id = FOLDERID_Desktop;
      break;
    case Location::kStartMenu:
      folder_id = FOLDERID_Programs;
      break;
    case Location::kStartMenuCommon:
      folder_id = FOLDERID_CommonPrograms;
      break;
    default:
      error = "Invalid location type";
      return "";
  }

  wchar_t* path_wide = nullptr;
  HRESULT hr = SHGetKnownFolderPath(folder_id, 0, nullptr, &path_wide);
  if (FAILED(hr)) {
    error = "Failed to get folder path: " + std::to_string(hr);
    return "";
  }

  std::string path = toNarrowString(path_wide);
  CoTaskMemFree(path_wide);

  return path;
}

bool ShortcutManager::pinToTaskbar(const std::string& target_path) {
  // Note: There is no official documented API to programmatically pin to taskbar
  // in Windows 10+. The verb-based approach was deprecated.
  //
  // The recommended approach is to:
  // 1. Create a Desktop shortcut (which we already do)
  // 2. Instruct the user to manually pin it
  //
  // Some alternatives that may work but are not recommended:
  // - Using undocumented shell verbs (may break in future Windows versions)
  // - Using the Windows.UI.Shell.TaskbarManager API (requires UWP/WinRT)
  // - Creating a shortcut in a special "User Pinned" folder (fragile)
  //
  // For now, we return false to indicate this is not implemented.
  // Applications should guide users to manually pin the app.

  (void)target_path;  // Suppress unused parameter warning
  return false;
}

std::string ShortcutManager::getShortcutPath(Location location,
                                            const std::string& shortcut_name,
                                            std::string& error) {
  std::string location_path = getLocationPath(location, error);
  if (location_path.empty()) {
    return "";
  }

  // Create the directory if it doesn't exist
  std::error_code ec;
  std::filesystem::create_directories(location_path, ec);
  if (ec) {
    error = "Failed to create directory: " + ec.message();
    return "";
  }

  // Build the full shortcut path with .lnk extension
  std::filesystem::path shortcut_path(location_path);
  shortcut_path /= shortcut_name + ".lnk";

  return shortcut_path.string();
}

}  // namespace veil::windows

#endif  // _WIN32
