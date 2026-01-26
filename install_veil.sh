#!/usr/bin/env bash
#
# VEIL One-Line Automated Installer
#
# This script performs a complete automated installation and configuration
# of VEIL Server/Client on Ubuntu/Debian systems with optional Qt6 GUI.
#
# One-line install:
#   curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh | sudo bash
#
# Or download and run manually:
#   wget https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh
#   chmod +x install_veil.sh
#   sudo ./install_veil.sh [OPTIONS]
#
# Options:
#   --mode=server|client   Installation mode (default: server)
#   --build=debug|release  Build type (default: release)
#   --with-gui             Install Qt6 GUI with monitoring dashboard
#   --no-service           Don't create/start systemd service
#   --dry-run              Show what would be done without making changes
#   --update               Update existing VEIL installation
#   --graceful             Graceful update with connection draining (use with --update)
#   --force                Force update, immediately terminate connections (use with --update)
#   --drain-timeout=N      Connection drain timeout in seconds (default: 300)
#   --yes                  Skip confirmation prompts (use with --update)
#   --help                 Show this help message
#
# Examples:
#   # Install server with GUI (recommended for monitoring)
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --with-gui
#
#   # Install client only
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --mode=client
#
#   # Install with debug build for development
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --build=debug --with-gui
#
#   # Update to latest version
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --update
#
#   # Graceful update with zero-downtime (drain connections first)
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --update --graceful
#
#   # Force update (immediately kill existing instance)
#   curl -sSL https://...install_veil.sh | sudo bash -s -- --update --force
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
VEIL_REPO="${VEIL_REPO:-https://github.com/VisageDvachevsky/veil-windows-client.git}"
VEIL_BRANCH="${VEIL_BRANCH:-main}"
INSTALL_DIR="/usr/local"
CONFIG_DIR="/etc/veil"
BUILD_DIR="/tmp/veil-build-$$"
LOG_DIR="/var/log/veil"
EXTERNAL_INTERFACE=""

# Installation options (can be overridden via command line)
INSTALL_MODE="${INSTALL_MODE:-server}"    # server or client
BUILD_TYPE="${BUILD_TYPE:-release}"       # debug or release
WITH_GUI="${WITH_GUI:-false}"             # Install Qt6 GUI
CREATE_SERVICE="${CREATE_SERVICE:-true}"  # Create systemd service
DRY_RUN="${DRY_RUN:-false}"               # Dry run mode
VERBOSE="${VERBOSE:-false}"               # Verbose output

# Update-related options
UPDATE_MODE="${UPDATE_MODE:-false}"       # Update existing installation
GRACEFUL_UPDATE="${GRACEFUL_UPDATE:-false}"  # Graceful connection draining
FORCE_UPDATE="${FORCE_UPDATE:-false}"     # Force update (kill connections)
DRAIN_TIMEOUT="${DRAIN_TIMEOUT:-300}"     # Connection drain timeout in seconds
SKIP_CONFIRM="${SKIP_CONFIRM:-false}"     # Skip confirmation prompts
BACKUP_DIR="${BACKUP_DIR:-/etc/veil/backup}"  # Backup directory for updates

# Version tracking
INSTALLED_VERSION=""
AVAILABLE_VERSION=""

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
# UPDATE/UPGRADE FUNCTIONALITY
# ============================================================================

# Detect if VEIL is already installed
detect_existing_installation() {
    log_step "Detecting existing VEIL installation..."

    local found_installation="false"
    local binary_name="veil-${INSTALL_MODE}"
    local binary_path="${INSTALL_DIR}/bin/${binary_name}"

    # Check for binary
    if [[ -f "$binary_path" ]]; then
        found_installation="true"
        log_info "Found ${binary_name} at ${binary_path}"

        # Try to get version
        if "$binary_path" --version &>/dev/null 2>&1; then
            INSTALLED_VERSION=$("$binary_path" --version 2>&1 | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "unknown")
        else
            # Try alternative version detection
            INSTALLED_VERSION=$(strings "$binary_path" 2>/dev/null | grep -oE 'VEIL [0-9]+\.[0-9]+\.[0-9]+' | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "unknown")
        fi
        log_info "Installed version: ${INSTALLED_VERSION:-unknown}"
    fi

    # Check for configuration
    if [[ -d "$CONFIG_DIR" ]]; then
        log_info "Found configuration directory: $CONFIG_DIR"
        if [[ -f "$CONFIG_DIR/${INSTALL_MODE}.conf" ]]; then
            log_info "Found ${INSTALL_MODE} configuration file"
        fi
    fi

    # Check for systemd service
    local service_name="veil-${INSTALL_MODE}"
    if systemctl list-unit-files "${service_name}.service" &>/dev/null 2>&1; then
        local service_status
        service_status=$(systemctl is-active "${service_name}" 2>/dev/null || echo "not-found")
        log_info "Systemd service status: ${service_status}"
    fi

    if [[ "$found_installation" == "true" ]]; then
        return 0
    else
        return 1
    fi
}

