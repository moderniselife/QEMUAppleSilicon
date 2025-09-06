#!/bin/bash

# This script builds the following from source in the following order 
# The original repo's wiki is wrong, they require a specific order.
# 1. libplist
# 2. usbmuxd
# 3. libusbmuxd
# 4. libtatsu
# 5. libimobiledevice-glue
# 6. libimobiledevice
# 7. libirecovery
# 8. ideviceinstaller
# 9. libideviceactivation
# 10. idevicerestore

PACKAGES="build-essential wget pkg-config checkinstall git autoconf automake libtool-bin libssl-dev libreadline-dev libusb-1.0-0-dev libcurl4-openssl-dev libzip-dev zlib1g-dev libxml2-dev udev"
REPOS=(
	"https://github.com/libimobiledevice/libplist.git"
	"https://github.com/libimobiledevice/usbmuxd.git"
	"https://github.com/libimobiledevice/libusbmuxd.git"
	"https://github.com/libimobiledevice/libtatsu.git"
	"https://github.com/libimobiledevice/libimobiledevice-glue.git"
	"https://github.com/libimobiledevice/libimobiledevice.git"
	"https://github.com/libimobiledevice/libirecovery.git"
	"https://github.com/libimobiledevice/ideviceinstaller.git"
	"https://github.com/libimobiledevice/libideviceactivation.git"
	"https://github.com/libimobiledevice/idevicerestore.git"
)
# Install dependencies
sudo apt-get install -y $PACKAGES

# Fetch git repos in parallel
for repo in "${REPOS[@]}"; do
	git clone "$repo"
done

# Build and install in order
for repo in "${REPOS[@]}"; do
    PACKAGE_NAME="$(basename "$repo")"
	echo "Starting installation of $PACKAGE_NAME"
	cd "$PACKAGE_NAME"
    echo "Configuring $PACKAGE_NAME"
	PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/ ./autogen.sh
    echo "Building $PACKAGE_NAME"
    PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/ make -j$(nproc)
    echo "Installing $PACKAGE_NAME"
    sudo make install
    echo "Finished installation of $PACKAGE_NAME"
	cd ..
done
