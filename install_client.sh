#!/usr/bin/env bash
#
# VEIL Client One-Line Automated Installer
#
# This script performs a complete automated installation and configuration
# of VEIL Client on Ubuntu/Debian systems with optional Qt6 GUI.
#
# ⚠ SECURITY NOTICE: curl | bash installation is NOT RECOMMENDED
# The secure installation method verifies script integrity before execution.
#
# RECOMMENDED SECURE INSTALLATION:
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256
#   sha256sum -c install_client.sh.sha256
#   less install_client.sh  # Review the script (IMPORTANT!)
#   sudo bash install_client.sh [OPTIONS]
#
# One-line install (NOT RECOMMENDED - no integrity verification):
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh | sudo bash
#
# Options:
#   --build=debug|release  Build type (default: release)
#   --with-gui             Install Qt6 GUI client
#   --no-service           Don't create/start systemd service
#   --dry-run              Show what would be done without making changes
#   --help                 Show this help message
#
# Examples (SECURE METHOD - RECOMMENDED):
#   # Download and verify
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256
#   sha256sum -c install_client.sh.sha256
#
#   # Install CLI-only client
#   sudo bash install_client.sh
#
#   # Install client with Qt6 GUI
#   sudo bash install_client.sh --with-gui
#
#   # Install with debug build
#   sudo bash install_client.sh --build=debug --with-gui
#

set -e  # Exit on error
set -u  # Exit on undefined variable
set -o pipefail  # Exit on pipe failure

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Configuration variables
VEIL_REPO="${VEIL_REPO:-https://github.com/VisageDvachevsky/veil-core.git}"
VEIL_BRANCH="${VEIL_BRANCH:-main}"
INSTALL_DIR="/usr/local"
CONFIG_DIR="/etc/veil"
BUILD_DIR="/tmp/veil-client-build-$$"
LOG_DIR="/var/log/veil"

# Security: Expected SHA256 checksum of this script
# This is automatically updated by CI when the script changes
# To verify manually: sha256sum install_client.sh
EXPECTED_SHA256=""

# Security options
SKIP_INTEGRITY_CHECK="${SKIP_INTEGRITY_CHECK:-false}"  # For testing only, NOT recommended

# Installation options (can be overridden via command line)
BUILD_TYPE="${BUILD_TYPE:-release}"       # debug or release
WITH_GUI="${WITH_GUI:-false}"             # Install Qt6 GUI
CREATE_SERVICE="${CREATE_SERVICE:-true}"  # Create systemd service
DRY_RUN="${DRY_RUN:-false}"               # Dry run mode
VERBOSE="${VERBOSE:-false}"               # Verbose output

# Functions for colored output
log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

log_debug() {
    if [[ "$VERBOSE" == "true" ]]; then
        echo -e "${CYAN}[DEBUG]${NC} $*"
    fi
}

log_step() {
    echo -e "${MAGENTA}[STEP]${NC} ${BOLD}$*${NC}"
}

# ============================================================================
# SECURITY: INTEGRITY VERIFICATION
# ============================================================================

