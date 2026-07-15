/*
 * MinNT - boot/setupldr.c
 * MinNT Setup Loader (setupldr)
 *
 * A separate, minimal multiboot2 kernel used only for installing
 * MinNT to a target disk. Equivalent to Windows' boot.wim + setup.exe
 * pair or Tiny Core's installer environment.
 *
 * setupldr contains ONLY:
 *   - Multiboot2 entry stub (setupldr_entry.S)
 *   - HAL init (PIC, PIT, serial for DbgPrint)
 *   - Multiboot2 framebuffer parsing
 *   - Framebuffer primitive layer (text rendering)
 *   - PS/2 keyboard
 *   - AHCI disk access (read + write)
 *   - FAT32 format + write
 *   - The installer TUI
 *
 * It does NOT contain:
 *   - Window manager / win32k
 *   - Network stack
 *   - WMI / RPC / COM
 *   - Any bundled apps
 *   - Explorer shell
 *   - Audio subsystem
 *
 * Target size: <64KB. After install, the full minint.elf is copied to
 * the target disk and the user reboots into it.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

/* ---- Forward declarations of HAL primitives exposed from hal/. ---- */
NTSTATUS NTAPI HalInitSystem(VOID);
NTSTATUS NTAPI HalpKbdInit(VOID);
BOOLEAN NTAPI HalpKbdHasKey(VOID);
CHAR NTAPI HalpKbdGetChar(VOID);

NTSTATUS NTAPI HalpFbInit(PVOID FramebufferAddr, ULONG Width, ULONG Height, ULONG Pitch, ULONG Bpp);
ULONG NTAPI HalpFbGetWidth(VOID);
ULONG NTAPI HalpFbGetHeight(VOID);
VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
VOID NTAPI HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);

NTSTATUS NTAPI AhciInitSystem(VOID);
ULONG NTAPI AhciGetDiskCount(VOID);
ULONG64 NTAPI AhciGetDiskSize(ULONG DiskNumber);
VOID NTAPI AhciGetDiskModel(ULONG DiskNumber, PCHAR Buffer, ULONG MaxLen);
NTSTATUS NTAPI AhciReadSectorsEx(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer);
NTSTATUS NTAPI AhciWriteSectorsEx(ULONG DiskNumber, ULONG64 Lba, ULONG Count, PVOID Buffer);

NTSTATUS NTAPI OsInstallInit(VOID);
ULONG NTAPI OsInstallScanDisks(VOID);
ULONG NTAPI OsInstallScanPartitions(ULONG DiskNumber);
NTSTATUS NTAPI OsInstallGetDisk(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                PCHAR OutModel, ULONG MaxLen);
NTSTATUS NTAPI OsInstallGetPartition(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                     PCHAR OutFs, ULONG MaxLen, PBOOLEAN OutBootable);
NTSTATUS NTAPI OsInstallSelectDisk(ULONG DiskIndex);
NTSTATUS NTAPI OsInstallSelectPartition(ULONG PartitionIndex);
NTSTATUS NTAPI OsInstallRun(PCHAR ProgressMessage, PULONG Percent);

/* ---- Multiboot2 framebuffer info ---- */
typedef struct _MB2_TAG_HEADER {
    ULONG Type;
    ULONG Size;
} MB2_TAG_HEADER;

typedef struct _MB2_FB_TAG {
    ULONG Type;
    ULONG Size;
    ULONG64 Addr;
    ULONG Pitch;
    ULONG Width;
    ULONG Height;
    UCHAR Bpp;
    UCHAR FbType;
    USHORT Reserved;
} MB2_FB_TAG;

#define MB2_TAG_TYPE_FRAMEBUFFER 8

