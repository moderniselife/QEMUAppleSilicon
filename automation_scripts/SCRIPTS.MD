# 📱 QEMUAppleSilicon Automation Scripts

A comprehensive collection of automation scripts for setting up and managing QEMUAppleSilicon - the premier iOS virtualization solution for Apple Silicon and x86 systems.

## :emoji: Notes
1. **Please run all scripts from the root directory.**
```bash
# Example
cd QEMUAppleSilicon
./automation_scripts/setup_qemu_apple_silicon.sh
```

## 🎯 Quick Start

1. **Initial Setup**: `./automation_scripts/setup_qemu_apple_silicon.sh`
2. **Start Companion VM**: `./automation_scripts/start_companion_vm.sh`
3. **Transfer firmware and root_ticket**: `scp -P 2222 iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw root_ticket.der user@localhost:~/`
4. **Transfer libimobiledevice tools**: `./automation_scripts/setup_libimobiledevice_tools.sh`
5. **Build and Install libimobiledevice tools**: `chmod +x ./automation_scripts/setup_libimobiledevice_tools.sh && sudo ./automation_scripts/setup_libimobiledevice_tools.sh` *(⚠️ Important: Must be run with sudo on the companion VM)*
6. **Transfer Restore Script to Companion VM**: `scp -P 2222 restore.sh user@localhost:~/`
7. **Run iPhone Restore**: `./run_iphone.sh restore`
8. **Run Restore Script on Companion VM**: `ssh -p 2222 user@localhost 'bash -s' < chmod +x restore.sh && ./restore.sh`
9. **⚠️ Wait for restore completion** - As soon as iOS screen turns black, you **MUST** stop the iPhone VM to prevent data corruption when patching the root filesystem
10. **Mount the filesystem for patching**: `./automation_scripts/mount-apfs-disk.sh nvram` *(⚠️ Important: Must be run on macOS host)*
   - **Note**: QEMUAppleSilicon repo examples reference `nvram.1` but we only have `nvram` - try `nvram` first, fallback to `root` if needed
11. **Apply patches to the root filesystem**: `./automation_scripts/apply-patches.sh` *(🚧 WIP - Coming soon! ⚠️ Important: Must be run on macOS host)*
12. **Run iPhone Normal**: `./run_iphone.sh`

---

## 🚀 Installation Scripts

### `setup_qemu_apple_silicon.sh`

**Purpose**: Complete automated setup of QEMUAppleSilicon environment including dependencies, QEMU compilation, firmware preparation, and disk creation.

**Features**:
- ✅ Cross-platform support (macOS & Linux)
- ✅ Automatic dependency installation
- ✅ QEMU compilation with Apple Silicon optimizations
- ✅ iOS firmware (IPSW) download and extraction
- ✅ SEP firmware preparation and AP ticket creation
- ✅ Virtual disk image creation

**Usage**:
```bash
# Run the complete setup (requires sudo for dependencies)
cd QEMUAppleSilicon
./automation_scripts/setup_qemu_apple_silicon.sh
```

**What it installs**:
- **macOS**: Uses Homebrew to install build tools, Python dependencies
- **Linux**: Uses apt-get for Ubuntu/Debian systems

**Generated Files**:
```
├── build/
│   ├── qemu-system-aarch64          # Main QEMU binary
│   └── qemu-img                     # Disk image utility
├── firmware/
│   ├── iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/  # Extracted IPSW
│   ├── sep-firmware.n104.RELEASE.new.img4           # SEP firmware
│   ├── AppleSEPROM-Cebu-B1                          # SEP ROM
│   └── root_ticket.der                              # AP ticket
└── output/
    ├── root (16GB)                  # Main filesystem
    ├── firmware (8MB)               # Firmware partition
    └── [additional disk images]     # System partitions
```

---

### `start_companion_vm.sh`

**Purpose**: Launches a companion ARM64 Linux VM that handles USB connectivity and restore operations for the iPhone VM.

**Why needed**: The companion VM acts as a bridge for USB operations and provides the necessary Linux environment for `idevicerestore` functionality.

**Usage**:
```bash
# Basic usage - starts with default USB unix socket
./automation_scripts/start_companion_vm.sh

# Custom USB connection parameters
./automation_scripts/start_companion_vm.sh run [usb_type] [usb_addr] [usb_port]

# Examples:
./automation_scripts/start_companion_vm.sh run unix /tmp/usb_connection 0
./automation_scripts/start_companion_vm.sh run ipv4 127.0.0.1 8030
```

