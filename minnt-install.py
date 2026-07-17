#!/usr/bin/env python3
"""
MinNT One-Command Installer
Extracts the installer package and flashes it to a USB drive.

Usage:
    sudo python3 minnt-install.py
    sudo python3 minnt-install.py --auto /dev/sdX
"""

import os
import sys
import subprocess
import tarfile
import shutil
import glob
import argparse
from pathlib import Path

# ANSI colors for that beautiful terminal output
class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    END = '\033[0m'

def print_banner():
    """Print the legendary MinNT banner"""
    banner = f"""
{Colors.CYAN}{Colors.BOLD}
    __  __ _____ _   _ _____   _____             _   _ ______ ____  
   |  \\/  |_   _| \\ | |_   _| |  _  |_   _  ___| |_(_)/ ___|/ ___| 
   | |\\/| | | | |  \\| | | |   | |_| | | | |/ _ \\ __| | |  |_ \\___ \\ 
   | |  | | | | | |\\  | | |   |  _  | |_| |  __/ |_| | |__| | ___) |
   |_|  |_| |_| |_| \\_| |_|   |_| |_|\\__,_|\\___|\\__|_|\\____/|____/ 
{Colors.END}
{Colors.YELLOW}    ════════════════════════════════════════════════════════════{Colors.END}
{Colors.WHITE}    One-Command USB Installer{Colors.END}
{Colors.YELLOW}    ════════════════════════════════════════════════════════════{Colors.END}
"""
    print(banner)

def check_root():
    """Check if running as root"""
    if os.geteuid() != 0:
        print(f"{Colors.RED}ERROR: Must be run as root!{Colors.END}")
        print(f"{Colors.YELLOW}Try: sudo python3 {sys.argv[0]}{Colors.END}")
        sys.exit(1)

def find_package():
    """Find the MinNT installer package"""
    # Look for the package in current directory and parent directories
    search_paths = [
        Path.cwd(),
        Path.cwd().parent,
        Path.cwd().parent.parent,
        Path('/home/dheavy/molecular-ai-factory/Server-V2/minint-master'),
    ]
    
    for path in search_paths:
        packages = list(path.glob('MinNT-Installer-*.tar.gz'))
        if packages:
            return packages[0]
    
    return None

def extract_package(package_path, extract_dir):
    """Extract the installer package"""
    print(f"{Colors.CYAN}[1/4] Extracting package...{Colors.END}")
    print(f"  Package: {package_path}")
    
    # Create extraction directory
    if extract_dir.exists():
        shutil.rmtree(extract_dir)
    extract_dir.mkdir(parents=True)
    
    # Extract
    try:
        with tarfile.open(package_path, 'r:gz') as tar:
            tar.extractall(extract_dir)
        print(f"  {Colors.GREEN}Extracted to: {extract_dir}{Colors.END}")
    except Exception as e:
        print(f"  {Colors.RED}ERROR: Failed to extract: {e}{Colors.END}")
        sys.exit(1)
    
    # Find the extracted directory
    extracted_dirs = [d for d in extract_dir.iterdir() if d.is_dir()]
    if not extracted_dirs:
        print(f"  {Colors.RED}ERROR: No directory found in package{Colors.END}")
        sys.exit(1)
    
    return extracted_dirs[0]

def get_usb_devices():
    """Get list of removable USB devices"""
    devices = []
    
    for dev_path in glob.glob('/sys/block/*'):
        devname = os.path.basename(dev_path)
        
        # Skip non-removable, loop, ram, nvme
        if devname.startswith(('loop', 'ram', 'nvme', 'dm-', 'zram', 'sr')):
            continue
        
        # Check if removable
        removable_file = dev_path / 'removable'
        if not removable_file.exists():
            continue
        
        try:
            with open(removable_file) as f:
                if f.read().strip() != '1':
                    continue
        except:
            continue
        
        # Get size
        size_file = dev_path / 'size'
        if not size_file.exists():
            continue
        
        try:
            with open(size_file) as f:
                sectors = int(f.read().strip())
            bytes_size = sectors * 512
        except:
            continue
        
        # Get model
        model = ""
        model_file = dev_path / 'device' / 'model'
        if model_file.exists():
            try:
                with open(model_file) as f:
                    model = f.read().strip()
            except:
                pass
        
        device_path = f'/dev/{devname}'
        devices.append((device_path, bytes_size, model))
    
    devices.sort(key=lambda x: x[0])
    return devices

