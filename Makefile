# MinNT build
# make          -> minint.elf (multiboot2 kernel)
# make iso      -> minint.iso (needs grub-mkrescue + xorriso; run on ZorinOS)
# make run      -> QEMU boot (needs qemu-system-x86_64)
# make run-elf  -> QEMU direct multiboot of the ELF, no ISO needed

CC      := gcc
AS      := gcc
LD      := ld

CFLAGS  := -std=gnu11 -m64 -ffreestanding -fno-stack-protector -fno-pic \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel \
           -fshort-wchar -fno-builtin -nostdlib -Wall -Wextra -O2 -g \
           -Iinclude -Iwin32k -Indis -Itcpip/lwip_src -Iminint
ASFLAGS := -m64 -ffreestanding -fno-pic -c
LDFLAGS := -n -T linker.ld -z noexecstack -z max-page-size=0x1000 --no-relax

OBJS := boot/mbentry.o \
         boot/chain/chain.o \
         boot/chain/smss.o boot/chain/winlogon.o boot/chain/explorer.o \
         boot/chain/smss_real/sminit.o boot/chain/smss_real/smss.o boot/chain/smss_real/smsubsys.o \
         boot/chain/smss_real/smsessn.o boot/chain/smss_real/smutil.o \
         boot/chain/smss_real/smloop.o boot/chain/smss_real/smsbapi.o \
         boot/chain/smss_real/crashdmp.o boot/chain/smss_real/pagefile.o \
         ke/trap.o ke/ctxswap.o ke/idt.o ke/irql.o ke/bugcheck.o ke/dispatch.o minint/ke/smp.o minint/ke/aptramp.o minint/ke/apic.o \
          ke/syscall.o ke/services.o ke/pe.o ke/exe.o ke/exports/kernel32_exports.o ke/exports/ntdll_exports.o ke/exports/user32_exports.o ke/exports/gdi32_exports.o ke/exports/advapi32_exports.o ke/exports/ole32_exports.o ke/exports/dxgi_exports.o ke/exports/d3dcompiler_exports.o ke/exports/shell32_exports.o ke/exports/ws2_32_exports.o \
         hal/hal.o hal/fb.o hal/kbd.o hal/mouse.o hal/mb2fb.o rtl/rtl.o rtl/rtlsupp.o \
         lib/font/ttf.o \
          drivers/ata/ahci.o \
         mm/mminit.o mm/mmuser.o mm/cache.o \
         ex/pool.o ob/obmgr.o ps/psmgr.o \
         io/iomgr.o cm/cm.o cm/cmpers.o se/se.o fs/fs.o fs/fat32.o fs/partition.o fs/ntfs.o lpc/lpc.o \
         ndk/ndk_shim.o \
win32k/win32k.o win32k/gdikernel.o win32k/usermsg.o win32k/userwnd.o \
          win32k/d3d12/d3d12.o win32k/d3d12/samples/d3d12_triangle.o \
          usb/uhci.o usb/usbclass.o usb/usbenum.o \
ndis/ndis.o ndis/miniport.o \
           rtw/rtw_usb.o \
           tcpip/lwip_port.o tcpip/lwip_stdio.o \
           tcpip/lwip_src/core/def.o tcpip/lwip_src/core/inet_chksum.o \
           tcpip/lwip_src/core/init.o tcpip/lwip_src/core/mem.o \
           tcpip/lwip_src/core/memp.o tcpip/lwip_src/core/netif.o \
           tcpip/lwip_src/core/pbuf.o tcpip/lwip_src/core/raw.o \
           tcpip/lwip_src/core/stats.o tcpip/lwip_src/core/sys.o \
           tcpip/lwip_src/core/timeouts.o tcpip/lwip_src/core/udp.o \
           tcpip/lwip_src/core/tcp.o tcpip/lwip_src/core/tcp_in.o \
           tcpip/lwip_src/core/tcp_out.o tcpip/lwip_src/core/ip.o \
           tcpip/lwip_src/core/ipv4/etharp.o tcpip/lwip_src/core/ipv4/ip4.o \
           tcpip/lwip_src/core/ipv4/ip4_addr.o tcpip/lwip_src/core/ipv4/icmp.o \
           tcpip/lwip_src/core/ipv4/ip4_frag.o \
           tcpip/lwip_src/netif/ethernet.o \
           firmware/rtw_fw_blob.o \
          user/user_app_bin.o \
          init/kiinit.o

all: minint.elf

# Guard: an empty placeholder still builds; driver reports it at runtime
firmware/rtl8821cu.bin:
	@echo "firmware: no rtl8821cu.bin — creating empty placeholder"
	@echo "firmware: run firmware/fetch-firmware.sh to embed the real blob"
	@touch $@

firmware/rtw_fw_blob.o: firmware/rtw_fw_blob.S firmware/rtl8821cu.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

# Embed raw binary as an ELF object (objcopy --rename-section .data=.rodata)
user/user_app_bin.o: user/user_app.bin
	$(LD) -r -b binary $< -o $@ 2>/dev/null || \
	objcopy -I binary -O elf64-x86-64 -B i386 --rename-section .data=.rodata $< $@

minint.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "== Kernel built =="
	@size $@ || true

iso: minint.elf
	mkdir -p isoroot/boot/grub
	cp minint.elf isoroot/boot/
	printf 'set timeout=5\nset default=0\nmenuentry "MinNT (Live/Demo Mode)" {\n  multiboot2 /boot/minint.elf\n  boot\n}\nmenuentry "MinNT (Debug Mode)" {\n  multiboot2 /boot/minint.elf\n  boot\n}\n' \
    > isoroot/boot/grub/grub.cfg
	grub-mkrescue -o minint.iso isoroot

run: iso
	qemu-system-x86_64 -cdrom minint.iso -serial stdio -m 256M -boot order=d,menu=off


clean:
	rm -f $(OBJS) minint.elf minint.iso
	rm -rf isoroot

tcpip/lwip_src/core/ipv4/%.o: tcpip/lwip_src/core/ipv4/%.c
	$(CC) $(CFLAGS) -Itcpip/lwip_src -c $< -o $@

tcpip/lwip_src/netif/%.o: tcpip/lwip_src/netif/%.c
	$(CC) $(CFLAGS) -Itcpip/lwip_src -c $< -o $@

tcpip/lwip_src/core/%.o: tcpip/lwip_src/core/%.c
	$(CC) $(CFLAGS) -Itcpip/lwip_src -c $< -o $@

tcpip/sys_arch.o: tcpip/lwip_src/sys_arch.c
	$(CC) $(CFLAGS) -Itcpip/lwip_src -c $< -o $@

# Rules for minint/ subdirectory
minint/ke/%.o: minint/ke/%.c
	$(CC) $(CFLAGS) -c $< -o $@

minint/ke/%.o: minint/ke/%.S
	$(AS) $(ASFLAGS) $< -o $@

minint/tcpip/%.o: minint/tcpip/%.c
	$(CC) $(CFLAGS) -c $< -o $@
