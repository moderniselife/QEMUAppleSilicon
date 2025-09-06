#!/bin/bash
# Script to mount the root disk
# Usage: ./mount_apfs_disk.sh [disk]
# Example: ./mount_apfs_disk.sh root  
# Options: root, firmware, syscfg, ctrl_bits, nvram, effaceable, panic_log, sep_nvram, sep_ssc
# Note: You need to have the disk created first
# You can create the disks using ./create_disks.sh
# Default is root

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

QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
OUTPUT_DIR="${QEMU_DIR}/output"
DISK=${1:-root}

print_header "APFS Disk Mounting Tool"
print_info "Mounting disk: ${BOLD}${DISK}${NC}"
print_info "Disk path: ${WHITE}${OUTPUT_DIR}/${DISK}${NC}"

# Check if disk exists
if [ ! -f "${OUTPUT_DIR}/${DISK}" ]; then
    print_error "Disk ${DISK} does not exist. Please create it first using ./create_disks.sh"
    exit 1
fi

# Attach the disk image and capture output to get disk identifier
print_step "Attaching disk image..."
ATTACH_OUTPUT=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -blocksize 4096 "${OUTPUT_DIR}/${DISK}" 2>&1)

# Extract the disk identifier from the attach output
# The output typically contains lines like "/dev/disk6    GUID_partition_scheme"
DISK_IDENTIFIER=$(echo "$ATTACH_OUTPUT" | grep -E '^/dev/disk[0-9]+' | awk '{print $1}' | head -n 1)

if [ -z "$DISK_IDENTIFIER" ]; then
    print_error "Failed to find disk identifier from attach output"
    print_info "Attach output was:"
    echo "$ATTACH_OUTPUT"
    exit 1
fi

print_success "Disk attached as: ${BOLD}${DISK_IDENTIFIER}${NC}"

# Check if System volume is already mounted
if [ -d "/Volumes/System" ]; then
    print_info "System volume is already mounted"
    # Enable ownership on the System volume
    print_step "Enabling ownership on /Volumes/System..."
    sudo diskutil enableownership /Volumes/System
    # Unlock the System volume
    print_step "Unlocking System volume..."
    sudo diskutil unlockVolume /Volumes/System

    # Enable ownership on the System volume
    # print_step "Enabling ownership on /Volumes/System..."
    # sudo mount_apfs -o nobrowse,rw /dev/disk5s1 /Volumes/System
else
    print_step "System volume not found, attempting to mount all volumes on disk..."
    # Mount all volumes on the disk
    # diskutil mountDisk "$DISK_IDENTIFIER"
    sudo mkdir -p /Volumes/System
    sudo mount_apfs -o nobrowse,rw /dev/disk5s1 /Volumes/System
    
    # Wait a moment for mount to complete
    sleep 2
    
    # Enable ownership if System volume is now mounted
    if [ -d "/Volumes/System" ]; then
        print_step "Enabling ownership on /Volumes/System..."
        sudo diskutil enableownership /Volumes/System
    else
        print_warning "System volume still not found after mounting"
    fi
fi

print_info "Current disk layout:"
diskutil list

print_info "Mounted volumes:"
ls -la /Volumes/ | grep -E "(System|Hardware|Preboot|xART)" || print_warning "No APFS volumes found in /Volumes/"

print_success "Script completed successfully! 🎉"
