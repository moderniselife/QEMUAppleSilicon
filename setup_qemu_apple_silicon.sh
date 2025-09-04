#!/bin/bash
set -e

# QEMUAppleSilicon Setup Script
# This script automates the setup of QEMUAppleSilicon on Linux/macOS

# Configuration
QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${QEMU_DIR}/build"
FIRMWARE_DIR="${QEMU_DIR}/firmware"
OUTPUT_DIR="${QEMU_DIR}/output"
IPSW_URL="https://updates.cdn-apple.com/2020SummerSeed/fullrestores/001-35886/5FE9BE2E-17F8-41C8-96BB-B76E2B225888/iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw"
SEP_ROM_URL="https://securerom.fun/resources/SEPROM/AppleSEPROM-Cebu-B1"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}This script needs root privileges to install dependencies.${NC}"
    echo "Please enter your password when prompted."
    sudo -v
fi

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to install dependencies
install_dependencies() {
    echo -e "${GREEN}Installing dependencies...${NC}"
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if ! command_exists brew; then
            echo -e "${RED}Homebrew not found. Please install it from https://brew.sh/${NC}"
            exit 1
        fi
        
        brew update
        brew install libtool glib libtasn1 meson ninja pixman gnutls libgcrypt pkgconf lzfse capstone nettle ncurses libslirp libssh libpng jpeg-turbo zstd
        
    elif [[ -f /etc/debian_version ]]; then
        # Debian/Ubuntu
        sudo apt-get update
        sudo apt-get install -y build-essential libtool meson ninja-build pkg-config \
            libcapstone-dev device-tree-compiler libglib2.0-dev gnutls-bin \
            libjpeg-turbo8-dev libpng-dev libslirp-dev libssh-dev libusb-1.0-0-dev \
            liblzo2-dev libncurses5-dev libpixman-1-dev libsnappy-dev vde2 zstd \
            libgnutls28-dev libgmp10 libgmp3-dev lzfse liblzfse-dev libgtk-3-dev \
            libsdl2-dev python3-pip wget unzip
        
        # Install Python dependencies
        pip3 install pyasn1 pyasn1-modules pyimg4 pyyaml
        
    else
        echo -e "${RED}Unsupported OS. Please install dependencies manually.${NC}"
        exit 1
    fi
}

# Function to build QEMU
build_qemu() {
    echo -e "${GREEN}Building QEMU...${NC}"
    
    # Create build directory
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    # Configure QEMU
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PKG_CONFIG_PATH=$(brew --prefix)/lib/pkgconfig ../configure --target-list=aarch64-softmmu
    else
        ../configure --target-list=aarch64-softmmu
    fi
    
    # Build QEMU
    make -j$(nproc)
    
    # Return to the original directory
    cd ..
}

# Function to create disk images
create_disk_images() {
    echo -e "${GREEN}Creating disk images...${NC}"
    
    mkdir -p "${OUTPUT_DIR}"
    cd "${OUTPUT_DIR}"
    
    # Create disk images
    "${BUILD_DIR}/qemu-img" create -f raw root 16G
    "${BUILD_DIR}/qemu-img" create -f raw firmware 8M
    "${BUILD_DIR}/qemu-img" create -f raw syscfg 128K
    "${BUILD_DIR}/qemu-img" create -f raw ctrl_bits 8K
    "${BUILD_DIR}/qemu-img" create -f raw nvram 8K
    "${BUILD_DIR}/qemu-img" create -f raw effaceable 4K
    "${BUILD_DIR}/qemu-img" create -f raw panic_log 1M
    "${BUILD_DIR}/qemu-img" create -f raw sep_nvram 2K
    "${BUILD_DIR}/qemu-img" create -f raw sep_ssc 128K
    
    # Return to the original directory
    cd ..
}

# Function to download and extract IPSW
download_ipsw() {
    echo -e "${GREEN}Downloading iOS firmware...${NC}"
    
    mkdir -p "${FIRMWARE_DIR}"
    cd "${FIRMWARE_DIR}"
    
    if [ ! -f "iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw" ]; then
        wget "${IPSW_URL}" -O "iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw"
    else
        echo "IPSW file already exists. Skipping download."
    fi
    
    # Extract IPSW
    if [ ! -d "iPhone11_8_iPhone12_1_14.0_18A5351d_Restore" ]; then
        echo "Extracting IPSW..."
        unzip -q "iPhone11,8,iPhone12,1_14.0_18A5351d_Restore.ipsw" -d "iPhone11_8_iPhone12_1_14.0_18A5351d_Restore"
    fi
    
    # Download and create AP ticket
    if [ ! -f "root_ticket.der" ]; then
        echo "Downloading AP ticket creation script..."
        wget -q "https://github.com/ChefKissInc/QEMUAppleSiliconTools/raw/refs/heads/master/create_apticket.py"
        
        echo "Creating AP ticket..."
        python3 create_apticket.py n104ap "iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/BuildManifest.plist" ticket.shsh2 root_ticket.der
    fi
    
    # Return to the original directory
    cd ..
}

