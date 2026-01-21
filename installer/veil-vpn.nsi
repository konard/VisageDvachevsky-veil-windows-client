; VEIL VPN Windows Installer Script (NSIS)
;
; This script creates a Windows installer for VEIL VPN client.
; It handles:
; - Installing application files (fresh install or upgrade)
; - Detecting and upgrading existing installations
; - Installing Wintun driver
; - Creating Start Menu shortcuts
; - Creating Desktop shortcut
; - Registering the Windows service
; - Setting up uninstaller
; - Preserving user settings during upgrades
;
; Build with: makensis veil-vpn.nsi

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "WinVer.nsh"
!include "WordFunc.nsh"

; ============================================================================
; General Configuration
; ============================================================================

!define PRODUCT_NAME "VEIL VPN"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_VERSION_MAJOR 1
!define PRODUCT_VERSION_MINOR 0
!define PRODUCT_VERSION_PATCH 0
!define PRODUCT_PUBLISHER "VEIL Project"
!define PRODUCT_WEB_SITE "https://github.com/VisageDvachevsky/veil-windows-client"
!define PRODUCT_UPDATE_URL "https://api.github.com/repos/VisageDvachevsky/veil-windows-client/releases/latest"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\veil-client-gui.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!define PRODUCT_SETTINGS_KEY "Software\VEIL VPN"

; Installer attributes
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "veil-vpn-${PRODUCT_VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\VEIL VPN"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

; Compression
SetCompressor /SOLID lzma

; Version information in executable
VIProductVersion "${PRODUCT_VERSION_MAJOR}.${PRODUCT_VERSION_MINOR}.${PRODUCT_VERSION_PATCH}.0"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} Installer"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) VEIL Project"

; Variables for upgrade detection
Var PREVIOUS_VERSION
Var PREVIOUS_INSTDIR
Var IS_UPGRADE

; ============================================================================
; Modern UI Configuration
; ============================================================================

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Header image and branding
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT

; Welcome page - custom text for upgrade scenario
!define MUI_WELCOMEPAGE_TITLE_3LINES
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ${PRODUCT_NAME}.$\r$\n$\r$\nIf you already have VEIL VPN installed, this installer will upgrade your existing installation while preserving your settings.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

; License page
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"

; Upgrade detection page (custom)
Page custom UpgradePageCreate UpgradePageLeave

; Directory page (skip if upgrading to same location)
!define MUI_PAGE_CUSTOMFUNCTION_PRE DirectoryPagePre
!insertmacro MUI_PAGE_DIRECTORY

; Components page
!define MUI_PAGE_CUSTOMFUNCTION_PRE ComponentsPagePre
!insertmacro MUI_PAGE_COMPONENTS

; Install files page
!insertmacro MUI_PAGE_INSTFILES

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\veil-client-gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME}"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View release notes"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION ShowReleaseNotes
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

; ============================================================================
; Custom Page Functions for Upgrade Detection
; ============================================================================

Function UpgradePageCreate
  ; Only show if an existing installation is detected
  ${If} $IS_UPGRADE != "1"
    Abort ; Skip this page
  ${EndIf}

  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ; Title
  ${NSD_CreateLabel} 0 0 100% 20u "Existing Installation Detected"
  Pop $0
  CreateFont $1 "$(^Font)" "12" "700"
  SendMessage $0 ${WM_SETFONT} $1 0

  ; Message
  ${NSD_CreateLabel} 0 30u 100% 50u "VEIL VPN version $PREVIOUS_VERSION is already installed at:$\r$\n$\r$\n$PREVIOUS_INSTDIR$\r$\n$\r$\nThe installer will upgrade your existing installation. Your settings and configuration will be preserved."
  Pop $0

  ; Radio buttons for upgrade options
  ${NSD_CreateRadioButton} 0 90u 100% 12u "Upgrade existing installation (recommended)"
  Pop $R1
  ${NSD_Check} $R1

  ${NSD_CreateRadioButton} 0 106u 100% 12u "Install to a different location"
  Pop $R2

  ${NSD_CreateCheckBox} 0 130u 100% 12u "Backup existing settings before upgrade"
  Pop $R3
  ${NSD_Check} $R3

  nsDialogs::Show