**Configuration**:
- **RAM**: 2GB allocated
- **CPU**: ARM Cortex-A72 emulation
- **Storage**: Auto-creates 20GB qcow2 disk
- **Network**: SSH forwarding on port 2222
- **USB**: Configurable USB-over-TCP connection

**VM Specifications**:
```bash
# VM Hardware
-M virt                          # ARM virt machine
-cpu cortex-a72                  # ARM CPU
-m 2048                          # 2GB RAM
-device virtio-gpu-pci           # Graphics
-netdev user,hostfwd=tcp::2222-:22  # SSH access
```

---

### `run_iphone.sh`

**Purpose**: The main script to boot the iPhone VM in either restore or normal mode with comprehensive hardware emulation.

**Modes**:
- **`restore`**: Boot into recovery mode for iOS installation
- **`run`**: Normal iOS boot (default)

**Usage**:
```bash
# Restore mode (first time setup)
cd QEMUAppleSilicon
./automation_scripts/run_iphone.sh restore

# Normal mode
cd QEMUAppleSilicon
./automation_scripts/run_iphone.sh
./automation_scripts/run_iphone.sh run

# Custom USB configuration
cd QEMUAppleSilicon
./automation_scripts/run_iphone.sh run unix /tmp/usb_connection 0
./automation_scripts/run_iphone.sh run ipv4 127.0.0.1 8030
```

**Hardware Emulation**:
```bash
# iPhone 11 Pro (N104AP) Hardware Profile
-M t8030                         # A13 Bionic SoC
-cpu max                         # Maximum CPU features
-smp 7                          # 7 CPU cores
-m 4G                           # 4GB RAM
-device ramfb                    # Framebuffer
-device usb-ehci,usb-kbd,usb-mouse  # USB devices
```

**Storage Layout**:
| Partition | Size | Purpose |
|-----------|------|---------|
| `root` | 16GB | Main iOS filesystem |
| `firmware` | 8MB | Boot firmware |
| `syscfg` | 128KB | System configuration |
| `ctrl_bits` | 8KB | Control bits |
| `nvram` | 8KB | Non-volatile RAM |
| `effaceable` | 4KB | Secure storage |
| `panic_log` | 1MB | Crash logs |
| `sep_nvram` | 2KB | SEP NVRAM |
| `sep_ssc` | 128KB | SEP secure storage |

---

### `setup_libimobiledevice_tools.sh`

**Purpose**: Builds and installs libimobiledevice tools from source on the companion VM in the correct dependency order.

**Why needed**: The companion VM requires `idevicerestore` and related tools to perform iOS restore operations. Building from source ensures compatibility and gets the latest features.

**Build Order** (Critical - dependencies must be built in this sequence):
1. **libplist** - Property list library
2. **usbmuxd** - USB multiplexing daemon
3. **libusbmuxd** - USB multiplexing library
4. **libtatsu** - TSS library for Apple's signing server
5. **libimobiledevice-glue** - Common library functions
6. **libimobiledevice** - Main iOS device communication library
7. **libirecovery** - iOS recovery mode library
8. **ideviceinstaller** - App installation tool
9. **libideviceactivation** - Device activation library
10. **idevicerestore** - iOS restore tool

**Usage**:
```bash
# Transfer to companion VM
scp -P 2222 setup_libimobiledevice_tools.sh user@localhost:~/

# SSH into companion VM and run with sudo
ssh -p 2222 user@localhost
chmod +x setup_libimobiledevice_tools.sh
sudo ./setup_libimobiledevice_tools.sh
```

**System Requirements**:
- **OS**: Debian/Ubuntu Linux (companion VM)
- **RAM**: 2GB+ recommended for compilation
- **Storage**: ~500MB for source code and build artifacts
- **Network**: Internet connection for downloading source repositories

**Dependencies Installed**:
```bash
build-essential pkg-config checkinstall git autoconf automake 
libtool-bin libssl-dev libreadline-dev libusb-1.0-0-dev 
libcurl4-openssl-dev libzip-dev zlib1g-dev libxml2-dev udev
```

**Build Process**:
1. 📦 Installs system dependencies via `apt-get`
2. 🔄 Clones all 10 GitHub repositories in parallel
3. 🛠️ Builds each package in dependency order using `autogen.sh` and `make`
4. 📦 Installs each package with `sudo make install`

**Verification**:
```bash
# Test that idevicerestore was installed successfully
which idevicerestore
idevicerestore --version

# Test device detection (if iPhone VM is connected)
idevice_id -l
```