# Function to prepare SEP firmware
prepare_sep_firmware() {
    echo -e "${GREEN}Preparing SEP firmware...${NC}"
    
    cd "${FIRMWARE_DIR}"
    
    # Check if we already have the final SEP firmware
    if [ ! -f "sep-firmware.n104.RELEASE.new.img4" ]; then
        echo -e "${YELLOW}SEP firmware not found. Creating it...${NC}"
        
        # Download SEP ticket creation script if not exists
        if [ ! -f "create_septicket.py" ]; then
            echo "Downloading SEP ticket creation script..."
            wget -q "https://github.com/ChefKissInc/QEMUAppleSiliconTools/raw/refs/heads/master/create_septicket.py"
        fi
        
        # Create SEP ticket
        if [ ! -f "sep_root_ticket.der" ]; then
            echo "Creating SEP ticket..."
            python3 create_septicket.py n104ap "iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/BuildManifest.plist" ticket.shsh2 sep_root_ticket.der
            if [ $? -ne 0 ]; then
                echo -e "${RED}Failed to create SEP ticket. Please check the error above.${NC}"
                exit 1
            fi
        fi
        
        # Check if img4 utility is installed
        if ! command -v img4 &> /dev/null; then
            echo -e "${YELLOW}img4 utility not found. Please install it from https://github.com/xerub/img4lib${NC}"
            echo "After installation, make sure it's in your PATH and run this script again."
            exit 1
        fi
        
        # Decrypt the SEP firmware
        echo "Decrypting SEP firmware..."
        # Note: You'll need to replace THE_SEP_FW_IV_AND_THE_SEP_FW_KEY_CONCATENATED with the actual keys
        # You can find these by searching for "iOS firmware keys" for iPhone11,8 on iOS 14.0 beta 5
        if [ ! -f "sep-firmware.n104.RELEASE" ]; then
            echo -e "${YELLOW}Please provide the SEP firmware IV and key (concatenated) for decryption:${NC}"
            read -p "Enter SEP firmware IV+KEY: " SEP_KEY
            
            if [ -z "$SEP_KEY" ]; then
                echo -e "${RED}No key provided. Please find the SEP firmware keys and try again.${NC}"
                echo "You can search for 'iPhone11,8 iOS 14.0 beta 5 SEP firmware keys' to find them."
                exit 1
            fi
            
            img4 -i "iPhone11_8_iPhone12_1_14.0_18A5351d_Restore/Firmware/all_flash/sep-firmware.n104.RELEASE.im4p" \
                 -o sep-firmware.n104.RELEASE \
                 -k "$SEP_KEY"
            
            if [ $? -ne 0 ] || [ ! -f "sep-firmware.n104.RELEASE" ]; then
                echo -e "${RED}Failed to decrypt SEP firmware. Please check the key and try again.${NC}"
                exit 1
            fi
        fi
        
        # Repackage the firmware to an IMG4
        echo "Repackaging SEP firmware..."
        img4 -A -F \
             -o sep-firmware.n104.RELEASE.new.img4 \
             -i sep-firmware.n104.RELEASE \
             -M sep_root_ticket.der \
             -T rsep \
             -V ff86cbb5e06c820266308202621604696d706c31820258ff87a3e8e0730e300c1604747a3073020407d98000ff868bc9da730e300c160461726d73020400d20000ff87a389da7382010e3082010a160474626d730482010039643535663434646630353239653731343134656461653733396135313135323233363864626361653434386632333132313634356261323237326537366136633434643037386439313434626564383530616136353131343863663363356365343365656536653566326364636666336664313532316232623062376464353461303436633165366432643436623534323537666531623633326661653738313933326562383838366339313537623963613863366331653137373730336531373735616663613265313637626365353435626635346366653432356432653134653734336232303661386337373234386661323534663439643532636435ff87a389da7282010e3082010a160474626d720482010064643434643762663039626238333965353763383037303431326562353636343131643837386536383635343337613861303266363464383431346664343764383634336530313335633135396531393062656535643435333133363838653063323535373435333533326563303163363530386265383236333738353065623761333036343162353464313236306663313434306562663862343063306632646262616437343964643461656339376534656238646532346330663265613432346161613438366664663631363961613865616331313865383839383566343138643263366437363364303434363063393531386164353766316235636664
        
        if [ $? -ne 0 ] || [ ! -f "sep-firmware.n104.RELEASE.new.img4" ]; then
            echo -e "${RED}Failed to repackage SEP firmware.${NC}"
            exit 1
        fi
        
        echo -e "${GREEN}SEP firmware prepared successfully.${NC}"
    else
        echo -e "${GREEN}SEP firmware already exists.${NC}"
    fi
    
    # Check for SEP ROM
    if [ ! -f "AppleSEPROM-Cebu-B1" ]; then
        echo -e "${YELLOW}SEP ROM not found. Downloading from ${SEP_ROM_URL}...${NC}"
        
        # Try to download the SEP ROM
        if wget -q --show-progress -O "AppleSEPROM-Cebu-B1" "${SEP_ROM_URL}"; then
            if [ -f "AppleSEPROM-Cebu-B1" ]; then
                echo -e "${GREEN}Successfully downloaded SEP ROM.${NC}"
            else
                echo -e "${RED}Failed to save SEP ROM. Please check permissions and try again.${NC}"
                exit 1
            fi
        else
            echo -e "${RED}Failed to download SEP ROM from ${SEP_ROM_URL}${NC}"
            echo "Please download it manually and place it in the ${FIRMWARE_DIR} directory as 'AppleSEPROM-Cebu-B1'"
            read -p "Press Enter to continue once you've added the file..."
            
            if [ ! -f "AppleSEPROM-Cebu-B1" ]; then
                echo -e "${RED}SEP ROM still not found. Please add it and try again.${NC}"
                exit 1
            fi
        fi
    else
        echo -e "${GREEN}SEP ROM found.${NC}"
    fi
    
    # Return to the original directory
    cd ..
}

