#!/bin/bash
# Script to patch iOS filesystem for QEMUAppleSilicon
# Usage: ./patch_filesystem.sh
# Prerequisites: System volume must be mounted (use ./mount_apfs_disk.sh first)
# Note: This script requires sudo privileges for filesystem modifications

set -e  # Exit on any error

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Color functions
print_info() {
    echo -e "${CYAN}ℹ️  $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_header() {
    echo -e "${PURPLE}${BOLD}🔧 $1${NC}"
}

print_step() {
    echo -e "${BLUE}🔄 $1${NC}"
}

# Script variables
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_VOLUME="/Volumes/System"
DYLD_CACHE_PATH="${SYSTEM_VOLUME}/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e"
LAUNCHD_PLIST_PATH="${SYSTEM_VOLUME}/System/Library/xpc/launchd.plist"
PATCH_DYLD_SH_URL="https://github.com/ChefKissInc/QEMUAppleSiliconTools/raw/refs/heads/master/PatchDYLD.sh"
PATCH_DYLD_FISH_URL="https://github.com/ChefKissInc/QEMUAppleSiliconTools/raw/refs/heads/master/PatchDYLD.fish"

# Function to check prerequisites
check_prerequisites() {
    print_step "Checking prerequisites..."
    
    # Check if System volume is mounted
    if [ ! -d "$SYSTEM_VOLUME" ]; then
        print_error "System volume not mounted at $SYSTEM_VOLUME"
        print_info "Please run ./mount_apfs_disk.sh first to mount the filesystem"
        exit 1
    fi
    
    # Check if running as root or with sudo
    if [ "$EUID" -ne 0 ]; then
        print_error "This script requires sudo privileges"
        print_info "Please run: sudo $0"
        exit 1
    fi
    
    # Check if dyld cache exists
    if [ ! -f "$DYLD_CACHE_PATH" ]; then
        print_error "Dyld shared cache not found at $DYLD_CACHE_PATH"
        exit 1
    fi
    
    # Check if launchd.plist exists
    if [ ! -f "$LAUNCHD_PLIST_PATH" ]; then
        print_error "Launch daemon plist not found at $LAUNCHD_PLIST_PATH"
        exit 1
    fi
    
    # Check if required tools are available
    for tool in curl plutil; do
        if ! command -v "$tool" &> /dev/null; then
            print_error "Required tool '$tool' not found"
            exit 1
        fi
    done
    
    print_success "All prerequisites met"
}

# Function to patch Dyld Shared Cache
patch_dyld_cache() {
    print_header "Patching Dyld Shared Cache"
    
    # Create backup of original dyld cache
    print_step "Creating backup of original dyld shared cache..."
    cp "$DYLD_CACHE_PATH" "${SCRIPT_DIR}/dyld_shared_cache_arm64e.orig"
    print_success "Backup created: ${SCRIPT_DIR}/dyld_shared_cache_arm64e.orig"
    
    # Determine which shell to use and download appropriate patch script
    if command -v fish &> /dev/null; then
        print_info "Fish shell detected, downloading PatchDYLD.fish..."
        PATCH_SCRIPT="${SCRIPT_DIR}/PatchDYLD.fish"
        curl -L -o "$PATCH_SCRIPT" "$PATCH_DYLD_FISH_URL"
        SHELL_CMD="fish"
    else
        print_info "Using bash/sh, downloading PatchDYLD.sh..."
        PATCH_SCRIPT="${SCRIPT_DIR}/PatchDYLD.sh"
        curl -L -o "$PATCH_SCRIPT" "$PATCH_DYLD_SH_URL"
        SHELL_CMD="bash"
    fi
    
    if [ ! -f "$PATCH_SCRIPT" ]; then
        print_error "Failed to download patch script"
        exit 1
    fi
    
    print_success "Patch script downloaded: $PATCH_SCRIPT"
    
    # Make patch script executable and run it
    print_step "Making patch script executable and running..."
    chmod +x "$PATCH_SCRIPT"
    
    # Change to script directory and run patch
    cd "$SCRIPT_DIR"
    if [ "$SHELL_CMD" = "fish" ]; then
        fish "$PATCH_SCRIPT"
    else
        bash "$PATCH_SCRIPT"
    fi
    
    print_success "Dyld shared cache patching completed"
}

