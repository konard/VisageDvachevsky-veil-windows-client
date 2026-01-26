# Windows TLS/SSL Setup Guide for VEIL VPN Client

## Overview

The VEIL VPN Client GUI uses Qt6 Network for checking updates via HTTPS. On Windows, Qt6 requires a TLS/SSL backend to be available for HTTPS connections.

## Understanding the TLS Warning

If you see warnings like:
```
qt.network.ssl: No functional TLS backend was found
qt.network.ssl: No TLS backend is available
QSslSocket::connectToHostEncrypted: TLS initialization failed
```

**Important:** These warnings only affect the update checker feature. The VPN tunnel itself uses the VEIL protocol and is NOT affected by missing TLS support in Qt.

## Qt6 TLS Backends on Windows

Qt6 on Windows supports two TLS backends:

1. **Windows Schannel** (native, recommended)
   - Built into Windows
   - No additional files needed
   - Available in Qt6 6.2+

2. **OpenSSL** (requires DLLs)
   - Requires libssl-3-x64.dll and libcrypto-3-x64.dll
   - Must be distributed with the application

## Checking Your TLS Backend Status

When you run the VEIL VPN Client, it will print diagnostic information at startup:

```
=== VEIL VPN Client Startup ===
Qt Version: 6.x.x
Application Version: 0.1.0
Qt Network SSL Support: true/false
Available TLS backends: [...]
Active TLS backend: schannel/openssl/none
===============================
```

## Solutions

### Option 1: Use Qt6 Built with Schannel Support (Recommended)

If Qt6 was built with Schannel support, it should work automatically on Windows without any additional files.

**To verify your Qt build has Schannel:**
```powershell
# Check Qt configuration
qmake -query
# Look for ssl-related configuration in Qt build
```

### Option 2: Add OpenSSL DLLs

If you need OpenSSL support:

1. Download OpenSSL 3.x for Windows:
   - From https://github.com/openssl/openssl/releases
   - Or from https://slproweb.com/products/Win32OpenSSL.html

2. Copy these DLLs to your VEIL installation directory:
   - `libssl-3-x64.dll`
   - `libcrypto-3-x64.dll`

3. Restart the VEIL VPN Client

### Option 3: Use Windows Builds with Bundled SSL

Official VEIL VPN releases for Windows should include proper TLS support. If you built from source:

1. Ensure Qt6 was configured with Schannel or OpenSSL support
2. For Schannel: Qt must be built with `-schannel` flag
3. For OpenSSL: Bundle the DLLs with your build

## Building Qt6 with Schannel Support

If you're building Qt6 from source on Windows:

```powershell
configure.bat -schannel -ssl -openssl-runtime
```

## Troubleshooting

### TLS Backend Not Available

**Symptom:** Update checks fail with SSL errors

**Solutions:**
1. Check if you're using an official VEIL build (should have SSL bundled)
2. Add OpenSSL DLLs to installation directory
3. Rebuild Qt6 with Schannel support

### Service Start Failures

**Symptom:** "Failed to start VEIL service"

**Solutions:**
1. Run VEIL Client as Administrator
2. Check Windows Event Viewer for service errors
3. Review console output with detailed logging
4. Verify veil-service.exe exists in installation directory

**Check service status:**
```powershell
# Check if service is installed
sc query VeilVPN

# Check service configuration
sc qc VeilVPN

# View service logs in Event Viewer
eventvwr.msc
# Navigate to: Windows Logs > Application
# Look for events from source "VeilVPN"
```

## Enabling Debug Output

To see detailed diagnostic logs:

1. Run VEIL Client from command line:
```powershell
cd "C:\Program Files\VEIL VPN"
.\veil-client-gui.exe
```

2. Check the console output for detailed logs about:
   - TLS backend status
   - Service installation/startup
   - Daemon connection attempts
   - Any error messages

## Additional Resources

- Qt6 SSL Documentation: https://doc.qt.io/qt-6/ssl.html
- Qt6 Windows Deployment: https://doc.qt.io/qt-6/windows-deployment.html
- VEIL VPN GitHub Issues: https://github.com/VisageDvachevsky/veil-core/issues

## Note for Developers

When building Windows releases:

1. Ensure Qt6 is built with Schannel support OR
2. Bundle OpenSSL 3.x DLLs with the installer
3. Update deployment scripts to include SSL libraries
4. Test SSL support before release: check `QSslSocket::supportsSsl()`

Example CMake configuration for Qt deployment:
```cmake
# Deploy Qt6 with SSL support
if(WIN32)
    # Use windeployqt to deploy Qt dependencies
    # For Schannel: no extra action needed if Qt built with it
    # For OpenSSL: bundle libssl-3-x64.dll and libcrypto-3-x64.dll
endif()
```
