#!/usr/bin/env bash
#
# VEIL Client Configuration Generator
#
# Generates a .veil config file from an existing VEIL server installation.
# The generated file can be imported directly into the Windows GUI client
# via the "Import Configuration File..." button in the setup wizard.
#
# Usage:
#   sudo ./generate_client_config.sh [OPTIONS]
#
# Options:
#   -o, --output PATH    Output file path (default: /etc/veil/client-config.veil)
#   -s, --server IP      Override server address (default: auto-detect public IP)
#   -p, --port PORT      Override server port (default: read from server.conf or 4433)
#   -c, --config-dir DIR VEIL config directory (default: /etc/veil)
#   -h, --help           Show this help message
#
# Examples:
#   # Generate config with auto-detected settings
#   sudo ./generate_client_config.sh
#
#   # Generate to a specific path for easy transfer
#   sudo ./generate_client_config.sh -o /tmp/my-vpn.veil
#
#   # Override server address (useful behind NAT)
#   sudo ./generate_client_config.sh -s vpn.example.com
#
#   # Generate with custom port
#   sudo ./generate_client_config.sh -s vpn.example.com -p 5555
#

set -e
set -u
set -o pipefail

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Defaults
CONFIG_DIR="/etc/veil"
OUTPUT_PATH=""
SERVER_OVERRIDE=""
PORT_OVERRIDE=""

log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }

show_help() {
    cat << 'EOF'
VEIL Client Configuration Generator

Generates a .veil config file from an existing VEIL server installation.
The generated file can be imported into the Windows GUI client setup wizard.

USAGE:
    sudo ./generate_client_config.sh [OPTIONS]

OPTIONS:
    -o, --output PATH    Output file path (default: /etc/veil/client-config.veil)
    -s, --server IP      Override server address (default: auto-detect public IP)
    -p, --port PORT      Override server port (default: read from server.conf or 4433)
    -c, --config-dir DIR VEIL config directory (default: /etc/veil)
    -h, --help           Show this help message

EXAMPLES:
    # Generate config with auto-detected settings
    sudo ./generate_client_config.sh

    # Generate to a specific path for easy transfer
    sudo ./generate_client_config.sh -o /tmp/my-vpn.veil

    # Override server address (useful behind NAT or with a domain name)
    sudo ./generate_client_config.sh -s vpn.example.com

    # Full example with all options
    sudo ./generate_client_config.sh -s vpn.example.com -p 5555 -o ~/Desktop/vpn.veil

SECURITY:
    The generated .veil file contains your server's cryptographic keys.
    Transfer it securely using SCP, SFTP, or a USB drive.
    NEVER send via email, chat, or other insecure channels.

For more information, visit: https://github.com/VisageDvachevsky/veil-core
EOF
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -o|--output)
                OUTPUT_PATH="$2"
                shift
                ;;
            --output=*)
                OUTPUT_PATH="${1#*=}"
                ;;
            -s|--server)
                SERVER_OVERRIDE="$2"
                shift
                ;;
            --server=*)
                SERVER_OVERRIDE="${1#*=}"
                ;;
            -p|--port)
                PORT_OVERRIDE="$2"
                shift
                ;;
            --port=*)
                PORT_OVERRIDE="${1#*=}"
                ;;
            -c|--config-dir)
                CONFIG_DIR="$2"
                shift
                ;;
            --config-dir=*)
                CONFIG_DIR="${1#*=}"
                ;;
            -h|--help)
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

    # Set default output path
    if [[ -z "$OUTPUT_PATH" ]]; then
        OUTPUT_PATH="$CONFIG_DIR/client-config.veil"
    fi
}

