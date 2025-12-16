#!/bin/bash

# OpenLux - Management Script
# Facilitates common project operations
#
# Usage:
#   ./openluxtool.sh              # Interactive mode (menu)
#   ./openluxtool.sh [command]    # Direct command mode
#
# Available commands:
#   build, compile            - Compile the project
#   upload, upload-serial     - Upload via USB
#   upload-ota                - Upload via OTA
#   monitor, monitor-serial   - Monitor via Serial
#   monitor-wifi              - Monitor via WiFi/Telnet
#   format                    - Format code
#   check                     - Verify quality tools
#   lint                      - Run linting
#   quality                   - Full quality build
#   clean                     - Clean build files
#
# Examples:
#   ./openluxtool.sh build        # Compile
#   ./openluxtool.sh format       # Format code
#   ./openluxtool.sh quality      # Full quality build

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configurazione
ESP32_IP="192.168.1.58"
ESP32_HOSTNAME="openlux.local"
TELNET_PORT="23"

# Banner
show_banner() {
    echo -e "${BLUE}"
    echo "╔═══════════════════════════════════════════╗"
    echo "║      OpenLux - Management Script         ║"
    echo "╚═══════════════════════════════════════════╝"
    echo -e "${NC}"
}

# Function to show menu
show_menu() {
    echo ""
    echo -e "${GREEN}=== BUILD & UPLOAD ===${NC}"
    echo "  1) Compile project"
    echo "  2) Upload via USB/Serial (first time)"
    echo "  3) Upload via OTA WiFi (update)"
    echo "  4) Upload + Serial Monitor"
    echo "  5) Upload OTA + WiFi Monitor"
    echo ""
    echo -e "${GREEN}=== MONITORING ===${NC}"
    echo "  6) Serial Monitor (log via USB)"
    echo "  7) WiFi Monitor (log via Telnet)"
    echo ""
    echo -e "${GREEN}=== CODE QUALITY ===${NC}"
    echo "  8) Format code (clang-format)"
    echo "  9) Check quality tools"
    echo " 10) Run linting (pio check)"
    echo " 11) Full quality build (format + lint + compile)"
    echo ""
    echo -e "${GREEN}=== UTILITIES ===${NC}"
    echo " 12) Clean build"
    echo "  0) Exit"
    echo ""
}

# Function to compile
compile() {
    echo -e "${YELLOW}→ Compiling...${NC}"
    pio run
}

# Function for serial upload
upload_serial() {
    echo -e "${YELLOW}→ Uploading via Serial...${NC}"
    echo -e "${BLUE}ℹ Make sure ESP32 is connected via USB${NC}"
    pio run -e openlux -t upload
}

# Function for OTA upload
upload_ota() {
    echo ""
    echo -e "${YELLOW}Select connection method:${NC}"
    echo "  1) Configured IP address (${ESP32_IP})"
    echo "  2) mDNS hostname (${ESP32_HOSTNAME})"
    echo "  3) Enter custom IP"
    echo ""
    read -p "Choice: " ota_choice

    case $ota_choice in
        1)
            echo -e "${YELLOW}→ OTA Upload via IP ${ESP32_IP}...${NC}"
            echo -e "${BLUE}ℹ OTA Password: openlux123${NC}"
            pio run -e openlux-ota -t upload
            ;;
        2)
            echo -e "${YELLOW}→ OTA Upload via hostname ${ESP32_HOSTNAME}...${NC}"
            echo -e "${BLUE}ℹ OTA Password: openlux123${NC}"
            # Temporarily use hostname
            pio run -e openlux-ota -t upload --upload-port $ESP32_HOSTNAME
            ;;
        3)
            read -p "Enter ESP32 IP address: " custom_ip
            echo -e "${YELLOW}→ OTA Upload via IP ${custom_ip}...${NC}"
            echo -e "${BLUE}ℹ OTA Password: openlux123${NC}"
            pio run -e openlux-ota -t upload --upload-port $custom_ip
            ;;
        *)
            echo -e "${RED}✗ Invalid choice${NC}"
            return
            ;;
    esac
}