# Get latest available version from repository
get_available_version() {
    log_info "Checking available version..."

    if [[ "$DRY_RUN" == "true" ]]; then
        AVAILABLE_VERSION="latest"
        log_info "[DRY-RUN] Would check available version from repository"
        return
    fi

    # Clone a minimal version to get the version info
    local temp_dir="/tmp/veil-version-check-$$"
    if git clone --depth 1 --branch "$VEIL_BRANCH" "$VEIL_REPO" "$temp_dir" &>/dev/null 2>&1; then
        # Try to extract version from CMakeLists.txt or version file
        if [[ -f "$temp_dir/CMakeLists.txt" ]]; then
            AVAILABLE_VERSION=$(grep -oE 'project\([^)]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' "$temp_dir/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "")
        fi
        if [[ -z "$AVAILABLE_VERSION" && -f "$temp_dir/VERSION" ]]; then
            AVAILABLE_VERSION=$(cat "$temp_dir/VERSION" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo "")
        fi
        rm -rf "$temp_dir"
    fi

    if [[ -z "$AVAILABLE_VERSION" ]]; then
        AVAILABLE_VERSION="latest"
        log_debug "Could not determine exact version, using 'latest'"
    else
        log_info "Available version: ${AVAILABLE_VERSION}"
    fi
}

# Count active client connections (for server mode)
count_active_connections() {
    local count=0

    if [[ "$INSTALL_MODE" != "server" ]]; then
        echo "0"
        return
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        echo "0"
        return
    fi

    # Method 1: Check VEIL server's internal connection tracking (if available)
    local binary_path="${INSTALL_DIR}/bin/veil-server"
    if [[ -f "$binary_path" ]] && "$binary_path" --status &>/dev/null 2>&1; then
        count=$("$binary_path" --status 2>/dev/null | grep -oE 'clients?:[[:space:]]*[0-9]+' | grep -oE '[0-9]+' || echo "0")
    fi

    # Method 2: Check established connections on the VEIL port
    if [[ "$count" == "0" ]]; then
        local veil_port=4433
        if [[ -f "$CONFIG_DIR/server.conf" ]]; then
            veil_port=$(grep -E '^listen_port' "$CONFIG_DIR/server.conf" 2>/dev/null | grep -oE '[0-9]+' || echo "4433")
        fi
        # Count UDP connections (VEIL uses UDP)
        # Note: grep -c always outputs a number, even 0, so no fallback needed
        # The || echo "0" was causing "0\n0" output when grep -c returned 0 with exit code 1
        count=$(ss -u -n sport = :"$veil_port" 2>/dev/null | grep -c ESTAB)
        # If no ESTAB, count any connected peers
        if [[ "$count" == "0" ]]; then
            # wc -l always outputs a number, fallback only needed if ss command fails completely
            count=$(ss -u -n sport = :"$veil_port" 2>/dev/null | tail -n +2 | wc -l)
        fi
        # Ensure count is a valid number (default to 0 if empty or invalid)
        count=${count:-0}
    fi

    echo "$count"
}

# Get VEIL process IDs
get_veil_pids() {
    local binary_name="veil-${INSTALL_MODE}"
    pgrep -x "$binary_name" 2>/dev/null || true
}

# Check if VEIL service is running
is_service_running() {
    local service_name="veil-${INSTALL_MODE}"
    systemctl is-active --quiet "$service_name" 2>/dev/null
}

# Pre-update safety checks
perform_safety_checks() {
    log_step "Performing pre-update safety checks..."

    local errors=0

    # Check 1: Disk space (need at least 500MB for build)
    local available_space
    available_space=$(df -BM /tmp 2>/dev/null | tail -1 | awk '{print $4}' | tr -d 'M')
    if [[ -n "$available_space" ]] && [[ "$available_space" -lt 500 ]]; then
        log_error "Insufficient disk space in /tmp: ${available_space}MB available, 500MB required"
        ((errors++))
    else
        log_debug "Disk space check passed: ${available_space}MB available"
    fi

    # Check 2: Available memory (need at least 512MB)
    local available_mem
    available_mem=$(free -m 2>/dev/null | awk '/^Mem:/{print $7}')
    if [[ -n "$available_mem" ]] && [[ "$available_mem" -lt 512 ]]; then
        log_warn "Low available memory: ${available_mem}MB (512MB recommended)"
    else
        log_debug "Memory check passed: ${available_mem}MB available"
    fi

    # Check 3: Verify we can write to necessary directories
    if [[ ! -w "$INSTALL_DIR" ]]; then
        log_error "Cannot write to installation directory: $INSTALL_DIR"
        ((errors++))
    fi

    if [[ -d "$CONFIG_DIR" && ! -w "$CONFIG_DIR" ]]; then
        log_error "Cannot write to configuration directory: $CONFIG_DIR"
        ((errors++))
    fi

    # Check 4: Verify git is available
    if ! command -v git &>/dev/null; then
        log_error "git is not installed (required for update)"
        ((errors++))
    fi

    if [[ "$errors" -gt 0 ]]; then
        log_error "Pre-update safety checks failed with $errors error(s)"
        return 1
    fi

    log_success "All safety checks passed"
    return 0
}

# Create backup of configuration and keys
create_config_backup() {
    log_step "Backing up configuration files..."

    local backup_timestamp
    backup_timestamp=$(date +%Y%m%d-%H%M%S)
    local backup_path="${BACKUP_DIR}/${backup_timestamp}"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create backup at ${backup_path}"
        log_info "[DRY-RUN] Would backup: configuration files, keys, and binaries"
        return
    fi

    # Create backup directory
    mkdir -p "$backup_path"

    # Backup configuration files
    local files_backed_up=0
    for config_file in "$CONFIG_DIR"/*.conf "$CONFIG_DIR"/*.key "$CONFIG_DIR"/*.seed; do
        if [[ -f "$config_file" ]]; then
            cp -p "$config_file" "$backup_path/"
            log_debug "Backed up: $(basename "$config_file")"
            ((files_backed_up++))
        fi
    done

    # Backup binaries
    local binary_name="veil-${INSTALL_MODE}"
    local binary_path="${INSTALL_DIR}/bin/${binary_name}"
    if [[ -f "$binary_path" ]]; then
        cp -p "$binary_path" "$backup_path/${binary_name}.backup"
        log_debug "Backed up: ${binary_name} binary"
        ((files_backed_up++))
    fi

    # Also backup GUI binary if it exists
    if [[ -f "${binary_path}-gui" ]]; then
        cp -p "${binary_path}-gui" "$backup_path/${binary_name}-gui.backup"
        log_debug "Backed up: ${binary_name}-gui binary"
        ((files_backed_up++))
    fi

    # Store backup metadata
    cat > "$backup_path/backup.info" <<EOF
backup_timestamp=${backup_timestamp}
installed_version=${INSTALLED_VERSION:-unknown}
install_mode=${INSTALL_MODE}
backup_date=$(date)
EOF

    log_success "Backup created: ${backup_path} (${files_backed_up} files)"
    echo "$backup_path"
}

# Backup current binary before replacement
backup_binary() {
    local binary_name="veil-${INSTALL_MODE}"
    local binary_path="${INSTALL_DIR}/bin/${binary_name}"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would backup ${binary_path} to ${binary_path}.backup"
        return
    fi

    if [[ -f "$binary_path" ]]; then
        cp -p "$binary_path" "${binary_path}.backup"
        log_info "Binary backed up to: ${binary_path}.backup"
    fi

    # Also backup GUI binary if updating with GUI
    if [[ "$WITH_GUI" == "true" && -f "${binary_path}-gui" ]]; then
        cp -p "${binary_path}-gui" "${binary_path}-gui.backup"
        log_info "GUI binary backed up to: ${binary_path}-gui.backup"
    fi
}

# Rollback to previous binary version
rollback_update() {
    log_error "Update failed! Initiating automatic rollback..."

    local binary_name="veil-${INSTALL_MODE}"
    local binary_path="${INSTALL_DIR}/bin/${binary_name}"
    local service_name="veil-${INSTALL_MODE}"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would restore ${binary_path}.backup to ${binary_path}"
        return
    fi

    # Restore binary from backup
    if [[ -f "${binary_path}.backup" ]]; then
        cp -p "${binary_path}.backup" "$binary_path"
        log_info "Restored binary from backup"
    else
        log_error "No backup binary found at ${binary_path}.backup"
        log_error "Manual recovery may be required. Check: $BACKUP_DIR"
        return 1
    fi

    # Restore GUI binary if it exists
    if [[ -f "${binary_path}-gui.backup" ]]; then
        cp -p "${binary_path}-gui.backup" "${binary_path}-gui"
        log_info "Restored GUI binary from backup"
    fi

    # Try to restart the service with the old binary
    if systemctl is-enabled --quiet "$service_name" 2>/dev/null; then
        log_info "Attempting to restart service with restored binary..."
        if systemctl restart "$service_name" 2>/dev/null; then
            sleep 2
            if systemctl is-active --quiet "$service_name"; then
                log_success "Service restarted successfully with previous version"
                return 0
            fi
        fi
        log_error "Service failed to start after rollback"
        return 1
    fi

    log_success "Rollback completed"
    return 0
}

# Verify new binary works correctly
verify_new_binary() {
    log_info "Verifying new binary..."

    local binary_name="veil-${INSTALL_MODE}"
    local binary_path="${INSTALL_DIR}/bin/${binary_name}"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would verify ${binary_path}"
        return 0
    fi

    # Check if binary exists
    if [[ ! -f "$binary_path" ]]; then
        log_error "New binary not found at ${binary_path}"
        return 1
    fi

    # Check if binary is executable
    if [[ ! -x "$binary_path" ]]; then
        log_error "New binary is not executable"
        return 1
    fi

    # Try to run with --help or --version to verify it's a valid executable
    if "$binary_path" --help &>/dev/null 2>&1 || "$binary_path" --version &>/dev/null 2>&1; then
        log_debug "Binary executes correctly"
    else
        # Some binaries might not have --help, try just checking if it runs
        if ! ldd "$binary_path" 2>&1 | grep -q "not found"; then
            log_debug "Binary dependencies are satisfied"
        else
            log_error "Binary has missing dependencies"
            ldd "$binary_path" 2>&1 | grep "not found"
            return 1
        fi
    fi

    log_success "New binary verified successfully"
    return 0
}

# Validate and fix configuration for compatibility with new version
validate_and_fix_config() {
    log_step "Validating configuration for compatibility..."

    local config_file="$CONFIG_DIR/${INSTALL_MODE}.conf"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would validate and fix ${config_file}"
        return 0
    fi

    # Only validate server configuration
    if [[ "$INSTALL_MODE" != "server" ]]; then
        log_info "Client mode, skipping configuration validation"
        return 0
    fi

    if [[ ! -f "$config_file" ]]; then
        log_warn "Configuration file not found: ${config_file}"
        return 0
    fi

    # Extract IP pool configuration
    local ip_pool_start=$(grep -E '^\s*start\s*=' "$config_file" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ')
    local ip_pool_end=$(grep -E '^\s*end\s*=' "$config_file" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ')
    local max_clients=$(grep -E '^\s*max_clients\s*=' "$config_file" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ')

    # If we couldn't extract values, skip validation
    if [[ -z "$ip_pool_start" || -z "$ip_pool_end" || -z "$max_clients" ]]; then
        log_warn "Could not extract IP pool or max_clients from config, skipping validation"
        return 0
    fi

    log_info "Current config: IP pool ${ip_pool_start} - ${ip_pool_end}, max_clients=${max_clients}"

    # Calculate IP pool size
    local pool_size=$(calculate_ip_pool_size "$ip_pool_start" "$ip_pool_end")

    log_info "IP pool size: ${pool_size}"

    # Check if max_clients exceeds pool size
    if [[ "$max_clients" -gt "$pool_size" ]]; then
        log_warn "Configuration issue detected: max_clients (${max_clients}) exceeds IP pool size (${pool_size})"
        log_info "Adjusting max_clients to ${pool_size} to match IP pool size"

        # Create a backup before modification
        cp "$config_file" "${config_file}.pre-upgrade-fix.$(date +%s)"

        # Replace max_clients value in the config file
        sed -i "s/^\s*max_clients\s*=.*/max_clients = ${pool_size}/" "$config_file"

        # Verify the change
        local new_max_clients=$(grep -E '^\s*max_clients\s*=' "$config_file" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ')
        if [[ "$new_max_clients" == "$pool_size" ]]; then
            log_success "Configuration fixed: max_clients updated from ${max_clients} to ${pool_size}"
        else
            log_error "Failed to update max_clients in configuration"
            return 1
        fi
    else
        log_success "Configuration is valid: max_clients (${max_clients}) within IP pool size (${pool_size})"
    fi

    return 0
}

# Stop VEIL service gracefully with connection draining
graceful_stop_service() {
    local service_name="veil-${INSTALL_MODE}"
    local timeout="$DRAIN_TIMEOUT"
    local start_time
    start_time=$(date +%s)

    log_step "Gracefully stopping VEIL ${INSTALL_MODE}..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would gracefully stop ${service_name} with ${timeout}s drain timeout"
        return 0
    fi

    # Check current connections
    local initial_connections
    initial_connections=$(count_active_connections)
    log_info "Active connections: ${initial_connections}"

    if [[ "$initial_connections" -gt 0 ]]; then
        log_info "Waiting for connections to drain (timeout: ${timeout}s)..."

        # Send SIGTERM to allow graceful shutdown
        # The VEIL server should handle this by:
        # 1. Stop accepting new connections
        # 2. Wait for existing connections to close
        # 3. Exit cleanly

        # First, tell systemd to stop (which sends SIGTERM)
        systemctl stop "$service_name" --no-block 2>/dev/null || true

        # Wait for connections to drain
        local elapsed=0
        while [[ "$elapsed" -lt "$timeout" ]]; do
            local current_connections
            current_connections=$(count_active_connections)

            if [[ "$current_connections" -eq 0 ]]; then
                log_success "All clients disconnected (took ${elapsed}s)"
                break
            fi

            log_info "Waiting for ${current_connections} client(s) to disconnect... (${elapsed}s/${timeout}s)"
            sleep 5
            elapsed=$(( $(date +%s) - start_time ))
        done

        # Check if we timed out
        local final_connections
        final_connections=$(count_active_connections)
        if [[ "$final_connections" -gt 0 ]]; then
            log_warn "Drain timeout reached with ${final_connections} connection(s) still active"
            if [[ "$FORCE_UPDATE" != "true" ]]; then
                log_warn "Forcefully terminating remaining connections..."
            fi
        fi
    fi

    # Ensure service is fully stopped
    systemctl stop "$service_name" 2>/dev/null || true

    # Kill any remaining processes
    local pids
    pids=$(get_veil_pids)
    if [[ -n "$pids" ]]; then
        log_debug "Killing remaining VEIL processes: $pids"
        echo "$pids" | xargs -r kill -9 2>/dev/null || true
        sleep 1
    fi

    log_success "VEIL ${INSTALL_MODE} stopped"
    return 0
}

# Force stop VEIL service
force_stop_service() {
    local service_name="veil-${INSTALL_MODE}"

    log_step "Force stopping VEIL ${INSTALL_MODE}..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would force stop ${service_name}"
        return 0
    fi

    # Stop the service
    systemctl stop "$service_name" 2>/dev/null || true

    # Kill all VEIL processes
    local pids
    pids=$(get_veil_pids)
    if [[ -n "$pids" ]]; then
        log_info "Killing processes: $pids"
        echo "$pids" | xargs -r kill -9 2>/dev/null || true
        sleep 1
    fi

    log_success "VEIL ${INSTALL_MODE} force stopped"
    return 0
}

# Prompt for user confirmation
confirm_update() {
    local message="$1"
    local connections="$2"

    if [[ "$SKIP_CONFIRM" == "true" ]]; then
        return 0
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would prompt for confirmation"
        return 0
    fi

    echo ""
    if [[ "$connections" -gt 0 ]]; then
        log_warn "$message"
        log_warn "Active connections: $connections"
    else
        log_info "$message"
    fi
    echo ""

    read -r -p "Continue? (yes/no): " response
    case "$response" in
        yes|Yes|YES|y|Y)
            return 0
            ;;
        *)
            log_info "Update cancelled by user"
            exit 0
            ;;
    esac
}

