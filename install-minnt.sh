#!/bin/bash
# MinNT One-Command Complete Installer
# This script does EVERYTHING: builds, extracts, and flashes

set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
RED='\033[91m'
GREEN='\033[92m'
YELLOW='\033[93m'
CYAN='\033[96m'
MAGENTA='\033[95m'
BOLD='\033[1m'
END='\033[0m'

# Banner
echo -e "${CYAN}${BOLD}"
cat << 'EOF'
    __  __ _____ _   _ _____   _____             _   _ ______ ____  
   |  \/  |_   _| \ | |_   _| |  _  |_   _  ___| |_(_)/ ___|/ ___| 
   | |\/| | | | |  \| | | |   | |_| | | | |/ _ \ __| | |  |_ \___ \ 
   | |  | | | | | |\  | | |   |  _  | |_| |  __/ |_| | |__| | ___) |
   |_|  |_|_|_| |_| \_| |_|   |_| |_|\__,_|\___|\__|_|\____/|____/ 
EOF
echo -e "${END}"
echo -e "${YELLOW}    ════════════════════════════════════════════════════════════${END}"
echo -e "${WHITE}    One-Command Complete Installer${END}"
echo -e "${YELLOW}    ════════════════════════════════════════════════════════════${END}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: Must be run as root!${END}"
    echo -e "${YELLOW}Try: sudo $0${END}"
    exit 1
fi

# Step 1: Build everything
echo -e "${CYAN}[1/5] Building MinNT from source...${END}"
echo "  Cleaning previous build..."
make clean > /dev/null 2>&1 || true

echo "  Building kernel..."
if make 2>&1 | grep -q "Kernel built"; then
    echo -e "  ${GREEN}Kernel built: $(ls -lh minint.elf | awk '{print $5}')${END}"
else
    echo -e "  ${RED}ERROR: Kernel build failed!${END}"
    exit 1
fi

echo "  Building ISO..."
if make iso 2>&1 | grep -q "completed successfully"; then
    echo -e "  ${GREEN}ISO built: $(ls -lh minint.iso | awk '{print $5}')${END}"
else
    echo -e "  ${RED}ERROR: ISO build failed!${END}"
    exit 1
fi

# Step 2: Create installer package
echo ""
echo -e "${CYAN}[2/5] Creating installer package...${END}"

INSTALLER_DIR="MinNT-Installer-$(date +%Y%m%d)"
rm -rf "$INSTALLER_DIR"
mkdir -p "$INSTALLER_DIR"

# Copy files
cp minint.iso "$INSTALLER_DIR/"
cp minint.elf "$INSTALLER_DIR/"

# Create flash script
cat > "$INSTALLER_DIR/flash-to-usb.sh" << 'FLASH_EOF'
#!/bin/bash
# MinNT USB Flasher
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: Must be run as root"
    exit 1
fi

echo "Available USB devices:"
lsblk -d -o NAME,SIZE,MODEL,TRAN | grep usb

read -p "Enter USB device (e.g., /dev/sdb): " USB_DEVICE
read -p "Type YES to erase and flash: " CONFIRM

if [ "$CONFIRM" = "YES" ]; then
    dd if=minint.iso of="$USB_DEVICE" bs=4M status=progress
    sync
    echo "Done!"
fi
FLASH_EOF

chmod +x "$INSTALLER_DIR/flash-to-usb.sh"

# Create README
cat > "$INSTALLER_DIR/README.txt" << 'README_EOF'
MinNT Installer Package
========================

Extract and run flash-to-usb.sh to install MinNT on real hardware.
README_EOF

# Create tarball
tar -czf "$INSTALLER_DIR.tar.gz" "$INSTALLER_DIR"
echo -e "  ${GREEN}Package created: $INSTALLER_DIR.tar.gz ($(ls -lh $INSTALLER_DIR.tar.gz | awk '{print $5}'))${END}"

# Step 3: Extract package
echo ""
echo -e "${CYAN}[3/5] Extracting installer package...${END}"

EXTRACT_DIR="/tmp/minnt-install-$$"
rm -rf "$EXTRACT_DIR"
mkdir -p "$EXTRACT_DIR"

tar -xzf "$INSTALLER_DIR.tar.gz" -C "$EXTRACT_DIR"
EXTRACTED_DIR=$(ls -d "$EXTRACT_DIR"/MinNT-Installer-* 2>/dev/null | head -1)

if [ -z "$EXTRACTED_DIR" ]; then
    echo -e "${RED}ERROR: Extraction failed!${END}"
    exit 1
fi

ISO_PATH="$EXTRACTED_DIR/minint.iso"
if [ ! -f "$ISO_PATH" ]; then
    echo -e "${RED}ERROR: ISO not found in package!${END}"
    exit 1
fi

echo -e "  ${GREEN}Extracted to: $EXTRACTED_DIR${END}"
echo "  ISO size: $(du -h "$ISO_PATH" | cut -f1)"

