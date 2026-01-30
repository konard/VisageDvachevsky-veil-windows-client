# VEIL Windows Client - Comprehensive Work Plan

This document outlines a prioritized work plan for all open issues without PRs, organized by category with explicit dependencies.

**Last Updated:** 2026-01-30

---

## Executive Summary

Total open issues: **28**
- Critical bugs: **2**
- DPI evasion improvements: **3**
- Startup/Launch improvements: **4**
- UI/UX improvements: **17**
- Performance/Architecture: **2**

---

## Category 1: Critical Bugs (Must Fix First)

### Issue #71: Decryption failures after successful handshake
**Priority:** P0 - Blocker
**Status:** Open
**Estimated Effort:** 1-2 weeks
**Dependencies:** None

**Description:** After successful handshake, data packets fail to decrypt on the server side.

**Root Cause Hypotheses:**
1. Version mismatch between Windows client and Linux server binaries
2. Different libsodium versions or builds
3. Cross-platform byte ordering issue

**Investigation Steps:**
1. Compare key fingerprints between client/server after handshake
2. Verify sequence number obfuscation values match
3. Check nonce generation consistency
4. Test with same libsodium version

**Critical:** This prevents actual VPN usage and must be resolved before other work.

---

## Category 2: Startup/Launch Issues (High Priority)

These issues address user feedback about needing to launch daemon/GUI separately and opening GUI twice.

### Issue #127: Fix race condition - GUI fails to connect on first launch ⭐
**Priority:** P0 - Critical
**Status:** Open
**Estimated Effort:** 1-2 days
**Dependencies:** None
**Blocks:** #128, #129

**Description:** GUI needs to be opened twice to connect to daemon due to race condition between service startup and IPC connection.

**Solution:** Implement Windows Event synchronization
- Service signals "VEIL_SERVICE_READY" event when Named Pipe is ready
- GUI waits for event (timeout 5s) before attempting connection
- Eliminates unreliable 2000ms fixed delay

**Files to Modify:**
- `src/windows/service/service_main.cpp` - Add `signal_ready()` after IPC start
- `src/windows/gui/mainwindow.cpp` - Add `waitForServiceReady()` before connection

**Why This First:** Fixes immediate user pain point with minimal changes.

---

### Issue #128: Enable automatic service startup on Windows boot
**Priority:** P1 - High
**Status:** Open
**Estimated Effort:** 4 hours
**Dependencies:** #127 (should fix race condition first)
**Enables:** #129 (makes launcher simpler)

**Description:** Service currently uses `SERVICE_DEMAND_START`. Change to `SERVICE_AUTO_START` so service is always ready when GUI launches.

**Solution:**
```cpp
// In service_manager.cpp
CreateServiceA(..., SERVICE_AUTO_START, ...)  // Changed from SERVICE_DEMAND_START
```

**Benefits:**
- Service always running when GUI starts
- Faster GUI startup (no service wait)
- Enables auto-connect at boot
- Better enterprise deployment

**Optional Enhancement:** Use `SERVICE_DELAYED_AUTO_START_INFO` for systems with many startup services.

---

### Issue #129: Create unified launcher executable (veil-vpn.exe)
**Priority:** P2 - Medium
**Status:** Open
**Estimated Effort:** 3-5 days
**Dependencies:** #127, #128 (works better with auto-start service)
**Enables:** #130 (installer needs launcher)

**Description:** Single executable that handles elevation, service installation, and GUI launch.

**New File:** `src/windows/launcher_main.cpp`

