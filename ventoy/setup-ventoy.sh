#!/bin/bash
# MinNT Ventoy Setup Script
# "Don't go chasing waterfalls, stick to Ventoy!" 🔥🥒💀

set -e

echo "=========================================="
echo "💀🔥🥒 MinNT Ventoy Setup Script 🥒🔥💀"
echo "=========================================="
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root for Ventoy install
if [ "$EUID" -ne 0 ]; then 
   echo -e "${YELLOW}Note: Run as root if you need to install Ventoy${NC}"
fi

echo "Step 1: Checking for MinNT ISO..."
if [ -f "minint.iso" ]; then
    echo -e "${GREEN}✓ minint.iso found ($(ls -lh minint.iso | awk '{print $5}'))${NC}"
else
    echo -e "${RED}✗ minint.iso not found!${NC}"
    echo "Run 'make iso' first!"
    exit 1
fi

echo ""
echo "Step 2: Checking for Ventoy..."

# Try to find Ventoy
VENTOY_FOUND=0

if command -v ventoy &> /dev/null; then
    echo -e "${GREEN}✓ Ventoy command found${NC}"
    VENTOY_FOUND=1
elif [ -f "/usr/bin/ventoy" ]; then
    echo -e "${GREEN}✓ Ventoy found at /usr/bin/ventoy${NC}"
    VENTOY_FOUND=1
elif [ -f "./Ventoy2Disk.sh" ]; then
    echo -e "${GREEN}✓ Ventoy2Disk.sh found in current directory${NC}"
    VENTOY_FOUND=1
else
    echo -e "${YELLOW}! Ventoy not found. Installing...${NC}"
    
    # Try to install Ventoy
    if command -v apt &> /dev/null; then
        echo "Attempting to install Ventoy via apt..."
        sudo apt update && sudo apt install -y ventoy || echo "Package not available"
    elif command -v dnf &> /dev/null; then
        echo "Attempting to install Ventoy via dnf..."
        sudo dnf install -y ventoy || echo "Package not available"
    elif command -v pacman &> /dev/null; then
        echo "Attempting to install Ventoy via pacman..."
        sudo pacman -S ventoy || echo "Package not available"
    fi
    
    # Download Ventoy if not installed
    if ! command -v ventoy &> /dev/null && [ ! -f "./Ventoy2Disk.sh" ]; then
        echo "Downloading Ventoy..."
        VENTOY_URL="https://github.com/ventoy/Ventoy/releases/download/v1.0.96/ventoy-1.0.96-linux.tar.gz"
        wget -q --show-progress "$VENTOY_URL" -O ventoy.tar.gz || {
            echo -e "${RED}Failed to download Ventoy${NC}"
            echo "Please download manually from: https://ventoy.net"
            exit 1
        }
        
        tar -xzf ventoy.tar.gz
        mv ventoy-* ventoy-install
        echo -e "${GREEN}✓ Ventoy downloaded${NC}"
    fi
fi

echo ""
echo "Step 3: USB Device Selection"
echo "Available USB devices:"
echo "----------------------"
lsblk -d -o NAME,SIZE,TYPE,MODEL | grep -E "usb|USB" || lsblk -d -o NAME,SIZE,TYPE,MODEL | head -20
echo ""

read -p "Enter USB device (e.g., sdb, sdc): " USB_DEVICE

if [ -z "$USB_DEVICE" ]; then
    echo -e "${RED}No device specified!${NC}"
    exit 1
fi

# Safety check
if [[ "$USB_DEVICE" == *"sd"[ab]* ]] || [[ "$USB_DEVICE" == *"nvme"* ]]; then
    echo -e "${RED}WARNING: $USB_DEVICE looks like a system disk!${NC}"
    read -p "Are you SURE this is your USB device? (yes/no): " CONFIRM
    if [ "$CONFIRM" != "yes" ]; then
        echo "Aborted!"
        exit 1
    fi
fi

USB_PATH="/dev/$USB_DEVICE"

if [ ! -b "$USB_PATH" ]; then
    echo -e "${RED}Device $USB_PATH not found!${NC}"
    exit 1
fi

echo -e "${YELLOW}Installing Ventoy to $USB_PATH...${NC}"

# Install Ventoy
if [ -f "./Ventoy2Disk.sh" ]; then
    sudo ./Ventoy2Disk.sh -i "$USB_PATH"
elif [ -f "./ventoy-install/Ventoy2Disk.sh" ]; then
    sudo ./ventoy-install/Ventoy2Disk.sh -i "$USB_PATH"
else
    echo -e "${RED}Ventoy2Disk.sh not found!${NC}"
    echo "Please install Ventoy manually"
    exit 1
fi

echo ""
echo -e "${GREEN}✓ Ventoy installed!${NC}"

echo ""
echo "Step 4: Copying MinNT to USB..."

# Find Ventoy partition
VENTOY_MOUNT=$(lsblk -o NAME,LABEL,MOUNTPOINT | grep -i ventoy | awk '{print $3}' | head -1)

if [ -z "$VENTOY_MOUNT" ]; then
    echo "Ventoy partition not mounted. Trying to mount..."
    
    # Try to find and mount
    VENTOY_PART="${USB_PATH}1"
    if [ ! -b "$VENTOY_PART" ]; then
        VENTOY_PART="${USB_PATH}p1"
    fi
    
    if [ -b "$VENTOY_PART" ]; then
        mkdir -p /tmp/ventoy-mount
        sudo mount "$VENTOY_PART" /tmp/ventoy-mount && VENTOY_MOUNT="/tmp/ventoy-mount"
    fi
fi

if [ -n "$VENTOY_MOUNT" ]; then
    echo "Copying minint.iso to $VENTOY_MOUNT..."
    cp minint.iso "$VENTOY_MOUNT/"
    
    # Copy config files
    if [ -d "ventoy" ]; then
        echo "Copying Ventoy config..."
        mkdir -p "$VENTOY_MOUNT/ventoy"
        cp ventoy/ventoy_grub.cfg "$VENTOY_MOUNT/ventoy/" 2>/dev/null || true
    fi
    
    sync
    echo -e "${GREEN}✓ Files copied!${NC}"
    
    # Unmount
    if [ "$VENTOY_MOUNT" = "/tmp/ventoy-mount" ]; then
        sudo umount "$VENTOY_MOUNT"
        rmdir "$VENTOY_MOUNT"
    fi
else
    echo -e "${YELLOW}! Could not find Ventoy partition${NC}"
    echo "Please manually copy minint.iso to the Ventoy USB"
fi

echo ""
echo "=========================================="
echo -e "${GREEN}💀🔥🥒 SETUP COMPLETE! 🥒🔥💀${NC}"
echo "=========================================="
echo ""
echo "Your USB is ready! To boot MinNT:"
echo ""
echo "1. Plug USB into target PC"
echo "2. Power on and press F12/F8/ESC for boot menu"
echo "3. Select USB device"
echo "4. Choose 'MinNT - Real Hardware USB OS'"
echo ""
echo "What's working on real hardware:"
echo "  ✅ AMD/NVIDIA/Intel GPUs (color bars!)"
echo "  ✅ USB 3.0 xHCI (keyboards & mice!)"
echo "  ✅ AHCI/NVMe storage (NTFS/FAT32!)"
echo "  ✅ Network (WiFi via RTW88!)"
echo ""
echo -e "${YELLOW}MINNT OR NOTHING AT ALL! 💀🔥🥒${NC}"
echo ""
