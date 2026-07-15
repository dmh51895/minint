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
         boot/chain/cpl.o boot/chain/admin.o boot/chain/admin2.o \
         boot/chain/smss.o boot/chain/winlogon.o boot/chain/explorer.o \
         boot/chain/smss_real/sminit.o boot/chain/smss_real/smss.o boot/chain/smss_real/smsubsys.o \
         boot/chain/smss_real/smsessn.o boot/chain/smss_real/smutil.o \
         boot/chain/smss_real/smloop.o boot/chain/smss_real/smsbapi.o \
         boot/chain/smss_real/crashdmp.o boot/chain/smss_real/pagefile.o \
         ke/trap.o ke/ctxswap.o ke/idt.o ke/irql.o ke/bugcheck.o ke/dispatch.o ke/timer.o minint/ke/smp.o minint/ke/aptramp.o minint/ke/apic.o \
          ke/syscall.o ke/services.o ke/pe.o ke/exe.o ke/exports/kernel32_exports.o ke/exports/ntdll_exports.o ke/exports/user32_exports.o ke/exports/gdi32_exports.o ke/exports/advapi32_exports.o ke/exports/ole32_exports.o ke/exports/dxgi_exports.o ke/exports/d3dcompiler_exports.o ke/exports/shell32_exports.o ke/exports/ws2_32_exports.o \
         hal/hal.o hal/fb.o hal/kbd.o hal/mouse.o hal/mb2fb.o rtl/rtl.o rtl/rtlsupp.o \
         lib/font/ttf.o \
          drivers/ata/ahci.o \
         mm/mminit.o mm/mmuser.o mm/cache.o \
         ex/pool.o ex/lookaside.o ex/pushlock.o ex/eresource.o ex/workitem.o ex/rundown.o ob/obmgr.o ps/psmgr.o \
          io/iomgr.o cm/cm.o cm/cmpers.o se/se.o se/scm.o fs/fs.o fs/fat32.o fs/partition.o fs/ntfs.o fs/recycle.o fs/reparse.o fs/vss.o fs/exfat.o fs/iso9660.o fs/filter.o ole32/comreg.o lpc/lpc.o rpc/rpc.o wmi/wmi.o acpi/acpi.o \
          io/pnp.o debug/kd.o \
          shell/ns.o shell/safeusb.o shell/sync.o display/topo.o diag/etw.o diag/reliability.o print/spooler.o \
          boot/safemode.o boot/bootcfg.o boot/menu.o boot/bootargs.o boot/profile.o boot/registry.o \
          input/touch.o input/ime.o input/gamepad.o input/steam_input.o input/remap.o input/touchpad.o input/ctrlname.o input/hid.o input/tsf.o \
          mm/quotas.o mm/profiles.o \
          media/codecs.o security/tpm.o setupapi/installer.o setupapi/osinstall.o \
           audio/engine.o ps/apc.o ps/job.o ps/fiber.o \
           admin/mmc.o \
           apps/notepad.o apps/calculator.o apps/terminal.o apps/taskmgr.o apps/properties.o \
           wine/wine.o wine/wineboot.o winsxs/fusion.o com/apartment.o \
           ndk/ndk_shim.o \
win32k/win32k.o win32k/gdikernel.o win32k/usermsg.o win32k/userwnd.o \
          win32k/atom.o win32k/clipboard.o win32k/base.o win32k/capture.o \
          win32k/desktop.o win32k/dirs.o win32k/dragdrop.o win32k/event.o \
          win32k/ex.o win32k/icons.o win32k/keyboard.o win32k/libmgmt.o \
          win32k/loadbits.o win32k/logon.o win32k/queue.o win32k/movesizs.o \
          win32k/profile.o win32k/syscmd.o win32k/taskman.o win32k/timers.o \
          win32k/winmgr.o win32k/winable.o win32k/validate.o win32k/winwhere.o \
          win32k/update.o win32k/settings.o \
          win32k/d3d12/d3d12.o win32k/d3d12/samples/d3d12_triangle.o \
           usb/uhci.o usb/usbclass.o usb/usbenum.o usb/xhci.o usb/xhci_enum.o usb/hid_kbd.o usb/hid_mouse.o \
           drivers/gpu/gpu.o drivers/gpu/amd/amdgpu.o drivers/gpu/intel/intelgpu.o drivers/gpu/nvidia/nvidia.o drivers/gpu/virtio/virtio_gpu.o drivers/gpu/sw/swgpu.o \
ndis/ndis.o ndis/miniport.o ndis/connections.o \
           rtw/rtw_usb.o \
           tcpip/lwip_port.o tcpip/lwip_stdio.o net/firewall.o net/dhcp.o net/dns.o \
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
	printf 'set timeout=10\nset default=0\n\ninsmod all_video\nset gfxmode=1920x1080x32,1024x768x32,1024x768,800x600,auto\nset gfxpayload=keep\n\nmenuentry "MinNT - Run from Disk (Live Mode)" {\n  echo "Loading MinNT Live..."\n  multiboot2 /boot/minint.elf\n  boot\n}\nmenuentry "MinNT - Install to Hard Drive" {\n  echo "Loading MinNT Installer..."\n  multiboot2 /boot/minint.elf /install\n  boot\n}\nmenuentry "MinNT - Safe Mode (Minimal Drivers)" {\n  echo "Loading MinNT Safe Mode..."\n  multiboot2 /boot/minint.elf /safemode\n  boot\n}\nmenuentry "MinNT - Safe Mode with Networking" {\n  echo "Loading MinNT Safe Mode + Network..."\n  multiboot2 /boot/minint.elf /safemode /network\n  boot\n}\nmenuentry "MinNT - Debug Mode (Serial Output)" {\n  echo "Loading MinNT (Debug)..."\n  set gfxpayload=text\n  multiboot2 /boot/minint.elf /debug\n  boot\n}\nmenuentry "MinNT - Text Mode Terminal" {\n  echo "Loading MinNT Terminal..."\n  set gfxpayload=text\n  multiboot2 /boot/minint.elf /terminal\n  boot\n}\nmenuentry "MinNT - Recovery Console" {\n  echo "Loading MinNT Recovery..."\n  set gfxpayload=text\n  multiboot2 /boot/minint.elf /recovery\n  boot\n}\nmenuentry "Reboot" {\n  reboot\n}\nmenuentry "Power Off" {\n  halt\n}\n' \
    > isoroot/boot/grub/grub.cfg
	grub-mkrescue -o minint.iso isoroot

run: iso
	qemu-system-x86_64 -cdrom minint.iso -serial stdio -m 512M -boot order=d,menu=off -vga std


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

