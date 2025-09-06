#!/bin/bash
# Script to unmount APFS disk images
# Usage: ./unmount-apfs-disk.sh [disk]
# Example: ./unmount-apfs-disk.sh root
# Options: root, firmware, syscfg, ctrl_bits, nvram, effaceable, panic_log, sep_nvram, sep_ssc
# Default: root

QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
OUTPUT_DIR="${QEMU_DIR}/output"
DISK=${1:-root}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Unmounting iOS disk image: ${DISK}${NC}"

# Find the disk image that corresponds to our file
DISK_PATH="${OUTPUT_DIR}/${DISK}"

if [ ! -f "$DISK_PATH" ]; then
    echo -e "${RED}Disk file ${DISK_PATH} does not exist.${NC}"
    exit 1
fi

echo "Searching for mounted disk images..."

# Find all mounted disk images that match our disk name or show all iOS-related disk images
echo "Checking for iOS disk images..."

# Method 1: Try to find by exact path match
DISK_IDENTIFIER=$(hdiutil info | grep -A 1 "$DISK_PATH" | grep "/dev/disk" | awk '{print $1}' | head -n 1)

# Method 2: If no exact match, look for attached iOS disk images
if [ -z "$DISK_IDENTIFIER" ]; then
    echo "No exact path match found, searching for attached iOS disk images..."
    
    # Find disk images with iOS APFS containers
    echo "Scanning for iOS disk images..."
    
    IOS_DISKS=""
    for disk_img in $(diskutil list | grep "disk image" | awk '{print $1}'); do
        # Extract just the base disk number (e.g., /dev/disk4 -> 4)
        disk_base_num=$(echo "$disk_img" | sed 's|/dev/disk||')
        # The synthesized container is usually disk_base_num + 1
        synth_container_num=$((disk_base_num + 1))
        synth_disk="disk$synth_container_num"
        
        echo "Checking $disk_img -> container $synth_disk for iOS volumes..."
        
        # Check if this synthesized container has iOS characteristics
        if diskutil apfs list | grep -A 30 "Container $synth_disk" | grep -q "xART\|Hardware"; then
            echo "Found iOS container in $synth_disk (from image $disk_img)"
            IOS_DISKS="$IOS_DISKS $disk_img"
        fi
    done
    
    if [ -n "$IOS_DISKS" ]; then
        echo "Found iOS disk images:$IOS_DISKS"
        
        # Use the first one found
        DISK_IDENTIFIER=$(echo "$IOS_DISKS" | awk '{print $1}')
        echo -e "${YELLOW}Will detach: $DISK_IDENTIFIER${NC}"
    else
        echo -e "${RED}No iOS disk images found automatically.${NC}"
        echo "Available disk images:"
        diskutil list | grep "disk image"
        echo "Please run: hdiutil detach /dev/diskX manually"
        exit 1
    fi
fi

if [ -z "$DISK_IDENTIFIER" ]; then
    echo -e "${RED}Could not find disk identifier for mounted image${NC}"
    exit 1
fi

echo -e "${GREEN}Found mounted disk: ${DISK_IDENTIFIER}${NC}"

# Show what volumes are mounted before unmounting
echo "Currently mounted volumes:"
diskutil list | grep "$DISK_IDENTIFIER" -A 10

# Unmount all volumes first
echo -e "${YELLOW}Unmounting all volumes...${NC}"
diskutil unmountDisk "$DISK_IDENTIFIER"

# Detach the disk image
echo -e "${YELLOW}Detaching disk image...${NC}"
hdiutil detach "$DISK_IDENTIFIER"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Successfully unmounted ${DISK} disk image${NC}"
else
    echo -e "${RED}Failed to unmount ${DISK} disk image${NC}"
    echo "You may need to force unmount with: hdiutil detach ${DISK_IDENTIFIER} -force"
    exit 1
fi