# Function for serial monitor
monitor_serial() {
    echo -e "${YELLOW}→ Starting Serial Monitor...${NC}"
    echo -e "${BLUE}ℹ Press Ctrl+C to exit${NC}"
    echo ""
    pio device monitor
}

# Function for WiFi monitor (Telnet via netcat)
monitor_wifi() {
    echo ""
    echo -e "${YELLOW}Select connection method:${NC}"
    echo "  1) IP address (${ESP32_IP})"
    echo "  2) mDNS hostname (${ESP32_HOSTNAME})"
    echo "  3) Enter custom IP"
    echo ""
    read -p "Choice: " telnet_choice

    case $telnet_choice in
        1)
            echo -e "${YELLOW}→ Telnet connection to ${ESP32_IP}:${TELNET_PORT}...${NC}"
            echo -e "${BLUE}ℹ Press Ctrl+C to disconnect${NC}"
            echo ""
            nc $ESP32_IP $TELNET_PORT
            ;;
        2)
            echo -e "${YELLOW}→ Telnet connection to ${ESP32_HOSTNAME}:${TELNET_PORT}...${NC}"
            echo -e "${BLUE}ℹ Press Ctrl+C to disconnect${NC}"
            echo ""
            nc $ESP32_HOSTNAME $TELNET_PORT
            ;;
        3)
            read -p "Enter ESP32 IP address: " custom_ip
            echo -e "${YELLOW}→ Telnet connection to ${custom_ip}:${TELNET_PORT}...${NC}"
            echo -e "${BLUE}ℹ Press Ctrl+C to disconnect${NC}"
            echo ""
            nc $custom_ip $TELNET_PORT
            ;;
        *)
            echo -e "${RED}✗ Invalid choice${NC}"
            return
            ;;
    esac
}

# Function for upload + monitor (Serial)
upload_and_monitor_serial() {
    echo -e "${YELLOW}→ Upload via Serial + Monitor...${NC}"
    echo -e "${BLUE}ℹ Make sure ESP32 is connected via USB${NC}"
    pio run -e openlux -t upload -t monitor
}

# Function for OTA upload + WiFi monitor
upload_ota_and_monitor_wifi() {
    echo ""
    echo -e "${YELLOW}Select connection method:${NC}"
    echo "  1) Configured IP address (${ESP32_IP})"
    echo "  2) mDNS hostname (${ESP32_HOSTNAME})"
    echo "  3) Enter custom IP"
    echo ""
    read -p "Choice: " ota_choice

    local target_host=""

    case $ota_choice in
        1)
            target_host=$ESP32_IP
            ;;
        2)
            target_host=$ESP32_HOSTNAME
            ;;
        3)
            read -p "Enter ESP32 IP address: " target_host
            ;;
        *)
            echo -e "${RED}✗ Invalid choice${NC}"
            return
            ;;
    esac

    echo -e "${YELLOW}→ OTA Upload via ${target_host}...${NC}"
    echo -e "${BLUE}ℹ OTA Password: openlux123${NC}"
    echo ""

    if [ "$ota_choice" = "1" ]; then
        pio run -e openlux-ota -t upload
    else
        pio run -e openlux-ota -t upload --upload-port $target_host
    fi

    if [ $? -eq 0 ]; then
        echo ""
        echo -e "${GREEN}✓ Upload completed!${NC}"
        echo -e "${YELLOW}→ Waiting 5 seconds for ESP32 reboot...${NC}"
        sleep 5

        echo -e "${YELLOW}→ Telnet connection to ${target_host}:${TELNET_PORT}...${NC}"
        echo -e "${BLUE}ℹ Press Ctrl+C to disconnect${NC}"
        echo ""
        nc $target_host $TELNET_PORT
    else
        echo -e "${RED}✗ Upload failed${NC}"
    fi
}

