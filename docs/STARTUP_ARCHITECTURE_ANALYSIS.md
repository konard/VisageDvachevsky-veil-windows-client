# VEIL VPN Client Startup Architecture Analysis

This document analyzes the current startup architecture issues and proposes solutions for the daemon/GUI launch problems reported.

---

## Problem Statement

The user reported the following issues:
1. **Separate executable launches required** - User must run daemon (veil-service.exe) and GUI (veil-client-gui.exe) separately
2. **No unified shortcut** - No single shortcut to launch everything with admin privileges
3. **GUI needs to be opened twice** - GUI doesn't connect to daemon on first launch; requires reopening
4. **Admin elevation UX is poor** - Current elevation flow is cumbersome

---

## Current Architecture Analysis

### Current Flow (What Happens Now)

```
User double-clicks veil-client-gui.exe
    ↓
main() checks elevation::is_elevated()
    ↓ (If not elevated)
QMessageBox "Administrator Rights Required"
    ↓
elevation::request_elevation() - UAC prompt
    ↓ (If approved)
New elevated process starts, old exits
    ↓
MainWindow::MainWindow() constructor
    ↓
ipcManager_->connectToDaemon() ← FAILS (service not running yet)
    ↓
ensureServiceRunning() called
    ↓ (Service not installed)
QMessageBox "Service Installation Required"
    ↓ (User clicks Yes)
ServiceManager::install() + start_and_wait()
    ↓
QTimer::singleShot(2000ms) retry connection
    ↓
ipcManager_->connectToDaemon() ← Usually works now
```

### Identified Problems

| Problem | Location | Impact |
|---------|----------|--------|
| Race condition | `mainwindow.cpp:132-158` | Service may not be ready after 2000ms delay |
| Double open needed | IPC connection timing | Named pipe not created until service is fully running |
| No unified launcher | Build system | Users must manage two executables |
| Blocking elevation | `main.cpp:56-82` | Poor UX when elevation fails |

### Why GUI Needs to Be Opened Twice

1. **First launch**: GUI starts → service not running → `ensureServiceRunning()` installs/starts service → waits 2000ms → tries IPC connection → may still fail if Named Pipe not ready
2. **Second launch**: Service already running → Named Pipe ready → IPC connection succeeds immediately

The root cause is the **race condition** between:
- Service startup (creates Named Pipe in `run_service()` at line 276-288)
- GUI attempting IPC connection

---

## Solution Options Analysis

### Option A: Daemon as DLL (Not Recommended)

**Description**: Compile daemon logic as a DLL and load it in-process with GUI.

**Implementation**:
```cpp
// In GUI main.cpp
LoadLibrary("veil-daemon.dll");
// Call daemon initialization from DLL
```

**Pros**:
- Single executable for user
- No IPC overhead
- Simpler deployment

**Cons**:
- ❌ **Security risk**: Daemon runs in user space, not SYSTEM
- ❌ **No admin privileges isolation**: VPN operations need SYSTEM rights for TUN/routing
- ❌ **Process isolation lost**: GUI crash = VPN crash
- ❌ **Memory isolation lost**: GUI vulnerabilities could compromise VPN
- ❌ **Background operation lost**: GUI close = VPN disconnect
- ❌ **Startup boot not possible**: Can't run VPN before user login

**Verdict**: ❌ **Not suitable for VPN application** - Security and stability requirements mandate separate process with elevated privileges.

---

### Option B: Windows Service with Auto-Start (Recommended - Incremental)

**Description**: Keep current architecture but improve startup reliability and auto-start configuration.

**Implementation**:
1. Set service startup type to "Automatic" during installation
2. Improve IPC connection retry logic with exponential backoff
3. Add proper "waiting for service" UI with progress indicator

**Changes Required**:

```cpp
// src/windows/service_manager.cpp - Install with auto-start
bool ServiceManager::install(const std::string& executable_path, std::string& error) {
  // Add SERVICE_AUTO_START instead of SERVICE_DEMAND_START
  SC_HANDLE service = CreateServiceA(
      scm, kServiceName, kDisplayName,
      SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
      SERVICE_AUTO_START,  // ← Changed from SERVICE_DEMAND_START
      SERVICE_ERROR_NORMAL,
      executable_path.c_str(),
      nullptr, nullptr, nullptr, nullptr, nullptr);
}

// src/gui-client/ipc_client_manager.cpp - Better retry logic
void IpcClientManager::attemptReconnect() {
  // Use exponential backoff instead of fixed 5s interval
  int delay = std::min(60000, 1000 * (1 << reconnectAttempts_));
  // ...
}

// src/gui-client/mainwindow.cpp - Connection status UI
void MainWindow::onDaemonConnectionAttempt() {
  statusBar()->showMessage(tr("Connecting to VEIL service... (attempt %1)").arg(attempt));
  connectionWidget_->setConnectionState(ConnectionState::kConnecting);
}
```

**Pros**:
- ✅ Minimal code changes
- ✅ Service runs at boot (before login)
- ✅ Better UX with retry feedback
- ✅ Maintains security model

**Cons**:
- User still sees "connecting" state initially
- Two executables still exist (but transparent to user)

**Verdict**: ✅ **Good incremental improvement**

---

### Option C: Unified Launcher Executable (Recommended - Best UX)

**Description**: Create a launcher (veil-vpn.exe) that:
1. Ensures service is installed and running
2. Then launches GUI
3. Handles all elevation in one place

**Implementation**:

