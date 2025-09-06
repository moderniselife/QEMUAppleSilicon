#!/bin/bash

LAUNCHD_PLIST_PATH="/Users/josephshenton/Projects/launchd.plist"
OUTPUT_PLIST_PATH="/Users/josephshenton/Projects/launchd-modified.plist"

# Services to disable
SERVICES_TO_DISABLE=(
    "com.apple.voicemail.vmd"
    "com.apple.CommCenter"
    "com.apple.locationd"
)

# Fix 1: Set proper locale to handle UTF-8
export LC_ALL=C

# Fix 2: Convert binary plist to XML format
plutil -convert xml1 "$LAUNCHD_PLIST_PATH"

echo "Disabling services using Python plist manipulation..."

# Process all services with Python
for service in "${SERVICES_TO_DISABLE[@]}"; do
    echo "Processing service: $service"
    
    python3 - << EOF
import plistlib
import sys

try:
    with open('$LAUNCHD_PLIST_PATH', 'rb') as f:
        plist_data = plistlib.load(f)
    
    # Check if services are nested under LaunchDaemons
    if 'LaunchDaemons' in plist_data:
        launch_daemons = plist_data['LaunchDaemons']
        
        if isinstance(launch_daemons, dict):
            # Look for our specific service
            service_name = '$service'
            found_services = []
            for key in launch_daemons.keys():
                if service_name in key:
                    found_services.append(key)
            
            if found_services:
                # Disable all matching services
                for target_service in found_services:
                    launch_daemons[target_service]['Disabled'] = True
                    print(f"Disabled: {target_service}")
                
                # Save the modified plist back to original file
                with open('$LAUNCHD_PLIST_PATH', 'wb') as f:
                    plistlib.dump(plist_data, f)
            else:
                print(f"No services found matching '{service_name}'")
        else:
            print(f"LaunchDaemons is not a dict: {launch_daemons}")
    else:
        print("No LaunchDaemons key found in plist")
        
except Exception as e:
    print(f"Python plist patching failed: {e}")
    sys.exit(1)
EOF

done

echo "All services processed. Check $LAUNCHD_PLIST_PATH for modifications"