static VOID SetupParseFramebuffer(PVOID Mb2Info)
{
    if (!Mb2Info) return;
    ULONG totalSize = *(ULONG *)Mb2Info;
    UCHAR *ptr = (UCHAR *)Mb2Info + 8;
    UCHAR *end = (UCHAR *)Mb2Info + totalSize;
    while (ptr < end) {
        MB2_TAG_HEADER *tag = (MB2_TAG_HEADER *)ptr;
        if (tag->Type == 0 && tag->Size == 0) break;
        if (tag->Type == MB2_TAG_TYPE_FRAMEBUFFER) {
            MB2_FB_TAG *fb = (MB2_FB_TAG *)ptr;
            HalpFbInit((PVOID)(ULONG_PTR)fb->Addr, fb->Width, fb->Height,
                        fb->Pitch, fb->Bpp);
            DbgPrint("SETUPLDR: framebuffer %ux%u at %p\n",
                     fb->Width, fb->Height, (PVOID)(ULONG_PTR)fb->Addr);
            return;
        }
        ULONG aligned = (tag->Size + 7) & ~7;
        ptr += aligned;
    }
    DbgPrint("SETUPLDR: no framebuffer tag; using VGA text fallback\n");
}

/* ---- A minimal text-mode installer UI ----
 *
 * We draw a header bar, a step indicator, the disk list, and wait
 * for keyboard input. No mouse, no fancy widgets - just text.
 */

#define FB_W_EST 1024
#define FB_H_EST 768

static VOID DrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg)
{
    HalpFbDrawString(X, Y, Str, Fg, Bg);
}

static VOID DrawHeader(ULONG W)
{
    HalpFbFillRect(0, 0, W, 32, 0x00306EA5);
    DrawString(8, 8, "MinNT Setup", 0x00FFFFFF, 0x00306EA5);
}

static VOID DrawStep(ULONG Y, BOOLEAN Current, const CHAR *Text)
{
    ULONG Fg = Current ? 0x00FFFF00 : 0x00CCCCCC;
    DrawString(40, Y, Text, Fg, 0x00000000);
}

static ULONG WaitKey(VOID)
{
    while (!HalpKbdHasKey()) {
        /* PIT-style wait. MinNT HAL exposes KeStallExecutionProcessor. */
        extern VOID KeStallExecutionProcessor(ULONG Microseconds);
        KeStallExecutionProcessor(40000);
    }
    return (ULONG)HalpKbdGetChar();
}

static BOOLEAN IsUp(ULONG k)   { return k == 0x80 || k == 'w'; }
static BOOLEAN IsDown(ULONG k) { return k == 0x81 || k == 's'; }
static BOOLEAN IsEnter(ULONG k) { return k == '\r' || k == '\n'; }
static BOOLEAN IsEsc(ULONG k)   { return k == 27; }

/* Pick an item from a list with up/down/enter. Returns index or -1. */
static LONG Pick(ULONG X, ULONG Y, ULONG ItemCount,
                  CHAR Names[][64], ULONG Stride)
{
    ULONG sel = 0;
    for (;;) {
        for (ULONG i = 0; i < ItemCount; i++) {
            ULONG Fg = (i == sel) ? 0x0000FF00 : 0x00CCCCCC;
            /* Clean row first via FillRect (not DrawRect!) */
            HalpFbFillRect(X, Y + i * 18, FB_W_EST - X - 16, 18, 0x00000000);
            DrawString(X, Y + i * 18, Names[i * Stride], Fg, 0x00000000);
        }
        ULONG k = WaitKey();
        if (IsUp(k) && sel > 0) sel--;
        else if (IsDown(k) && sel < ItemCount - 1) sel++;
        else if (IsEnter(k)) return (LONG)sel;
        else if (IsEsc(k)) return -1;
    }
}

/* ---- The actual setup flow ---- */

static VOID ClearScreen(VOID)
{
    ULONG W = HalpFbGetWidth(), H = HalpFbGetHeight();
    HalpFbFillRect(0, 0, W, H, 0x00000000);
}