# Display update summary
display_update_summary() {
    local service_name="veil-${INSTALL_MODE}"

    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              VEIL ${INSTALL_MODE^} Update Complete                               ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "This was a dry run - no changes were made"
        echo ""
        return
    fi

    echo -e "${BLUE}Update Summary:${NC}"
    echo "  • Mode: ${INSTALL_MODE}"
    echo "  • Previous Version: ${INSTALLED_VERSION:-unknown}"
    echo "  • New Version: ${AVAILABLE_VERSION:-latest}"
    echo "  • Update Type: $(if [[ "$GRACEFUL_UPDATE" == "true" ]]; then echo "Graceful"; elif [[ "$FORCE_UPDATE" == "true" ]]; then echo "Force"; else echo "Standard"; fi)"
    echo ""

    echo -e "${BLUE}Backup Location:${NC}"
    echo "  • Backup Dir: ${BACKUP_DIR}"
    echo "  • Binary Backup: ${INSTALL_DIR}/bin/veil-${INSTALL_MODE}.backup"
    echo ""

    # Display client configuration data for server mode
    if [[ "$INSTALL_MODE" == "server" ]]; then
        echo -e "${CYAN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${CYAN}║              Client Configuration Data                                 ║${NC}"
        echo -e "${CYAN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${YELLOW}Copy the following data to configure your clients:${NC}"
        echo ""

        # Get server public IP
        local public_ip
        public_ip=$(curl -s ifconfig.me 2>/dev/null || echo "<YOUR_SERVER_IP>")

        # Get server port from config
        local server_port="4433"
        if [[ -f "$CONFIG_DIR/server.conf" ]]; then
            server_port=$(grep -E '^\s*listen_port\s*=' "$CONFIG_DIR/server.conf" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ' || echo "4433")
        fi

        echo -e "${BOLD}Server IP:${NC}"
        echo "  $public_ip"
        echo ""

        echo -e "${BOLD}Server Port:${NC}"
        echo "  $server_port"
        echo ""

        # Display Key in base64
        if [[ -f "$CONFIG_DIR/server.key" ]]; then
            echo -e "${BOLD}Pre-Shared Key (base64):${NC}"
            base64 -w 0 "$CONFIG_DIR/server.key"
            echo ""
            echo ""
        else
            echo -e "${YELLOW}⚠ Warning: $CONFIG_DIR/server.key not found${NC}"
            echo ""
        fi

        # Display Seed in base64
        if [[ -f "$CONFIG_DIR/obfuscation.seed" ]]; then
            echo -e "${BOLD}Obfuscation Seed (base64):${NC}"
            base64 -w 0 "$CONFIG_DIR/obfuscation.seed"
            echo ""
            echo ""
        else
            echo -e "${YELLOW}⚠ Warning: $CONFIG_DIR/obfuscation.seed not found${NC}"
            echo ""
        fi

        echo -e "${CYAN}Transfer these files to clients securely:${NC}"
        echo "  scp $CONFIG_DIR/server.key user@client:/etc/veil/client.key"
        echo "  scp $CONFIG_DIR/obfuscation.seed user@client:/etc/veil/obfuscation.seed"
        echo ""
        echo -e "${YELLOW}⚠ NEVER send these keys over email or insecure channels!${NC}"
        echo ""
    fi

    if is_service_running; then
        log_success "VEIL ${INSTALL_MODE} is running!"
    else
        log_warn "Service is not running. Start with: sudo systemctl start ${service_name}"
    fi

    echo ""
    echo -e "${BLUE}Management Commands:${NC}"
    echo "  • Check status:  sudo systemctl status ${service_name}"
    echo "  • View logs:     sudo journalctl -u ${service_name} -f"
    echo "  • Rollback:      sudo cp ${INSTALL_DIR}/bin/veil-${INSTALL_MODE}.backup ${INSTALL_DIR}/bin/veil-${INSTALL_MODE}"
    echo ""

    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}                    Update completed successfully!                          ${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════════════${NC}"
    echo ""
}

# Perform the update
perform_update() {
    local service_name="veil-${INSTALL_MODE}"

    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║              VEIL Update/Upgrade Mode                                   ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$DRY_RUN" == "true" ]]; then
        echo -e "${YELLOW}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${YELLOW}║                        DRY RUN MODE ENABLED                            ║${NC}"
        echo -e "${YELLOW}║              No changes will be made to your system                    ║${NC}"
        echo -e "${YELLOW}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
    fi

    # Step 1: Detect existing installation
    if ! detect_existing_installation; then
        log_error "No existing VEIL installation detected"
        log_info "Use install mode (without --update) for fresh installation"
        exit 1
    fi

    # Step 2: Get available version
    get_available_version

    # Step 3: Compare versions
    if [[ "$INSTALLED_VERSION" == "$AVAILABLE_VERSION" && "$INSTALLED_VERSION" != "unknown" ]]; then
        log_info "Already running the latest version ($INSTALLED_VERSION)"
        if [[ "$FORCE_UPDATE" != "true" ]]; then
            log_info "Use --force to reinstall the same version"
            exit 0
        fi
        log_warn "Force reinstalling same version as requested"
    fi

    # Step 4: Perform safety checks
    if ! perform_safety_checks; then
        exit 1
    fi

    # Step 5: Count active connections
    local active_connections
    active_connections=$(count_active_connections)
    log_info "Active client connections: ${active_connections}"

    # Step 6: Confirm update
    if [[ "$active_connections" -gt 0 ]]; then
        if [[ "$GRACEFUL_UPDATE" == "true" ]]; then
            confirm_update "Graceful update will drain ${active_connections} connection(s) (timeout: ${DRAIN_TIMEOUT}s)" "$active_connections"
        elif [[ "$FORCE_UPDATE" == "true" ]]; then
            confirm_update "Force update will immediately disconnect ${active_connections} client(s)!" "$active_connections"
        else
            confirm_update "Update will restart the service, disconnecting ${active_connections} client(s)" "$active_connections"
        fi
    fi

    # Step 7: Create backup
    local backup_path
    backup_path=$(create_config_backup)

    # Step 8: Backup binary
    backup_binary

    # Step 9: Stop service
    if is_service_running; then
        if [[ "$GRACEFUL_UPDATE" == "true" ]]; then
            graceful_stop_service
        else
            force_stop_service
        fi
    fi

    # Step 10: Build and install new version
    detect_os
    install_dependencies
    build_veil

    # Step 11: Verify new binary
    if ! verify_new_binary; then
        log_error "New binary verification failed"
        rollback_update
        exit 1
    fi

    # Step 11.5: Validate and fix configuration for compatibility
    if ! validate_and_fix_config; then
        log_error "Configuration validation/fix failed"
        rollback_update
        exit 1
    fi

    # Step 12: Start service
    log_step "Starting VEIL ${INSTALL_MODE} service..."
    systemctl daemon-reload
    if systemctl start "$service_name" 2>/dev/null; then
        sleep 2
        if is_service_running; then
            log_success "VEIL ${INSTALL_MODE} started successfully!"
        else
            log_error "Service started but is not running"
            log_error "Checking logs..."
            journalctl -u "$service_name" -n 20 --no-pager
            rollback_update
            exit 1
        fi
    else
        log_error "Failed to start service"
        rollback_update
        exit 1
    fi

    # Step 13: Display summary
    display_update_summary
}

# ============================================================================
# END UPDATE/UPGRADE FUNCTIONALITY
# ============================================================================

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
VEIL One-Line Automated Installer

USAGE:
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh | sudo bash
    curl -sSL https://raw.githubusercontent.com/VisageDvachevsky/veil-core/main/install_veil.sh | sudo bash -s -- [OPTIONS]

    Or download and run:
    ./install_veil.sh [OPTIONS]

INSTALLATION OPTIONS:
    --mode=MODE         Installation mode: 'server' or 'client' (default: server)
    --build=TYPE        Build type: 'debug' or 'release' (default: release)
    --with-gui          Install Qt6 GUI with real-time monitoring dashboard
    --no-service        Skip systemd service creation (useful for containers)
    --dry-run           Show what would be done without making changes
    --verbose           Enable verbose output
    --help              Show this help message

UPDATE OPTIONS:
    --update            Update existing VEIL installation to latest version
    --graceful          Graceful update: drain connections before update (default timeout: 300s)
    --force             Force update: immediately terminate all connections
    --drain-timeout=N   Custom drain timeout in seconds (use with --graceful)
    --yes               Skip confirmation prompts

ENVIRONMENT VARIABLES:
    VEIL_REPO           Git repository URL (default: https://github.com/VisageDvachevsky/veil-core.git)
    VEIL_BRANCH         Git branch to use (default: main)
    INSTALL_MODE        Same as --mode
    BUILD_TYPE          Same as --build
    WITH_GUI            Set to 'true' for GUI installation

INSTALLATION EXAMPLES:
    # Basic server installation (CLI only)
    curl -sSL https://...install_veil.sh | sudo bash

    # Server with Qt6 GUI monitoring dashboard
    curl -sSL https://...install_veil.sh | sudo bash -s -- --with-gui

    # Client-only installation
    curl -sSL https://...install_veil.sh | sudo bash -s -- --mode=client

    # Development setup with debug build and GUI
    curl -sSL https://...install_veil.sh | sudo bash -s -- --build=debug --with-gui --verbose

    # Preview what would be installed
    curl -sSL https://...install_veil.sh | sudo bash -s -- --dry-run --with-gui

UPDATE EXAMPLES:
    # Update to latest version (will prompt for confirmation if connections active)
    curl -sSL https://...install_veil.sh | sudo bash -s -- --update

    # Graceful update with zero-downtime (drain connections first)
    curl -sSL https://...install_veil.sh | sudo bash -s -- --update --graceful

    # Graceful update with custom drain timeout (10 minutes)
    curl -sSL https://...install_veil.sh | sudo bash -s -- --update --graceful --drain-timeout=600

    # Force update immediately (will kill active connections)
    curl -sSL https://...install_veil.sh | sudo bash -s -- --update --force

    # Update client
    curl -sSL https://...install_veil.sh | sudo bash -s -- --mode=client --update

GUI FEATURES (--with-gui):
    • Real-time traffic monitoring with dynamic graphs
    • Protocol state visualization
    • Connected clients list with statistics
    • Live event log viewer
    • Performance metrics dashboard (CPU, memory, bandwidth)
    • Session management interface
    • Export diagnostics functionality

UPDATE FEATURES:
    • Automatic version detection and comparison
    • Configuration and key preservation
    • Binary backup and automatic rollback on failure
    • Graceful connection draining for zero-downtime updates
    • Pre-update safety checks (disk space, memory)
    • Active client connection counting for servers

For more information, visit: https://github.com/VisageDvachevsky/veil-core
EOF
}

# Parse command line arguments
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mode=*)
                INSTALL_MODE="${1#*=}"
                if [[ "$INSTALL_MODE" != "server" && "$INSTALL_MODE" != "client" ]]; then
                    log_error "Invalid mode: $INSTALL_MODE (must be 'server' or 'client')"
                    exit 1
                fi
                ;;
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
            --update)
                UPDATE_MODE="true"
                ;;
            --graceful)
                GRACEFUL_UPDATE="true"
                ;;
            --force)
                FORCE_UPDATE="true"
                ;;
            --drain-timeout=*)
                DRAIN_TIMEOUT="${1#*=}"
                if ! [[ "$DRAIN_TIMEOUT" =~ ^[0-9]+$ ]]; then
                    log_error "Invalid drain timeout: $DRAIN_TIMEOUT (must be a number)"
                    exit 1
                fi
                ;;
            --yes|-y)
                SKIP_CONFIRM="true"
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

    # Validate update mode options
    if [[ "$UPDATE_MODE" != "true" ]]; then
        if [[ "$GRACEFUL_UPDATE" == "true" || "$FORCE_UPDATE" == "true" ]]; then
            log_error "--graceful and --force can only be used with --update"
            exit 1
        fi
    fi

    if [[ "$GRACEFUL_UPDATE" == "true" && "$FORCE_UPDATE" == "true" ]]; then
        log_error "Cannot use both --graceful and --force at the same time"
        exit 1
    fi
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
            log_info "[DRY-RUN] Would install Qt6 packages: qt6-base-dev qt6-charts-dev"
        fi
        if [[ "$INSTALL_MODE" == "server" ]]; then
            log_info "[DRY-RUN] Would install: iptables iproute2"
        fi
        return
    fi

    run_cmd $PKG_UPDATE

    # Base packages needed for all builds
    local base_packages=""
    local qt_packages=""
    local network_packages=""

    case "$OS" in
        ubuntu|debian)
            base_packages="build-essential cmake libsodium-dev pkg-config git ca-certificates curl ninja-build"
            qt_packages="qt6-base-dev libqt6charts6-dev"
            network_packages="iptables iproute2"
            ;;
        centos|rhel|fedora)
            base_packages="gcc-c++ cmake libsodium-devel pkgconfig git ca-certificates curl ninja-build"
            qt_packages="qt6-qtbase-devel qt6-qtcharts-devel"
            network_packages="iptables iproute"
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

    if [[ "$INSTALL_MODE" == "server" ]]; then
        log_info "Installing network tools for server..."
        # shellcheck disable=SC2086
        run_cmd $PKG_INSTALL $network_packages
    fi

    log_success "Dependencies installed successfully"
}