# Verify the integrity of this script using embedded SHA256 checksum
verify_script_integrity() {
    if [[ "$SKIP_INTEGRITY_CHECK" == "true" ]]; then
        log_warn "⚠ SECURITY WARNING: Script integrity check is DISABLED"
        log_warn "This is NOT recommended in production environments"
        return 0
    fi

    # If running from stdin (curl | bash), we can't verify
    if [[ ! -f "$0" || "$0" == "bash" || "$0" == "-bash" || "$0" == "sh" ]]; then
        echo ""
        echo -e "${RED}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║                    ⚠ SECURITY WARNING ⚠                               ║${NC}"
        echo -e "${RED}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        log_error "This script is being executed from stdin (curl | bash pattern)"
        log_error "This is INSECURE and prevents integrity verification!"
        echo ""
        log_warn "Recommended secure installation:"
        echo ""
        echo "  1. Download the script:"
        echo "     curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh"
        echo ""
        echo "  2. Download the checksum:"
        echo "     curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256"
        echo ""
        echo "  3. Verify integrity:"
        echo "     sha256sum -c install_client.sh.sha256"
        echo ""
        echo "  4. Review the script (IMPORTANT!):"
        echo "     less install_client.sh"
        echo ""
        echo "  5. Execute with sudo:"
        echo "     sudo bash install_client.sh [OPTIONS]"
        echo ""
        echo -e "${YELLOW}If you understand the risks and want to proceed anyway, you have 10 seconds to abort...${NC}"
        echo ""

        # Give user a chance to abort
        for i in {10..1}; do
            echo -ne "\rProceeding in ${i} seconds... (Press Ctrl+C to abort)"
            sleep 1
        done
        echo ""
        echo ""

        log_warn "Proceeding without integrity verification (NOT RECOMMENDED)"
        return 0
    fi

    # If EXPECTED_SHA256 is empty, warn but continue
    if [[ -z "$EXPECTED_SHA256" ]]; then
        log_warn "No embedded checksum found in script"
        log_warn "This may be a development version or the script hasn't been published yet"

        # Try to fetch checksum from GitHub
        log_info "Attempting to fetch checksum from GitHub..."
        local remote_checksum
        if remote_checksum=$(curl -sSL "https://raw.githubusercontent.com/VisageDvachevsky/veil-core/${VEIL_BRANCH}/install_client.sh.sha256" 2>/dev/null | awk '{print $1}'); then
            if [[ -n "$remote_checksum" ]]; then
                log_info "Fetched remote checksum: ${remote_checksum:0:16}..."
                EXPECTED_SHA256="$remote_checksum"
            fi
        fi

        if [[ -z "$EXPECTED_SHA256" ]]; then
            log_warn "Could not fetch remote checksum, skipping verification"
            return 0
        fi
    fi

    log_info "Verifying script integrity..."

    # Calculate actual SHA256 of the script
    local script_path
    script_path=$(realpath "$0" 2>/dev/null || readlink -f "$0" 2>/dev/null || echo "$0")

    local actual_sha256
    if command -v sha256sum >/dev/null 2>&1; then
        actual_sha256=$(sha256sum "$script_path" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        actual_sha256=$(shasum -a 256 "$script_path" | awk '{print $1}')
    else
        log_warn "Neither sha256sum nor shasum found, skipping integrity check"
        log_warn "Install coreutils package for sha256sum support"
        return 0
    fi

    # Compare checksums
    if [[ "$actual_sha256" == "$EXPECTED_SHA256" ]]; then
        log_success "✓ Script integrity verified (SHA256: ${actual_sha256:0:16}...)"
        return 0
    else
        echo ""
        echo -e "${RED}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║                  ⚠ SECURITY ALERT: INTEGRITY CHECK FAILED ⚠           ║${NC}"
        echo -e "${RED}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        log_error "Script integrity verification FAILED!"
        log_error "This script may have been tampered with or corrupted!"
        echo ""
        log_error "Expected SHA256: $EXPECTED_SHA256"
        log_error "Actual SHA256:   $actual_sha256"
        echo ""
        log_error "DO NOT PROCEED! This could be a security breach."
        echo ""
        log_info "To verify manually:"
        echo "  1. Download from official source: https://github.com/VisageDvachevsky/veil-core"
        echo "  2. Verify with: sha256sum -c install_client.sh.sha256"
        echo ""

        if [[ "$DRY_RUN" == "true" ]]; then
            log_warn "Dry-run mode: would exit with error"
            return 1
        fi

        exit 1
    fi
}

# Execute command or show in dry-run mode
run_cmd() {
    if [[ "$DRY_RUN" == "true" ]]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} Would execute: $*"
        return 0
    fi
    log_debug "Executing: $*"
    "$@"
}