VOID NTAPI SetupLoaderMain(PVOID BootInfo)
{
    /* Skip the full HalInitSystem - PIC/PIT init isn't needed
     * for the installer. The setupldr binary is a minimal kernel
     * with just enough HAL to print to COM1 + draw to framebuffer.
     * Full system services come up in minint.elf. */
    __asm__ __volatile__("mov $0x3F8, %%rdx\n\t"
                         "mov $'A', %%al\n\t"
                         "outb %%al, %%dx" : : : "rdx", "al");
    HalpSerialInit();
    __asm__ __volatile__("mov $0x3F8, %%rdx\n\t"
                         "mov $'B', %%al\n\t"
                         "outb %%al, %%dx" : : : "rdx", "al");
    DbgPrint("\n=== MinNT Setup Loader (setupldr) ===\n");
    __asm__ __volatile__("mov $0x3F8, %%rdx\n\t"
                         "mov $'C', %%al\n\t"
                         "outb %%al, %%dx" : : : "rdx", "al");
    SetupParseFramebuffer(BootInfo);
    HalpKbdInit();
    OsInstallInit();
    ULONG diskCount = OsInstallScanDisks();
    DbgPrint("SETUPLDR: %u disks detected\n", diskCount);

    ULONG W = HalpFbGetWidth();

    ClearScreen();
    DrawHeader(W);
    DrawStep(48, TRUE, "Select target disk");
    DrawStep(72, FALSE, "Select partition");
    DrawStep(96, FALSE, "Format & install MinNT");
    DrawStep(120, FALSE, "Reboot");

    if (diskCount == 0) {
        DrawString(40, 200, "No disks detected.", 0x00FF0000, 0x00000000);
        DrawString(40, 220, "Press any key to halt.", 0x00FFFFFF, 0x00000000);
        WaitKey();
        for (;;) { __asm__ __volatile__("hlt"); }
    }

    /* Build the disk list. */
    CHAR diskNames[8][64];
    for (ULONG i = 0; i < diskCount; i++) {
        ULONG Num;
        ULONG64 Size;
        CHAR Model[40];
        OsInstallGetDisk(i, &Num, &Size, Model, 40);
        ULONG64 GB = Size / (1024ULL * 1024 * 1024);
        ULONG k = 0;
        CHAR prefix[] = "[";
        for (ULONG j = 0; prefix[j] && k < 60; j++) diskNames[i][k++] = prefix[j];
        diskNames[i][k++] = '0' + (CHAR)(Num % 10);
        diskNames[i][k++] = ']';
        diskNames[i][k++] = ' ';
        for (ULONG j = 0; Model[j] && k < 60; j++) diskNames[i][k++] = Model[j];
        diskNames[i][k++] = ' ';
        diskNames[i][k++] = '(';
        if (GB == 0) { diskNames[i][k++] = '0'; }
        else {
            CHAR tmp[20]; ULONG t = 0;
            ULONG64 v = GB;
            while (v > 0 && t < 19) { tmp[t++] = '0' + (CHAR)(v % 10); v /= 10; }
            while (t > 0 && k < 60) diskNames[i][k++] = tmp[--t];
        }
        diskNames[i][k++] = 'G'; diskNames[i][k++] = 'B'; diskNames[i][k++] = ')';
        diskNames[i][k] = 0;
    }

    DrawString(40, 200, "Select the disk to install MinNT onto:", 0x00FFFFFF, 0x00000000);
    LONG diskIdx = Pick(40, 220, diskCount, diskNames, 1);
    if (diskIdx < 0) {
        for (;;) { __asm__ __volatile__("hlt"); }
    }
    OsInstallSelectDisk((ULONG)diskIdx);

    ClearScreen();
    DrawHeader(W);
    DrawStep(48, TRUE, "Select target disk (done)");
    DrawStep(72, TRUE, "Select partition");
    DrawStep(96, FALSE, "Format & install MinNT");
    DrawStep(120, FALSE, "Reboot");

    ULONG partCount = OsInstallScanPartitions((ULONG)diskIdx);
    if (partCount == 0) {
        DrawString(40, 200, "No partitions found; the whole disk will be used.",
                   0x00FFFFFF, 0x00000000);
        DrawString(40, 220, "Press ENTER to format and install, ESC to cancel.",
                   0x00FFFFFF, 0x00000000);
        if (IsEsc(WaitKey())) {
            for (;;) { __asm__ __volatile__("hlt"); }
        }
        OsInstallSelectPartition(0);
    } else {
        CHAR partNames[OSINSTALL_MAX_PARTITIONS][64];
        for (ULONG i = 0; i < partCount; i++) {
            ULONG Pn; ULONG64 Sz; CHAR Fs[16]; BOOLEAN Bootable;
            OsInstallGetPartition(i, &Pn, &Sz, Fs, 16, &Bootable);
            ULONG64 MB = Sz / (1024ULL * 1024);
            ULONG k = 0;
            partNames[i][k++] = '[';
            partNames[i][k++] = '0' + (CHAR)(Pn % 10);
            partNames[i][k++] = ']';
            partNames[i][k++] = ' ';
            if (Bootable) { partNames[i][k++] = '*'; partNames[i][k++] = ' '; }
            for (ULONG j = 0; Fs[j] && k < 30; j++) partNames[i][k++] = Fs[j];
            partNames[i][k++] = ' ';
            partNames[i][k++] = '(';
            if (MB == 0) { partNames[i][k++] = '0'; }
            else {
                CHAR tmp[20]; ULONG t = 0;
                ULONG64 v = MB;
                while (v > 0 && t < 19) { tmp[t++] = '0' + (CHAR)(v % 10); v /= 10; }
                while (t > 0 && k < 60) diskNames[i][k++] = tmp[--t];
            }
            partNames[i][k++] = 'M'; partNames[i][k++] = 'B'; partNames[i][k++] = ')';
            partNames[i][k] = 0;
        }
        DrawString(40, 200, "Select the partition to install onto:", 0x00FFFFFF, 0x00000000);
        LONG partIdx = Pick(40, 220, partCount, partNames, 1);
        if (partIdx < 0) {
            for (;;) { __asm__ __volatile__("hlt"); }
        }
        OsInstallSelectPartition((ULONG)partIdx);
    }

    ClearScreen();
    DrawHeader(W);
    DrawStep(48, TRUE, "Select target disk (done)");
    DrawStep(72, TRUE, "Select partition (done)");
    DrawStep(96, TRUE, "Format & install MinNT");
    DrawStep(120, FALSE, "Reboot");

    DrawString(40, 200, "Press ENTER to install, ESC to cancel.", 0x00FFFF00, 0x00000000);
    if (IsEsc(WaitKey())) {
        for (;;) { __asm__ __volatile__("hlt"); }
    }

    /* Run the actual install. */
    ClearScreen();
    DrawHeader(W);
    DrawString(40, 200, "Installing MinNT to disk...", 0x00FFFF00, 0x00000000);
    CHAR progress[128] = "Starting...";
    ULONG percent = 0;
    OsInstallRun(progress, &percent);

    if (OsInstallIsComplete()) {
        ClearScreen();
        DrawHeader(W);
        DrawString(40, 200, "Install complete!", 0x0000FF00, 0x00000000);
        DrawString(40, 220, "Remove the install media and press ENTER to reboot.", 0x00FFFFFF, 0x00000000);
        WaitKey();
        /* Reboot via ACPI reset. */
        extern NTSTATUS NTAPI AcpiReboot(VOID);
        AcpiReboot();
    } else {
        DrawString(40, 400, "Install failed. Press any key to halt.", 0x00FF0000, 0x00000000);
        WaitKey();
    }

    for (;;) { __asm__ __volatile__("hlt"); }
}
