#!/bin/bash
# MinNT Installer Builder
# Builds the complete MinNT system with working installer

set -e

echo "=========================================="
echo "  MinNT Installer Builder"
echo "=========================================="
echo ""

# Check if we're in the right directory
if [ ! -f "Makefile" ]; then
    echo "ERROR: Must be run from minint-master directory"
    exit 1
fi

# Clean previous build
echo "[1/5] Cleaning previous build..."
make clean > /dev/null 2>&1

# Build the kernel
echo "[2/5] Building kernel..."
if ! make 2>&1 | grep -q "Kernel built"; then
    echo "ERROR: Kernel build failed!"
    exit 1
fi
echo "  Kernel built: $(ls -lh minint.elf | awk '{print $5}')"

# Build the ISO
echo "[3/5] Building bootable ISO..."
if ! make iso 2>&1 | grep -q "completed successfully"; then
    echo "ERROR: ISO build failed!"
    exit 1
fi
echo "  ISO built: $(ls -lh minint.iso | awk '{print $5}')"

# Create installer package
echo "[4/5] Creating installer package..."
INSTALLER_DIR="MinNT-Installer-$(date +%Y%m%d)"
mkdir -p "$INSTALLER_DIR"

# Copy ISO
cp minint.iso "$INSTALLER_DIR/"

# Copy kernel
cp minint.elf "$INSTALLER_DIR/"

# Create flash script
cat > "$INSTALLER_DIR/flash-to-usb.sh" << 'FLASH_SCRIPT'
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
FLASH_SCRIPT

chmod +x "$INSTALLER_DIR/flash-to-usb.sh"

# Create README
cat > "$INSTALLER_DIR/README.txt" << 'README'
MinNT Installer Package
=======================

This package contains everything you need to install MinNT on real hardware.

Contents:
  - minnt.iso: Bootable ISO image (25MB)
  - minint.elf: MinNT kernel (3.5MB)
  - flash-to-usb.sh: USB flashing script

Installation:
  1. Run: sudo ./flash-to-usb.sh
  2. Select your USB drive
  3. Boot from the USB on the target machine
  4. Select "Install to Hard Drive" from GRUB menu
  5. Follow the installer TUI

The installer will:
  - Detect all disks via AHCI
  - Allow you to select target disk
  - Create FAT32 partition
  - Install MinNT kernel and bootloader
  - Configure system settings

After installation, remove the USB and reboot to boot MinNT.
README

# Create zip archive
echo "[5/5] Creating distribution archive..."
tar -czf "$INSTALLER_DIR.tar.gz" "$INSTALLER_DIR"

echo ""
echo "=========================================="
echo "  Build Complete!"
echo "=========================================="
echo ""
echo "Installer package: $INSTALLER_DIR.tar.gz"
echo "Size: $(ls -lh $INSTALLER_DIR.tar.gz | awk '{print $5}')"
echo ""
echo "To install on real hardware:"
echo "  1. Extract: tar -xzf $INSTALLER_DIR.tar.gz"
echo "  2. cd $INSTALLER_DIR"
echo "  3. sudo ./flash-to-usb.sh"
echo ""
echo "To test in QEMU:"
echo "  qemu-system-x86_64 -cdrom minint.iso -serial stdio -m 512M"
echo ""