# Show usage/help
show_help() {
    cat << 'EOF'
VEIL Client One-Line Automated Installer

USAGE (SECURE METHOD - RECOMMENDED):
    # Download script and checksum
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256

    # Verify integrity
    sha256sum -c install_client.sh.sha256

    # Review script (IMPORTANT!)
    less install_client.sh

    # Execute
    sudo bash install_client.sh [OPTIONS]

USAGE (NOT RECOMMENDED - no integrity verification):
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh | sudo bash
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh | sudo bash -s -- [OPTIONS]

OPTIONS:
    --build=TYPE        Build type: 'debug' or 'release' (default: release)
    --with-gui          Install Qt6 GUI client
    --no-service        Skip systemd service creation (useful for containers)
    --dry-run           Show what would be done without making changes
    --verbose           Enable verbose output
    --help              Show this help message

ENVIRONMENT VARIABLES:
    VEIL_REPO           Git repository URL (default: https://github.com/VisageDvachevsky/veil-core.git)
    VEIL_BRANCH         Git branch to use (default: main)
    BUILD_TYPE          Same as --build
    WITH_GUI            Set to 'true' for GUI installation

EXAMPLES (SECURE METHOD):
    # Download and verify first
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh -o install_client.sh
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_client.sh.sha256 -o install_client.sh.sha256
    sha256sum -c install_client.sh.sha256

    # Basic CLI-only client installation
    sudo bash install_client.sh

    # Client with Qt6 GUI
    sudo bash install_client.sh --with-gui

    # Development setup with debug build and GUI
    sudo bash install_client.sh --build=debug --with-gui --verbose

    # Preview what would be installed
    sudo bash install_client.sh --dry-run --with-gui

GUI FEATURES (--with-gui):
    • Connection status monitoring
    • Real-time traffic statistics
    • Settings management interface
    • Diagnostics and logs viewer
    • Connection management

For more information, visit: https://github.com/VisageDvachevsky/veil-core
EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --build=*)
                BUILD_TYPE="${1#*=}"
                if [[ "$BUILD_TYPE" != "debug" && "$BUILD_TYPE" != "release" ]]; then
                    log_error "Invalid build type: $BUILD_TYPE (must be 'debug' or 'release')"
                    exit 1
                fi
                ;;
            --with-gui)
                WITH_GUI="true"
                ;;
            --no-service)
                CREATE_SERVICE="false"
                ;;
            --dry-run)
                DRY_RUN="true"
                ;;
            --verbose)
                VERBOSE="true"
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                log_info "Use --help for usage information"
                exit 1
                ;;
        esac
        shift
    done
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        if [[ "$DRY_RUN" == "true" ]]; then
            log_warn "Not running as root (dry-run mode continues anyway)"
        else
            log_error "This script must be run as root (use sudo)"
            exit 1
        fi
    fi
}

# Detect OS and package manager
detect_os() {
    log_info "Detecting operating system..."

    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        OS=$ID
        OS_VERSION=$VERSION_ID
        log_info "Detected: $NAME $VERSION"
    else
        log_error "Cannot detect OS. /etc/os-release not found."
        exit 1
    fi

    case "$OS" in
        ubuntu|debian)
            PKG_MANAGER="apt-get"
            PKG_UPDATE="apt-get update"
            PKG_INSTALL="apt-get install -y"
            ;;
        centos|rhel|fedora)
            PKG_MANAGER="yum"
            PKG_UPDATE="yum check-update || true"
            PKG_INSTALL="yum install -y"
            ;;
        *)
            log_error "Unsupported OS: $OS"
            log_error "This installer supports Ubuntu, Debian, CentOS, RHEL, and Fedora"
            exit 1
            ;;
    esac
}

# Install build dependencies
install_dependencies() {
    log_step "Installing build dependencies..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would update package lists"
        log_info "[DRY-RUN] Would install: build-essential cmake libsodium-dev pkg-config git ninja-build"
        if [[ "$WITH_GUI" == "true" ]]; then
            log_info "[DRY-RUN] Would install Qt6 packages: qt6-base-dev"
        fi
        return
    fi

    run_cmd $PKG_UPDATE

    # Base packages needed for all builds
    local base_packages=""
    local qt_packages=""

    case "$OS" in
        ubuntu|debian)
            base_packages="build-essential cmake libsodium-dev pkg-config git ca-certificates curl ninja-build"
            qt_packages="qt6-base-dev"
            ;;
        centos|rhel|fedora)
            base_packages="gcc-c++ cmake libsodium-devel pkgconfig git ca-certificates curl ninja-build"
            qt_packages="qt6-qtbase-devel"
            ;;
    esac

    log_info "Installing base build tools..."
    # shellcheck disable=SC2086
    run_cmd $PKG_INSTALL $base_packages

    if [[ "$WITH_GUI" == "true" ]]; then
        log_info "Installing Qt6 packages for GUI..."
        # shellcheck disable=SC2086
        if ! run_cmd $PKG_INSTALL $qt_packages 2>/dev/null; then
            log_warn "Qt6 packages not available in default repositories"
            log_warn "Attempting to install from alternative sources..."

            case "$OS" in
                ubuntu)
                    # Try enabling universe repository for Ubuntu
                    run_cmd add-apt-repository -y universe 2>/dev/null || true
                    run_cmd $PKG_UPDATE
                    # shellcheck disable=SC2086
                    run_cmd $PKG_INSTALL $qt_packages || {
                        log_error "Failed to install Qt6. GUI will not be available."
                        log_warn "You can try installing Qt6 manually: sudo apt install qt6-base-dev"
                        WITH_GUI="false"
                    }
                    ;;
                *)
                    log_error "Qt6 installation failed. GUI will not be available."
                    WITH_GUI="false"
                    ;;
            esac
        fi
    fi

    log_success "Dependencies installed successfully"
}