# Clone and build VEIL from source
build_veil() {
    log_step "Building VEIL from source..."

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

    log_info "Building VEIL ($BUILD_TYPE mode)..."

    # Determine CMake options based on installation mode
    local cmake_opts=(
        "-DCMAKE_BUILD_TYPE=${BUILD_TYPE^}"  # Capitalize first letter
        "-DVEIL_BUILD_TESTS=OFF"
        "-GNinja"
    )

    # Enable/disable server build based on mode
    if [[ "$INSTALL_MODE" == "client" ]]; then
        cmake_opts+=("-DVEIL_BUILD_SERVER=OFF")
        log_info "Client mode: building client only..."
    else
        cmake_opts+=("-DVEIL_BUILD_SERVER=ON")
        log_info "Server mode: building both server and client..."
    fi

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

    # Build
    log_info "Compiling (this may take a few minutes)..."
    run_cmd cmake --build . -j"$(nproc)"

    # Install
    log_info "Installing VEIL binaries..."
    run_cmd cmake --install . --prefix "$INSTALL_DIR"

    # Create log directory
    run_cmd mkdir -p "$LOG_DIR"
    run_cmd chmod 755 "$LOG_DIR"

    log_success "VEIL built and installed to $INSTALL_DIR/bin"

    # Show what was installed
    log_info "Installed binaries:"
    if [[ "$INSTALL_MODE" == "server" ]]; then
        ls -la "$INSTALL_DIR/bin/veil-server"* 2>/dev/null || true
    else
        ls -la "$INSTALL_DIR/bin/veil-client"* 2>/dev/null || true
    fi
}