**⚠️ Important Notes**:
- Must be run with `sudo` for installation privileges
- Build order is **critical** - do not modify the sequence
- Takes ~10-15 minutes on modern hardware
- Requires active internet connection throughout build

---

### `restore.sh`

**Purpose**: Performs iOS restore operation using `idevicerestore` on the companion VM.

**Prerequisites**: 
- Must be run **inside the companion VM**
- Requires `idevicerestore` tool installed
- Needs IPSW file and tickets present

**Usage**:
```bash
# Transfer to companion VM and execute
scp -P 2222 restore.sh user@localhost:~/
ssh -p 2222 user@localhost
chmod +x restore.sh
./restore.sh
```

**Restore Command**:
```bash
idevicerestore --erase --restore-mode \
    -i 0x1122334455667788 \
    iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw \
    -T root_ticket.der
```

---

## 🔧 Maintenance & Troubleshooting Scripts

### `mount_apfs_disk.sh`

**Purpose**: Mount QEMU APFS disk images on macOS host for inspection and modification with intelligent disk detection and comprehensive error handling.

**✨ Features**:
- ✅ Automatic disk identifier detection from `hdiutil attach` output
- ✅ Smart volume mounting with fallback mechanisms
- ✅ Automatic ownership enablement for System volume
- ✅ Comprehensive error handling and status reporting
- ✅ Support for all QEMU disk partitions
- ✅ Detailed progress feedback and troubleshooting info

**Usage**:
```bash
# Mount the root filesystem (default)
./automation_scripts/mount_apfs_disk.sh
./automation_scripts/mount_apfs_disk.sh root

# Mount other partitions
./automation_scripts/mount_apfs_disk.sh firmware
./automation_scripts/mount_apfs_disk.sh syscfg
./automation_scripts/mount_apfs_disk.sh nvram
./automation_scripts/mount_apfs_disk.sh effaceable
./automation_scripts/mount_apfs_disk.sh panic_log
./automation_scripts/mount_apfs_disk.sh sep_nvram
./automation_scripts/mount_apfs_disk.sh sep_ssc
./automation_scripts/mount_apfs_disk.sh ctrl_bits
```

**Available Disk Options**:
| Disk Name | Purpose | Typical Mount Points |
|-----------|---------|---------------------|
| `root` | Main iOS filesystem | `/Volumes/System` |
| `firmware` | Boot firmware | N/A (raw partition) |
| `syscfg` | System configuration | N/A (raw partition) |
| `ctrl_bits` | Control bits | N/A (raw partition) |
| `nvram` | Non-volatile RAM | `/Volumes/Hardware`, `/Volumes/Preboot` |
| `effaceable` | Secure storage | N/A (raw partition) |
| `panic_log` | Crash logs | N/A (raw partition) |
| `sep_nvram` | SEP NVRAM | N/A (raw partition) |
| `sep_ssc` | SEP secure storage | N/A (raw partition) |

**Example Output**:
```bash
$ ./automation_scripts/mount_apfs_disk.sh root
Mounting disk: root
Disk path: /Users/user/QEMUAppleSilicon/output/root
Attaching disk image...
Disk attached as: /dev/disk6
System volume is already mounted
Enabling ownership on /Volumes/System...
File system user/group ownership enabled
Current disk layout:
/dev/disk6 (disk image):
   #:                       TYPE NAME                    SIZE       IDENTIFIER
   0:      GUID_partition_scheme                        +17.2 GB    disk6
   1:                 Apple_APFS Container disk7         17.2 GB    disk6s1

/dev/disk7 (synthesized):
   #:                       TYPE NAME                    SIZE       IDENTIFIER
   0:      APFS Container Scheme -                      +17.2 GB    disk7
                                 Physical Store disk6s1
   1:                APFS Volume System                  3.5 GB     disk7s1
   2:                APFS Volume xART                    10.5 MB    disk7s3
   3:                APFS Volume Hardware                25.2 MB    disk7s4
   4:                APFS Volume Preboot                 20.5 KB    disk7s5

Mounted volumes:
drwxr-xr-x  2 root  wheel  64 Sep  6 11:30 Hardware
drwxr-xr-x  2 root  wheel  64 Sep  6 11:30 Preboot
drwxr-xr-x  2 root  wheel  64 Sep  6 11:30 System
drwxr-xr-x  2 root  wheel  64 Sep  6 11:30 xART
Script completed successfully!
```

