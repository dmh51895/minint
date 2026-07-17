#!/bin/bash
# MinNT One-Command USB Installer (Bash Edition)
# Extracts and flashes MinNT to a USB drive

set -e

# Colors
RED='\033[91m'
GREEN='\033[92m'
YELLOW='\033[93m'
CYAN='\033[96m'
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
echo -e "${WHITE}    One-Command USB Installer${END}"
echo -e "${YELLOW}    ════════════════════════════════════════════════════════════${END}"
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: Must be run as root!${END}"
    echo -e "${YELLOW}Try: sudo $0${END}"
    exit 1
fi

# Parse arguments
AUTO_DEVICE=""
PACKAGE_PATH=""
SKIP_CONFIRM=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --auto)
            AUTO_DEVICE="$2"
            shift 2
            ;;
        --package)
            PACKAGE_PATH="$2"
            shift 2
            ;;
        --yes)
            SKIP_CONFIRM="yes"
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${END}"
            exit 1
            ;;
    esac
done

# Find package
if [ -n "$PACKAGE_PATH" ]; then
    PACKAGE="$PACKAGE_PATH"
else
    PACKAGE=$(ls MinNT-Installer-*.tar.gz 2>/dev/null | head -1)
    if [ -z "$PACKAGE" ]; then
        PACKAGE=$(ls /home/dheavy/molecular-ai-factory/Server-V2/minint-master/MinNT-Installer-*.tar.gz 2>/dev/null | head -1)
    fi
fi

if [ -z "$PACKAGE" ] || [ ! -f "$PACKAGE" ]; then
    echo -e "${RED}ERROR: Installer package not found!${END}"
    echo -e "${YELLOW}Looking for: MinNT-Installer-*.tar.gz${END}"
    echo -e "${YELLOW}Or specify with --package /path/to/package${END}"
    exit 1
fi

echo -e "${CYAN}[1/4] Extracting package...${END}"
echo "  Package: $PACKAGE"

EXTRACT_DIR="/tmp/minnt-installer-$$"
rm -rf "$EXTRACT_DIR"
mkdir -p "$EXTRACT_DIR"

tar -xzf "$PACKAGE" -C "$EXTRACT_DIR"
INSTALLER_DIR=$(ls -d "$EXTRACT_DIR"/MinNT-Installer-* 2>/dev/null | head -1)

if [ -z "$INSTALLER_DIR" ]; then
    echo -e "${RED}ERROR: Extraction failed!${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

ISO_PATH="$INSTALLER_DIR/minint.iso"
if [ ! -f "$ISO_PATH" ]; then
    echo -e "${RED}ERROR: minint.iso not found!${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

ISO_SIZE=$(du -h "$ISO_PATH" | cut -f1)
echo -e "  ${GREEN}Extracted to: $INSTALLER_DIR${END}"
echo -e "  ISO size: $ISO_SIZE"
echo ""

# Get USB devices
echo -e "${CYAN}[2/4] Available USB devices:${END}"
echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"
printf "${BOLD}%3s  %-12s  %10s  %s${END}\n" "#" "Device" "Size" "Model"
echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"