main() {
    parse_args "$@"

    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║              VEIL Client Configuration Generator                       ║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    # Check for required files
    if [[ ! -f "$CONFIG_DIR/server.key" ]]; then
        log_error "Pre-shared key not found: $CONFIG_DIR/server.key"
        log_error "Is VEIL server installed? Run the server installer first."
        exit 1
    fi

    if [[ ! -f "$CONFIG_DIR/obfuscation.seed" ]]; then
        log_error "Obfuscation seed not found: $CONFIG_DIR/obfuscation.seed"
        log_error "Is VEIL server installed? Run the server installer first."
        exit 1
    fi

    # Determine server address
    local server_address="$SERVER_OVERRIDE"
    if [[ -z "$server_address" ]]; then
        log_info "Auto-detecting server public IP..."
        server_address=$(curl -s --max-time 10 ifconfig.me 2>/dev/null || echo "")
        if [[ -z "$server_address" ]]; then
            server_address=$(curl -s --max-time 10 api.ipify.org 2>/dev/null || echo "")
        fi
        if [[ -z "$server_address" ]]; then
            log_error "Could not auto-detect public IP"
            log_error "Use -s/--server to specify the server address manually"
            exit 1
        fi
        log_info "Detected public IP: $server_address"
    else
        log_info "Using specified server: $server_address"
    fi

    # Determine server port
    local server_port="$PORT_OVERRIDE"
    if [[ -z "$server_port" ]]; then
        if [[ -f "$CONFIG_DIR/server.conf" ]]; then
            server_port=$(grep -E '^\s*listen_port\s*=' "$CONFIG_DIR/server.conf" | grep -v '^#' | sed 's/.*=\s*//' | tr -d ' ')
        fi
        server_port="${server_port:-4433}"
        log_info "Using server port: $server_port"
    else
        log_info "Using specified port: $server_port"
    fi

    # Validate port
    if ! [[ "$server_port" =~ ^[0-9]+$ ]] || [[ "$server_port" -lt 1 ]] || [[ "$server_port" -gt 65535 ]]; then
        log_error "Invalid port number: $server_port"
        exit 1
    fi

    # Encode keys as base64
    log_info "Encoding cryptographic keys..."
    local psk_base64
    psk_base64=$(base64 -w 0 "$CONFIG_DIR/server.key")

    local seed_base64
    seed_base64=$(base64 -w 0 "$CONFIG_DIR/obfuscation.seed")

    # Validate key sizes
    local key_size
    key_size=$(wc -c < "$CONFIG_DIR/server.key")
    if [[ "$key_size" -ne 32 ]]; then
        log_warn "Pre-shared key is $key_size bytes (expected 32)"
    fi

    local seed_size
    seed_size=$(wc -c < "$CONFIG_DIR/obfuscation.seed")
    if [[ "$seed_size" -ne 32 ]]; then
        log_warn "Obfuscation seed is $seed_size bytes (expected 32)"
    fi

    # Generate the .veil config file
    log_info "Generating .veil config file..."

    cat > "$OUTPUT_PATH" <<VEILEOF
{
  "server": {
    "address": "${server_address}",
    "port": ${server_port}
  },
  "crypto": {
    "presharedKey": "${psk_base64}",
    "obfuscationSeed": "${seed_base64}"
  },
  "advanced": {
    "obfuscation": true
  },
  "dpi": {
    "mode": 1
  },
  "routing": {
    "routeAllTraffic": true
  },
  "connection": {
    "autoReconnect": true
  }
}
VEILEOF

    chmod 600 "$OUTPUT_PATH"

    echo ""
    echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              Client Config Generated Successfully                      ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${BOLD}File:${NC} ${OUTPUT_PATH}"
    echo -e "${BOLD}Server:${NC} ${server_address}:${server_port}"
    echo ""
    echo -e "${CYAN}How to use:${NC}"
    echo "  1. Transfer this file to your Windows machine securely"
    echo "  2. Open VEIL VPN client"
    echo "  3. Click 'Import Configuration File...' in the setup wizard"
    echo "  4. Select this .veil file"
    echo "  5. Review settings and click Finish"
    echo ""
    echo -e "${CYAN}Secure transfer examples:${NC}"
    echo "  scp ${OUTPUT_PATH} user@windows-machine:~/Desktop/"
    echo "  rsync -e ssh ${OUTPUT_PATH} user@windows-machine:~/Desktop/"
    echo ""
    echo -e "${YELLOW}⚠ SECURITY WARNING${NC}"
    echo -e "${YELLOW}  This file contains your server's cryptographic keys.${NC}"
    echo -e "${YELLOW}  Transfer securely (SCP/SFTP/USB). NEVER via email or chat.${NC}"
    echo -e "${YELLOW}  Delete the file from the transfer medium after import.${NC}"
    echo ""
    log_success "Done!"
}

main "$@"
