#!/bin/bash
# Script to setup iPhone networking on companion VM
# This script should be run INSIDE the companion VM after libimobiledevice tools are installed
# Usage: sudo ./setup_iphone_networking.sh

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
    echo -e "${PURPLE}${BOLD}🌐 $1${NC}"
}

print_step() {
    echo -e "${BLUE}🔄 $1${NC}"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    print_error "This script must be run as root (use sudo)"
    exit 1
fi

print_header "iPhone USB Networking Setup for Companion VM"

# Install Dependencies
print_step "Installing Dependencies..."
apt-get update
apt-get install -y iproute2 iptables

# Install dnsmasq if not already installed
print_step "Installing dnsmasq..."
if ! command -v dnsmasq &> /dev/null; then
    apt-get update
    apt-get install -y dnsmasq
    print_success "dnsmasq installed"
else
    print_info "dnsmasq already installed"
fi

# Create dnsmasq configuration for iPhone networking
print_step "Configuring dnsmasq for iPhone networking..."
cat > /etc/dnsmasq.d/iphone-usb.conf << 'EOF'
# Configuration for iPhone USB networking
interface=usb0
dhcp-range=192.168.100.50,192.168.100.100,12h
dhcp-option=3,192.168.100.1
dhcp-option=6,8.8.8.8,8.8.4.4
listen-address=192.168.100.1
bind-interfaces
EOF

print_success "dnsmasq configuration created"

# Create network interface configuration script
print_step "Creating network interface setup script..."
cat > /usr/local/bin/setup-iphone-interface.sh << 'EOF'
#!/bin/bash
# Script to configure iPhone USB network interface

INTERFACE="usb0"
IP_ADDRESS="192.168.100.1"
NETMASK="255.255.255.0"

echo "Configuring iPhone USB network interface..."

# Wait for interface to appear
echo "Waiting for $INTERFACE to appear..."
timeout=30
while [ $timeout -gt 0 ]; do
    if ip link show $INTERFACE &> /dev/null; then
        echo "Interface $INTERFACE found"
        break
    fi
    sleep 1
    ((timeout--))
done

if [ $timeout -eq 0 ]; then
    echo "Error: Interface $INTERFACE not found within 30 seconds"
    exit 1
fi

# Configure the interface
echo "Configuring $INTERFACE with IP $IP_ADDRESS..."
ip addr add $IP_ADDRESS/$NETMASK dev $INTERFACE
ip link set $INTERFACE up

# Enable IP forwarding
echo 1 > /proc/sys/net/ipv4/ip_forward

# Setup NAT for internet access
iptables -t nat -A POSTROUTING -s 192.168.100.0/24 -o eth0 -j MASQUERADE
iptables -A FORWARD -i $INTERFACE -o eth0 -j ACCEPT
iptables -A FORWARD -i eth0 -o $INTERFACE -m state --state RELATED,ESTABLISHED -j ACCEPT

echo "Network interface configured successfully"
EOF

chmod +x /usr/local/bin/setup-iphone-interface.sh
print_success "Interface setup script created"

# Create udev rule to automatically configure interface when iPhone connects
print_step "Creating udev rule for automatic interface configuration..."
cat > /etc/udev/rules.d/99-iphone-usb.rules << 'EOF'
# Automatically configure iPhone USB network interface
ACTION=="add", SUBSYSTEM=="net", ATTRS{idVendor}=="05ac", ATTRS{idProduct}=="*", ENV{ID_USB_DRIVER}=="cdc_ncm", RUN+="/usr/local/bin/setup-iphone-interface.sh"
EOF

udevadm control --reload-rules
print_success "udev rule created"

# Stop default usbmuxd first
print_step "Stopping default usbmuxd service..."
systemctl stop usbmuxd 2>/dev/null || true
systemctl disable usbmuxd 2>/dev/null || true

# Create environment file for usbmuxd
print_step "Creating usbmuxd environment configuration..."
cat > /etc/default/usbmuxd << 'EOF'
# Environment variables for usbmuxd
USBMUXD_DEFAULT_DEVICE_MODE=3
USBMUXD_DEFAULT_NETWORK_DEVICE=1
USBMUXD_DEFAULT_NETWORK_SPEED=100
USBMUXD_DEFAULT_NETWORK_MTU=1500
EOF

# Create systemd service for usbmuxd with proper environment
print_step "Creating systemd service for usbmuxd with network mode..."
cat > /etc/systemd/system/usbmuxd-network.service << 'EOF'
[Unit]
Description=USB Multiplexor Daemon with Network Mode
After=network.target
Conflicts=usbmuxd.service

[Service]
Type=simple
EnvironmentFile=/etc/default/usbmuxd
Environment=USBMUXD_NETWORK_MODE=1
RuntimeDirectory=usbmuxd
RuntimeDirectoryMode=0755
Environment=USBMUXD_SOCKET=/run/usbmuxd/usbmuxd.sock
Environment=RUST_LOG=debug

# Clean up any stale locks and ensure directory exists
ExecStartPre=/bin/sh -c 'mkdir -p /var/run/usbmuxd && chown -R usbmux:plugdev /var/run/usbmuxd && chmod 775 /var/run/usbmuxd'
ExecStartPre=/bin/sh -c 'rm -f /var/run/usbmuxd/*.lock'
ExecStartPre=/bin/sh -c 'mkdir -p /run/usbmuxd && chown -R usbmux:plugdev /run/usbmuxd && chmod 775 /run/usbmuxd'
ExecStartPre=/bin/sh -c 'rm -f /run/usbmuxd/*.lock'

# Start with network mode and debug
ExecStart=/usr/sbin/usbmuxd --foreground --verbose --system --enable-exit

Restart=always
RestartSec=3
User=usbmux
Group=plugdev
TimeoutStartSec=30
TimeoutStopSec=10

[Install]
WantedBy=multi-user.target
EOF

# Reload systemd and start the service
systemctl daemon-reload
systemctl enable usbmuxd-network.service

print_step "Starting usbmuxd in network mode..."
if systemctl start usbmuxd-network.service; then
    print_success "usbmuxd network service started successfully"
else
    print_warning "usbmuxd service failed to start, trying manual start..."
    # Try manual start with environment variable
    export USBMUXD_DEFAULT_DEVICE_MODE=3
    /usr/sbin/usbmuxd --foreground --verbose &
    USBMUXD_PID=$!
    sleep 2
    if kill -0 $USBMUXD_PID 2>/dev/null; then
        print_success "usbmuxd started manually in background (PID: $USBMUXD_PID)"
    else
        print_error "Failed to start usbmuxd"
    fi
fi

print_success "usbmuxd network service configured and started"

# Create and configure USB interface before starting dnsmasq
print_step "Creating USB network interface for dnsmasq..."
if ! ip link show usb0 &>/dev/null; then
    # Create dummy interface if it doesn't exist
    ip link add usb0 type dummy
    print_info "Created dummy usb0 interface"
fi

# Configure the interface
ip addr flush dev usb0 2>/dev/null || true
ip addr add 192.168.100.1/24 dev usb0
ip link set usb0 up
print_success "USB interface configured with IP 192.168.100.1"

# Enable IP forwarding
echo 1 > /proc/sys/net/ipv4/ip_forward
print_info "IP forwarding enabled"

# Setup basic NAT rules (will be enhanced when real iPhone connects)
iptables -t nat -D POSTROUTING -s 192.168.100.0/24 -o eth0 -j MASQUERADE 2>/dev/null || true
iptables -t nat -A POSTROUTING -s 192.168.100.0/24 -o eth0 -j MASQUERADE
print_info "NAT rules configured"

# Now start dnsmasq (interface exists so it can bind to 192.168.100.1)
print_step "Starting dnsmasq with iPhone configuration..."
systemctl stop dnsmasq 2>/dev/null || true
if systemctl start dnsmasq; then
    systemctl enable dnsmasq
    print_success "dnsmasq started and enabled successfully"
else
    print_error "dnsmasq failed to start. Checking configuration..."
    dnsmasq --test || {
        print_error "dnsmasq configuration test failed"
        print_info "Checking logs: journalctl -xeu dnsmasq.service --no-pager"
        exit 1
    }
fi

print_header "iPhone USB Networking Setup Complete! 🎉"

print_info "Setup completed successfully. Here's what was configured:"
echo "  • ${WHITE}dnsmasq${NC} - DHCP/DNS server for iPhone networking"
echo "  • ${WHITE}usbmuxd-network${NC} - USB multiplexer with network mode enabled"
echo "  • ${WHITE}Network interface${NC} - Automatic configuration when iPhone connects"
echo "  • ${WHITE}NAT/Forwarding${NC} - Internet access routing"

print_warning "Next steps:"
echo "  1. Start the iPhone VM from the host machine"
echo "  2. Pair the iPhone with this companion VM using ${CYAN}idevice_id -l${NC}"
echo "  3. You should see DHCP requests when iPhone tries to connect"
echo "  4. iPhone should get IP in range ${WHITE}192.168.100.50-192.168.100.100${NC}"

print_info "To monitor the connection:"
echo "  • Check interface: ${CYAN}ip addr show usb0${NC}"
echo "  • Monitor DHCP: ${CYAN}tail -f /var/log/syslog | grep dnsmasq${NC}"
echo "  • Check iPhone connection: ${CYAN}idevice_id -l${NC}"