FunctionEnd

Function UpgradePageLeave
  ; Check which option was selected
  ${NSD_GetState} $R1 $0
  ${If} $0 == ${BST_CHECKED}
    ; Use existing install directory
    StrCpy $INSTDIR $PREVIOUS_INSTDIR
  ${EndIf}

  ; Check if backup is requested
  ${NSD_GetState} $R3 $0
  ${If} $0 == ${BST_CHECKED}
    ; Backup settings
    Call BackupSettings
  ${EndIf}
FunctionEnd

Function DirectoryPagePre
  ; If upgrading and user chose to upgrade in place, skip directory selection
  ${NSD_GetState} $R1 $0
  ${If} $IS_UPGRADE == "1"
  ${AndIf} $0 == ${BST_CHECKED}
    Abort ; Skip this page
  ${EndIf}
FunctionEnd

Function ComponentsPagePre
  ; Nothing to customize, but function must exist
FunctionEnd

Function BackupSettings
  ; Backup QSettings from registry
  DetailPrint "Backing up user settings..."

  ; Create backup directory
  CreateDirectory "$INSTDIR\backup"

  ; Export registry settings to file
  nsExec::ExecToLog 'reg export "HKCU\Software\VEIL VPN" "$INSTDIR\backup\settings.reg" /y'

  ; Backup any config files
  ${If} ${FileExists} "$INSTDIR\config\*.*"
    CopyFiles /SILENT "$INSTDIR\config\*.*" "$INSTDIR\backup\config\"
  ${EndIf}

  DetailPrint "Settings backup complete"
FunctionEnd

Function ShowReleaseNotes
  ; Open release notes in browser
  ExecShell "open" "${PRODUCT_WEB_SITE}/releases"
FunctionEnd

; ============================================================================
; Installer Sections
; ============================================================================

Section "VEIL VPN Client (required)" SecMain
  SectionIn RO

  ; Stop running instance if upgrading
  ${If} $IS_UPGRADE == "1"
    DetailPrint "Stopping any running VEIL VPN instances..."
    ; Try to gracefully close the GUI
    FindWindow $0 "" "VEIL VPN Client"
    ${If} $0 != 0
      SendMessage $0 ${WM_CLOSE} 0 0
      Sleep 2000
    ${EndIf}
    ; Force kill if still running
    nsExec::ExecToLog 'taskkill /F /IM veil-client-gui.exe'
  ${EndIf}

  SetOutPath "$INSTDIR"
  SetOverwrite on

  ; Write version information
  FileOpen $0 "$INSTDIR\version.txt" w
  FileWrite $0 "${PRODUCT_VERSION}$\r$\n"
  FileWrite $0 "Installed: $\r$\n"
  FileClose $0

  ; Main application files
  File "bin\veil-client-gui.exe"
  ; Note: veil-client.exe and veil-service.exe are not built on Windows
  ; They require the transport layer which is currently Linux-only

  ; Qt DLLs (if not using static build)
  File /nonfatal "bin\Qt6Core.dll"
  File /nonfatal "bin\Qt6Gui.dll"
  File /nonfatal "bin\Qt6Widgets.dll"
  File /nonfatal "bin\Qt6Network.dll"

  ; Qt plugins
  SetOutPath "$INSTDIR\platforms"
  File /nonfatal "bin\platforms\qwindows.dll"

  SetOutPath "$INSTDIR\styles"
  File /nonfatal "bin\styles\qwindowsvistastyle.dll"

  ; Other dependencies
  SetOutPath "$INSTDIR"
  File /nonfatal "bin\libsodium.dll"
  File /nonfatal "bin\spdlog.dll"
  File /nonfatal "bin\fmt.dll"

  ; Documentation
  SetOutPath "$INSTDIR\docs"
  File "docs\README.md"
  File /nonfatal "docs\CHANGELOG.md"

  ; Configuration templates
  SetOutPath "$INSTDIR\config"
  File /nonfatal "config\client.json.example"

  ; Translations
  SetOutPath "$INSTDIR\translations"
  File /nonfatal "translations\veil_*.qm"

  ; Write installation directory to registry
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\veil-client-gui.exe"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Add/Remove Programs entry
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\veil-client-gui.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLUpdateInfo" "${PRODUCT_WEB_SITE}/releases"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoRepair" 1
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "VersionMajor" "${PRODUCT_VERSION_MAJOR}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "VersionMinor" "${PRODUCT_VERSION_MINOR}"

  ; Calculate and store installed size
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "EstimatedSize" "$0"

  ; Store version in application settings for GUI access
  WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "Version" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${PRODUCT_SETTINGS_KEY}" "InstallPath" "$INSTDIR"