# Clone and build VEIL client from source
build_veil_client() {
    log_step "Building VEIL client from source..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would clone $VEIL_REPO (branch: $VEIL_BRANCH)"
        log_info "[DRY-RUN] Would build with: BUILD_TYPE=$BUILD_TYPE, WITH_GUI=$WITH_GUI"
        log_info "[DRY-RUN] Would install binaries to $INSTALL_DIR/bin"
        return
    fi

    log_info "Cloning VEIL repository..."

    # Clean up previous build directory if exists
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    run_cmd git clone --depth 1 --branch "$VEIL_BRANCH" "$VEIL_REPO" "$BUILD_DIR"
    cd "$BUILD_DIR"

    log_info "Building VEIL client ($BUILD_TYPE mode)..."

    # Determine CMake options
    local cmake_opts=(
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE^}"  # Capitalize first letter
        "-DVEIL_BUILD_TESTS=OFF"
        "-DVEIL_BUILD_SERVER=OFF"  # Don't build server when installing client
        "-GNinja"
    )

    # Enable/disable GUI build
    if [[ "$WITH_GUI" == "true" ]]; then
        cmake_opts+=("-DVEIL_BUILD_GUI=ON")
        log_info "Building with Qt6 GUI support..."
    else
        cmake_opts+=("-DVEIL_BUILD_GUI=OFF")
    fi

    # Configure build directory
    local build_subdir="build/${BUILD_TYPE}"
    mkdir -p "$build_subdir"
    cd "$build_subdir"

    # Run CMake configure
    log_info "Configuring build..."
    run_cmd cmake ../.. "${cmake_opts[@]}"

    # Build client only
    log_info "Compiling client (this may take a few minutes)..."
    run_cmd cmake --build . --target veil-client -j"$(nproc)"

    # Build GUI client if requested
    if [[ "$WITH_GUI" == "true" ]]; then
        log_info "Compiling GUI client..."
        if run_cmd cmake --build . --target veil-client-gui -j"$(nproc)" 2>/dev/null; then
            log_success "GUI client built successfully"
        else
            log_warn "GUI client build failed (Qt6 may not be available)"
            WITH_GUI="false"
        fi
    fi

    # Install
    log_info "Installing VEIL client binaries..."
    run_cmd cmake --install . --prefix "$INSTALL_DIR"

    # Create log directory
    run_cmd mkdir -p "$LOG_DIR"
    run_cmd chmod 755 "$LOG_DIR"

    # Install setup wizard script
    log_info "Installing setup wizard..."
    run_cmd mkdir -p "$INSTALL_DIR/share/veil"
    if [[ -f "$BUILD_DIR/scripts/setup-wizard.sh" ]]; then
        run_cmd cp "$BUILD_DIR/scripts/setup-wizard.sh" "$INSTALL_DIR/share/veil/setup-wizard.sh"
        run_cmd chmod 755 "$INSTALL_DIR/share/veil/setup-wizard.sh"
        log_success "Setup wizard installed to $INSTALL_DIR/share/veil/setup-wizard.sh"
    elif [[ -f "$BUILD_DIR/../scripts/setup-wizard.sh" ]]; then
        run_cmd cp "$BUILD_DIR/../scripts/setup-wizard.sh" "$INSTALL_DIR/share/veil/setup-wizard.sh"
        run_cmd chmod 755 "$INSTALL_DIR/share/veil/setup-wizard.sh"
        log_success "Setup wizard installed to $INSTALL_DIR/share/veil/setup-wizard.sh"
    fi

    log_success "VEIL client built and installed to $INSTALL_DIR/bin"

    # Show what was installed
    log_info "Installed binaries:"
    ls -la "$INSTALL_DIR/bin/veil-client"* 2>/dev/null || true
}

