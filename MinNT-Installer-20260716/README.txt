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
