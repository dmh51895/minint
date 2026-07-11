# MinNT 🥒

A barebones NT 6.x-architecture kernel: real boot, real IRQL, real object
manager, real blue screen. Kernel + important files implemented; everything
else diagrammed in `ARCHITECTURE.md` as the patch roadmap.

**Status: boots clean in QEMU.** Long mode, 255MB managed, System process
PID 4, two kernel threads context-switching, PIT at 100Hz, and a
`KeBugCheckEx` that produces an honest white-on-blue stop screen with real
stop codes (it caught two genuine bring-up bugs, working exactly as designed).

## Build (ZorinOS / any Linux with gcc + binutils)

```sh
make            # -> minint.elf
make iso        # -> minint.iso   (needs grub-pc-bin, xorriso, mtools)
make run        # boots the ISO in QEMU with COM1 on stdio
```

Note: QEMU's `-kernel` flag only speaks multiboot1 — this kernel is
multiboot2, so always boot via the ISO/GRUB path.

## Layout

```
boot/mbentry.S    multiboot2 → long mode → KiSystemStartup
ke/               trap.S ctxswap.S idt.c irql.c bugcheck.c
mm/mminit.c       PFN database + physical allocator + MmMapPage
ex/pool.c         tagged NonPaged pool (BAD_POOL_HEADER enforced)
ob/obmgr.c        object types/headers/handles/names
ps/psmgr.c        EPROCESS/ETHREAD, System PID 4, round-robin dispatch
hal/hal.c         PIC, PIT, COM1, VGA, DbgPrint
rtl/rtl.c         mem* + UNICODE_STRING
init/kiinit.c     phase 0/1 init
include/nt/       ntdef.h ke.h mm.h ex.h ob.h ps.h hal.h rtl.h
linker.ld         kernel at 1MB
ARCHITECTURE.md   full NT 6.x diagram + dependency-ordered patch roadmap
```

## Debug tips

- All `DbgPrint` output mirrors to COM1: `-serial stdio` or
  `-serial file:serial.log`.
- Trigger a test bugcheck: `KeBugCheck(MANUALLY_INITIATED_CRASH);`
- The IRQL model bugchecks 0xA on illegal raise/lower — if you see it,
  read params: P1=target, P2=current, P4=1 raise path / 2 lower path.