def human_size(b):
    """Convert bytes to human readable format"""
    for unit in ('B', 'KB', 'MB', 'GB', 'TB'):
        if b < 1024:
            return f"{b:.1f} {unit}"
        b /= 1024
    return f"{b:.1f} PB"

def select_device(devices, auto_device=None):
    """Let user select USB device"""
    if auto_device:
        # Auto mode - use specified device
        for dev, size, model in devices:
            if dev == auto_device:
                return dev, size, model
        print(f"{Colors.RED}ERROR: Device {auto_device} not found or not removable!{Colors.END}")
        sys.exit(1)
    
    if not devices:
        print(f"{Colors.RED}ERROR: No removable USB devices found!{Colors.END}")
        print(f"{Colors.YELLOW}Make sure a USB drive is plugged in.{Colors.END}")
        sys.exit(1)
    
    print(f"\n{Colors.CYAN}[2/4] Available USB devices:{Colors.END}")
    print(f"{Colors.YELLOW}{'─' * 70}{Colors.END}")
    print(f"{Colors.BOLD}{'#':>3}  {'Device':<12}  {'Size':>10}  {'Model'}{Colors.END}")
    print(f"{Colors.YELLOW}{'─' * 70}{Colors.END}")
    
    for i, (dev, size, model) in enumerate(devices, 1):
        print(f"  {i:>2}  {dev:<12}  {human_size(size):>10}  {model}")
    
    print(f"{Colors.YELLOW}{'─' * 70}{Colors.END}")
    
    while True:
        try:
            choice = input(f"\n{Colors.CYAN}Select USB device [1-{len(devices)}]: {Colors.END}").strip()
            idx = int(choice) - 1
            if 0 <= idx < len(devices):
                return devices[idx]
        except (ValueError, KeyboardInterrupt):
            print(f"\n{Colors.RED}Cancelled{Colors.END}")
            sys.exit(0)
        
        print(f"{Colors.RED}Invalid selection!{Colors.END}")

def confirm_flash(device, size, model, auto_yes=False):
    """Confirm the flash operation"""
    print(f"\n{Colors.YELLOW}{'═' * 70}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.RED}  WARNING: DESTRUCTIVE OPERATION{Colors.END}")
    print(f"{Colors.YELLOW}{'═' * 70}{Colors.END}")
    print(f"  Device:  {Colors.BOLD}{device}{Colors.END}")
    print(f"  Size:    {human_size(size)}")
    print(f"  Model:   {model}")
    print(f"{Colors.YELLOW}{'═' * 70}{Colors.END}")
    print(f"  {Colors.RED}ALL DATA ON THIS DEVICE WILL BE ERASED!{Colors.END}")
    print(f"{Colors.YELLOW}{'═' * 70}{Colors.END}")
    
    if auto_yes:
        print(f"\n{Colors.MAGENTA}Auto-yes mode: proceeding...{Colors.END}")
        return True
    
    while True:
        confirm = input(f"\n{Colors.CYAN}Type YES to continue (or 'no' to cancel): {Colors.END}").strip()
        if confirm == 'no':
            print(f"\n{Colors.RED}Cancelled{Colors.END}")
            sys.exit(0)
        if confirm == 'YES':
            return True
        print(f"{Colors.RED}Invalid input!{Colors.END}")

def flash_device(device, iso_path):
    """Flash the ISO to the USB device"""
    print(f"\n{Colors.CYAN}[3/4] Flashing MinNT to {device}...{Colors.END}")
    
    # Unmount any mounted partitions
    print(f"  Unmounting partitions...")
    try:
        subprocess.run(['umount', device], capture_output=True)
        for part in glob.glob(f'{device}*'):
            subprocess.run(['umount', part], capture_output=True)
    except:
        pass
    
    # Zero out first 10MB to kill partition tables
    print(f"  Wiping partition table...")
    subprocess.run(
        ['dd', 'if=/dev/zero', f'of={device}', 'bs=1M', 'count=10', 'status=none'],
        check=False
    )
    
    # Flash the ISO
    print(f"  Writing MinNT ISO ({human_size(os.path.getsize(iso_path))})...")
    try:
        result = subprocess.run(
            ['dd', f'if={iso_path}', f'of={device}', 'bs=4M', 'status=progress'],
            check=True
        )
    except subprocess.CalledProcessError as e:
        print(f"  {Colors.RED}ERROR: Flash failed: {e}{Colors.END}")
        sys.exit(1)
    
    # Sync
    print(f"  Syncing...")
    subprocess.run(['sync'], check=True)
    
    # Force kernel to re-read partition table
    subprocess.run(['blockdev', '--rereadpt', device], capture_output=True)
    
    print(f"  {Colors.GREEN}Flash complete!{Colors.END}")