Create new `src/windows/launcher_main.cpp`:
```cpp
int main(int argc, char* argv[]) {
  // 1. Request elevation if not admin
  if (!elevation::is_elevated()) {
    return elevation::request_elevation("") ? 0 : 1;
  }

  // 2. Ensure service is installed and running
  if (!ServiceManager::is_installed()) {
    std::string error;
    if (!ServiceManager::install(get_service_path(), error)) {
      MessageBoxA(NULL, error.c_str(), "VEIL Installation Failed", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  if (!ServiceManager::is_running()) {
    std::string error;
    if (!ServiceManager::start_and_wait(error)) {
      MessageBoxA(NULL, error.c_str(), "VEIL Service Start Failed", MB_OK | MB_ICONERROR);
      return 1;
    }
  }

  // 3. Wait for Named Pipe to be ready
  wait_for_named_pipe("\\\\.\\pipe\\veil-client", 5000);

  // 4. Launch GUI (can run as regular user since service handles VPN)
  ShellExecuteA(NULL, "open", "veil-client-gui.exe", NULL, NULL, SW_SHOW);

  return 0;
}
```

**Pros**:
- ✅ Single shortcut for users
- ✅ Clean startup sequence
- ✅ Proper error handling before GUI shows
- ✅ GUI no longer needs elevation logic
- ✅ Better installer integration

**Cons**:
- Three executables to build/maintain
- Slightly more complex

**Verdict**: ✅ **Best user experience**

---

### Option D: GUI as Service Controller (Current + Improved)

**Description**: Keep current architecture but add proper synchronization.

**Implementation**:
1. Service writes a "ready" flag to shared memory or registry when IPC is ready
2. GUI polls this flag before attempting connection
3. Or: GUI subscribes to Windows service state change events

```cpp
// In service_main.cpp after IPC server starts
void signal_ready() {
  HANDLE event = CreateEventA(NULL, TRUE, FALSE, "Global\\VEIL_SERVICE_READY");
  SetEvent(event);
}

// In mainwindow.cpp before IPC connection
void MainWindow::waitForServiceReady() {
  HANDLE event = OpenEventA(SYNCHRONIZE, FALSE, "Global\\VEIL_SERVICE_READY");
  if (event) {
    WaitForSingleObject(event, 5000);  // Wait up to 5 seconds
    CloseHandle(event);
  }
}
```

**Pros**:
- ✅ Fixes race condition properly
- ✅ Minimal architecture change

**Cons**:
- Adds Windows event complexity
- Still requires admin for GUI on first run

**Verdict**: ✅ **Good technical fix for race condition**

---

## Recommended Solution: Combination of B + C + D

### Phase 1: Fix Race Condition (Issue #127)
Implement Option D - proper synchronization between service and GUI.
- Add "ready" event signal in service
- Wait for event in GUI before IPC connection
- This fixes the "open twice" problem

### Phase 2: Service Auto-Start (Issue #128)
Implement Option B - automatic service startup.
- Change service install to SERVICE_AUTO_START
- Service runs at Windows boot
- GUI always finds service ready

### Phase 3: Unified Launcher (Issue #129)
Implement Option C - single launcher executable.
- Create veil-vpn.exe launcher
- Handles elevation, service, and GUI
- Create Start Menu/Desktop shortcuts to launcher

### Phase 4: Installer Integration (Issue #130)
- NSIS/WiX installer creates proper shortcuts
- Registers service during installation
- Uninstaller properly removes service

---

## Implementation Priority

| Priority | Issue | Description | Effort |
|----------|-------|-------------|--------|
| **P0** | #127 | Fix race condition (ready event) | 1-2 days |
| **P1** | #128 | Service auto-start | 0.5 day |
| **P2** | #129 | Unified launcher | 2-3 days |
| **P3** | #130 | Installer integration | 3-5 days |

---

## Dependencies

```
#127 (Race condition)
    ↓
#128 (Auto-start) ←─────────────┐
    ↓                           │
#129 (Unified launcher) ←───────┤ (can be done in parallel after #127)
    ↓                           │
#130 (Installer) ←──────────────┘
```

---

## Why NOT to Use DLL Approach

For a VPN client, the DLL approach has fundamental problems:

1. **Privilege Separation**:
   - VPN daemon needs SYSTEM privileges for network adapter management
   - GUI should run with minimal privileges for security
   - DLL shares process = shares privileges = security risk

2. **Stability**:
   - GUI crashes should not disconnect VPN
   - With DLL, GUI crash = VPN crash = network disruption

3. **Background Operation**:
   - Users expect VPN to stay connected when GUI is closed
   - DLL can't run independently of GUI

4. **Boot Startup**:
   - Corporate/security VPNs often need to connect before user login
   - Service can run at boot; GUI (with DLL) cannot

The current service-based architecture is **correct** - it just needs better synchronization and UX.

---

## Summary

| Approach | Security | Stability | UX | Effort | Recommendation |
|----------|----------|-----------|-------|--------|----------------|
| DLL | ❌ Poor | ❌ Poor | ✅ Simple | Medium | ❌ Not Suitable |
| Service + Auto-Start | ✅ Good | ✅ Good | ⚠️ OK | Low | ✅ Quick Win |
| Unified Launcher | ✅ Good | ✅ Good | ✅ Best | Medium | ✅ Best Solution |
| Ready Event Sync | ✅ Good | ✅ Good | ✅ Good | Low | ✅ Essential Fix |

**Final Recommendation**: Implement Ready Event Sync (#127) immediately to fix the "open twice" bug, then proceed with Auto-Start (#128) and Unified Launcher (#129) for complete solution.