**Implementation Details**:
```bash
# Enhanced disk attachment with output capture
ATTACH_OUTPUT=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage \
    -blocksize 4096 "${OUTPUT_DIR}/${DISK}" 2>&1)

# Robust disk identifier extraction
DISK_IDENTIFIER=$(echo "$ATTACH_OUTPUT" | grep -E '^/dev/disk[0-9]+' | \
    awk '{print $1}' | head -n 1)

# Smart volume mounting with checks
if [ -d "/Volumes/System" ]; then
    sudo diskutil enableownership /Volumes/System
else
    diskutil mountDisk "$DISK_IDENTIFIER"
    sleep 2
    [ -d "/Volumes/System" ] && sudo diskutil enableownership /Volumes/System
fi
```

**⚠️ Prerequisites**:
- **macOS Host**: Script must be run on macOS (uses `hdiutil` and `diskutil`)
- **Disk Images**: Target disk must exist in `output/` directory
- **Admin Privileges**: `sudo` access required for ownership operations
- **Free Mount Points**: Ensure `/Volumes/System` and related mount points are available

**Common Mount Points After Success**:
- `/Volumes/System` - Main iOS root filesystem
- `/Volumes/Hardware` - Hardware-specific data
- `/Volumes/Preboot` - Boot configuration
- `/Volumes/xART` - Extended runtime data

**Troubleshooting**:
```bash
# If disk attachment fails
ls -la output/          # Verify disk exists
diskutil list          # Check current disk layout

# If ownership enablement fails  
sudo diskutil list     # Check volume permissions
sudo umount /Volumes/System  # Unmount and retry

# To unmount when done
./automation_scripts/unmount_apfs_disk.sh
```

---

### `create_disks.sh`

**Purpose**: Manually create all required virtual disk images for iPhone VM.

**Usage**:
```bash
# Create all disk images
./automation_scripts/create_disks.sh
```

**Generated Disks**:
```bash
qemu-img create -f raw root 16G        # Main filesystem
qemu-img create -f raw firmware 8M     # Boot firmware  
qemu-img create -f raw syscfg 128K     # System config
qemu-img create -f raw ctrl_bits 8K    # Control bits
qemu-img create -f raw nvram 8K        # NVRAM
qemu-img create -f raw effaceable 4K   # Secure storage
qemu-img create -f raw panic_log 1M    # Crash logs
qemu-img create -f raw sep_nvram 2K    # SEP NVRAM
qemu-img create -f raw sep_ssc 128K    # SEP storage
```

---

### `disk_corrupt_need_new_disks.sh`

**Purpose**: Nuclear option - completely wipe and recreate all virtual disks when corruption occurs.

**⚠️ Warning**: This will **permanently delete** all iOS data and require a complete restore.

**Usage**:
```bash
# Only use when disks are corrupted beyond repair
./automation_scripts/disk_corrupt_need_new_disks.sh
```

**Process**:
1. 🗑️ Removes all existing disk files
2. 🔄 Creates fresh disk images
3. 📝 Requires complete iOS restore afterward

---

## 🚨 Troubleshooting Guide

### Common Issues

**USB Re-entrant IO Errors**:
- Restart companion VM: `./automation_scripts/start_companion_vm.sh`
- Restart iPhone VM: `./automation_scripts/run_iphone.sh`
- Check USB connection parameters

**Build Failures on macOS**:
```bash
# Ensure Xcode tools are installed
xcode-select --install

# Fix potential path issues
export PATH="/usr/bin:$PATH"
hash -r
```

**Missing Dependencies**:
```bash
# macOS
brew install wget libtool glib meson ninja pixman gnutls

# Linux (Ubuntu/Debian)  
sudo apt-get install build-essential meson ninja-build pkg-config
```

### File Permissions
```bash
# Make scripts executable
chmod +x *.sh

# Fix potential ownership issues
sudo chown -R $USER:$USER ../QEMUAppleSilicon/
```

---

## 📋 Prerequisites

- **macOS**: Xcode Command Line Tools, Homebrew
- **Linux**: Build tools, development packages
- **Storage**: ~50GB free space for iOS firmware and disks
- **RAM**: 8GB+ recommended for smooth operation
- **CPU**: Multi-core processor (ARM64 or x86_64)

---

## 🔗 Related Resources

- [QEMUAppleSilicon GitHub](https://github.com/ChefKissInc/QEMUAppleSilicon)
- [iOS Firmware Keys](https://www.theiphonewiki.com/wiki/Firmware_Keys)
- [QEMU Documentation](https://qemu.readthedocs.io/)

---

*Last Updated: 2025-09-06 | QEMUAppleSilicon Automation Scripts v1.0*