# Authenticode Signature Verification Experiment

## Overview

This document describes the Windows Authenticode signature verification implementation added to the auto-updater to address issue #222 (Critical: Auto-updater accepts unsigned executables without integrity verification).

## Implementation Details

### Location
- **File**: `src/common/updater/auto_updater.cpp`
- **Function**: `verify_authenticode_signature(const std::string& file_path, std::string& error)`

### How It Works

The implementation uses the Windows `WinVerifyTrust` API to verify executable signatures:

1. **File Path Conversion**: Converts the file path to a wide string for Windows API compatibility
2. **Structure Initialization**: Sets up `WINTRUST_FILE_INFO` and `WINTRUST_DATA` structures
3. **Verification Settings**:
   - `WTD_UI_NONE`: No user interface during verification
   - `WTD_REVOKE_WHOLECHAIN`: Checks certificate revocation for entire chain
   - `WTD_SAFER_FLAG`: Uses safer validation mode
4. **Result Handling**: Provides detailed error messages for different failure scenarios

### Error Codes Handled

| Code | Meaning | User Message |
|------|---------|--------------|
| `ERROR_SUCCESS` | Valid signature | Signature verified successfully |
| `TRUST_E_NOSIGNATURE` | No signature found | File is not signed |
| `TRUST_E_EXPLICIT_DISTRUST` | Signature distrusted | Explicitly distrusted by admin |
| `TRUST_E_SUBJECT_NOT_TRUSTED` | Chain validation failed | Certificate chain validation failed |
| `CRYPT_E_SECURITY_SETTINGS` | Security settings issue | May need admin privileges |
| `TRUST_E_BAD_DIGEST` | File tampered | Hash mismatch (file modified) |

### Integration Points

The signature verification is integrated at two critical points:

1. **After Download** (`download_update` method):
   - Verifies signature immediately after successful download
   - Runs after SHA-256 checksum verification (if provided)
   - Prevents storing malicious files on disk

2. **Before Installation** (`install_update` method):
   - Final safety check before executing installer
   - Prevents running unsigned/untrusted executables
   - Provides defense-in-depth protection

## Testing

### Unit Tests
Located in `tests/unit/auto_updater_tests.cpp`:

1. **InstallUpdateRejectsNonexistentFile**: Verifies rejection of non-existent files
2. **InstallUpdateRejectsUnsignedFile**: Verifies rejection of unsigned executables

### Manual Testing (Windows Only)

To test with a real signed executable:

```cpp
#include "common/updater/auto_updater.h"
#include <iostream>

int main() {
    veil::updater::AutoUpdater updater;
    std::string error;

    // Test with Windows system executable (always signed)
    bool result = updater.install_update("C:\\Windows\\System32\\calc.exe", error);

    if (result) {
        std::cout << "Signature verified successfully!" << std::endl;
    } else {
        std::cout << "Verification failed: " << error << std::endl;
    }

    return 0;
}
```

To test with an unsigned executable:

```cpp
// Create a minimal unsigned test file
std::ofstream test_file("unsigned.exe", std::ios::binary);
test_file << "MZ\x90\x00";  // Minimal DOS header
test_file.close();

veil::updater::AutoUpdater updater;
std::string error;
bool result = updater.install_update("unsigned.exe", error);

// Should fail with "File is not signed" message
assert(!result);
assert(error.find("not signed") != std::string::npos);
```

## Security Benefits

1. **Supply Chain Protection**: Prevents installation of malicious updates from compromised sources
2. **MITM Defense**: Detects executables modified during download
3. **Defense in Depth**: Multiple verification layers (checksum + signature)
4. **Certificate Chain Validation**: Verifies entire trust chain, not just file signature
5. **Revocation Checking**: Checks if signing certificates have been revoked

## Limitations

1. **Windows Only**: Authenticode verification is Windows-specific
2. **Requires Valid Certificate**: Installers must be properly code-signed
3. **Network Dependency**: Revocation checking may require internet access to certificate authority
4. **Admin Privileges**: Some verification scenarios may require elevated privileges

## Future Enhancements

Potential improvements for future versions:

1. **Certificate Pinning**: Pin expected signing certificate thumbprint
2. **Timestamp Verification**: Verify signature timestamp to detect expired certificates
3. **Custom Trust Store**: Allow specifying custom trusted certificate store
4. **Detailed Certificate Info**: Log signer information for audit purposes
5. **Signature Caching**: Cache verification results to reduce redundant checks

## References

- [Microsoft WinVerifyTrust Documentation](https://docs.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)
- [Authenticode Overview](https://docs.microsoft.com/en-us/windows/win32/seccrypto/authenticode)
- [CWE-494: Download of Code Without Integrity Check](https://cwe.mitre.org/data/definitions/494.html)
- [OWASP: Insecure Software Update](https://owasp.org/www-community/vulnerabilities/Insecure_Software_Update)