# Step 4: Select and flash USB device
echo ""
echo -e "${CYAN}[4/5] Selecting USB device...${END}"

# List USB devices
USB_DEVICES=()
for dev in /sys/block/*; do
    devname=$(basename "$dev")
    case "$devname" in
        loop*|ram*|nvme*|dm-*|zram*|sr*) continue ;;
    esac
    
    if [ -f "$dev/removable" ] && [ "$(cat "$dev/removable" 2>/dev/null)" = "1" ]; then
        if [ -f "$dev/size" ]; then
            sectors=$(cat "$dev/size")
            size_bytes=$((sectors * 512))
            model=""
            [ -f "$dev/device/model" ] && model=$(cat "$dev/device/model" 2>/dev/null | tr -d '\n')
            USB_DEVICES+=("/dev/$devname:$size_bytes:$model")
        fi
    fi
done

if [ ${#USB_DEVICES[@]} -eq 0 ]; then
    echo -e "${RED}ERROR: No USB devices found!${END}"
    echo -e "${YELLOW}Plug in a USB drive and try again.${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"
printf "${BOLD}%3s  %-12s  %10s  %s${END}\n" "#" "Device" "Size" "Model"
echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"

for i in "${!USB_DEVICES[@]}"; do
    IFS=':' read -r dev_path size_bytes model <<< "${USB_DEVICES[$i]}"
    size_human=$(numfmt --to=iec --suffix=B "$size_bytes" 2>/dev/null || echo "$size_bytes B")
    printf "  %2d  %-12s  %10s  %s\n" $((i+1)) "$dev_path" "$size_human" "$model"
done

echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"

while true; do
    read -p "$(echo -e "${CYAN}Select USB device [1-${#USB_DEVICES[@]}]: ${END}")" choice
    if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#USB_DEVICES[@]}" ]; then
        idx=$((choice - 1))
        IFS=':' read -r SELECTED size_bytes model <<< "${USB_DEVICES[$idx]}"
        break
    fi
    echo -e "${RED}Invalid selection!${END}"
done

# Confirm
echo ""
echo -e "${YELLOW}══════════════════════════════════════════════════════════════════════${END}"
echo -e "${BOLD}${RED}  WARNING: DESTRUCTIVE OPERATION${END}"
echo -e "${YELLOW}══════════════════════════════════════════════════════════════════════${END}"
echo "  Device:  ${BOLD}$SELECTED${END}"
size_human=$(numfmt --to=iec --suffix=B "$size_bytes" 2>/dev/null || echo "$size_bytes B")
echo "  Size:    $size_human"
echo "  Model:   $model"
echo -e "${YELLOW}══════════════════════════════════════════════════════════════════════${END}"
echo -e "  ${RED}ALL DATA WILL BE ERASED!${END}"
echo -e "${YELLOW}══════════════════════════════════════════════════════════════════════${END}"

while true; do
    read -p "$(echo -e "${CYAN}Type YES to continue: ${END}")" confirm
    if [ "$confirm" = "YES" ]; then
        break
    fi
    echo -e "${RED}Must type YES exactly!${END}"
done

# Step 5: Flash
echo ""
echo -e "${CYAN}[5/5] Flashing MinNT to $SELECTED...${END}"

echo "  Unmounting partitions..."
umount "$SELECTED" 2>/dev/null || true
for part in "${SELECTED}"*; do
    umount "$part" 2>/dev/null || true
done

echo "  Wiping partition table..."
dd if=/dev/zero of="$SELECTED" bs=1M count=10 status=none 2>/dev/null || true

echo "  Writing MinNT ISO..."
if ! dd if="$ISO_PATH" of="$SELECTED" bs=4M status=progress; then
    echo -e "${RED}ERROR: Flash failed!${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

echo "  Syncing..."
sync
blockdev --rereadpt "$SELECTED" 2>/dev/null || true

echo -e "  ${GREEN}Flash complete!${END}"

# Verify
echo ""
echo "  Verifying..."
FIRST_SECTOR=$(dd if="$SELECTED" bs=512 count=1 status=none 2>/dev/null | head -c 32769)
if echo "$FIRST_SECTOR" | grep -q "CD001"; then
    echo -e "  ${GREEN}ISO signature verified!${END}"
fi

# Success
echo ""
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo -e "${BOLD}${GREEN}  INSTALLATION COMPLETE!${END}"
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo "  Device: ${BOLD}$SELECTED${END}"
echo "  Size:   $size_human"
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo ""
echo -e "${CYAN}Next steps:${END}"
echo "  1. Remove the USB drive"
echo "  2. Plug into target machine"
echo "  3. Boot from USB"
echo "  4. Select 'Install to Hard Drive'"
echo "  5. Follow installer TUI"
echo ""
echo -e "${MAGENTA}Happy installing! 🥒🔥${END}"

# Cleanup
rm -rf "$EXTRACT_DIR"