# List removable devices
USB_DEVICES=()
for dev in /sys/block/*; do
    devname=$(basename "$dev")
    
    # Skip non-removable
    case "$devname" in
        loop*|ram*|nvme*|dm-*|zram*|sr*)
            continue
            ;;
    esac
    
    # Check removable
    if [ -f "$dev/removable" ]; then
        if [ "$(cat "$dev/removable" 2>/dev/null)" = "1" ]; then
            # Get size
            if [ -f "$dev/size" ]; then
                sectors=$(cat "$dev/size")
                size_bytes=$((sectors * 512))
                size_human=$(numfmt --to=iec --suffix=B "$size_bytes" 2>/dev/null || echo "$size_bytes B")
                
                # Get model
                model=""
                if [ -f "$dev/device/model" ]; then
                    model=$(cat "$dev/device/model" 2>/dev/null | tr -d '\n')
                fi
                
                device_path="/dev/$devname"
                USB_DEVICES+=("$device_path:$size_bytes:$model")
                
                # Display
                num=$(( ${#USB_DEVICES[@]} ))
                printf "  %2d  %-12s  %10s  %s\n" "$num" "$device_path" "$size_human" "$model"
            fi
        fi
    fi
done

echo -e "${YELLOW}──────────────────────────────────────────────────────────────────────${END}"

if [ ${#USB_DEVICES[@]} -eq 0 ]; then
    echo -e "${RED}ERROR: No removable USB devices found!${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

# Select device
if [ -n "$AUTO_DEVICE" ]; then
    SELECTED="$AUTO_DEVICE"
    # Find matching device
    for dev_info in "${USB_DEVICES[@]}"; do
        IFS=':' read -r dev_path size_bytes model <<< "$dev_info"
        if [ "$dev_path" = "$AUTO_DEVICE" ]; then
            SELECTED="$dev_path"
            break
        fi
    done
    
    if [ "$SELECTED" != "$AUTO_DEVICE" ]; then
        echo -e "${RED}ERROR: Device $AUTO_DEVICE not found or not removable!${END}"
        rm -rf "$EXTRACT_DIR"
        exit 1
    fi
else
    while true; do
        read -p "$(echo -e "${CYAN}Select USB device [1-${#USB_DEVICES[@]}]: ${END}")" choice
        
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#USB_DEVICES[@]}" ]; then
            idx=$((choice - 1))
            IFS=':' read -r SELECTED size_bytes model <<< "${USB_DEVICES[$idx]}"
            break
        else
            echo -e "${RED}Invalid selection!${END}"
        fi
    done
fi

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
echo -e "  ${RED}ALL DATA ON THIS DEVICE WILL BE ERASED!${END}"
echo -e "${YELLOW}══════════════════════════════════════════════════════════════════════${END}"

if [ -z "$SKIP_CONFIRM" ]; then
    while true; do
        read -p "$(echo -e "${CYAN}Type YES to continue (or 'no' to cancel): ${END}")" confirm
        if [ "$confirm" = "no" ]; then
            echo -e "${RED}Cancelled${END}"
            rm -rf "$EXTRACT_DIR"
            exit 0
        fi
        if [ "$confirm" = "YES" ]; then
            break
        fi
        echo -e "${RED}Invalid input!${END}"
    done
else
    echo -e "${MAGENTA}Auto-yes mode: proceeding...${END}"
fi

# Flash
echo ""
echo -e "${CYAN}[3/4] Flashing MinNT to $SELECTED...${END}"

# Unmount
echo "  Unmounting partitions..."
umount "$SELECTED" 2>/dev/null || true
for part in "${SELECTED}"*; do
    umount "$part" 2>/dev/null || true
done

# Wipe partition table
echo "  Wiping partition table..."
dd if=/dev/zero of="$SELECTED" bs=1M count=10 status=none 2>/dev/null || true

# Flash ISO
echo "  Writing MinNT ISO ($ISO_SIZE)..."
if ! dd if="$ISO_PATH" of="$SELECTED" bs=4M status=progress; then
    echo -e "${RED}ERROR: Flash failed!${END}"
    rm -rf "$EXTRACT_DIR"
    exit 1
fi

# Sync
echo "  Syncing..."
sync

# Force kernel to re-read partition table
blockdev --rereadpt "$SELECTED" 2>/dev/null || true

echo -e "  ${GREEN}Flash complete!${END}"

# Verify
echo ""
echo -e "${CYAN}[4/4] Verifying flash...${END}"
FIRST_SECTOR=$(dd if="$SELECTED" bs=512 count=1 status=none 2>/dev/null | head -c 32769)
if echo "$FIRST_SECTOR" | grep -q "CD001"; then
    echo -e "  ${GREEN}ISO signature found - flash verified!${END}"
else
    echo -e "  ${YELLOW}WARNING: ISO signature not found${END}"
fi

# Success
echo ""
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo -e "${BOLD}${GREEN}  USB INSTALLER READY!${END}"
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo "  Device: ${BOLD}$SELECTED${END}"
echo "  Size:   $size_human"
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"
echo ""
echo -e "${CYAN}Next steps:${END}"
echo "  1. Remove the USB drive"
echo "  2. Plug it into the target machine"
echo "  3. Boot from USB (set BIOS/UEFI boot order)"
echo "  4. Select 'Install to Hard Drive' from GRUB menu"
echo "  5. Follow the installer TUI"
echo ""
echo -e "${YELLOW}The installer will:${END}"
echo "  - Detect all disks via AHCI"
echo "  - Allow you to select target disk"
echo "  - Create FAT32 partition"
echo "  - Install MinNT kernel and bootloader"
echo "  - Configure system settings"
echo ""
echo -e "${GREEN}══════════════════════════════════════════════════════════════════════${END}"

# Cleanup
rm -rf "$EXTRACT_DIR"

echo ""
echo -e "${CYAN}Done! Happy installing! 🥒🔥${END}"