# Detect external network interface
detect_external_interface() {
    log_info "Detecting external network interface..."

    if [[ "$DRY_RUN" == "true" ]]; then
        EXTERNAL_INTERFACE="eth0"
        log_info "[DRY-RUN] Would auto-detect external interface (using eth0 as placeholder)"
        return
    fi

    # Try to find the default route interface
    EXTERNAL_INTERFACE=$(ip route | grep default | awk '{print $5}' | head -n1)

    if [[ -z "$EXTERNAL_INTERFACE" ]]; then
        log_warn "Could not auto-detect external interface"
        log_warn "Available interfaces:"
        ip -o link show | awk -F': ' '{print "  - " $2}'

        # Default to common interface names
        for iface in eth0 ens3 enp0s3 ens33; do
            if ip link show "$iface" &>/dev/null; then
                EXTERNAL_INTERFACE="$iface"
                log_warn "Using $EXTERNAL_INTERFACE (please verify this is correct)"
                break
            fi
        done

        if [[ -z "$EXTERNAL_INTERFACE" ]]; then
            log_error "Could not determine external interface"
            log_error "Please edit /etc/veil/server.conf manually and set 'external_interface'"
            EXTERNAL_INTERFACE="eth0"  # Fallback
        fi
    else
        log_success "External interface: $EXTERNAL_INTERFACE"
    fi
}

