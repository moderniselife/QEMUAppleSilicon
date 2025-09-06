# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

QEMUAppleSilicon is a fork of QEMU that provides Apple ARM device guest support, specifically targeting iPhone 11 running iOS 14.0 beta 5 on the T8030 SoC. This is a complex emulation project that implements Apple-specific hardware components and boot processes.

## Development Commands

### Build System
This project uses a dual build system: autotools/configure + Make, with Meson as the underlying build system.

**Initial Setup:**
```bash
# Install dependencies (automatically handled by setup script)
./setup_qemu_apple_silicon.sh

# Manual configuration for development
mkdir build && cd build
../configure --target-list=aarch64-softmmu
```

**Core Development Commands:**
```bash
# Configure build (development)
../configure --target-list=aarch64-softmmu --enable-debug --disable-werror

# Build the project
make -j$(nproc)

# Build specific component 
make -j$(nproc) qemu-system-aarch64

# Clean build
make clean

# Distclean (removes all build artifacts)
make distclean
```

**Running the Emulator:**
```bash
# Use the provided convenience script
./run_iphone.sh [restore|run] [usb_type] [usb_addr] [usb_port]

# Manual execution example
./build/qemu-system-aarch64 -M t8030 -cpu max -smp 7 -m 4G \
  -serial mon:stdio -device ramfb \
  -kernel firmware/kernelcache.research.iphone12b \
  -dtb firmware/DeviceTree.n104ap.im4p
```

### Testing
```bash
# Run built-in tests
make check

# Run specific test suites
make check-qtest
```

## Architecture Overview

### Core Components

**Hardware Abstraction Layer:**
- `hw/arm/apple-silicon/` - Apple Silicon SoC implementations
  - `t8030.c` - Main T8030 (iPhone 11) SoC implementation
  - `a13.c` - A13 processor emulation
  - Device-specific implementations (DART, SEP, AIC, etc.)

**Key SoC Features:**
- **T8030 SoC**: iPhone 11 SoC with A13 Bionic processor emulation
- **Hardware Devices**: AIC (interrupt controller), DART (IOMMU), SEP (Secure Enclave), SPMI (power management)
- **Boot Process**: Custom Apple boot sequence with IMG4 support, SEP firmware, and device tree processing

**Critical Subsystems:**
- **Boot System** (`hw/arm/apple-silicon/boot.c`): Handles Apple-specific boot sequence
- **Memory Management**: Custom AMCC (Apple Memory Cache Controller) implementation
- **Security**: SEP (Secure Enclave Processor) emulation and IMG4 format support
- **I/O**: Custom Apple I2C, SPI, UART implementations
- **Display**: DisplayPipe v4 implementation for iPhone screen emulation

### Build Architecture

**Configuration System:**
- Primary: `configure` script (autotools-based)
- Backend: Meson build system (`meson.build`)
- Platform configs: `hw/arm/apple-silicon/t8030-config.c.inc`

**Key Directories:**
- `hw/arm/apple-silicon/` - Apple-specific hardware implementations
- `include/hw/arm/apple-silicon/` - Apple hardware headers
- `target/arm/` - ARM CPU emulation (Apple A13 extends this)
- `hw/misc/apple-silicon/` - Apple peripheral devices
- `hw/audio/apple-silicon/` - Audio subsystem components

### Apple-Specific Features

**Firmware Handling:**
- IMG4 format support for encrypted firmware components
- SEP (Secure Enclave Processor) firmware loading and execution
- Device Tree Blob (DTB) processing for hardware configuration
- TrustCache validation for code signing

**Security Model:**
- ECID (Exclusive Chip Identification) for device-specific operations
- AP (Application Processor) ticket validation
- SEP ROM and firmware authentication
- Secure boot chain implementation

**Hardware Emulation Specifics:**
- Custom NVME controller for iOS storage emulation
- Apple GPIO implementation
- SPMI bus for power management IC communication
- USB Type-C controller emulation for device connectivity

## Development Guidelines

### Working with Apple Silicon Code

**Key Files to Understand:**
- `hw/arm/apple-silicon/t8030.c` - Main SoC implementation, start here
- `include/hw/arm/apple-silicon/t8030.h` - SoC structure definitions
- `hw/arm/apple-silicon/boot.c` - Apple boot process implementation

**Common Development Patterns:**
- Apple devices use custom memory maps defined in config includes
- Hardware registers are typically memory-mapped with specific offsets
- Most Apple hardware requires firmware blobs and device tree configuration
- SEP operations require careful coordination between AP and SEP firmware

### Firmware Requirements

The emulator requires specific firmware files:
- iOS IPSW (iPhone Software) containing kernel and device tree
- SEP firmware (sep-firmware.n104.RELEASE.new.img4)
- SEP ROM (AppleSEPROM-Cebu-B1)
- AP ticket (root_ticket.der) for secure boot validation

### USB Connectivity

The project implements USB passthrough via TCP connection:
- Unix socket: `/tmp/usb_connection`
- TCP modes: IPv4 with custom addressing
- Companion VM setup required for full USB functionality

### Memory Layout

Critical memory regions for T8030:
- SROM: 0x100000000 (512KB) - Secure ROM
- SRAM: 0x19C000000 (4MB) - Static RAM  
- DRAM: 0x800000000 - Main system RAM
- SEPROM: 0x240000000 (8MB) - SEP ROM space

## Project-Specific Considerations

### Development Environment
- Requires Apple-specific firmware files not included in repository
- Setup script (`setup_qemu_apple_silicon.sh`) automates dependency installation
- macOS and Linux development supported (Windows not tested)

### Contributing Guidelines
- Apple Silicon hardware implementations require deep understanding of iOS internals
- Device implementations should follow existing patterns in `hw/arm/apple-silicon/`
- Memory-mapped devices use consistent register offset patterns
- SEP-related changes require careful testing with encrypted firmware

### Debugging
- Use `-serial mon:stdio` for QEMU monitor access
- SEP operations can be traced via custom debug flags
- Device tree modifications require corresponding code changes
- Firmware authentication failures are common during development

