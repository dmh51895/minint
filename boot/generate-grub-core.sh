#!/bin/bash
# Generate embedded GRUB core.img for MinNT
# This creates a minimal GRUB 2 core.img that loads minint.elf

set -e

GRUB_CORE_IMG="boot/grub_core.img"
MKIMAGE="/usr/sbin/grub-mkimage"

if [ ! -f "$MKIMAGE" ]; then
    echo "ERROR: grub-mkimage not found. Install grub-pc-bin."
    exit 1
fi

echo "Creating embedded GRUB core.img..."

# Create a temporary directory for GRUB config
GRUB_TMP=$(mktemp -d)
trap "rm -rf $GRUB_TMP" EXIT

# Create GRUB config
cat > "$GRUB_TMP/grub.cfg" << 'EOF'
set timeout=0
set default=0

insmod all_video
set gfxmode=1024x768x32,800x600x32,640x480x32
set gfxpayload=keep

menuentry "MinNT" {
    multiboot2 /boot/minint.elf
    boot
}
EOF

# Generate the GRUB core.img
$MKIMAGE -O i386-pc \
    -o "$GRUB_CORE_IMG" \
    -c "$GRUB_TMP/grub.cfg" \
    -p /boot/grub \
    biosdisk part_msdos fat multiboot multiboot2 normal configfile serial

echo "GRUB core.img created: $GRUB_CORE_IMG"
ls -lh "$GRUB_CORE_IMG"

# Now embed it in the kernel
echo "Embedding core.img into kernel..."
cp "$GRUB_CORE_IMG" boot/grub_core.bin

# Create a linker script to embed the binary
cat > boot/grub_core_linker.ld << EOF
ENTRY(grub_core_start)

SECTIONS {
    . = ALIGN(4096);
    grub_core_start = .;
    .grub_core_img : {
        KEEP(*(.grub_core_img))
    }
    grub_core_end = .;
}
EOF

# Build the object file
gcc -c boot/grub_core_linker.ld -o boot/grub_core_linker.o 2>/dev/null || true

echo "Done! GRUB core.img is embedded in boot/grub_core.bin"
echo ""
echo "To build the kernel with embedded core.img, run:"
echo "  make clean && make"