# Generate cryptographic keys
generate_keys() {
    log_step "Generating cryptographic keys..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create directory $CONFIG_DIR"
        log_info "[DRY-RUN] Would generate pre-shared key: $CONFIG_DIR/server.key"
        log_info "[DRY-RUN] Would generate obfuscation seed: $CONFIG_DIR/obfuscation.seed"
        return
    fi

    run_cmd mkdir -p "$CONFIG_DIR"
    run_cmd chmod 700 "$CONFIG_DIR"

    # Generate pre-shared key (32 bytes)
    if [[ ! -f "$CONFIG_DIR/server.key" ]]; then
        head -c 32 /dev/urandom > "$CONFIG_DIR/server.key"
        run_cmd chmod 600 "$CONFIG_DIR/server.key"
        log_success "Generated pre-shared key: $CONFIG_DIR/server.key"
    else
        log_warn "Pre-shared key already exists, skipping generation"
    fi

    # Generate obfuscation seed (32 bytes)
    if [[ ! -f "$CONFIG_DIR/obfuscation.seed" ]]; then
        head -c 32 /dev/urandom > "$CONFIG_DIR/obfuscation.seed"
        run_cmd chmod 600 "$CONFIG_DIR/obfuscation.seed"
        log_success "Generated obfuscation seed: $CONFIG_DIR/obfuscation.seed"
    else
        log_warn "Obfuscation seed already exists, skipping generation"
    fi
}

# Calculate IP pool size from start and end IP addresses
calculate_ip_pool_size() {
    local start_ip="$1"
    local end_ip="$2"

    # Convert IP address to integer
    ip_to_int() {
        local ip=$1
        local a b c d
        IFS=. read -r a b c d <<< "$ip"
        echo "$((a * 256 ** 3 + b * 256 ** 2 + c * 256 + d))"
    }

    local start_int=$(ip_to_int "$start_ip")
    local end_int=$(ip_to_int "$end_ip")

    # Calculate pool size (inclusive)
    echo "$((end_int - start_int + 1))"
}