SectionEnd

Section "Wintun Driver" SecWintun
  SetOutPath "$INSTDIR\driver"

  ; Download Wintun if not bundled
  ; For production, the driver should be bundled
  File "driver\wintun.dll"

  ; Copy to System32 for system-wide access
  ${If} ${RunningX64}
    CopyFiles /SILENT "$INSTDIR\driver\wintun.dll" "$SYSDIR\wintun.dll"
  ${EndIf}

SectionEnd

; Windows Service section disabled - veil-service.exe is not built on Windows yet
; Section "Windows Service" SecService
;   ; Install the Windows service
;   DetailPrint "Installing VEIL VPN Service..."
;   nsExec::ExecToLog '"$INSTDIR\veil-service.exe" --install'
;   Pop $0
;
;   ${If} $0 != 0
;     DetailPrint "Warning: Failed to install service (error code: $0)"
;     MessageBox MB_OK|MB_ICONEXCLAMATION "Failed to install Windows service. You may need to install it manually using Administrator privileges."
;   ${Else}
;     DetailPrint "Service installed successfully"
;
;     ; Start the service
;     DetailPrint "Starting VEIL VPN Service..."
;     nsExec::ExecToLog '"$INSTDIR\veil-service.exe" --start'
;     Pop $0
;   ${EndIf}
;
; SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\veil-client-gui.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\veil-client-gui.exe"
SectionEnd

Section "Auto-start with Windows" SecAutoStart
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${PRODUCT_NAME}" "$INSTDIR\veil-client-gui.exe --minimized"
SectionEnd

; ============================================================================
; Section Descriptions
; ============================================================================

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Core application files (required)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecWintun} "Wintun network driver for VPN connectivity"
  ; SecService disabled - service not built on Windows yet
  ; !insertmacro MUI_DESCRIPTION_TEXT ${SecService} "Install and start the VPN background service"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create Start Menu shortcuts"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create Desktop shortcut"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutoStart} "Automatically start VEIL VPN when Windows starts"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ============================================================================
; Uninstaller Section
; ============================================================================