def verify_flash(device, iso_path):
    """Verify the flash was successful"""
    print(f"\n{Colors.CYAN}[4/4] Verifying flash...{Colors.END}")
    
    # Read first 512 bytes from device
    try:
        result = subprocess.run(
            ['dd', f'if={device}', 'bs=512', 'count=1', 'status=none'],
            capture_output=True,
            check=True
        )
        first_sector = result.stdout[:512]
        
        # Check for ISO signature
        if b'CD001' in first_sector:
            print(f"  {Colors.GREEN}ISO signature found - flash verified!{Colors.END}")
        else:
            print(f"  {Colors.YELLOW}WARNING: ISO signature not found{Colors.END}")
    except:
        print(f"  {Colors.YELLOW}WARNING: Could not verify flash{Colors.END}")

def print_success(device, size):
    """Print success message"""
    print(f"\n{Colors.GREEN}{'═' * 70}{Colors.END}")
    print(f"{Colors.BOLD}{Colors.GREEN}  USB INSTALLER READY!{Colors.END}")
    print(f"{Colors.GREEN}{'═' * 70}{Colors.END}")
    print(f"  Device: {Colors.BOLD}{device}{Colors.END}")
    print(f"  Size:   {human_size(size)}")
    print(f"{Colors.GREEN}{'═' * 70}{Colors.END}")
    print(f"\n{Colors.CYAN}Next steps:{Colors.END}")
    print(f"  1. Remove the USB drive")
    print(f"  2. Plug it into the target machine")
    print(f"  3. Boot from USB (set BIOS/UEFI boot order)")
    print(f"  4. Select 'Install to Hard Drive' from GRUB menu")
    print(f"  5. Follow the installer TUI")
    print(f"\n{Colors.YELLOW}The installer will:{Colors.END}")
    print(f"  - Detect all disks via AHCI")
    print(f"  - Allow you to select target disk")
    print(f"  - Create FAT32 partition")
    print(f"  - Install MinNT kernel and bootloader")
    print(f"  - Configure system settings")
    print(f"\n{Colors.GREEN}{'═' * 70}{Colors.END}")

def main():
    """Main function"""
    parser = argparse.ArgumentParser(
        description='MinNT One-Command USB Installer',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
{Colors.BOLD}Examples:{Colors.END}
  sudo python3 minnt-install.py                    # Interactive mode
  sudo python3 minnt-install.py --auto /dev/sdb    # Auto mode (skip prompts)
  sudo python3 minnt-install.py --package /path/to/MinNT-Installer.tar.gz
        """
    )
    parser.add_argument('--auto', metavar='DEVICE', help='Auto-flash to specified device')
    parser.add_argument('--package', metavar='PATH', help='Path to installer package')
    parser.add_argument('--yes', action='store_true', help='Skip confirmation prompt')
    
    args = parser.parse_args()
    
    # Check root
    check_root()
    
    # Print banner
    print_banner()
    
    # Find package
    if args.package:
        package_path = Path(args.package)
    else:
        package_path = find_package()
    
    if not package_path or not package_path.exists():
        print(f"{Colors.RED}ERROR: Installer package not found!{Colors.END}")
        print(f"{Colors.YELLOW}Looking for: MinNT-Installer-*.tar.gz{Colors.END}")
        print(f"{Colors.YELLOW}Or specify with --package{Colors.END}")
        sys.exit(1)
    
    # Extract package
    extract_dir = Path('/tmp/minnt-installer')
    installer_dir = extract_package(package_path, extract_dir)
    
    # Find ISO
    iso_path = installer_dir / 'minint.iso'
    if not iso_path.exists():
        print(f"{Colors.RED}ERROR: minint.iso not found in package!{Colors.END}")
        sys.exit(1)
    
    print(f"  ISO size: {human_size(iso_path.stat().st_size)}")
    
    # Get USB devices
    devices = get_usb_devices()
    
    # Select device
    device, size, model = select_device(devices, args.auto)
    
    # Confirm
    if not confirm_flash(device, size, model, args.yes or args.auto):
        sys.exit(1)
    
    # Flash
    flash_device(device, iso_path)
    
    # Verify
    verify_flash(device, iso_path)
    
    # Success
    print_success(device, size)
    
    # Cleanup
    try:
        shutil.rmtree(extract_dir)
    except:
        pass

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Colors.RED}Cancelled{Colors.END}")
        sys.exit(1)