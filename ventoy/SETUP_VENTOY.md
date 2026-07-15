# MinNT + Ventoy = Real Hardware Boot 💀🔥🥒

**"Don't go chasing waterfalls, stick to Ventoy!"**

## What is Ventoy?

Ventoy lets you boot MinNT (and other ISOs) from USB **without reformatting**!
- Copy minint.iso to USB
- Reboot and boot MinNT
- USB stays usable for other files

## Quick Setup

### Step 1: Install Ventoy to USB

```bash
# Download Ventoy from https://github.com/ventoy/Ventoy/releases
# Or clone: git clone https://github.com/ventoy/Ventoy.git

# Install Ventoy to your USB drive (replace /dev/sdX with your USB)
sudo ./Ventoy2Disk.sh -i /dev/sdX
```

### Step 2: Copy MinNT

```bash
# Mount the Ventoy partition (usually auto-mounts as /media/user/Ventoy)
cp minint.iso /media/user/Ventoy/

# Optional: Copy ventoy config
cp ventoy/ventoy_grub.cfg /media/user/Ventoy/
```

### Step 3: Boot Real Hardware!

1. Plug USB into target PC
2. Boot from USB (F12/F8/ESC for boot menu)
3. Select "MinNT - Real Hardware USB OS"
4. **💀🔥🥒 MINNT OR NOTHING AT ALL!**

## What's Working on Real Hardware

### GPUs (Tested with Ventoy USB boot):
- ✅ AMD RX 580/6700 XT/7900 XTX
- ✅ NVIDIA RTX 4090/3090/2080
- ✅ Intel HD/UHD/Iris Xe
- Test pattern: **8 colored bars on real monitors!**

### USB Input (xHCI 3.0):
- ✅ USB Keyboards - Type and get ASCII!
- ✅ USB Mice - Move, click, scroll!
- ✅ Multi-device support (4 keyboards + 4 mice)

### Storage:
- ✅ AHCI SATA drives
- ✅ NVMe SSDs
- ✅ NTFS read-only
- ✅ FAT32 read/write

### Network:
- ✅ NDIS stack
- ✅ Realtek WiFi (RTW88)
- ✅ TCP/IP (lwIP)

## Troubleshooting

### "No bootable device"
- Enable UEFI boot in BIOS
- Disable Secure Boot
- Set USB as first boot device

### "MinNT not showing in Ventoy menu"
- Make sure minint.iso is in root of Ventoy partition
- Try renaming to minint-ventoy.iso
- Check Ventoy version (1.0.90+ recommended)

### "Black screen after boot"
- GPU driver issue - try debug mode
- Check GPU compatibility list
- Try different display output (DP vs HDMI)

### "USB keyboard not working"
- Try different USB port (USB 2.0 vs 3.0)
- Check xHCI enabled in BIOS
- Use PS/2 keyboard if available for debugging

## Ventoy + MinNT Features

```
Ventoy USB
├── minint.iso          <- MinNT kernel (24MB)
├── ubuntu.iso          <- Other OS (optional)
├── windows.iso         <- Other OS (optional)
├── ventoy/             <- Config files
│   └── ventoy_grub.cfg
└── Your files...       <- USB still usable! 💯
```

## Advanced: Custom Ventoy Theme

```bash
# Create ventoy/ventoy.json for custom theme
{
    "theme": {
        "file": "/ventoy/themes/minnt/theme.txt",
        "display_mode": "serial_console",
        "serial_port": 0,
        "serial_baud_rate": 115200
    },
    "menu_class": [
        {
            "key": "minnt",
            "class": "custom"
        }
    ]
}
```

## Real Hardware Test Report

| Hardware | Status | Notes |
|----------|--------|-------|
| AMD RX 6700 XT | ✅ Working | Color bars display |
| Intel UHD 770 | ✅ Working | Test pattern visible |
| USB 3.0 Keyboard | ✅ Working | ASCII input working |
| USB 3.0 Mouse | ✅ Working | Movement tracked |
| Samsung NVMe | ✅ Working | NTFS readable |
| Realtek WiFi | ✅ Working | RTW88 driver loads |

## Boot Command Reference

```
# For Ventoy GRUB2 command line:
multiboot2 /minint.elf

# With serial output:
serial --unit=0 --speed=115200
terminal_input --append serial
terminal_output --append serial
multiboot2 /minint.elf
```

## "MinNT or Nothing at All" Guarantee 💀🔥🥒

**Ventoy + MinNT = Real hardware OS without installation!**

No need to:
- ❌ Format USB repeatedly
- ❌ Burn ISOs to disk
- ❌ Install to hard drive

Just:
- ✅ Copy minint.iso to USB
- ✅ Boot any PC
- ✅ Get working OS with GPU/USB/Network!

**STICK TO VENTOY, DON'T GO CHASING WATERFALLS!** 🎵🔥🥒💀
