# VEIL Windows Client - UI/UX Improvement Roadmap

## Overview

This document outlines the UI/UX improvement roadmap for the VEIL VPN Windows Client, based on comprehensive code review of the GUI components.

## Current Status

### Recently Implemented Features
- Modern dark theme with glassmorphism design
- Animated status indicator with state-based colors
- System tray integration with context menu
- Settings page with comprehensive configuration options
- Diagnostics view for protocol metrics
- Update checker with notification dialog
- Service auto-start on Windows

### Architecture
- Qt-based GUI (`src/gui-client/`)
- IPC communication with daemon (`ipc_client_manager.cpp`)
- Windows service integration (`src/windows/`)

---

## Phase 1: UI Polish & User Feedback (P0 - Critical)

### 1.1 Add Loading States for Async Operations
**Priority:** P0
**Impact:** High UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Several async operations lack visual feedback:
- Service installation (shows dialog but no progress)
- Settings save (brief "Saved!" text, but no loading state during save)
- Update check (status bar message only)
- Initial daemon connection attempts

**Current Code References:**
- `mainwindow.cpp:132-158` - Initial daemon connection with `QTimer::singleShot`
- `mainwindow.cpp:1129-1296` - `ensureServiceRunning()` with multiple async waits
- `settings_widget.cpp:699-762` - `saveSettings()` with delayed button text reset

**Solution:**
1. Add progress indicator (spinning loader) during service operations
2. Add loading overlay during settings save
3. Add connection progress bar during daemon connection attempts

---

### 1.2 Improve Error Message Presentation
**Priority:** P0
**Impact:** High UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Error messages are technical and lack actionable guidance:
- `connection_widget.cpp:675-678` - Generic timeout message
- `mainwindow.cpp:557-559` - Error shown in status bar (easy to miss)
- Error label styling is functional but could be more prominent

**Solution:**
1. Add error categorization (network, configuration, permission)
2. Provide actionable steps for common errors
3. Add "Copy Error Details" button for support
4. Show error notifications in system tray

---

### 1.3 Add Input Validation Feedback Improvements
**Priority:** P0
**Impact:** Medium UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Settings validation shows errors but lacks positive feedback:
- `settings_widget.cpp:571-637` - `validateSettings()` only shows errors
- No indication when fields are valid
- No real-time validation during typing

**Solution:**
1. Add green checkmark for valid fields
2. Add debounced real-time validation (200ms delay)
3. Add validation summary before save

---

## Phase 2: Accessibility & Usability (P1 - High)

### 2.1 Add Keyboard Navigation Support
**Priority:** P1
**Impact:** High accessibility improvement
**Effort:** Medium
**Dependencies:** None

**Problem:** Limited keyboard navigation:
- `mainwindow.cpp:659-673` - Only few shortcuts defined (Ctrl+1/2/3, Ctrl+M, Ctrl+Enter, Ctrl+D)
- No focus indicators in dark theme
- Tab order not explicitly set

**Solution:**
1. Add focus ring styling for all interactive elements
2. Ensure logical tab order throughout the app
3. Add keyboard shortcuts for common actions:
   - `Escape` - Cancel/Back
   - `Space` - Toggle connection
   - `Ctrl+S` - Save settings
4. Add shortcut hints in tooltips

---

### 2.2 Add High-DPI Support
**Priority:** P1
**Impact:** Medium UX improvement
**Effort:** Medium
**Dependencies:** None

**Problem:** Fixed pixel sizes may not scale well:
- `connection_widget.cpp:21` - `setFixedSize(160, 160)` for status ring
- `theme.h:66-73` - Fixed font sizes
- `mainwindow.cpp:168-169` - Fixed minimum window size `480x720`

**Solution:**
1. Use `Qt::AA_EnableHighDpiScaling`
2. Convert fixed pixel sizes to DPI-aware values
3. Test on 125%, 150%, 200% scaling
4. Add SVG icons instead of raster images

---

### 2.3 Add Light Theme Support
**Priority:** P1
**Impact:** Medium UX improvement
**Effort:** Medium
**Dependencies:** None

**Problem:** Only dark theme available:
- `theme.h:42-58` - Light theme colors defined but unused
- `theme.h:507-560` - `getLightThemeStylesheet()` exists but not used
- No theme toggle in settings

**Solution:**
1. Add theme selection to settings (Dark/Light/System)
2. Implement system theme detection
3. Apply light theme stylesheet
4. Persist theme preference

---

### 2.4 Improve Settings Organization
**Priority:** P1
**Impact:** Medium UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Settings page is long with many sections:
- 7 sections in a single scroll view
- No clear visual hierarchy
- TUN Interface settings are advanced but shown with basic settings

**Solution:**
1. Add collapsible sections
2. Group settings into tabs (Basic, Network, Security, Advanced)
3. Add "Simple/Advanced" toggle to hide complex options
4. Add search/filter for settings

---

## Phase 3: Feature Enhancements (P2 - Medium)

### 3.1 Add Connection Statistics History
**Priority:** P2
**Impact:** Medium UX improvement
**Effort:** Medium
**Dependencies:** Diagnostics infrastructure

**Problem:** No historical data for connection stats:
- `connection_widget.cpp:622-630` - Only current metrics shown
- No graphs or trends
- Connection history not persisted

**Solution:**
1. Add connection history view (last 10 connections)
2. Add bandwidth graphs (last 5 minutes)
3. Add latency trend graph
4. Export connection logs

---

### 3.2 Add Server Selection/Favorites
**Priority:** P2
**Impact:** High UX improvement
**Effort:** Medium
**Dependencies:** Settings refactoring

**Problem:** Only single server configuration:
- `settings_widget.cpp:136-173` - Single server address field
- No server list or quick switching
- No ping/latency preview