# Function to clean
clean() {
    echo -e "${YELLOW}→ Cleaning build...${NC}"
    pio run -t clean
    echo -e "${GREEN}✓ Build cleaned${NC}"
}

# Function to format code
format_code() {
    echo -e "${YELLOW}→ Formatting code...${NC}"

    # Check if clang-format is installed
    if ! command -v clang-format &> /dev/null; then
        echo -e "${RED}✗ clang-format not found!${NC}"
        echo -e "${BLUE}ℹ Install with: brew install clang-format${NC}"
        return 1
    fi

    # Show clang-format version
    CLANG_VERSION=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    echo -e "${GREEN}✓ Using clang-format ${CLANG_VERSION}${NC}"
    echo -e "${BLUE}ℹ Using .clang-format config from project root${NC}"

    # Run clang-format on source files
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
    cd "$PROJECT_ROOT"

    # Collect source files from src/ directory
    echo -e "${BLUE}ℹ Searching for C/C++ files in src/...${NC}"
    FILES=($(find src -type f \( -name "*.cpp" -o -name "*.h" \) 2>/dev/null))

    if [ ${#FILES[@]} -eq 0 ]; then
        echo -e "${YELLOW}⚠ No source files found to format${NC}"
        return 0
    fi

    echo -e "${BLUE}ℹ Formatting ${#FILES[@]} files...${NC}"

    # Format each file using project's .clang-format config
    for file in "${FILES[@]}"; do
        echo -e "  → ${file}"
        clang-format -i --style=file "$file"
        if [ $? -ne 0 ]; then
            echo -e "${RED}✗ clang-format failed on $file${NC}"
            return 1
        fi
    done

    echo -e "${GREEN}✓ Formatting completed successfully${NC}"
    echo -e "${BLUE}ℹ Formatted ${#FILES[@]} files${NC}"
    echo -e "${BLUE}ℹ Run 'git diff' to see changes${NC}"
}

# Function to check quality tools
check_quality_tools() {
    echo -e "${YELLOW}→ Checking quality tools...${NC}"
    local missing=0

    if command -v clang-format >/dev/null 2>&1; then
        echo -e "${GREEN}✓ clang-format found${NC}"
    else
        echo -e "${YELLOW}⚠ clang-format not found (format command will fail)${NC}"
        missing=1
    fi

    if command -v clang-tidy >/dev/null 2>&1; then
        echo -e "${GREEN}✓ clang-tidy found${NC}"
    else
        echo -e "${YELLOW}⚠ clang-tidy not found (pio check may be limited)${NC}"
        missing=1
    fi

    if command -v cppcheck >/dev/null 2>&1; then
        echo -e "${GREEN}✓ cppcheck found${NC}"
    else
        echo -e "${YELLOW}⚠ cppcheck not found (pio check may be limited)${NC}"
        missing=1
    fi

    if [ $missing -ne 0 ]; then
        echo -e "${BLUE}ℹ Install on macOS: brew install clang-format clang-tidy cppcheck${NC}"
        echo -e "${BLUE}ℹ Install on Linux: sudo apt install clang-format clang-tidy cppcheck${NC}"
    fi

    return 0
}

# Function to run linting
run_linting() {
    echo -e "${YELLOW}→ Running linting...${NC}"
    echo -e "${BLUE}ℹ Uses platformio check on env 'openlux'${NC}"
    echo ""

    cd "$(dirname "${BASH_SOURCE[0]}")/.."

    # If tools are missing, warn but still attempt; PlatformIO will use what is available
    if ! command -v clang-tidy >/dev/null 2>&1; then
        echo -e "${YELLOW}⚠ clang-tidy not found; PlatformIO check may skip it${NC}"
    fi
    if ! command -v cppcheck >/dev/null 2>&1; then
        echo -e "${YELLOW}⚠ cppcheck not found; PlatformIO check may skip it${NC}"
    fi

    pio check -e openlux
    STATUS=$?

    echo ""
    if [ $STATUS -eq 0 ]; then
        echo -e "${GREEN}✓ Linting completed${NC}"
    else
        echo -e "${YELLOW}⚠ Linting encountered issues (see above). Not failing the script.${NC}"
        return 0
    fi
}

# Function for full quality build
compile_with_quality() {
    echo -e "${YELLOW}→ Full quality build...${NC}"
    echo ""

    # 1. Format code
    echo -e "${BLUE}[1/3] Formatting code...${NC}"
    format_code
    if [ $? -ne 0 ]; then
        echo -e "${RED}✗ Formatting failed${NC}"
        return 1
    fi

    echo ""
    echo -e "${BLUE}[2/3] Linting...${NC}"
    run_linting || true

    echo ""
    echo -e "${BLUE}[3/3] Compiling...${NC}"
    compile

    if [ $? -eq 0 ]; then
        echo ""
        echo -e "${GREEN}✓✓✓ Full build completed successfully! ✓✓✓${NC}"
        echo -e "${GREEN}Code is formatted, checked, and compiled.${NC}"
    else
        echo -e "${RED}✗ Compilation failed${NC}"
        return 1
    fi
}

# ==============================================================================
# MAIN
# ==============================================================================


show_banner

if [ $# -gt 0 ]; then
    command=$1
    shift

    case $command in
        build|compile)
            compile
            exit $?
            ;;
        upload|upload-serial)
            upload_serial
            exit $?
            ;;
        upload-ota)
            upload_ota
            exit $?
            ;;
        monitor|monitor-serial)
            monitor_serial
            exit $?
            ;;
        monitor-wifi)
            monitor_wifi
            exit $?
            ;;
        format)
            format_code
            exit $?
            ;;
        check)
            check_quality_tools
            exit $?
            ;;
        lint)
            run_linting
            exit $?
            ;;
        quality)
            compile_with_quality
            exit $?
            ;;
        clean)
            clean
            exit $?
            ;;
        help|--help|-h)
            echo "Usage: $0 [command]"
            echo ""
            echo "Available commands:"
            echo "  build, compile            - Compile the project"
            echo "  upload, upload-serial     - Upload via USB"
            echo "  upload-ota                - Upload via OTA"
            echo "  monitor, monitor-serial   - Monitor via Serial"
            echo "  monitor-wifi              - Monitor via WiFi/Telnet"
            echo "  format                    - Format code"
            echo "  check                     - Check quality tools"
            echo "  lint                      - Run linting"
            echo "  quality                   - Full quality build"
            echo "  clean                     - Clean build files"
            echo ""
            echo "Without arguments: starts interactive mode (menu)"
            exit 0
            ;;
        *)
            echo -e "${RED}✗ Unrecognized command: $command${NC}"
            echo "Use '$0 help' to see available commands"
            exit 1
            ;;
    esac
fi

# Main loop
while true; do
    show_menu
    read -p "Choice: " choice
    echo ""

    case $choice in
        1)
            compile
            ;;
        2)
            upload_serial
            ;;
        3)
            upload_ota
            ;;
        4)
            upload_and_monitor_serial
            ;;
        5)
            upload_ota_and_monitor_wifi
            ;;
        6)
            monitor_serial
            ;;
        7)
            monitor_wifi
            ;;
        8)
            format_code
            ;;
        9)
            check_quality_tools
            ;;
        10)
            run_linting
            ;;
        11)
            compile_with_quality
            ;;
        12)
            clean
            ;;
        0)
            echo -e "${GREEN}Goodbye!${NC}"
            exit 0
            ;;
        *)
            echo -e "${RED}✗ Invalid choice, please try again${NC}"
            ;;
    esac

    echo ""
    read -n1 -s -r -p "Press any key to continue..."
    echo ""
done
