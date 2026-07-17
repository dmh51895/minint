#!/bin/bash
# MinNT USB Installer Flasher
# Flashes the MinNT ISO to a USB drive for installation

if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must be run as root (use sudo)"
    exit 1
fi

if [ ! -f "minnt.iso" ]; then
    echo "ERROR: minnt.iso not found in current directory"
    exit 1
fi

echo "=========================================="
echo "  MinNT USB Installer"
echo "=========================================="
echo ""
echo "Available USB devices:"
echo ""

# List removable devices
lsblk -d -o NAME,SIZE,MODEL,TRAN | grep -E "usb|removable" || {
    echo "No USB devices found!"
    exit 1
}

echo ""
read -p "Enter USB device (e.g., /dev/sdb): " USB_DEVICE

if [ ! -b "$USB_DEVICE" ]; then
    echo "ERROR: $USB_DEVICE is not a block device"
    exit 1
fi

echo ""
echo "WARNING: This will erase all data on $USB_DEVICE!"
read -p "Type YES to continue: " CONFIRM

if [ "$CONFIRM" != "YES" ]; then
    echo "Cancelled"
    exit 0
fi

echo ""
echo "Flashing MinNT to $USB_DEVICE..."
dd if=minnt.iso of="$USB_DEVICE" bs=4M status=progress
sync

echo ""
echo "=========================================="
echo "  Installation Complete!"
echo "=========================================="
echo ""
echo "Remove the USB drive and boot from it on"
echo "the target machine to install MinNT."
echo ""