**Solution:**
1. Add server list management (add/edit/remove)
2. Add server quick-switch dropdown on main screen
3. Add server latency preview
4. Add "Favorite" marking
5. Add server import from URL/QR code

---

### 3.3 Add Onboarding Flow
**Priority:** P2
**Impact:** High UX improvement
**Effort:** Medium
**Dependencies:** None

**Problem:** New users have no guidance:
- Must manually configure server and key file
- No introduction to features
- No configuration import

**Solution:**
1. Add first-run setup wizard:
   - Welcome screen
   - Server configuration
   - Key file setup (with generate option)
   - Finish with test connection
2. Add configuration import (file, URL, clipboard)
3. Add help tooltips on first run

---

### 3.4 Add Quick Actions Widget
**Priority:** P2
**Impact:** Medium UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Common actions require multiple clicks:
- Must open settings to change server
- Must go to diagnostics for debug info
- No quick toggle for features

**Solution:**
1. Add expandable quick actions panel:
   - Kill switch toggle
   - Obfuscation mode quick switch
   - Copy IP address
   - Share connection status

---

## Phase 4: Advanced Features (P3 - Low)

### 4.1 Add Split Tunnel App Selection
**Priority:** P3
**Impact:** Medium UX improvement
**Effort:** High
**Dependencies:** Routing infrastructure

**Problem:** Split tunnel is route-based only:
- `settings_widget.cpp:275-312` - Only CIDR route input
- No per-application routing

**Solution:**
1. Add installed apps browser
2. Add per-app VPN inclusion/exclusion
3. Show app network usage stats

---

### 4.2 Add Multi-Language Support
**Priority:** P3
**Impact:** Medium UX improvement
**Effort:** High
**Dependencies:** None

**Problem:** English-only interface:
- `translations/` folder exists but empty
- No language selection
- Hardcoded strings throughout

**Solution:**
1. Extract all strings to translation files
2. Add language selection in settings
3. Initial languages: English, Russian, Chinese
4. Add translation contribution guide

---

### 4.3 Add Notification Customization
**Priority:** P3
**Impact:** Low UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** Fixed notification behavior:
- `mainwindow.cpp:994-999` - Minimize to tray always shows notification
- `system_tray.cpp:116-123` - Fixed notification style

**Solution:**
1. Add notification preferences in settings:
   - Enable/disable notifications
   - Notification sound
   - Show/hide details
2. Add notification history view

---

## Phase 5: Quality of Life (P4 - Nice to Have)

### 5.1 Add Connection Auto-Start
**Priority:** P4
**Impact:** Medium UX improvement
**Effort:** Low
**Dependencies:** Windows startup integration

**Problem:** Must manually connect each time:
- No "Connect on startup" option
- No "Connect when network available" option

**Solution:**
1. Add "Start minimized" option
2. Add "Auto-connect on startup" option
3. Add "Auto-connect on insecure network" option

---

### 5.2 Add Data Usage Tracking
**Priority:** P4
**Impact:** Low UX improvement
**Effort:** Medium
**Dependencies:** Statistics infrastructure

**Problem:** No data usage tracking:
- Current session bytes shown but not persisted
- No daily/monthly usage stats

**Solution:**
1. Track daily/monthly data usage
2. Add usage graph visualization
3. Add optional usage alerts
4. Export usage reports

---

### 5.3 Add Shortcut to Desktop/Start Menu
**Priority:** P4
**Impact:** Low UX improvement
**Effort:** Low
**Dependencies:** None

**Problem:** No shortcut creation during install
- User must navigate to installation folder

**Solution:**
1. Add desktop shortcut creation option
2. Add Start Menu entry
3. Add "Pin to Taskbar" helper

---

## Implementation Order and Dependencies

```
Phase 1 (P0 - Immediate):
  1.1 Loading States ────────┐
  1.2 Error Messages ────────┼─→ Baseline UX improvement
  1.3 Validation Feedback ───┘

Phase 2 (P1 - High):
  2.1 Keyboard Navigation ───┐
  2.2 High-DPI Support ──────┼─→ Accessibility
  2.3 Light Theme ───────────┤
  2.4 Settings Organization ─┘

Phase 3 (P2 - Medium):
  3.1 Stats History ─────────┐
  3.2 Server Selection ──────┼─→ Feature enhancements
  3.3 Onboarding Flow ───────┤
  3.4 Quick Actions ─────────┘

Phase 4 (P3 - Low):
  4.1 Split Tunnel Apps ─────┐
  4.2 Multi-Language ────────┼─→ Advanced features
  4.3 Notifications ─────────┘

Phase 5 (P4 - Nice to Have):
  5.1 Auto-Start ────────────┐
  5.2 Data Tracking ─────────┼─→ Quality of life
  5.3 Shortcuts ─────────────┘
```

## UX Metrics to Track

| Metric | Target |
|--------|--------|
| Time to first connection | < 60 seconds |
| Error message clarity | User understands action needed |
| Setting discoverability | All features accessible |
| Accessibility compliance | WCAG 2.1 AA |
| First-run completion rate | > 90% |

## Testing Strategy

1. **Usability Testing:** Test with real users for first-run experience
2. **Accessibility Testing:** Screen reader compatibility, keyboard navigation
3. **Visual Testing:** High-DPI, different window sizes
4. **Localization Testing:** RTL languages, character limits

## References

- [Issue #76](https://github.com/VisageDvachevsky/veil-windows-client/issues/76) - Performance and UX improvements
- [Qt High DPI Guide](https://doc.qt.io/qt-5/highdpi.html)
- [WCAG 2.1 Guidelines](https://www.w3.org/WAI/WCAG21/quickref/)
