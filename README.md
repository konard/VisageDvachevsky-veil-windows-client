# VEIL

[![CI](https://github.com/VisageDvachevsky/veil-core/actions/workflows/ci.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/ci.yml)
[![Unit Tests](https://github.com/VisageDvachevsky/veil-core/actions/workflows/unit-tests.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/unit-tests.yml)
[![Integration Tests](https://github.com/VisageDvachevsky/veil-core/actions/workflows/integration-tests.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/integration-tests.yml)
[![Security Tests](https://github.com/VisageDvachevsky/veil-core/actions/workflows/security-tests.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/security-tests.yml)
[![Network Emulation](https://github.com/VisageDvachevsky/veil-core/actions/workflows/network-emulation.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/network-emulation.yml)
[![CodeQL](https://github.com/VisageDvachevsky/veil-core/actions/workflows/codeql.yml/badge.svg)](https://github.com/VisageDvachevsky/veil-core/actions/workflows/codeql.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

A secure, high-performance UDP-based VPN with cryptographic handshakes, encrypted data transfer, and advanced traffic obfuscation to resist deep packet inspection (DPI).

## Features

- **Encrypted transport** — X25519 key exchange, ChaCha20-Poly1305 AEAD, HKDF key derivation
- **Traffic obfuscation** — Variable packet sizes, timing jitter, protocol wrappers (TLS, HTTP, QUIC-like)
- **Reliable UDP** — Selective ACK, retransmission, multiplexing, congestion control
- **Cross-platform** — Linux CLI + Qt6 GUI, Windows client with installer
- **IPC architecture** — Separated daemon/GUI with named-pipe communication
- **Auto-updater** — GitHub Releases integration with signature verification

## Quick Installation

### ⚠ Security Notice

The `curl | sudo bash` installation pattern is **NOT RECOMMENDED** as it provides no integrity verification. We strongly recommend using the secure installation method below.

### Client Installation (Recommended Secure Method)

```bash
# Download installer script
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh

# Download checksum
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256

# Verify integrity
sha256sum -c install_client.sh.sha256

# Review the script (IMPORTANT!)
less install_client.sh

# Install CLI-only client (with interactive setup wizard)
sudo bash install_client.sh

# Or install client with Qt6 GUI
sudo bash install_client.sh --with-gui
```

The installer includes an **interactive setup wizard** that guides you through server configuration and key file setup.

Run the wizard anytime after installation:

```bash
sudo /usr/local/share/veil/setup-wizard.sh
```

### Server Installation (Recommended Secure Method)

```bash
# Download installer script
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh -o install_veil.sh

# Download checksum
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh.sha256 -o install_veil.sh.sha256

# Verify integrity
sha256sum -c install_veil.sh.sha256

# Review the script (IMPORTANT!)
less install_veil.sh

# Install CLI-only server
sudo bash install_veil.sh

# Or install server with Qt6 GUI monitoring dashboard
sudo bash install_veil.sh --with-gui
```

<details>
<summary><strong>Alternative: One-Line Installation (NOT RECOMMENDED)</strong></summary>

⚠️ **Warning:** This method skips integrity verification and is vulnerable to man-in-the-middle attacks.

```bash
# Client
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh | sudo bash

# Server
curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh | sudo bash
```

The scripts will display a security warning and give you 10 seconds to abort before proceeding without integrity verification.
</details>

## Building from Source

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler (GCC 11+ or Clang 14+)
- libsodium
- Qt6 (optional, for GUI applications)

### Build Client Script

```bash
# Build CLI-only client
./build_client.sh

# Build client with Qt6 GUI
./build_client.sh --with-gui

# Clean build and install
./build_client.sh --with-gui --clean --install
```

### Manual Build Commands

```bash
# Configure and build debug version
cmake --preset debug
cmake --build build/debug -j$(nproc)

# Configure and build release version
cmake --preset release
cmake --build build/release -j$(nproc)
```

## Testing

### Running Unit and Integration Tests

```bash
# Run all tests
ctest --preset debug --output-on-failure

# Run specific test suite
./build/debug/tests/unit/veil_unit_tests --gtest_filter="PacketTests.*"
./build/debug/tests/integration/veil_integration_transport
```

### Network Emulation Testing (netem)

The integration tests support network emulation via Linux TC for testing under realistic network conditions. Netem tests require root privileges.

```bash
# Setup: add 50ms delay with 10ms jitter and 1% loss on loopback
sudo tc qdisc add dev lo root netem delay 50ms 10ms loss 1%

# Run netem tests (requires root and netem setup)
unset VEIL_SKIP_NETEM
sudo ./build/debug/tests/integration/veil_integration_transport

# Cleanup
sudo tc qdisc del dev lo root
```

## Troubleshooting

### Windows TLS/SSL Warnings

If you see Qt SSL/TLS warnings on Windows like:
```
qt.network.ssl: No functional TLS backend was found
```

**Note:** These warnings only affect the update checker feature. The VPN tunnel itself is NOT affected.

For detailed solutions, see [Windows TLS Setup Guide](docs/windows-tls-setup.md).

### Windows Service Issues

If the VEIL service fails to start:
1. Run VEIL Client as Administrator
2. Check Windows Event Viewer: `eventvwr.msc` > Windows Logs > Application
3. Run from command line to see detailed logs:
   ```powershell
   cd "C:\Program Files\VEIL VPN"
   .\veil-client-gui.exe
   ```
4. Verify service status:
   ```powershell
   sc query VeilVPN
   ```

## Project Structure

```
src/
├── common/
│   ├── auth/               # Authentication (PSK)
│   ├── cli/                # CLI argument parsing
│   ├── config/             # Configuration parsing
│   ├── crypto/             # Cryptographic primitives (AEAD, HKDF, X25519)
│   ├── daemon/             # Daemon lifecycle management
│   ├── gui/                # Shared GUI utilities
│   ├── handshake/          # Handshake protocol implementation
│   ├── ipc/                # IPC protocol for GUI/daemon communication
│   ├── logging/            # Logging utilities
│   ├── metrics/            # Performance metrics collection
│   ├── obfuscation/        # Traffic obfuscation (size/timing variance)
│   ├── packet/             # Packet builder and parser
│   ├── protocol_wrapper/   # Protocol wrappers (TLS, HTTP, QUIC-like)
│   ├── session/            # Session management and rotation
│   ├── signal/             # Signal handling
│   ├── updater/            # Auto-update from GitHub Releases
│   └── utils/              # Utilities (random, rate limiting)
├── transport/
│   ├── event_loop/         # I/O event loop
│   ├── mux/                # Multiplexing codec and retransmission
│   ├── pipeline/           # Packet processing pipeline
│   ├── session/            # Transport session management
│   ├── stats/              # Transport statistics
│   └── udp_socket/         # UDP socket wrapper
├── tun/                    # TUN device abstraction
├── tunnel/                 # VPN tunnel logic
├── windows/                # Windows-specific code (service, firewall, shortcuts)
├── client/                 # CLI client application
├── server/                 # CLI server application
├── gui-client/             # Qt6 GUI client (optional)
├── gui-server/             # Qt6 GUI server (optional)
└── tools/                  # Development and debugging tools
tests/
├── unit/                   # Unit tests (Google Test)
└── integration/            # Integration tests
```

## Documentation

- [Architecture Overview](docs/architecture_overview.md)
- [Windows Architecture](docs/windows_architecture.md)
- [DPI Bypass Modes](docs/dpi_bypass_modes.md)
- [Protocol Wrappers](docs/protocol_wrappers.md)
- [Thread Model](docs/thread_model.md)
- [Configuration](docs/configuration.md)
- [Deployment Guide](docs/deployment_guide.md)
- [Reliability](docs/reliability.md)
- [Translations](docs/TRANSLATIONS.md)
- [Windows TLS Setup](docs/windows-tls-setup.md)

## License

[Apache License 2.0](LICENSE)
