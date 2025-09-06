# USB Networking Service Debugging

## Current Issues
- `usbmuxd-network.service` fails with timeout (manual start works) ✓
- `dnsmasq.service` fails to start - **ROOT CAUSE FOUND**
- dnsmasq trying to bind to 192.168.100.1 but interface not configured yet

## Root Cause Analysis
dnsmasq error: `failed to create listening socket for 192.168.100.1: Cannot assign requested address`
- The USB network interface (192.168.100.1) needs to be configured before dnsmasq can bind to it

## Diagnostic Commands to Run on Companion VM

### 1. Check dnsmasq service status and logs
```bash
sudo systemctl status dnsmasq.service
sudo journalctl -xeu dnsmasq.service --no-pager
```

### 2. Check dnsmasq configuration
```bash
sudo dnsmasq --test
sudo cat /etc/dnsmasq.d/iphone-usb.conf
```

### 3. Check for port conflicts
```bash
sudo netstat -tulpn | grep :53
sudo lsof -i :53
```

### 4. Check usbmuxd service
```bash
sudo systemctl status usbmuxd-network.service
sudo journalctl -xeu usbmuxd-network.service --no-pager
```

### 5. Check and configure USB network interface
```bash
# Check current interfaces
ip addr show

# Check if USB interface setup script exists
ls -la /usr/local/bin/setup-iphone-interface.sh

# Check dnsmasq configuration
sudo cat /etc/dnsmasq.d/iphone-usb.conf

# Manually configure USB interface (if needed)
sudo ip link add usb0 type dummy
sudo ip addr add 192.168.100.1/24 dev usb0
sudo ip link set usb0 up
```

### 6. Manual restart sequence (correct order)
```bash
# Stop services
sudo systemctl stop dnsmasq
sudo systemctl stop usbmuxd-network

# Clear any locks
sudo rm -f /var/run/usbmuxd /tmp/usb_connection

# Configure interface first
sudo ip addr add 192.168.100.1/24 dev usb0 2>/dev/null || echo "Interface already configured"

# Start services in order
sudo systemctl start usbmuxd-network
sudo systemctl start dnsmasq
```

## USB Connection Issues

### Problems Identified:
1. **Companion VM USB Device Syntax Error**: Has `bus=ehci.0` twice in device specification
2. **Socket Connection Order**: iPhone VM and Companion VM both trying to use same socket
3. **Permission Issues**: `/tmp/usb_connection` socket may have wrong permissions

### iPhone VM Configuration:
```bash
# iPhone VM (server side)
-M t8030,usb-conn-type=unix,usb-conn-addr=/tmp/usb_connection
```

### Companion VM Configuration (Fixed):
```bash
# Companion VM (client side) - CORRECTED
-device usb-tcp-remote,conn-type=unix,conn-addr=/tmp/usb_connection
```

### Troubleshooting Steps:
```bash
# Check socket exists and permissions
ls -la /tmp/usb_connection

# Remove old socket
sudo rm -f /tmp/usb_connection

# Start iPhone VM first (creates socket)
./automation_scripts/run_iphone.sh

# Then start Companion VM (connects to socket)
```

## Using Updated Restart Script
Now that you've parameterized the restart script, you can run it with the correct username:

```bash
./automation_scripts/restart_usb_connection.sh joe 2222
```
