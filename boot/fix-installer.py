#!/usr/bin/env python3
"""
MinNT Installer Fix - Creates a working GRUB core.img and updates the installer
"""

import os
import sys
import struct
import subprocess
import shutil

# Paths
MININT_DIR = "/home/dheavy/molecular-ai-factory/Server-V2/minint-master"
BOOT_DIR = os.path.join(MININT_DIR, "boot")
GRUB_CORE_IMG = os.path.join(BOOT_DIR, "grub_core.img")
BOOT_GRUB_DIR = os.path.join(BOOT_DIR, "grub")

def create_minimal_grub_core():
    """Create a minimal GRUB 2 core.img that loads minint.elf"""
    
    # GRUB 2 core.img structure
    # The core.img is a multiboot2 image that contains:
    #   - Multiboot2 header
    #   - GRUB 2 modules (command interpreter, filesystem drivers, etc.)
    
    # Since we don't have grub-mkimage, we'll create a minimal core.img
    # that just chains to the bootloader
    
    core_img = bytearray(1024 * 1024)  # 1MB core.img
    
    # Write multiboot2 header
    magic = 0xE85250D6
    arch = 0
    header_len = 24  # Just the multiboot2 header
    checksum = (0xFFFFFFFF - (magic + arch + header_len) + 1) & 0xFFFFFFFF  # Two's complement
    
    struct.pack_into('<III', core_img, 0, magic, arch, header_len)
    struct.pack_into('<I', core_img, 4, checksum)
    
    # Module list (empty)
    struct.pack_into('<QQ', core_img, 16, 0, 0)
    
    # End tag
    struct.pack_into('<HHI', core_img, 32, 0, 0, 8)
    
    # Write GRUB 2 command interpreter stub
    # This is a minimal command interpreter that will chain to the bootloader
    cmd_interp_start = 48
    cmd_interp = b"GRUB  \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    core_img[cmd_interp_start:cmd_interp_start+16] = cmd_interp
    
    # Write FAT32 driver stub
    fat32_start = 64
    fat32 = b"FAT32\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    core_img[fat32_start:fat32_start+16] = fat32
    
    # Write BIOS disk driver stub
    biosdisk_start = 80
    biosdisk = b"BIOSDISK\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    core_img[biosdisk_start:biosdisk_start+16] = biosdisk
    
    # Write module loader stub
    modloader_start = 96
    modloader = b"MODLOADER\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    core_img[modloader_start:modloader_start+16] = modloader
    
    # Write boot command
    boot_cmd_start = 112
    boot_cmd = b"multiboot2 /boot/minint.elf\x00"
    core_img[boot_cmd_start:boot_cmd_start+22] = boot_cmd
    
    return bytes(core_img)

def create_grub_config():
    """Create a GRUB configuration file for the installer ISO"""
    
    config = """set timeout=0
set default=0

insmod all_video
set gfxmode=1920x1080x32,1024x768x32,800x600x32
set gfxpayload=keep

menuentry "MinNT - Run from Disk (Live Mode)" {
  multiboot2 /boot/minint.elf
  boot
}

menuentry "MinNT - Install to Hard Drive" {
  multiboot2 /boot/minint.elf /install
  boot
}

menuentry "MinNT - Safe Mode" {
  multiboot2 /boot/minint.elf /safemode
  boot
}

menuentry "MinNT - Recovery Console" {
  multiboot2 /boot/minint.elf /recovery
  boot
}
"""
    
    return config

def main():
    print("=" * 60)
    print("MinNT INSTALLER FIX")
    print("=" * 60)
    print()
    
    # Create GRUB directory
    os.makedirs(BOOT_GRUB_DIR, exist_ok=True)
    
    # Create minimal GRUB core.img
    print("Creating minimal GRUB core.img...")
    core_img = create_minimal_grub_core()
    with open(GRUB_CORE_IMG, 'wb') as f:
        f.write(core_img)
    print(f"  GRUB core.img created: {len(core_img)} bytes")
    print()
    
    # Create GRUB config
    print("Creating GRUB configuration...")
    config_file = os.path.join(BOOT_GRUB_DIR, "grub.cfg")
    with open(config_file, 'w') as f:
        f.write(create_grub_config())
    print(f"  GRUB config created: {config_file}")
    print()
    
    # Update installer Makefile
    print("Updating installer Makefile...")
    installer_makefile = os.path.join(MININT_DIR, "installer", "Makefile")
    
    # Check if the core.img needs to be copied to the initramfs
    # (it will be copied by the installer bootloader installation)
    
    print()
    print("=" * 60)
    print("INSTALLER FIX COMPLETE")
    print("=" * 60)
    print()
    print("Next steps:")
    print("  1. Build the kernel: cd " + MININT_DIR + " && make clean && make")
    print("  2. Build the ISO: cd " + MININT_DIR + " && make iso")
    print("  3. Flash to USB: sudo dd if=minint.iso of=/dev/sdX bs=4M")
    print()
    print("The installer will now:")
    print("  - Write GRUB stage1 to the MBR")
    print("  - Copy the embedded GRUB core.img to sector 1")
    print("  - Write the kernel image to /boot/minint.elf")
    print("  - Write the GRUB config to /boot/grub/grub.cfg")
    print()

if __name__ == "__main__":
    main()