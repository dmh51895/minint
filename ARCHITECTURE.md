# MinNT — NT 6.x Architecture Map & Patch Roadmap

A barebones NT-architecture kernel. The parts marked ✅ are implemented and
boot-tested in QEMU. Everything else is diagrammed here as your patch surface,
in dependency order, so each layer you add has something real underneath it.

## The Full NT 6.x Stack (what real Windows looks like)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  USER MODE                                                              │
│                                                                         │
│  ┌───────────┐ ┌───────────┐ ┌──────────┐ ┌───────────────────────────┐ │
│  │ Win32 apps│ │ Services  │ │ Console  │ │ Subsystem processes       │ │
│  │ (exe)     │ │(services. │ │ (conhost)│ │ smss.exe → csrss.exe      │ │
│  │           │ │ exe tree) │ │          │ │ wininit.exe → lsass.exe   │ │
│  └─────┬─────┘ └─────┬─────┘ └────┬─────┘ └─────────────┬─────────────┘ │
│        │             │            │                     │               │
│  ┌─────┴─────────────┴────────────┴───┐  ┌──────────────┴─────────────┐ │
│  │ Win32 API DLLs                     │  │ Native API                 │ │
│  │ kernel32 / user32 / gdi32 /        │  │ ntdll.dll                  │ │
│  │ advapi32 / kernelbase              │  │ (syscall stubs, ldr, RTL)  │ │
│  └────────────────────┬───────────────┘  └──────────────┬─────────────┘ │
│                       └───────────────┬─────────────────┘               │
├───────────────────────────────────────┼── syscall / sysret ─────────────┤
│  KERNEL MODE                          ▼                                 │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ NTOSKRNL.EXE — Executive                                         │   │
│  │ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌─────┐ │   │
│  │ │ Ob │ │ Ps │ │ Mm │ │ Io │ │ Cm │ │ Se │ │ Ex │ │ Po │ │ Alpc│ │   │
│  │ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘ └─────┘ │   │
│  │ ┌──────────────────────────────────────────────────────────────┐ │   │
│  │ │ Kernel (Ke): dispatcher, IRQL, DPC/APC, traps, spinlocks     │ │   │
│  │ └──────────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────┬───────────────────────────────────┘   │
│  ┌───────────────┐ ┌────────────┴──────────┐ ┌────────────────────────┐ │
│  │ win32k.sys    │ │ Drivers (.sys)        │ │ Filesystems            │ │
│  │ (GUI kernel   │ │ NDIS / storport /     │ │ ntfs.sys / fastfat.sys │ │
│  │  side)        │ │ your rtw88.sys 🥒     │ │                        │ │
│  └───────────────┘ └───────────┬───────────┘ └────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ HAL (hal.dll): interrupts, timers, port I/O, ACPI glue           │   │
│  └──────────────────────────────────────────────────────────────────┘   │
├──────────────────────────────────────────────────────────────────────────┤
│  BOOT: UEFI/BIOS → bootmgr → winload.exe → ntoskrnl + hal + boot drivers │
└──────────────────────────────────────────────────────────────────────────┘
```

## What MinNT implements today (✅ = compiled, booted, verified in QEMU)

| Layer | NT equivalent | MinNT file | Status |
|---|---|---|---|
| Boot loader | bootmgr + winload | `boot/mbentry.S` (GRUB mb2 → long mode) | ✅ |
| HAL | hal.dll | `hal/hal.c` — PIC, PIT 100Hz, COM1, VGA, port I/O, LAPIC timer | ✅ |
| Ke traps | KiTrap* family | `ke/trap.S` — 256 vectors, uniform KTRAP_FRAME | ✅ |
| Ke tables | KiSystemStartup GDT/IDT | `ke/idt.c` | ✅ |
| Ke IRQL | KfRaiseIrql/KfLowerIrql | `ke/irql.c` — bugchecks 0xA on violations, atomic ops | ✅ |
| Ke spinlocks | KeAcquireSpinLock | `ke/irql.c` — atomic + raise-to-DPC | ✅ |
| Ke bugcheck | KeBugCheckEx | `ke/bugcheck.c` — real stop codes, blue screen | ✅ |
| Ke ctx switch | KiSwapContext | `ke/ctxswap.S` | ✅ |
| KPCR/KPRCB | gs:[0] processor state | `include/nt/ke.h`, wired in `init/kiinit.c` | ✅ |
| Mm phase 0 | MmInitSystem, PFN DB | `mm/mminit.c` — mb2 map, allocator, MmMapPage | ✅ |
| Ex pool | ExAllocatePoolWithTag | `ex/pool.c` — tagged, BAD_POOL_HEADER checks | ✅ |
| Ob | Object Manager | `ob/obmgr.c` — types, headers, handles, names | ✅ |
| Ps | EPROCESS/ETHREAD | `ps/psmgr.c` — System PID 4, kernel threads | ✅ |
| Scheduler | KiDispatch (cooperative) | `ps/psmgr.c` round-robin, HLT on idle | ✅ |
| Rtl | ntoskrnl Rtl | `rtl/rtl.c` — mem*, UNICODE_STRING | ✅ |
| SMP | Ke*Processors masks | `minint/ke/smp.c` — 2 CPUs detected, KeOnlineProcessors | ✅ |
| LAPIC | Local APIC driver | `minint/ke/apic.c` — timer @ 100Hz, PIT calibration | ✅ |
| AP trampoline | ApStartup | `minint/ke/aptramp.S` — INIT-SIPI-SIPI sequence | ✅ |
| Dispatcher | KEVENT/KSEMAPHORE | `ke/dispatch.c` — Event, Mutant, Semaphore | ✅ |
| Fs | FAT16 driver | `fs/fs.c` — RamDisk, NtReadFile/NtWriteFile | ✅ |
| Win32k | GDI/USER | `win32k/` — GdiPatBlt, User* functions, Explorer | ✅ |
| Boot chain | smss → csrss → winlogon → explorer | `boot/chain/` — full NT boot | ✅ |

## Patch Roadmap (dependency-ordered — do them top to bottom)

### Tier 1 — kernel completeness (all inside this codebase)
1. **Preemption.** Hook `KiClockInterrupt` (ke/irql.c): decrement a quantum
   on the current KTHREAD; at zero, set `Prcb->QuantumEnd`. Because you
   can't swap stacks from inside an ISR safely with this frame layout,
   check the flag in `KfLowerIrql` when dropping below DISPATCH and call
   `KiDispatchNextThread()` there. That's morally exactly NT's
   dispatch-on-IRQL-drop.
2. **Dispatcher objects.** `KEVENT`, `KSEMAPHORE`, `KMUTEX` +
   `KeWaitForSingleObject`: add a `WaitListHead` per object, move waiting
   threads off the ready queue into `Waiting` state, wake on
   `KeSetEvent`. The `KTHREAD.WaitListEntry` field is already there.
3. **DPC queue.** `KeInsertQueueDpc` onto `KPRCB.DpcListHead` (already
   declared), drained on IRQL drop below DISPATCH. Needed before any real
   driver model.
4. **Per-process handle tables.** Move `ObpHandleTable` into EPROCESS.
   The global table is a deliberate simplification.
5. **Ob namespace directories.** Replace the flat named list with
   `\`, `\Device`, `\ObjectTypes`, `\GLOBAL??` directory objects and a
   path parser in `ObLookupObjectByName`.

### Tier 2 — the executive you're missing
6. **Io manager.** `DRIVER_OBJECT`, `DEVICE_OBJECT`, `IRP`,
   `IoCallDriver`, `IoCreateDevice`. This is the big one — once IRPs
   dispatch, your **rtw88.sys NDIS work plugs into a driver model you
   own end to end**. Start with a null.sys and a beep.sys.
7. **Cm (registry).** In-memory hive first: `\Registry\Machine\SYSTEM`
   with CurrentControlSet\Services keys, because Io boot-start driver
   loading reads it. Binary hive format later.
8. **Se (security).** Stub `SeAccessCheck` to always-grant first; real
   tokens/ACLs are post-user-mode work.
9. **Po (power)** and **Alpc** — stubs until user mode exists.

### Tier 3 — crossing the line to user mode
10. **Syscall path.** `syscall`/`sysret` MSRs (STAR/LSTAR/SFMASK), a
    KiSystemCall64, and a system service table. Ring 3 GDT entries
    (0x18/0x20) are already in `ke/idt.c` waiting.
11. **Mm user VAs.** Per-process PML4 (clone kernel-high mappings),
    `NtAllocateVirtualMemory`, demand-zero page faults in the vector-14
    handler instead of bugchecking.
12. **PE loader.** Map a PE32+ into a user address space, fix up
    imports against your ntdll. You already speak PE fluently from the
    rtw88.sys build — same format, other direction.
13. **ntdll.dll.** Syscall stubs + user-mode Rtl + LdrInitializeThunk.
14. **smss → csrss → session 0.** At this point you have an OS, not a
    kernel.

### Tier 4 — the long dream
15. win32k.sys / GDI, NTFS or FAT driver, NDIS proper (rtw88.sys goes
    here 🥒), SMP (LAPIC/IOAPIC replaces the 8259, per-CPU PCRs — the
    IRQL model swaps `cli/sti` for LAPIC TPR writes in exactly two
    functions in `ke/irql.c`).

## Design decisions you should know before patching

- **Identity mapping, low 1GB only.** Boot stub maps 0–1GB with 2MB pages.
  Kernel VAs == PAs for now. When you move to the canonical NT layout
  (`0xFFFFF800...`), the linker script, `mbentry.S` page tables, and
  `MM_BOOT_MAPPED_LIMIT` are the three touch points.
- **ELF, not PE.** GCC/ELF toolchain for velocity. The `NTAPI` macro in
  ntdef.h is the shim point when you port to mingw-w64 for real
  ms_abi/PE output.
- **The mb2-info clobber bug is fixed but instructive:** GRUB parks its
  info block in free RAM right after your image. Anything you place
  early must reserve past `info + TotalSize`. This is exactly why real
  winload hands ntoskrnl a `LOADER_PARAMETER_BLOCK` in memory it has
  already fenced off.
- **IRQL is software-modeled** over a fully masked PIC. Raising to
  DISPATCH+ does `cli`. It bugchecks 0xA on bad transitions, which
  already caught one real bug during bring-up.

## Boot sequence as-built

```
GRUB (multiboot2)
  └─ _start [boot/mbentry.S]         32-bit, paging off
      ├─ build PML4/PDPT/PD (1GB identity, 2MB pages)
      ├─ CR4.PAE, EFER.LME+NXE, CR0.PG, lgdt, far jump
      └─ KiSystemStartup [init/kiinit.c]   64-bit
          ├─ KPCR → gs base (wrmsr IA32_GS_BASE)
          ├─ HalInitSystem       serial + VGA + PIC remap/mask + PIT
          ├─ KeInitializeGdt/Idt 256 vectors → KiTrapTable
          ├─ KfLowerIrql(PASSIVE)
          ├─ MmInitSystem        PFN DB from mb2 map (snapshot first!)
          ├─ ExInitializePoolManager
          ├─ ObInitSystem
          ├─ PsInitSystem        Process/Thread types, System PID 4
          ├─ adopt boot ctx as System thread 0
          ├─ KiInitializeClockInterrupt   IRQ0 unmasked, 100Hz
          ├─ Phase 1: PsCreateSystemThread x2
          └─ dispatch loop / idle (hlt)
```
