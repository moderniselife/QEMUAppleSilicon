#!/bin/bash

# QEMUAppleSilicon Start Script
# Usage: ./start_iphone.sh [restore|run]

# Configuration
QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${QEMU_DIR}/build"
FIRMWARE_DIR="${QEMU_DIR}/firmware"
OUTPUT_DIR="${QEMU_DIR}/output"

# Check if QEMU is built
if [ ! -f "${BUILD_DIR}/qemu-system-aarch64" ]; then
    echo "Error: QEMU not found. Please run the setup script first."
    exit 1
fi

# Check for required files
if [ ! -f "${FIRMWARE_DIR}/sep-firmware.n104.RELEASE.new.img4" ] || \
   [ ! -f "${FIRMWARE_DIR}/AppleSEPROM-Cebu-B1" ] || \
   [ ! -f "${FIRMWARE_DIR}/root_ticket.der" ]; then
    echo "Error: Required firmware files not found in ${FIRMWARE_DIR}/"
    echo "Please make sure you have:"
    echo "  - sep-firmware.n104.RELEASE.new.img4"
    echo "  - AppleSEPROM-Cebu-B1"
    echo "  - root_ticket.der"
    exit 1
fi

# Set mode (default to 'run' if not specified)
MODE=${1:-run}

# Common QEMU arguments
QEMU_ARGS=(
    -M t8030
    -cpu max
    -smp 7
    -m 4G
    -serial mon:stdio
    -device ramfb
    -device usb-ehci
    -device usb-kbd
    -device usb-mouse
    -drive file=${OUTPUT_DIR}/sep_nvram,if=pflash,format=raw
    -drive file=${OUTPUT_DIR}/sep_ssc,if=pflash,format=raw
    -drive file=${OUTPUT_DIR}/root,format=raw,if=none,id=root -device nvme-ns,drive=root,bus=nvme-bus.0,nsid=1,nstype=1,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/firmware,format=raw,if=none,id=firmware -device nvme-ns,drive=firmware,bus=nvme-bus.0,nsid=2,nstype=2,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/syscfg,format=raw,if=none,id=syscfg -device nvme-ns,drive=syscfg,bus=nvme-bus.0,nsid=3,nstype=3,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/ctrl_bits,format=raw,if=none,id=ctrl_bits -device nvme-ns,drive=ctrl_bits,bus=nvme-bus.0,nsid=4,nstype=4,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/nvram,if=none,format=raw,id=nvram -device apple-nvram,drive=nvram,bus=nvme-bus.0,nsid=5,nstype=5,id=nvram,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/effaceable,format=raw,if=none,id=effaceable -device nvme-ns,drive=effaceable,bus=nvme-bus.0,nsid=6,nstype=6,logical_block_size=4096,physical_block_size=4096
    -drive file=${OUTPUT_DIR}/panic_log,format=raw,if=none,id=panic_log -device nvme-ns,drive=panic_log,bus=nvme-bus.0,nsid=7,nstype=8,logical_block_size=4096,physical_block_size=4096
    -device nvme,serial=nvme-1,id=nvme-bus.0
)

# Add display options based on OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    QEMU_ARGS+=(-display cocoa,zoom-to-fit=on,zoom-interpolation=on,show-cursor=on)
else
    if command -v gtk-launch >/dev/null 2>&1; then
        QEMU_ARGS+=(-display gtk,zoom-to-fit=on,show-cursor=on)
    else
        QEMU_ARGS+=(-display sdl,show-cursor=on)
    fi
fi

# Add mode-specific arguments
if [ "$MODE" = "restore" ]; then
    echo "Starting in restore mode..."
    if [ ! -f "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/038-44135-124.dmg" ]; then
        echo "Error: Restore image not found. Please make sure the IPSW was extracted correctly."
        exit 1
    fi
    
    QEMU_ARGS+=(
        -kernel "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/kernelcache.research.iphone12b"
        -dtb "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/Firmware/all_flash/DeviceTree.n104ap.im4p"
        -initrd "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/038-44135-124.dmg"
        -append "rd=md0 -v"
    )
else
    echo "Starting in normal mode..."
    QEMU_ARGS+=(
        -kernel "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/kernelcache.research.iphone12b"
        -dtb "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/Firmware/all_flash/DeviceTree.n104ap.im4p"
        -append "tlto_us=-1 mtxspin=-1 agm-genuine=1 agm-authentic=1 agm-trusted=1 serial=3 launchd_unsecure_cache=1 wdt=-1 -vm_compressor_wk_sw"
    )
fi

# Add machine-specific arguments
QEMU_ARGS+=(
    -machine "trustcache=${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/Firmware/038-44135-124.dmg.trustcache"
    -machine "ticket=${FIRMWARE_DIR}/root_ticket.der"
    -machine "sep-fw=${FIRMWARE_DIR}/sep-firmware.n104.RELEASE.new.img4"
    -machine "sep-rom=${FIRMWARE_DIR}/AppleSEPROM-Cebu-B1"
    -machine "kaslr-off=true"
)

# Run QEMU
echo "Starting QEMU with arguments:"
echo "${BUILD_DIR}/qemu-system-aarch64" "${QEMU_ARGS[@]}"
exec "${BUILD_DIR}/qemu-system-aarch64" "${QEMU_ARGS[@]}"