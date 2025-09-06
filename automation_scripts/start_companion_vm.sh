#!/bin/bash
#
# Companion VM Script
# This script starts a companion VM for QEMUAppleSilicon
# Companion VM is used to connect to the iPhone
# There is an issue where the USB Connection will occasionally have a re-entrant IO error
# When this happens, the companion VM will need to be restarted and the USB Connection will need to be reconnected
# This means you need to restart the iOS VM and reconnect the USB Connection as well.
#
# This script will create a new disk if it doesn't exist
# It will also create a new NVRAM file if it doesn't exist
# It will also create a new EFI_VARS file if it doesn't exist
#
QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
QEMU="${QEMU_DIR}/build/qemu-system-aarch64-unsigned"
ISO="${QEMU_DIR}/automation_scripts/isos/debian-13.0.0-arm64-netinst.iso"
ISO_URL="https://cdimage.debian.org/debian-cd/current/arm64/iso-cd/debian-13.0.0-arm64-netinst.iso"
DISK="${QEMU_DIR}/automation_scripts/disks/debian.qcow2"
MODE=${1:-run}
USB_TYPE=${2:-unix}
USB_ADDR=${3:-"/tmp/usb_connection"}
USB_PORT=${4:-0}

# Create disk if it doesn't exist
if [ ! -f "$DISK" ]; then
    echo "Disk $DISK does not exist. Creating..."
    "${QEMU_DIR}/build/qemu-img" create -f qcow2 "$DISK" 20G
fi

# Download ISO if it doesn't exist
if [ ! -f "$ISO" ]; then
    echo "ISO $ISO does not exist. Downloading..."
    wget -q "$ISO_URL" -O "$ISO"
fi

# Set EFI DIR, CODE, and VARS
EFI_DIR="$(brew --prefix qemu)/share/qemu"
EFI_CODE="$EFI_DIR/edk2-aarch64-code.fd"
EFI_VARS="$HOME/Downloads/edk2-aarch64-vars.fd"

# Create a blank 64 MiB NVRAM file
dd if=/dev/zero of="$EFI_VARS" bs=1m count=64

# Run QEMU
"$QEMU" \
  -accel tcg,thread=multi \
  -M virt \
  -cpu cortex-a72 \
  -m 2048 \
  -device virtio-gpu-pci \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -device usb-mouse,bus=xhci.0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -drive if=none,file="$DISK",format=qcow2,id=hd0 \
  -device virtio-blk-pci,drive=hd0 \
  -device virtio-scsi-pci,id=scsi0 \
  -boot order=d \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$EFI_CODE" \
  -drive if=pflash,format=raw,unit=1,file="$EFI_VARS" \
  -usb -device usb-ehci,id=ehci \
  -device usb-tcp-remote,bus=xhci.0,conn-type=${USB_TYPE},conn-addr=${USB_ADDR}$([ "$USB_TYPE" != "unix" ] && echo ",conn-port=${USB_PORT}"),bus=ehci.0