# Create server configuration
create_config() {
    log_step "Creating server configuration..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create server configuration at $CONFIG_DIR/server.conf"
        return
    fi

    if [[ -f "$CONFIG_DIR/server.conf" ]]; then
        log_warn "Configuration file already exists, backing up to server.conf.backup"
        run_cmd cp "$CONFIG_DIR/server.conf" "$CONFIG_DIR/server.conf.backup.$(date +%s)"
    fi

    # Define IP pool range
    local IP_POOL_START="10.8.0.2"
    local IP_POOL_END="10.8.0.254"

    # Calculate max_clients dynamically based on IP pool size
    local MAX_CLIENTS=$(calculate_ip_pool_size "$IP_POOL_START" "$IP_POOL_END")

    log_info "IP pool range: $IP_POOL_START to $IP_POOL_END"
    log_info "Calculated max_clients: $MAX_CLIENTS"

    cat > "$CONFIG_DIR/server.conf" <<EOF
# VEIL Server Configuration
# Auto-generated by install_veil.sh on $(date)

[server]
listen_address = 0.0.0.0
listen_port = 4433
daemon = false
verbose = false

[tun]
device_name = veil0
ip_address = 10.8.0.1
netmask = 255.255.255.0
mtu = 1400

[crypto]
preshared_key_file = $CONFIG_DIR/server.key

[obfuscation]
profile_seed_file = $CONFIG_DIR/obfuscation.seed

[nat]
external_interface = $EXTERNAL_INTERFACE
enable_forwarding = true
use_masquerade = true

[sessions]
max_clients = $MAX_CLIENTS
session_timeout = 300
idle_warning_sec = 270
absolute_timeout_sec = 86400
max_memory_per_session_mb = 10
cleanup_interval = 60
drain_timeout_sec = 5

[ip_pool]
start = $IP_POOL_START
end = $IP_POOL_END

[daemon]
pid_file = /var/run/veil-server.pid

[rate_limiting]
per_client_bandwidth_mbps = 100
per_client_pps = 10000
burst_allowance_factor = 1.5
reconnect_limit_per_minute = 5
enable_traffic_shaping = true

[degradation]
cpu_threshold_percent = 80
memory_threshold_percent = 85
enable_graceful_degradation = true
escalation_delay_sec = 5
recovery_delay_sec = 10

[logging]
level = info
rate_limit_logs_per_sec = 100
sampling_rate = 0.01
async_logging = true
format = json

[migration]
enable_session_migration = true
migration_token_ttl_sec = 300
max_migrations_per_session = 5
migration_cooldown_sec = 10
EOF

    chmod 600 "$CONFIG_DIR/server.conf"
    log_success "Configuration created: $CONFIG_DIR/server.conf"
    log_success "max_clients automatically set to $MAX_CLIENTS based on IP pool size"
}

# Configure system networking
configure_networking() {
    log_step "Configuring system networking..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would enable IP forwarding"
        log_info "[DRY-RUN] Would add NAT/MASQUERADE rule for 10.8.0.0/24"
        return
    fi

    # Enable IP forwarding
    log_info "Enabling IP forwarding..."
    run_cmd sysctl -w net.ipv4.ip_forward=1 > /dev/null

    # Make it permanent
    if ! grep -q "net.ipv4.ip_forward.*=.*1" /etc/sysctl.conf 2>/dev/null; then
        echo "net.ipv4.ip_forward = 1" >> /etc/sysctl.conf
    fi

    log_success "IP forwarding enabled"

    # Configure NAT/MASQUERADE
    log_info "Configuring NAT (MASQUERADE)..."

    # Check if rule already exists
    if ! iptables -t nat -C POSTROUTING -s 10.8.0.0/24 -o "$EXTERNAL_INTERFACE" -j MASQUERADE 2>/dev/null; then
        run_cmd iptables -t nat -A POSTROUTING -s 10.8.0.0/24 -o "$EXTERNAL_INTERFACE" -j MASQUERADE
        log_success "NAT rule added"
    else
        log_warn "NAT rule already exists"
    fi

    # Save iptables rules
    case "$OS" in
        ubuntu|debian)
            if command -v iptables-save >/dev/null 2>&1; then
                if command -v netfilter-persistent >/dev/null 2>&1; then
                    netfilter-persistent save
                elif [[ -d /etc/iptables ]]; then
                    iptables-save > /etc/iptables/rules.v4
                fi
            fi
            ;;
        centos|rhel|fedora)
            if command -v iptables-save >/dev/null 2>&1; then
                iptables-save > /etc/sysconfig/iptables 2>/dev/null || true
            fi
            ;;
    esac

    log_success "Networking configured"
}

# Configure firewall
configure_firewall() {
    log_step "Configuring firewall..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would allow UDP port 4433 through firewall"
        return
    fi

    # Detect and configure firewall
    if command -v ufw >/dev/null 2>&1 && ufw status | grep -q "Status: active"; then
        log_info "Detected UFW firewall"
        run_cmd ufw allow 4433/udp
        log_success "UFW: Allowed UDP port 4433"
    elif command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld; then
        log_info "Detected firewalld"
        run_cmd firewall-cmd --permanent --add-port=4433/udp
        run_cmd firewall-cmd --reload
        log_success "firewalld: Allowed UDP port 4433"
    else
        log_info "Configuring iptables directly..."
        if ! iptables -C INPUT -p udp --dport 4433 -j ACCEPT 2>/dev/null; then
            run_cmd iptables -A INPUT -p udp --dport 4433 -j ACCEPT
            log_success "iptables: Allowed UDP port 4433"
        else
            log_warn "Firewall rule already exists"
        fi
    fi
}

# Create systemd service
create_systemd_service() {
    if [[ "$CREATE_SERVICE" != "true" ]]; then
        log_info "Skipping systemd service creation (--no-service)"
        return
    fi

    log_step "Creating systemd service..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create /etc/systemd/system/veil-${INSTALL_MODE}.service"
        return
    fi

    local service_name="veil-${INSTALL_MODE}"
    local binary_name="veil-${INSTALL_MODE}"
    local config_file="${CONFIG_DIR}/${INSTALL_MODE}.conf"

    cat > "/etc/systemd/system/${service_name}.service" <<EOF
[Unit]
Description=VEIL VPN ${INSTALL_MODE^}
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/bin/${binary_name} --config ${config_file}
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
    log_success "Systemd service created: ${service_name}.service"

    # Also create GUI service if GUI is installed
    if [[ "$WITH_GUI" == "true" ]]; then
        create_gui_service
    fi
}

# Create GUI application service (optional)
create_gui_service() {
    local gui_service_name="veil-${INSTALL_MODE}-gui"
    local gui_binary="${INSTALL_DIR}/bin/veil-${INSTALL_MODE}-gui"

    # Check if GUI binary exists
    if [[ ! -f "$gui_binary" ]]; then
        log_warn "GUI binary not found at $gui_binary, skipping GUI service"
        return
    fi

    log_info "Creating GUI service..."

    cat > "/etc/systemd/system/${gui_service_name}.service" <<EOF
[Unit]
Description=VEIL VPN ${INSTALL_MODE^} GUI Monitor
After=network-online.target veil-${INSTALL_MODE}.service
Wants=veil-${INSTALL_MODE}.service

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
    log_success "GUI service created: ${gui_service_name}.service"
}