Section "Uninstall"
  ; Note: Service removal disabled - veil-service.exe is not built on Windows yet
  ; Stop and remove the service
  ; DetailPrint "Stopping VEIL VPN Service..."
  ; nsExec::ExecToLog '"$INSTDIR\veil-service.exe" --stop'
  ;
  ; DetailPrint "Uninstalling VEIL VPN Service..."
  ; nsExec::ExecToLog '"$INSTDIR\veil-service.exe" --uninstall'

  ; Remove files
  Delete "$INSTDIR\veil-client-gui.exe"
  ; Note: veil-client.exe and veil-service.exe are not built on Windows
  ; Delete "$INSTDIR\veil-client.exe"
  ; Delete "$INSTDIR\veil-service.exe"
  Delete "$INSTDIR\Qt6Core.dll"
  Delete "$INSTDIR\Qt6Gui.dll"
  Delete "$INSTDIR\Qt6Widgets.dll"
  Delete "$INSTDIR\Qt6Network.dll"
  Delete "$INSTDIR\libsodium.dll"
  Delete "$INSTDIR\spdlog.dll"
  Delete "$INSTDIR\fmt.dll"
  Delete "$INSTDIR\uninstall.exe"

  ; Remove directories
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\docs"
  RMDir /r "$INSTDIR\config"
  RMDir /r "$INSTDIR\driver"
  RMDir /r "$INSTDIR\translations"
  RMDir "$INSTDIR"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  ; Remove registry entries
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "${PRODUCT_NAME}"

  ; Remove Wintun from System32 (only if we installed it)
  ${If} ${RunningX64}
    Delete "$SYSDIR\wintun.dll"
  ${EndIf}

  SetAutoClose true

SectionEnd

; ============================================================================
; Installer Functions
; ============================================================================

Function .onInit
  ; Initialize variables
  StrCpy $IS_UPGRADE "0"
  StrCpy $PREVIOUS_VERSION ""
  StrCpy $PREVIOUS_INSTDIR ""

  ; Check for admin rights
  UserInfo::GetAccountType
  Pop $0
  StrCmp $0 "Admin" +3
    MessageBox MB_OK|MB_ICONSTOP "Administrator privileges are required to install ${PRODUCT_NAME}."
    Abort

  ; Check for 64-bit Windows
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}

  ; Check Windows version (require Windows 10 or later)
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "${PRODUCT_NAME} requires Windows 10 or later."
    Abort
  ${EndIf}

  ; Check for existing installation
  ReadRegStr $PREVIOUS_VERSION ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion"
  ${If} $PREVIOUS_VERSION != ""
    ; Existing installation found
    StrCpy $IS_UPGRADE "1"

    ; Get install directory
    ReadRegStr $PREVIOUS_INSTDIR ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "InstallLocation"
    ${If} $PREVIOUS_INSTDIR == ""
      ; Try alternate registry location
      ReadRegStr $PREVIOUS_INSTDIR HKLM "${PRODUCT_DIR_REGKEY}" ""
      ; Extract directory from exe path
      ${GetParent} $PREVIOUS_INSTDIR $PREVIOUS_INSTDIR
    ${EndIf}

    ; Use previous install directory as default
    ${If} $PREVIOUS_INSTDIR != ""
      StrCpy $INSTDIR $PREVIOUS_INSTDIR
    ${EndIf}

    ; Compare versions
    ${VersionCompare} "${PRODUCT_VERSION}" "$PREVIOUS_VERSION" $0
    ${If} $0 == 0
      ; Same version - ask if user wants to reinstall
      MessageBox MB_ICONQUESTION|MB_YESNO "${PRODUCT_NAME} version $PREVIOUS_VERSION is already installed.$\r$\n$\r$\nDo you want to reinstall it?" IDYES +2
      Abort
    ${ElseIf} $0 == 2
      ; Downgrade warning
      MessageBox MB_ICONEXCLAMATION|MB_YESNO "A newer version ($PREVIOUS_VERSION) of ${PRODUCT_NAME} is already installed.$\r$\n$\r$\nDo you want to downgrade to version ${PRODUCT_VERSION}?" IDYES +2
      Abort
    ${Else}
      ; Upgrade - show upgrade message
      ; The custom upgrade page will handle the rest
    ${EndIf}
  ${EndIf}

FunctionEnd

Function un.onInit
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Are you sure you want to completely remove ${PRODUCT_NAME}?" IDYES +2
  Abort
FunctionEnd

Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK "${PRODUCT_NAME} has been successfully removed from your computer."
FunctionEnd