# Function to create run script
create_run_script() {
    echo -e "${GREEN}Creating run script...${NC}"
    
    cat > "${QEMU_DIR}/run_iphone.sh" << 'EOF'
#!/bin/bash

# QEMUAppleSilicon Run Script
# Usage: ./run_iphone.sh [restore|run]

MODE=${1:-run}
QEMU_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${QEMU_DIR}/build"
FIRMWARE_DIR="${QEMU_DIR}/firmware"
OUTPUT_DIR="${QEMU_DIR}/output"

# Check for required files
if [ ! -f "${FIRMWARE_DIR}/sep-firmware.n104.RELEASE.new.img4" ] || \
   [ ! -f "${FIRMWARE_DIR}/AppleSEPROM-Cebu-B1" ] || \
   [ ! -f "${FIRMWARE_DIR}/root_ticket.der" ]; then
    echo "Required firmware files not found in ${FIRMWARE_DIR}/"
    echo "Please make sure you have:"
    echo "  - sep-firmware.n104.RELEASE.new.img4"
    echo "  - AppleSEPROM-Cebu-B1"
    echo "  - root_ticket.der"
    exit 1
fi

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
"${BUILD_DIR}/qemu-system-aarch64" "${QEMU_ARGS[@]}"
EOF

    chmod +x "${QEMU_DIR}/run_iphone.sh"
    echo -e "${GREEN}Run script created at ${QEMU_DIR}/run_iphone.sh${NC}"
    echo "Usage:"
    echo "  ./run_iphone.sh      - Run the emulated iPhone"
    echo "  ./run_iphone.sh restore - Start in restore mode"
}

# Main function
main() {
    echo -e "${GREEN}=== QEMUAppleSilicon Setup ===${NC}"
    
    # Install dependencies
    install_dependencies
    
    # Build QEMU
    if [ ! -f "${BUILD_DIR}/qemu-system-aarch64" ]; then
        build_qemu
    else
        echo -e "${YELLOW}QEMU already built. Skipping build.${NC}"
    fi
    
    # Create disk images
    if [ ! -f "${OUTPUT_DIR}/root" ]; then
        create_disk_images
    else
        echo -e "${YELLOW}Disk images already exist. Skipping creation.${NC}"
    fi
    
    # Download and extract IPSW
    if [ ! -d "${FIRMWARE_DIR}/iPhone11_8_iPhone12_1_14.0_18A5351d_Restore" ]; then
        download_ipsw
    else
        echo -e "${YELLOW}IPSW already downloaded and extracted.${NC}"
    fi
    
    # Prepare SEP firmware
    prepare_sep_firmware
    
    # Create run script
    create_run_script
    
    echo -e "\n${GREEN}=== Setup Complete ===${NC}"
    echo -e "To start the emulator:"
    echo -e "  1. First, run in restore mode: ${YELLOW}./run_iphone.sh restore${NC}"
    echo -e "  2. After restore completes, run normally: ${YELLOW}./run_iphone.sh${NC}"
    echo -e "\n${YELLOW}Note:${NC} Make sure you have the following files in the 'firmware' directory:"
    echo -e "  - sep-firmware.n104.RELEASE.new.img4"
    echo -e "  - AppleSEPROM-Cebu-B1"
    echo -e "  - root_ticket.der"
}

# Run the main function
main