# Start and enable service
start_service() {
    if [[ "$CREATE_SERVICE" != "true" ]]; then
        log_info "Skipping service start (--no-service)"
        return
    fi

    local service_name="veil-${INSTALL_MODE}"

    log_step "Starting VEIL ${INSTALL_MODE} service..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would enable and start ${service_name}.service"
        return
    fi

    run_cmd systemctl enable "$service_name"
    run_cmd systemctl start "$service_name"

    # Wait a moment for service to start
    sleep 2

    if systemctl is-active --quiet "$service_name"; then
        log_success "VEIL ${INSTALL_MODE} is running!"
    else
        log_error "Failed to start VEIL ${INSTALL_MODE}"
        log_error "Check logs with: sudo journalctl -u ${service_name} -n 50"
        exit 1
    fi
}

# Display summary and next steps
display_summary() {
    local public_ip
    public_ip=$(curl -s ifconfig.me 2>/dev/null || echo "<YOUR_SERVER_IP>")

    local service_name="veil-${INSTALL_MODE}"

    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              VEIL ${INSTALL_MODE^} Installation Complete                          ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "This was a dry run - no changes were made"
        echo ""
        return
    fi

    echo -e "${BLUE}Installation Summary:${NC}"
    echo "  • Mode: ${INSTALL_MODE}"
    echo "  • Build Type: ${BUILD_TYPE}"
    echo "  • GUI Installed: ${WITH_GUI}"
    echo "  • Service Created: ${CREATE_SERVICE}"
    echo ""

    if [[ "$INSTALL_MODE" == "server" ]]; then
        log_success "Server is running and ready for client connections"
        echo ""
        echo -e "${BLUE}Server Information:${NC}"
        echo "  • Server Address: $public_ip"
        echo "  • Server Port: 4433 (UDP)"
        echo "  • Tunnel Network: 10.8.0.0/24"
        echo "  • Server Tunnel IP: 10.8.0.1"
        echo ""
        echo -e "${BLUE}Configuration Files:${NC}"
        echo "  • Config: $CONFIG_DIR/server.conf"
        echo "  • PSK: $CONFIG_DIR/server.key"
        echo "  • Obfuscation Seed: $CONFIG_DIR/obfuscation.seed"
        echo ""
        echo -e "${YELLOW}⚠ IMPORTANT - Client Setup:${NC}"
        echo ""
        echo "To connect clients, securely transfer these files to each client:"
        echo "  1. $CONFIG_DIR/server.key → /etc/veil/client.key"
        echo "  2. $CONFIG_DIR/obfuscation.seed → /etc/veil/obfuscation.seed"
        echo ""
        echo "Example secure transfer:"
        echo "  scp $CONFIG_DIR/server.key user@client:/etc/veil/client.key"
        echo "  scp $CONFIG_DIR/obfuscation.seed user@client:/etc/veil/"
        echo ""
        echo -e "${YELLOW}⚠ NEVER send these keys over email or insecure channels!${NC}"
        echo ""
        echo -e "${BLUE}Network Status:${NC}"
        echo "  • IP Forwarding: $(sysctl -n net.ipv4.ip_forward 2>/dev/null || echo 'N/A')"
        echo "  • External Interface: $EXTERNAL_INTERFACE"
    else
        log_success "Client is ready for connection"
        echo ""
        echo -e "${BLUE}Client Configuration:${NC}"
        echo "  • Config: $CONFIG_DIR/client.conf"
        echo ""
        echo -e "${YELLOW}⚠ IMPORTANT - Before Connecting:${NC}"
        echo ""
        echo "You need the following files from your VEIL server:"
        echo "  1. server.key → $CONFIG_DIR/client.key"
        echo "  2. obfuscation.seed → $CONFIG_DIR/obfuscation.seed"
        echo ""
        echo "Edit $CONFIG_DIR/client.conf and set:"
        echo "  • server_address = <YOUR_SERVER_IP>"
        echo "  • server_port = 4433"
    fi

    echo ""
    if [[ "$CREATE_SERVICE" == "true" ]]; then
        echo -e "${BLUE}Management Commands:${NC}"
        echo "  • Check status:  sudo systemctl status ${service_name}"
        echo "  • View logs:     sudo journalctl -u ${service_name} -f"
        echo "  • Restart:       sudo systemctl restart ${service_name}"
        echo "  • Stop:          sudo systemctl stop ${service_name}"
    fi

    if [[ "$WITH_GUI" == "true" ]]; then
        echo ""
        echo -e "${CYAN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${CYAN}║                      GUI Monitoring Dashboard                          ║${NC}"
        echo -e "${CYAN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${CYAN}GUI Features:${NC}"
        echo "  • Real-time traffic monitoring with dynamic graphs"
        echo "  • Protocol state visualization"
        echo "  • Connected clients list with statistics"
        echo "  • Live event log viewer"
        echo "  • Performance metrics (CPU, memory, bandwidth)"
        echo "  • Export diagnostics functionality"
        echo ""
        echo -e "${CYAN}Launch GUI:${NC}"
        echo "  • Manual: ${INSTALL_DIR}/bin/veil-${INSTALL_MODE}-gui"
        if [[ "$CREATE_SERVICE" == "true" ]]; then
            echo "  • Service: sudo systemctl start veil-${INSTALL_MODE}-gui"
        fi
    fi

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

    cat > "$CONFIG_DIR/client.conf" <<EOF
# VEIL Client Configuration
# Auto-generated by install_veil.sh on $(date)

[client]
# Server connection settings - EDIT THESE
server_address = <YOUR_SERVER_IP>
server_port = 4433

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

# Main installation flow
main() {
    # Parse command line arguments first
    parse_args "$@"

    # Set trap for cleanup
    trap cleanup EXIT

    # Check root first
    check_root

    # Handle update mode
    if [[ "$UPDATE_MODE" == "true" ]]; then
        perform_update
        log_success "Update completed successfully!"
        exit 0
    fi

    # Fresh installation mode
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║              VEIL One-Line Automated Installer v2.1                    ║${NC}"
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
    log_info "  Mode: ${INSTALL_MODE}"
    log_info "  Build Type: ${BUILD_TYPE}"
    log_info "  With GUI: ${WITH_GUI}"
    log_info "  Create Service: ${CREATE_SERVICE}"
    echo ""

    # Common installation steps
    detect_os
    install_dependencies
    build_veil

    # Mode-specific installation
    if [[ "$INSTALL_MODE" == "server" ]]; then
        log_step "Configuring VEIL Server..."
        detect_external_interface
        generate_keys
        create_config
        configure_networking
        configure_firewall
    else
        log_step "Configuring VEIL Client..."
        create_client_config
    fi

    # Service setup (common)
    create_systemd_service
    start_service

    # Display summary
    display_summary

    log_success "Installation completed successfully!"
}

# Run main function
main "$@"
