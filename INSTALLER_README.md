# MinNT Installer Scripts

**Last Updated:** 2026-07-16 (Session 31)

This directory contains three installer scripts for deploying MinNT to real hardware.

---

## 🎯 Quick Start

**One-command installation (recommended):**
```bash
sudo ./install-minnt.sh
```

This builds MinNT from source, creates the installer package, extracts it, and flashes it to a USB drive - all in one command.

---

## 📦 The Three Scripts

### 1. `install-minnt.sh` - Complete One-Command Installer

**Purpose:** Does EVERYTHING: builds, packages, extracts, and flashes.

**Features:**
- ✅ Builds kernel from source
- ✅ Builds bootable ISO
- ✅ Creates installer package
- ✅ Extracts package
- ✅ Lists USB devices
- ✅ Flashes ISO to USB
- ✅ Verifies flash
- ✅ Shows success message

**Usage:**
```bash
sudo ./install-minnt.sh
```

**When to use:** When you want to build and install in one go.

---

### 2. `minnt-install.py` - Python Installer

**Purpose:** Python version with beautiful colored output and advanced features.

**Features:**
- ✅ Interactive device selection
- ✅ Auto mode (`--auto /dev/sdX`)
- ✅ Package path specification (`--package`)
- ✅ Skip confirmation (`--yes`)
- ✅ Beautiful ANSI colors
- ✅ Comprehensive error handling
- ✅ ISO signature verification

**Usage:**
```bash
# Interactive mode
sudo python3 minnt-install.py

# Auto mode (no prompts)
sudo python3 minnt-install.py --auto /dev/sdb --yes

# Custom package path
sudo python3 minnt-install.py --package /path/to/MinNT-Installer.tar.gz
```

**When to use:** When you want more control and better error handling.

---

### 3. `minnt-install.sh` - Bash Installer

**Purpose:** Lightweight bash version, no Python dependencies.

**Features:**
- ✅ Fast execution
- ✅ No external dependencies
- ✅ Same functionality as Python version
- ✅ Colored output
- ✅ USB device detection

**Usage:**
```bash
sudo ./minnt-install.sh
```

**When to use:** When Python is not available or you want a lightweight option.

---

## 🔧 How It Works

### Step 1: Build (only `install-minnt.sh`)
```bash
make clean
make          # Builds minint.elf (3.5MB)
make iso      # Builds minint.iso (25MB)
```

### Step 2: Package
Creates `MinNT-Installer-YYYYMMDD.tar.gz` containing:
- `minint.iso` - Bootable ISO
- `minint.elf` - MinNT kernel
- `flash-to-usb.sh` - USB flashing script
- `README.txt` - Instructions

### Step 3: Extract
Extracts the package to `/tmp/minnt-installer/`.

### Step 4: Select USB Device
Lists all removable USB devices and asks you to select one.

**Example output:**
```
══════════════════════════════════════════════════════════════════════
  #  Device         Size        Model
══════════════════════════════════════════════════════════════════════
   1  /dev/sdb      16.0 GB     SanDisk Ultra
   2  /dev/sdc      32.0 GB     Kingston DataTraveler
══════════════════════════════════════════════════════════════════════

Select USB device [1-2]:
```

### Step 5: Confirm
Requires typing "YES" to confirm the destructive operation.

**Warning shown:**
```
══════════════════════════════════════════════════════════════════════
  WARNING: DESTRUCTIVE OPERATION
══════════════════════════════════════════════════════════════════════
  Device:  /dev/sdb
  Size:    16.0 GB
  Model:   SanDisk Ultra
══════════════════════════════════════════════════════════════════════
  ALL DATA ON THIS DEVICE WILL BE ERASED!
══════════════════════════════════════════════════════════════════════

Type YES to continue (or 'no' to cancel):
```

### Step 6: Flash
Writes the ISO to the USB device using `dd`.

**Progress shown:**
```
[3/4] Flashing MinNT to /dev/sdb...
  Unmounting partitions...
  Wiping partition table...
  Writing MinNT ISO (25M)...
  1234567890 bytes (1.2 GB) copied, 30.0 s, 41.2 MB/s
  Syncing...
```

### Step 7: Verify
Checks for ISO signature (`CD001`) in the first sector.

### Step 8: Success
Shows success message with next steps.

---

## 🚀 Installation on Target Machine

After flashing the USB:

1. **Remove USB** from build machine
2. **Plug into target machine**
3. **Set BIOS/UEFI** to boot from USB
4. **Boot from USB**
5. **Select "Install to Hard Drive"** from GRUB menu
6. **Follow installer TUI:**
   - Select target disk
   - Confirm format
   - Wait for installation
   - Reboot

**The installer will:**
- Detect all disks via AHCI
- Allow you to select target disk
- Create FAT32 partition
- Install MinNT kernel and bootloader
- Configure system settings

---

## 🛡️ Safety Features

All three scripts include:

1. **Root check** - Must be run as root
2. **USB device filtering** - Only shows removable devices
3. **Explicit confirmation** - Requires "YES" to flash
4. **Unmount before flash** - Unmounts any mounted partitions
5. **Partition table wipe** - Wipes first 10MB before writing
6. **Sync after flash** - Ensures data is written to disk
7. **ISO signature verification** - Checks for valid ISO after flash
8. **Cleanup** - Removes temporary files

---

## 🐛 Troubleshooting

### "No removable USB devices found"
- Make sure a USB drive is plugged in
- Check that the USB drive is recognized by the system: `lsblk`
- Try a different USB port

### "ERROR: Must be run as root"
- Run with sudo: `sudo ./install-minnt.sh`

### "ERROR: Installer package not found"
- Build the package first: `./build-installer.sh`
- Or specify path: `--package /path/to/package.tar.gz`

### Flash fails
- Try a different USB drive
- Check USB drive for physical damage
- Try a different USB port
- Make sure USB drive is not write-protected

### Target machine won't boot from USB
- Enable USB boot in BIOS/UEFI
- Set USB as first boot device
- Try Legacy Boot mode instead of UEFI
- Try a different USB port

---

## 📊 Script Comparison

| Feature | install-minnt.sh | minnt-install.py | minnt-install.sh |
|---------|------------------|------------------|------------------|
| Builds from source | ✅ | ❌ | ❌ |
| Creates package | ✅ | ❌ | ❌ |
| Extracts package | ✅ | ✅ | ✅ |
| Flashes to USB | ✅ | ✅ | ✅ |
| Verifies flash | ✅ | ✅ | ✅ |
| Auto mode | ❌ | ✅ | ❌ |
| Custom package path | ❌ | ✅ | ❌ |
| Python required | ❌ | ✅ | ❌ |
| Colored output | ✅ | ✅ | ✅ |
| ASCII art banner | ✅ | ✅ | ✅ |
| Size | 9.6KB | 13KB | 11KB |

---

## 🎉 Success!

After successful installation, the target machine will boot MinNT with:
- Full NT 6.x architecture
- 56 subsystems initialized
- WINLOGON, EXPLORER, Control Panel
- 500MB free memory
- Stable operation

**Happy installing! 🥒🔥💀**