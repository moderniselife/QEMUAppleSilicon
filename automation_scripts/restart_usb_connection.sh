#!/bin/bash
# Script to restart USB connection when re-entrant IO errors occur
# This addresses the common "Blocked re-entrant IO on MemoryRegion: operational" error
# Usage: ./restart_usb_connection.sh username port
# Example: ./restart_usb_connection.sh user 2222

USERNAME=${1:-user}
PORT=${2:-2222}

set -e

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
    echo -e "${PURPLE}${BOLD}🔄 $1${NC}"
}

print_step() {
    echo -e "${BLUE}🔄 $1${NC}"
}

print_header "USB Connection Restart Tool"

print_warning "This script helps recover from re-entrant IO errors in USB passthrough"
print_info "When you see 'Blocked re-entrant IO on MemoryRegion: operational' errors,"
print_info "both the iPhone VM and Companion VM need to be restarted in the correct order."

echo ""
print_step "Step 1: Stopping iPhone VM (if running)..."
print_info "Please ${BOLD}manually stop the iPhone VM${NC} from the QEMU monitor or terminal"
print_info "You can usually do this by:"
echo "  • Pressing ${WHITE}Ctrl+C${NC} in the iPhone VM terminal"
echo "  • Or using the QEMU monitor: ${WHITE}(qemu) quit${NC}"
echo ""

read -p "Press Enter when iPhone VM is stopped..."
print_success "iPhone VM stopped"

print_step "Step 2: Restarting Companion VM USB services..."

# Function to restart companion VM services (run this on companion VM)
cat > /tmp/restart_companion_usb.sh << 'EOF'
#!/bin/bash
echo "🔄 Restarting USB services on Companion VM..."

# Stop usbmuxd
sudo systemctl stop usbmuxd-network.service 2>/dev/null || true
sudo pkill -f usbmuxd 2>/dev/null || true

# Wait a moment
sleep 2

# Restart usbmuxd with network mode
export USBMUXD_DEFAULT_DEVICE_MODE=3
sudo systemctl start usbmuxd-network.service || {
    echo "⚠️  Service failed, trying manual start..."
    sudo /usr/sbin/usbmuxd --foreground --verbose &
}

# Check if any idevice processes are stuck
sudo pkill -f idevice 2>/dev/null || true

# Remove any stale socket files
sudo rm -f /var/run/usbmuxd 2>/dev/null || true
sudo rm -f /tmp/usb_connection 2>/dev/null || true

echo "✅ Companion VM USB services restarted"
EOF

chmod +x /tmp/restart_companion_usb.sh

print_info "Transferring restart script to Companion VM..."
scp -P $PORT /tmp/restart_companion_usb.sh $USERNAME@localhost:/tmp/ 2>/dev/null || {
    print_warning "Could not transfer script. Please run these commands manually on Companion VM:"
    echo ""
    echo "${WHITE}# On Companion VM:${NC}"
    echo "sudo systemctl stop usbmuxd-network.service"
    echo "sudo pkill -f usbmuxd"
    echo "sleep 2"
    echo "export USBMUXD_DEFAULT_DEVICE_MODE=3"
    echo "sudo systemctl start usbmuxd-network.service"
    echo "sudo rm -f /var/run/usbmuxd /tmp/usb_connection"
    echo ""
    read -p "Press Enter when completed on Companion VM..."
    print_success "Companion VM services restarted manually"
    
    print_step "Step 3: Restart iPhone VM..."
    print_info "Now restart your iPhone VM with:"
    echo "  ${CYAN}./automation_scripts/run_iphone.sh${NC}"
    
    exit 0
}

print_info "Running restart script on Companion VM..."
ssh -p $PORT $USERNAME@localhost 'bash /tmp/restart_companion_usb.sh' || {
    print_error "Failed to restart services on Companion VM"
    print_info "Please manually run the restart commands shown above"
    exit 1
}

print_success "Companion VM services restarted successfully"

print_step "Step 3: Cleaning up local USB connections..."
# Clean up any local USB connection files
sudo rm -f /tmp/usb_connection 2>/dev/null || true
print_success "Local cleanup completed"

print_step "Step 4: Starting Companion VM (if not running)..."
print_info "Checking if Companion VM is accessible..."

if ssh -p $PORT -o ConnectTimeout=5 $USERNAME@localhost 'echo "VM accessible"' >/dev/null 2>&1; then
    print_success "Companion VM is running and accessible"
else
    print_warning "Companion VM may not be running. Starting it now..."
    ./automation_scripts/start_companion_vm.sh &
    COMPANION_PID=$!
    
    print_info "Waiting for Companion VM to boot..."
    for i in {1..30}; do
        if ssh -p $PORT -o ConnectTimeout=2 $USERNAME@localhost 'echo "VM ready"' >/dev/null 2>&1; then
            print_success "Companion VM is now accessible"
            break
        fi
        echo -n "."
        sleep 2
    done
    echo ""
fi

print_header "USB Connection Restart Complete! 🎉"

print_success "Recovery steps completed successfully:"
echo "  • ${WHITE}iPhone VM${NC} - Stopped"
echo "  • ${WHITE}Companion VM${NC} - USB services restarted"
echo "  • ${WHITE}USB connections${NC} - Cleaned up"

print_info "Now you can restart the iPhone VM:"
echo "  ${CYAN}./automation_scripts/run_iphone.sh${NC}"

print_warning "To prevent re-entrant IO errors in the future:"
echo "  • Avoid disconnecting/reconnecting USB too quickly"
echo "  • Let the iPhone VM fully boot before intensive USB operations"
echo "  • Monitor for the error and restart promptly when it occurs"

rm -f /tmp/restart_companion_usb.sh