**Launcher Flow:**
1. Check if running as admin → request elevation if needed
2. Check if service installed → install if needed
3. Check if service running → start if needed
4. Wait for service ready event (#127)
5. Launch GUI without elevation

**Benefits:** One-click launch for users, single desktop shortcut.

---

### Issue #130: Create Windows installer with shortcuts
**Priority:** P3 - Low
**Status:** Open
**Estimated Effort:** 1-2 weeks
**Dependencies:** #129 (launcher should exist first)

**Description:** NSIS or WiX installer for proper Windows integration.

**Installer Actions:**
- Copy executables to Program Files
- Install service with auto-start
- Create Desktop and Start Menu shortcuts
- Register uninstaller
- Add firewall rules (optional)

**Deliverable:** `veil-vpn-setup.exe`

---

## Category 3: DPI Evasion Improvements (High Priority)

From issue #76 technical limitations analysis.

### Issue #81: WebSocket without HTTP handshake (MERGED in PR #109)
**Priority:** High
**Status:** MERGED ✅
**Dependencies:** None

Already implemented! HttpHandshakeEmulator added in PR #109.

---

### Issue #82: Missing TLS record layer wrapper
**Priority:** High
**Status:** Open
**Estimated Effort:** 3-4 weeks
**Dependencies:** None
**Related:** #81

**Description:** Real WebSocket runs over TLS (wss://). Plain ws:// over UDP is rare and easily detected.

**Solution:** Implement TLS 1.3 record wrapper
- Wrap packets in TLS application data records
- Record format: Content type (0x17) + Version (0x0303) + Length + Payload
- Overhead: 5-20 bytes per record
- New mode: `ProtocolWrapperType::kTLS`

**References:** RFC 8446, `docs/protocol_wrappers.md:271-276`

---

### Issue #83: Predictable session rotation interval
**Priority:** Medium
**Status:** Open
**Estimated Effort:** 1 day
**Dependencies:** None

**Description:** Session ID rotates exactly every 30s. ML-based DPI can detect perfect periodicity.

**Solution:** Add exponential jitter
```cpp
duration compute_rotation_interval() {
  const duration base = 30s;
  double u = uniform_random(0.0, 1.0);
  double jitter = -log(u) * 10s;
  jitter = clamp(jitter, -10.0, 20.0);  // Range: 20-50s
  return base + seconds(static_cast<int>(jitter));
}
```

**Files:** `src/common/session/session_rotator.cpp`

---

### Issue #84: No ML-based DPI testing
**Priority:** Critical
**Status:** Open
**Estimated Effort:** 4-6 weeks
**Dependencies:** Should be done after #82, #83 implemented

**Description:** VEIL validated against signature-based DPI (nDPI) but not ML classifiers.

**Phases:**
1. **Build Dataset** (2 weeks)
   - Collect VEIL pcaps (all 4 modes, 10+ hours each)
   - Collect legitimate traffic (IoT, WebSocket, gaming, video streaming)

2. **Train Classifiers** (2 weeks)
   - Extract features (packet size histograms, timing, bursts)
   - Train: Random Forest, XGBoost, LSTM
   - Measure detection accuracy

3. **Iterate** (1-2 weeks)
   - If detection > 5% → improve obfuscation
   - Adversarial training loop

**Why Critical:** Unknown if VEIL survives modern ML-based censorship systems.

---

## Category 4: Architecture/Performance (Medium Priority)

### Issue #86: No 0-RTT session resumption
**Priority:** Low
**Status:** Open
**Estimated Effort:** 2-3 weeks
**Dependencies:** None

**Description:** Client must complete 1-RTT handshake before sending data. 0-RTT would reduce latency 50% for returning clients.

**Solution:**
- Server issues session tickets after handshake
- Client caches ticket
- On reconnect: send ticket + data in INIT packet
- Server validates and processes immediately

**Security:** Vulnerable to replay attacks - need anti-replay token.

---

### Issue #87: PSK authentication doesn't scale (MERGED in PR #108)
**Priority:** Low
**Status:** MERGED ✅
**Dependencies:** None

Already implemented! Per-client PSK authentication added in PR #108.

---

## Category 5: UI/UX Improvements

Organized by phases from `docs/UI_UX_ROADMAP.md`.

### Phase 1: Critical UX Fixes (P0)

**Dependencies:** These can be done in parallel, no interdependencies.

#### Issue #110: Add loading states for async operations
**Estimated Effort:** 1 week

- Service installation progress indicator
- Settings save loading overlay
- Daemon connection progress bar
- "Connecting to daemon..." spinner on startup

**Files:** `mainwindow.cpp`, `settings_widget.cpp`, `connection_widget.cpp`

---

#### Issue #111: Improve error message presentation
**Estimated Effort:** 1 week

- Error categorization (Network, Configuration, Permission, Daemon)
- Actionable error messages with steps
- "Copy Error Details" button
- System tray notifications for critical errors

**Files:** `connection_widget.cpp`, `mainwindow.cpp`

---

#### Issue #112: Add input validation visual feedback
**Estimated Effort:** 1 week

- Green checkmark for valid fields
- Red X for invalid fields
- Real-time validation with 200ms debounce
- Validation summary at top of settings

**Files:** `settings_widget.cpp`

---

### Phase 2: Accessibility & Polish (P1)

**Dependencies:** Should be done after Phase 1, but can be parallelized.

#### Issue #113: Add keyboard navigation and focus indicators
**Estimated Effort:** 1 week

- Focus ring styling for dark theme
- Missing shortcuts (Escape, Ctrl+S, Ctrl+,, F5)
- Explicit tab order
- Shortcut hints in tooltips

**Files:** `mainwindow.cpp`, `theme.h`

---

#### Issue #114: Add High-DPI display support
**Estimated Effort:** 1 week

- Enable `Qt::AA_EnableHighDpiScaling`
- Convert fixed sizes to DPI-aware
- Use `QFontMetrics` for text-based sizing
- Replace PNG/JPG with SVG icons
- Test at 125%, 150%, 175%, 200% DPI

**Files:** `main.cpp`, `connection_widget.cpp`, `mainwindow.cpp`, `theme.h`

---

#### Issue #115: Add light theme support
**Estimated Effort:** 1 week

- Theme enum (Dark/Light/System)
- System theme detection (Windows registry)
- Theme toggle in Advanced settings
- Persisted preference

**Files:** `theme.h`, `settings_widget.cpp`, `mainwindow.cpp`

**Note:** Light theme stylesheet already exists in `theme.h:507-560`, just needs wiring!

---

#### Issue #116: Improve settings organization
**Estimated Effort:** 1-2 weeks

- Collapsible sections widget
- Group into tabs (Basic, Network, Security, Advanced)
- "Show Advanced Settings" toggle
- Quick search/filter option

**Files:** `settings_widget.cpp`

---

### Phase 3: Feature Enhancements (P2)

**Dependencies:** These add new features, can be done after P0/P1.

#### Issue #117: Add connection statistics history and graphs
**Estimated Effort:** 2-3 weeks

- Connection history (last 10 connections)
- Bandwidth graphs (last 5 minutes)
- Latency trend graph
- Export to CSV/JSON

**Storage:** SQLite or file-based
**Visualization:** Qt Charts
**Files:** `diagnostics_widget.cpp`, new `statistics_widget.cpp`

---

#### Issue #118: Add server selection and favorites
**Estimated Effort:** 2-3 weeks

- Server list management (Add/Edit/Remove)
- Quick-switch dropdown on main screen
- Server latency preview (ping)
- Favorites system
- Import from URL or QR code

**Files:** `settings_widget.cpp`, new `server_list_widget.cpp`

---

#### Issue #119: Add first-run onboarding wizard
**Estimated Effort:** 2 weeks

- Welcome screen
- Server configuration step
- Key file setup (with generate option)
- Optional features configuration
- Test connection at end

**Files:** `mainwindow.cpp`, new `onboarding_wizard.cpp`

---

#### Issue #120: Add quick actions widget
**Estimated Effort:** 1 week

- Expandable quick actions panel on main screen
- Kill switch toggle
- Obfuscation mode switch
- Copy IP address
- Share connection status

**Files:** `connection_widget.cpp`, `system_tray.cpp`

---

### Phase 4: Advanced Features (P3)

**Dependencies:** Nice-to-have features for power users.

#### Issue #121: Add per-application split tunneling
**Estimated Effort:** 4-6 weeks (MAJOR FEATURE)

- Installed apps browser
- "Always use VPN" / "Never use VPN" lists
- App network usage stats (optional)

**Technical:** Requires WFP (Windows Filtering Platform) integration in daemon.
**Complexity:** High - significant daemon changes needed.

---

#### Issue #122: Add multi-language support (i18n)
**Estimated Effort:** 3-4 weeks

- Wrap all strings with `tr()`
- Generate `.ts` files with `lupdate`
- Translate (English, Russian, Chinese initially)
- Compile `.qm` files with `lrelease`
- Load translations at startup

**Files:** All `.cpp` and `.h` files with user-visible strings.

---

#### Issue #123: Add notification customization
**Estimated Effort:** 1 week

- Enable/disable notifications globally
- Per-event notification toggles
- Notification sound toggle
- Notification history view
- Quiet hours / DND integration

**Files:** `mainwindow.cpp`, `system_tray.cpp`, `settings_widget.cpp`

---

### Phase 5: Quality of Life (P4)

**Dependencies:** Polish features, lowest priority.

#### Issue #124: Add auto-connect and startup options
**Estimated Effort:** 1-2 weeks

- "Start minimized to tray"
- "Auto-connect on startup"
- "Launch on Windows startup" (enhance existing)
- "Auto-connect on insecure network" (by SSID)
- "Reconnect after sleep/hibernate"

**Files:** `mainwindow.cpp`, `settings_widget.cpp`

---

#### Issue #125: Add data usage tracking
**Estimated Effort:** 2 weeks

- Track daily/monthly data usage
- Usage visualization (daily/weekly/monthly graphs)
- Usage alerts (optional)
- Export usage reports to CSV

**Storage:** SQLite
**Files:** New `usage_tracker.cpp`, `statistics_widget.cpp`

---

#### Issue #126: Add desktop shortcuts creation
**Estimated Effort:** 1 week

- Desktop shortcut option
- Start Menu entry
- "Pin to Taskbar" helper
- Jump list with quick actions

**Implementation:** Windows Shell API
**Alternative:** Add to installer (#130)

---

## Dependency Graph

```
CRITICAL PATH:
#71 (Decryption bug) ← MUST FIX FIRST

STARTUP IMPROVEMENTS:
#127 (Race condition fix) ← Start here
    ↓
#128 (Auto-start service) ←───┐
    ↓                         │ Can be parallel after #127
#129 (Unified launcher) ←─────┤
    ↓                         │
#130 (Installer) ←────────────┘

DPI EVASION:
#81 (WebSocket HTTP) ✅ MERGED
#82 (TLS wrapper) ← Independent
#83 (Session jitter) ← Independent
    ↓ (all three should be done before testing)
#84 (ML testing) ← Test after improvements

PERFORMANCE:
#86 (0-RTT) ← Independent
#87 (Per-client PSK) ✅ MERGED

UI/UX PHASES:
Phase 1 (P0): #110, #111, #112 ← Parallel, no dependencies
    ↓
Phase 2 (P1): #113, #114, #115, #116 ← Parallel after Phase 1
    ↓
Phase 3 (P2): #117, #118, #119, #120 ← Parallel after Phase 2
    ↓
Phase 4 (P3): #121, #122, #123 ← Parallel after Phase 3
    ↓
Phase 5 (P4): #124, #125, #126 ← Parallel after Phase 4
```

---

## Recommended Implementation Order

### Immediate (Week 1-2)
1. **Issue #71** - Fix decryption bug (BLOCKER)
2. **Issue #127** - Fix race condition (immediate user pain)
3. **Issue #83** - Add session jitter (quick win, 1 day)

### Short-term (Month 1)
4. **Issue #128** - Auto-start service
5. **Issue #129** - Unified launcher
6. **Issue #110** - Loading states (P0 UX)
7. **Issue #111** - Error messages (P0 UX)
8. **Issue #112** - Input validation (P0 UX)

### Medium-term (Month 2-3)
9. **Issue #82** - TLS wrapper (major DPI improvement)
10. **Issue #113-116** - Phase 2 UX (parallel tasks)
11. **Issue #84** - ML-based DPI testing (after #82, #83)
12. **Issue #130** - Windows installer

### Long-term (Month 4-6)
13. **Issue #117-120** - Phase 3 UX features
14. **Issue #86** - 0-RTT resumption
15. **Issue #121-123** - Phase 4 advanced features
16. **Issue #124-126** - Phase 5 polish

---

## Work Packages

For parallel development, work can be split into these independent packages:

### Package A: Critical Fixes (1 developer, 2-3 weeks)
- #71, #127, #128, #129

### Package B: DPI Evasion (1 developer, 6-8 weeks)
- #82, #83, #84

### Package C: UI/UX Phase 1-2 (1 developer, 4-6 weeks)
- #110, #111, #112, #113, #114, #115, #116

### Package D: UI/UX Phase 3-4 (1 developer, 8-10 weeks)
- #117, #118, #119, #120, #121, #122, #123

### Package E: Deployment (1 developer, 2 weeks)
- #130

---

## Notes

- **Issue #81** and **#87** are already MERGED ✅
- **Issue #71** is a blocker - must be fixed before release
- **Issue #127** addresses user's immediate complaint about "opening GUI twice"
- UI/UX improvements are extensive but independent - can be parallelized
- DPI testing (#84) should happen after DPI improvements (#82, #83)
- Installer (#130) should be last - depends on launcher (#129)

---

## Success Metrics

### User Experience
- [ ] Single-click launch (no more "open twice")
- [ ] Fast startup (< 2 seconds)
- [ ] Clear error messages
- [ ] Responsive UI (loading states)

### Technical
- [ ] Decryption works reliably
- [ ] No race conditions
- [ ] ML detection < 5%
- [ ] Service auto-starts on boot

### Deployment
- [ ] Windows installer available
- [ ] Desktop/Start Menu shortcuts
- [ ] Clean uninstall process

---

**Total Estimated Effort:** 6-9 months with 1-2 developers
**With 3-4 developers (parallelized):** 3-4 months