# Function to disable problematic launch services
disable_launch_services() {
    print_header "Disabling Problematic Launch Services"
    
    # Services to disable
    SERVICES_TO_DISABLE=(
        "com.apple.voicemail.vmd"
        "com.apple.CommCenter"
        "com.apple.locationd"
    )
    
    # Set proper locale to handle UTF-8
    export LC_ALL=C
    
    # Create backup of original launchd.plist
    print_step "Creating backup of original launchd.plist..."
    cp "$LAUNCHD_PLIST_PATH" "${SCRIPT_DIR}/launchd.plist.orig"
    print_success "Backup created: ${SCRIPT_DIR}/launchd.plist.orig"
    
    # Convert binary plist to XML format
    print_step "Converting launchd.plist to XML format..."
    plutil -convert xml1 "$LAUNCHD_PLIST_PATH"
    print_success "Converted to XML format"
    
    # Disable each service using the working Python approach
    for service in "${SERVICES_TO_DISABLE[@]}"; do
        print_step "Disabling service: $service"
        
        # Use the exact working Python code from test-patches.sh
        python3 - << EOF
import plistlib
import sys

try:
    with open('$LAUNCHD_PLIST_PATH', 'rb') as f:
        plist_data = plistlib.load(f)
    
    # Check if services are nested under LaunchDaemons
    if 'LaunchDaemons' in plist_data:
        launch_daemons = plist_data['LaunchDaemons']
        
        if isinstance(launch_daemons, dict):
            # Look for our specific service
            service_name = '$service'
            found_services = []
            for key in launch_daemons.keys():
                if service_name in key:
                    found_services.append(key)
            
            if found_services:
                # Disable all matching services
                for target_service in found_services:
                    launch_daemons[target_service]['Disabled'] = True
                    print(f"Disabled: {target_service}")
                
                # Save the modified plist back to original file
                with open('$LAUNCHD_PLIST_PATH', 'wb') as f:
                    plistlib.dump(plist_data, f)
            else:
                print(f"No services found matching '{service_name}'")
        else:
            print(f"LaunchDaemons is not a dict: {launch_daemons}")
    else:
        print("No LaunchDaemons key found in plist")
        
except Exception as e:
    print(f"Python plist patching failed: {e}")
    sys.exit(1)
EOF
        
        if [ $? -eq 0 ]; then
            print_success "Successfully disabled: $service"
        else
            print_warning "Failed to disable or service not found: $service"
        fi
    done
    
    # Validate the modified plist
    print_step "Validating modified plist..."
    if plutil -lint "$LAUNCHD_PLIST_PATH" > /dev/null 2>&1; then
        print_success "Modified plist is valid"
    else
        print_error "Modified plist is invalid, restoring backup"
        cp "${SCRIPT_DIR}/launchd.plist.orig" "$LAUNCHD_PLIST_PATH"
        exit 1
    fi
    
    print_success "Launch services patching completed"
}

# Function to display completion information
show_completion_info() {
    print_header "Filesystem Patching Complete! 🎉"
    
    print_info "Files backed up to:"
    echo "  • ${WHITE}${SCRIPT_DIR}/dyld_shared_cache_arm64e.orig${NC} - Original dyld cache"
    echo "  • ${WHITE}${SCRIPT_DIR}/launchd.plist.orig${NC} - Original launch daemon plist"
    
    print_warning "Important Notes:"
    echo "  • You ${BOLD}MUST${NC} use the ${YELLOW}launchd_unsecure_cache=1${NC} boot argument"
    echo "  • This is required until you restore the original service cache"
    echo "  • The iPhone VM will not boot properly without this argument"
    
    print_info "Next Steps:"
    echo "  1. Unmount the filesystem: ${CYAN}./automation_scripts/unmount_apfs_disk.sh${NC}"
    echo "  2. Start the iPhone VM: ${CYAN}./automation_scripts/run_iphone.sh${NC}"
    echo "  3. Wait for data migration and setup screen to appear"
    
    print_success "Filesystem patching completed successfully!"
}

# Main execution
main() {
    print_header "iOS Filesystem Patcher for QEMUAppleSilicon"
    
    print_warning "This script will modify critical iOS system files"
    print_info "Press Ctrl+C within 5 seconds to cancel..."
    sleep 5
    
    check_prerequisites
    patch_dyld_cache
    disable_launch_services
    show_completion_info
}

# Run main function
main "$@"
