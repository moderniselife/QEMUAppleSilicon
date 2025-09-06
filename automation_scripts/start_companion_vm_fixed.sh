#!/bin/bash

# Fixed Companion VM startup script
QEMU="/Users/josephshenton/Projects/QEMUAppleSilicon/build/qemu-system-aarch64-unsigned"
EFI_CODE="/opt/homebrew/share/qemu/edk2-aarch64-code.fd"
EFI_VARS="/Users/josephshenton/Downloads/efi_vars.fd"
ISO="/Users/josephshenton/Downloads/debian-13.0.0-arm64-netinst.iso"
DISK="/Users/josephshenton/Downloads/debian.qcow2"
MODE=run
USB_TYPE=unix
USB_ADDR="/tmp/usb_connection"
USB_PORT=0

EFI_VARS="$HOME/Downloads/edk2-aarch64-vars.fd"

# Create a blank 64 MiB NVRAM file
dd if=/dev/zero of="$EFI_VARS" bs=1m count=64

# Ensure socket exists (iPhone VM should create it)
if [ ! -S "$USB_ADDR" ]; then
    echo "Warning: USB socket $USB_ADDR not found. Make sure iPhone VM is running first."
fi

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
  -device usb-tcp-remote,conn-type=ipv4,conn-addr=127.0.0.1,conn-port=8030,bus=ehci.0
#   -device usb-tcp-remote,bus=ehci.0,conn-type=${USB_TYPE},conn-addr=${USB_ADDR}$([ "$USB_TYPE" != "unix" ] && echo ",conn-port=${USB_PORT}")