# Interactive configuration wizard for inexperienced users
run_interactive_setup_wizard() {
    echo ""
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║                  VEIL Client Setup Wizard                              ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${BOLD}This wizard will help you configure your VEIL VPN client.${NC}"
    echo ""

    # Explain what's needed
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  To connect to a VEIL VPN server, you need the following information:${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "  1. ${BOLD}Server IP address or hostname${NC}"
    echo "     Example: vpn.example.com or 203.0.113.1"
    echo ""
    echo "  2. ${BOLD}Server port${NC}"
    echo "     Default: 4433 (ask your server administrator if different)"
    echo ""
    echo "  3. ${BOLD}Pre-shared key file${NC} (client.key)"
    echo "     A secret key file that must match the server's key"
    echo "     Your server administrator should provide this securely"
    echo ""
    echo "  4. ${BOLD}Obfuscation seed file${NC} (obfuscation.seed)"
    echo "     A seed file for traffic obfuscation (must match server)"
    echo "     Your server administrator should provide this securely"
    echo ""
    echo -e "${CYAN}Note: The wizard will ask you for the server details now.${NC}"
    echo -e "${CYAN}      The key files must be obtained from your server administrator.${NC}"
    echo ""

    read -p "Press Enter to continue with the setup wizard..."
    echo ""

    # Ask for server address
    echo -e "${BOLD}Server Configuration:${NC}"
    echo ""
    read -p "Enter server IP address or hostname: " SERVER_ADDRESS
    while [[ -z "$SERVER_ADDRESS" ]]; do
        echo -e "${RED}Server address cannot be empty!${NC}"
        read -p "Enter server IP address or hostname: " SERVER_ADDRESS
    done

    # Ask for server port
    read -p "Enter server port [default: 4433]: " SERVER_PORT
    SERVER_PORT="${SERVER_PORT:-4433}"

    # Validate port number
    if ! [[ "$SERVER_PORT" =~ ^[0-9]+$ ]] || [ "$SERVER_PORT" -lt 1 ] || [ "$SERVER_PORT" -gt 65535 ]; then
        log_warn "Invalid port number, using default: 4433"
        SERVER_PORT=4433
    fi

    echo ""
    echo -e "${GREEN}✓ Server configured: ${SERVER_ADDRESS}:${SERVER_PORT}${NC}"
    echo ""

    # Show next steps for key files
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  Next: Obtain Key Files from Your Server Administrator${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "You need to obtain these files from your VEIL server administrator:"
    echo ""
    echo "  1. ${BOLD}client.key${NC} - Pre-shared encryption key"
    echo "  2. ${BOLD}obfuscation.seed${NC} - Traffic obfuscation seed"
    echo ""
    echo -e "${CYAN}If you run the VEIL server yourself, you can copy them with:${NC}"
    echo ""
    echo "  scp user@${SERVER_ADDRESS}:/etc/veil/server.key $CONFIG_DIR/client.key"
    echo "  scp user@${SERVER_ADDRESS}:/etc/veil/obfuscation.seed $CONFIG_DIR/obfuscation.seed"
    echo ""
    echo -e "${CYAN}Or if you have direct access to the server:${NC}"
    echo ""
    echo "  sudo cat /etc/veil/server.key | ssh user@client-machine \"sudo tee $CONFIG_DIR/client.key\""
    echo "  sudo cat /etc/veil/obfuscation.seed | ssh user@client-machine \"sudo tee $CONFIG_DIR/obfuscation.seed\""
    echo ""
    echo -e "${YELLOW}⚠  SECURITY: Transfer these files securely! Never send via email or chat.${NC}"
    echo ""
}

# Create client configuration
create_client_config() {
    log_step "Creating client configuration..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create client configuration at $CONFIG_DIR/client.conf"
        return
    fi

    run_cmd mkdir -p "$CONFIG_DIR"
    run_cmd chmod 700 "$CONFIG_DIR"

    if [[ -f "$CONFIG_DIR/client.conf" ]]; then
        log_warn "Configuration file already exists, backing up"
        run_cmd cp "$CONFIG_DIR/client.conf" "$CONFIG_DIR/client.conf.backup.$(date +%s)"
    fi

    # Run interactive setup wizard
    run_interactive_setup_wizard

    cat > "$CONFIG_DIR/client.conf" <<EOF
# VEIL Client Configuration
# Auto-generated by install_client.sh on $(date)

[client]
# Server connection settings
server_address = ${SERVER_ADDRESS}
server_port = ${SERVER_PORT}

# Auto-reconnect settings
auto_reconnect = true
reconnect_delay_sec = 5
max_reconnect_attempts = 10

[tun]
device_name = veil0
mtu = 1400

[crypto]
preshared_key_file = $CONFIG_DIR/client.key

[obfuscation]
profile_seed_file = $CONFIG_DIR/obfuscation.seed

[logging]
level = info
log_file = $LOG_DIR/veil-client.log
EOF

    run_cmd chmod 600 "$CONFIG_DIR/client.conf"
    log_success "Client configuration created: $CONFIG_DIR/client.conf"
}

# Create systemd service
create_systemd_service() {
    if [[ "$CREATE_SERVICE" != "true" ]]; then
        log_info "Skipping systemd service creation (--no-service)"
        return
    fi

    log_step "Creating systemd service..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create /etc/systemd/system/veil-client.service"
        return
    fi

    cat > "/etc/systemd/system/veil-client.service" <<EOF
[Unit]
Description=VEIL VPN Client
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/bin/veil-client --config ${CONFIG_DIR}/client.conf
Restart=on-failure
RestartSec=5
User=root
Group=root

# Security hardening
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/run /var/log ${LOG_DIR}
NoNewPrivileges=true

# Resource limits
LimitNOFILE=65536
LimitNPROC=512

[Install]
WantedBy=multi-user.target
EOF

    run_cmd systemctl daemon-reload
    log_success "Systemd service created: veil-client.service"

    # Also create GUI service if GUI is installed
    if [[ "$WITH_GUI" == "true" ]]; then
        create_gui_service
    fi
}

# Create GUI application service (optional)
create_gui_service() {
    local gui_binary="${INSTALL_DIR}/bin/veil-client-gui"

    # Check if GUI binary exists
    if [[ ! -f "$gui_binary" ]]; then
        log_warn "GUI binary not found at $gui_binary, skipping GUI service"
        return
    fi

    log_info "Creating GUI service..."

    cat > "/etc/systemd/system/veil-client-gui.service" <<EOF
[Unit]
Description=VEIL VPN Client GUI Monitor
After=network-online.target veil-client.service
Wants=veil-client.service

[Service]
Type=simple
ExecStart=${gui_binary}
Restart=on-failure
RestartSec=5
# GUI needs display access
Environment=DISPLAY=:0
Environment=QT_QPA_PLATFORM=xcb

[Install]
WantedBy=graphical.target
EOF

    run_cmd systemctl daemon-reload
    log_success "GUI service created: veil-client-gui.service"
}

# Display summary and next steps
display_summary() {
    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              VEIL Client Installation Complete                         ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "This was a dry run - no changes were made"
        echo ""
        return
    fi

    echo -e "${BLUE}Installation Summary:${NC}"
    echo "  • Build Type: ${BUILD_TYPE}"
    echo "  • GUI Installed: ${WITH_GUI}"
    echo "  • Service Created: ${CREATE_SERVICE}"
    echo ""

    log_success "Client is ready for connection"
    echo ""
    echo -e "${BLUE}Configuration Files:${NC}"
    echo "  • Config: $CONFIG_DIR/client.conf"
    echo ""

    # Check if key files are present
    local missing_files=()
    if [[ ! -f "$CONFIG_DIR/client.key" ]]; then
        missing_files+=("client.key")
    fi
    if [[ ! -f "$CONFIG_DIR/obfuscation.seed" ]]; then
        missing_files+=("obfuscation.seed")
    fi

    if [[ ${#missing_files[@]} -gt 0 ]]; then
        echo -e "${YELLOW}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${YELLOW}║                 ⚠  IMPORTANT - Action Required  ⚠                      ║${NC}"
        echo -e "${YELLOW}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${RED}Missing required files:${NC}"
        for file in "${missing_files[@]}"; do
            echo "  ✗ $CONFIG_DIR/$file"
        done
        echo ""
        echo -e "${BOLD}You MUST obtain these files from your VEIL server before connecting:${NC}"
        echo ""
        echo "  1. ${BOLD}client.key${NC} - Pre-shared encryption key (32 bytes)"
        echo "     Must match the server's key for secure connection"
        echo ""
        echo "  2. ${BOLD}obfuscation.seed${NC} - Traffic obfuscation seed (32 bytes)"
        echo "     Must match the server's seed for protocol obfuscation"
        echo ""
        echo -e "${CYAN}Transfer from server (replace 'user@server' with actual credentials):${NC}"
        echo ""
        echo "  scp user@server:/etc/veil/server.key $CONFIG_DIR/client.key"
        echo "  scp user@server:/etc/veil/obfuscation.seed $CONFIG_DIR/obfuscation.seed"
        echo ""
        echo -e "${CYAN}Set correct permissions after transfer:${NC}"
        echo ""
        echo "  sudo chmod 600 $CONFIG_DIR/client.key $CONFIG_DIR/obfuscation.seed"
        echo ""
        echo -e "${YELLOW}⚠  The client will NOT connect without these files!${NC}"
        echo ""
    else
        echo -e "${GREEN}✓ All required key files are present${NC}"
        echo ""
    fi

    if [[ "$CREATE_SERVICE" == "true" ]]; then
        echo -e "${BLUE}Management Commands:${NC}"
        echo "  • Start client:  sudo systemctl start veil-client"
        echo "  • Check status:  sudo systemctl status veil-client"
        echo "  • View logs:     sudo journalctl -u veil-client -f"
        echo "  • Restart:       sudo systemctl restart veil-client"
        echo "  • Stop:          sudo systemctl stop veil-client"
        echo "  • Enable auto-start: sudo systemctl enable veil-client"
    fi

    if [[ "$WITH_GUI" == "true" ]]; then
        echo ""
        echo -e "${CYAN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${CYAN}║                      GUI Client Features                               ║${NC}"
        echo -e "${CYAN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${CYAN}GUI Features:${NC}"
        echo "  • Connection status monitoring"
        echo "  • Real-time traffic statistics"
        echo "  • Settings management interface"
        echo "  • Diagnostics and logs viewer"
        echo "  • Connection management"
        echo ""
        echo -e "${CYAN}Launch GUI:${NC}"
        echo "  • Manual: ${INSTALL_DIR}/bin/veil-client-gui"
        if [[ "$CREATE_SERVICE" == "true" ]]; then
            echo "  • Service: sudo systemctl start veil-client-gui"
        fi
    fi

    echo ""
    echo -e "${BLUE}Need Help with Configuration?${NC}"
    echo "  Run the interactive setup wizard anytime:"
    echo "    ${BOLD}sudo ${INSTALL_DIR}/share/veil/setup-wizard.sh${NC}"
    echo ""

    echo ""
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}                    Installation completed successfully!                    ${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo ""
}

# Cleanup function
cleanup() {
    if [[ -d "$BUILD_DIR" ]]; then
        log_info "Cleaning up build directory..."
        rm -rf "$BUILD_DIR"
    fi
}

# Main installation flow
main() {
    # Parse command line arguments first
    parse_args "$@"

    # Security: Verify script integrity (critical security check)
    verify_script_integrity

    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║              VEIL Client One-Line Automated Installer                  ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$DRY_RUN" == "true" ]]; then
        echo -e "${YELLOW}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${YELLOW}║                        DRY RUN MODE ENABLED                            ║${NC}"
        echo -e "${YELLOW}║              No changes will be made to your system                    ║${NC}"
        echo -e "${YELLOW}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
    fi

    log_info "Installation Configuration:"
    log_info "  Build Type: ${BUILD_TYPE}"
    log_info "  With GUI: ${WITH_GUI}"
    log_info "  Create Service: ${CREATE_SERVICE}"
    echo ""

    # Set trap for cleanup
    trap cleanup EXIT

    # Installation steps
    check_root
    detect_os
    install_dependencies
    build_veil_client
    create_client_config
    create_systemd_service

    # Display summary
    display_summary

    log_success "Installation completed successfully!"
}

# Run main function
main "$@"
