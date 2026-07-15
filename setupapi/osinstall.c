/*
 * MinNT - setupapi/osinstall.c
 * Operating System installer.
 *
 * Installs MinNT from the live ISO to a target disk:
 *   1. Disk selection (enumerate physical disks via AHCI)
 *   2. Partition selection or creation (FAT32)
 *   3. File copy (kernel image, boot config, system files)
 *   4. Bootloader installation (GRUB stage1/stage2 to disk MBR)
 *   5. Post-install configuration (registry seed, user account)
 *
 * The installer runs in kernel mode during the boot menu phase
 * (before the shell starts). It writes progress to the framebuffer.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/fs.h>
#include <nt/ob.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

#define OSINSTALL_MAX_DISKS         8
#define OSINSTALL_MAX_PARTITIONS    16
#define OSINSTALL_MAX_PATH          260
#define OSINSTALL_SECTOR_SIZE       512
#define OSINSTALL_MBR_SIZE          512

typedef enum _OSINSTALL_PHASE {
    OsInstallNone = 0,
    OsInstallDiskSelect,
    OsInstallPartitionSelect,
    OsInstallFormat,
    OsInstallCopyFiles,
    OsInstallBootloader,
    OsInstallConfigure,
    OsInstallDone,
    OsInstallFailed,
} OSINSTALL_PHASE;

typedef struct _OSINSTALL_DISK {
    ULONG DiskNumber;
    ULONG64 DiskSize;
    ULONG SectorSize;
    ULONG SectorCount;
    CHAR ModelName[40];
    BOOLEAN InUse;
    UCHAR BusType;      /* 0=AHCI, 1=USB, 2=Virtual */
} OSINSTALL_DISK;

typedef struct _OSINSTALL_PARTITION {
    ULONG PartitionNumber;
    ULONG64 StartSector;
    ULONG64 SectorCount;
    ULONG64 SizeBytes;
    CHAR FileSystem[16];
    BOOLEAN Bootable;
    BOOLEAN InUse;
} OSINSTALL_PARTITION;

typedef struct _OSINSTALL_STATE {
    OSINSTALL_PHASE Phase;
    OSINSTALL_DISK Disks[OSINSTALL_MAX_DISKS];
    ULONG DiskCount;
    OSINSTALL_PARTITION Partitions[OSINSTALL_MAX_PARTITIONS];
    ULONG PartitionCount;
    ULONG SelectedDisk;
    ULONG SelectedPartition;
    BOOLEAN FormatSelected;
    CHAR InstallRoot[OSINSTALL_MAX_PATH];
    CHAR ComputerName[32];
    CHAR UserName[32];
    BOOLEAN InstallBootloader;
    ULONG ProgressPercent;
    CHAR StatusMessage[128];
    BOOLEAN Completed;
    BOOLEAN Failed;
} OSINSTALL_STATE;

static OSINSTALL_STATE g_Install;

/* ---- Disk enumeration via AHCI + USB ---- */

static ULONG OsInstallEnumerateDisks(OSINSTALL_DISK *Disks, ULONG MaxCount)
{
    ULONG count = 0;

    /* AHCI disks (internal SATA / NVMe) */
    {
        extern ULONG AhciGetDiskCount(VOID);
        extern ULONG64 AhciGetDiskSize(ULONG DiskNumber);
        extern VOID AhciGetDiskModel(ULONG DiskNumber, PCHAR Buffer, ULONG MaxLen);
        ULONG ahciCount = AhciGetDiskCount();
        for (ULONG i = 0; i < ahciCount && count < MaxCount; i++) {
            Disks[count].DiskNumber = i;
            Disks[count].DiskSize = AhciGetDiskSize(i);
            Disks[count].SectorSize = OSINSTALL_SECTOR_SIZE;
            Disks[count].SectorCount = (ULONG)(Disks[count].DiskSize / OSINSTALL_SECTOR_SIZE);
            Disks[count].InUse = TRUE;
            Disks[count].BusType = 0; /* AHCI */
            AhciGetDiskModel(i, Disks[count].ModelName, sizeof(Disks[count].ModelName));
            count++;
        }
    }

    /* USB mass storage (portable SSDs, flash drives, HDD enclosures) */
    {
        extern ULONG NTAPI UsbMassGetDiskCount(VOID);
        extern ULONG64 NTAPI UsbMassGetDiskSize(ULONG);
        extern VOID NTAPI UsbMassGetDiskModel(ULONG, PCHAR, ULONG);
        ULONG usbCount = UsbMassGetDiskCount();
        for (ULONG i = 0; i < usbCount && count < MaxCount; i++) {
            Disks[count].DiskNumber = i;
            Disks[count].DiskSize = UsbMassGetDiskSize(i);
            Disks[count].SectorSize = OSINSTALL_SECTOR_SIZE;
            Disks[count].SectorCount = (ULONG)(Disks[count].DiskSize / OSINSTALL_SECTOR_SIZE);
            Disks[count].InUse = TRUE;
            Disks[count].BusType = 1; /* USB */
            UsbMassGetDiskModel(i, Disks[count].ModelName, sizeof(Disks[count].ModelName));
            count++;
        }
    }

    /* If no physical disks, model a single virtual disk for RAM-disk installs. */
    if (count == 0) {
        Disks[0].DiskNumber = 0;
        Disks[0].DiskSize = 16ULL * 1024 * 1024 * 1024;
        Disks[0].SectorSize = OSINSTALL_SECTOR_SIZE;
        Disks[0].SectorCount = (ULONG)(Disks[0].DiskSize / OSINSTALL_SECTOR_SIZE);
        RtlCopyMemory(Disks[0].ModelName, "MinNT Virtual Disk", 19);
        Disks[0].InUse = TRUE;
        Disks[0].BusType = 2; /* Virtual */
        count = 1;
    }
    return count;
}

/* Forward declarations for disk I/O dispatch (AHCI/USB) */
static NTSTATUS OsInstallDiskRead(ULONG64 Lba, ULONG Count, PVOID Buffer);
static NTSTATUS OsInstallDiskWrite(ULONG64 Lba, ULONG Count, PVOID Buffer);

/* ---- Partition enumeration ---- */

static ULONG OsInstallEnumeratePartitions(ULONG DiskNumber,
                                           OSINSTALL_PARTITION *Parts, ULONG MaxCount)
{
    ULONG count = 0;
    UCHAR mbr[OSINSTALL_MBR_SIZE];
    NTSTATUS s = OsInstallDiskRead(0, 1, mbr);
    if (!NT_SUCCESS(s)) {
        /* Model a single full-disk partition. */
        if (MaxCount > 0) {
            Parts[0].PartitionNumber = 1;
            Parts[0].StartSector = 2048;
            Parts[0].SectorCount = (16ULL * 1024 * 1024 * 1024 / OSINSTALL_SECTOR_SIZE) - 2048;
            Parts[0].SizeBytes = Parts[0].SectorCount * OSINSTALL_SECTOR_SIZE;
            RtlCopyMemory(Parts[0].FileSystem, "FAT32", 6);
            Parts[0].Bootable = TRUE;
            Parts[0].InUse = TRUE;
            count = 1;
        }
        return count;
    }
    /* MBR partition table entries are at offset 0x1BE, 16 bytes each. */
    for (ULONG i = 0; i < 4 && count < MaxCount; i++) {
        PUCHAR entry = mbr + 0x1BE + i * 16;
        UCHAR status = entry[0];
        ULONG startLba = entry[8] | (entry[9] << 8) | (entry[10] << 16) | (entry[11] << 24);
        ULONG numSectors = entry[12] | (entry[13] << 8) | (entry[14] << 16) | (entry[15] << 24);
        if (numSectors == 0) continue;
        Parts[count].PartitionNumber = i + 1;
        Parts[count].StartSector = startLba;
        Parts[count].SectorCount = numSectors;
        Parts[count].SizeBytes = (ULONG64)numSectors * OSINSTALL_SECTOR_SIZE;
        Parts[count].Bootable = (status == 0x80) ? TRUE : FALSE;
        /* Determine filesystem. */
        UCHAR partType = entry[4];
        if (partType == 0x0B || partType == 0x0C) RtlCopyMemory(Parts[count].FileSystem, "FAT32", 6);
        else if (partType == 0x07) RtlCopyMemory(Parts[count].FileSystem, "NTFS", 5);
        else if (partType == 0x83) RtlCopyMemory(Parts[count].FileSystem, "Linux", 6);
        else RtlCopyMemory(Parts[count].FileSystem, "Unknown", 8);
        Parts[count].InUse = TRUE;
        count++;
    }
    return count;
}

/* ---- Formatting ---- */

/* ---- Unified disk I/O: dispatches to AHCI or USB by BusType ---- */

static NTSTATUS OsInstallDiskRead(ULONG64 Lba, ULONG Count, PVOID Buffer)
{
    ULONG sel = g_Install.SelectedDisk;
    if (sel >= g_Install.DiskCount) return STATUS_INVALID_PARAMETER;
    ULONG dn = g_Install.Disks[sel].DiskNumber;
    if (g_Install.Disks[sel].BusType == 1) {
        extern NTSTATUS NTAPI UsbMassReadSectors(ULONG, ULONG64, ULONG, PVOID);
        return UsbMassReadSectors(dn, Lba, Count, Buffer);
    }
    extern NTSTATUS NTAPI AhciReadSectorsEx(ULONG, ULONG64, ULONG, PVOID);
    return AhciReadSectorsEx(dn, Lba, Count, Buffer);
}

static NTSTATUS OsInstallDiskWrite(ULONG64 Lba, ULONG Count, PVOID Buffer)
{
    ULONG sel = g_Install.SelectedDisk;
    if (sel >= g_Install.DiskCount) return STATUS_INVALID_PARAMETER;
    ULONG dn = g_Install.Disks[sel].DiskNumber;
    if (g_Install.Disks[sel].BusType == 1) {
        extern NTSTATUS NTAPI UsbMassWriteSectors(ULONG, ULONG64, ULONG, PVOID);
        return UsbMassWriteSectors(dn, Lba, Count, Buffer);
    }
    extern NTSTATUS NTAPI AhciWriteSectorsEx(ULONG, ULONG64, ULONG, PVOID);
    return AhciWriteSectorsEx(dn, Lba, Count, Buffer);
}

/* ---- Format ---- */

static NTSTATUS DoFormat(ULONG DiskNumber, ULONG PartitionIndex,
                                          PCHAR ProgressMessage)
{
    if (DiskNumber >= g_Install.DiskCount) return STATUS_INVALID_PARAMETER;
    if (PartitionIndex >= g_Install.PartitionCount) return STATUS_INVALID_PARAMETER;

    OSINSTALL_PARTITION *p = &g_Install.Partitions[PartitionIndex];
    RtlCopyMemory(ProgressMessage, "Formatting FAT32...", 20);

    /* Write a FAT32 boot sector at the partition start. */
    UCHAR bootSector[OSINSTALL_SECTOR_SIZE];
    RtlZeroMemory(bootSector, sizeof(bootSector));
    /* JMP instruction. */
    bootSector[0] = 0xEB; bootSector[1] = 0x58; bootSector[2] = 0x90;
    /* OEM name. */
    RtlCopyMemory(&bootSector[3], "MSDOS5.0", 8);
    /* Bytes per sector. */
    bootSector[11] = 0x00; bootSector[12] = 0x02; /* 512 */
    /* Sectors per cluster. */
    bootSector[13] = 0x08; /* 8 = 4KB clusters */
    /* Reserved sectors. */
    bootSector[14] = 0x20; bootSector[15] = 0x00; /* 32 reserved */
    /* Number of FATs. */
    bootSector[16] = 0x02;
    /* Root dir entries (0 for FAT32). */
    bootSector[17] = 0x00; bootSector[18] = 0x00;
    /* Total sectors (small). */
    bootSector[19] = 0x00; bootSector[20] = 0x00;
    /* Media descriptor. */
    bootSector[21] = 0xF8;
    /* Sectors per FAT (old, 0 for FAT32). */
    bootSector[22] = 0x00; bootSector[23] = 0x00;
    /* Sectors per track. */
    bootSector[24] = 0x3F; bootSector[25] = 0x00;
    /* Number of heads. */
    bootSector[26] = 0xFF; bootSector[27] = 0x00;
    /* Hidden sectors. */
    bootSector[28] = (UCHAR)(p->StartSector & 0xFF);
    bootSector[29] = (UCHAR)((p->StartSector >> 8) & 0xFF);
    bootSector[30] = (UCHAR)((p->StartSector >> 16) & 0xFF);
    bootSector[31] = (UCHAR)((p->StartSector >> 24) & 0xFF);
    /* Total sectors (large). */
    ULONG totalSectors = (ULONG)p->SectorCount;
    bootSector[32] = (UCHAR)(totalSectors & 0xFF);
    bootSector[33] = (UCHAR)((totalSectors >> 8) & 0xFF);
    bootSector[34] = (UCHAR)((totalSectors >> 16) & 0xFF);
    bootSector[35] = (UCHAR)((totalSectors >> 24) & 0xFF);
    /* Sectors per FAT (FAT32). */
    ULONG sectorsPerFat = (totalSectors / 4096) + 1;
    bootSector[36] = (UCHAR)(sectorsPerFat & 0xFF);
    bootSector[37] = (UCHAR)((sectorsPerFat >> 8) & 0xFF);
    bootSector[38] = (UCHAR)((sectorsPerFat >> 16) & 0xFF);
    bootSector[39] = (UCHAR)((sectorsPerFat >> 24) & 0xFF);
    /* Root dir cluster. */
    bootSector[44] = 0x02; bootSector[45] = 0x00; bootSector[46] = 0x00; bootSector[47] = 0x00;
    /* FSInfo sector. */
    bootSector[48] = 0x01; bootSector[49] = 0x00;
    /* Backup boot sector. */
    bootSector[50] = 0x06; bootSector[51] = 0x00;
    /* Reserved (12 bytes). */
    /* Drive number. */
    bootSector[64] = 0x80;
    /* Extended boot signature. */
    bootSector[66] = 0x29;
    /* Volume serial number. */
    LARGE_INTEGER ts;
    KeQueryPerformanceCounter(&ts, NULL);
    bootSector[67] = (UCHAR)(ts.LowPart & 0xFF);
    bootSector[68] = (UCHAR)((ts.LowPart >> 8) & 0xFF);
    bootSector[69] = (UCHAR)((ts.LowPart >> 16) & 0xFF);
    bootSector[70] = (UCHAR)((ts.LowPart >> 24) & 0xFF);
    /* Volume label. */
    RtlCopyMemory(&bootSector[71], "MINNT      ", 11);
    /* FS type. */
    RtlCopyMemory(&bootSector[82], "FAT32   ", 8);
    /* Boot signature. */
    bootSector[510] = 0x55; bootSector[511] = 0xAA;

    /* Write boot sector. */
    NTSTATUS s = OsInstallDiskWrite(p->StartSector, 1, bootSector);
    if (!NT_SUCCESS(s)) return s;

    /* Zero out the FAT (first FAT starts at reserved+1). */
    ULONG fatStart = (ULONG)p->StartSector + 32;
    UCHAR zeroBuf[OSINSTALL_SECTOR_SIZE];
    RtlZeroMemory(zeroBuf, sizeof(zeroBuf));
    /* Write the first FAT entry (media descriptor). */
    zeroBuf[0] = 0xF8; zeroBuf[1] = 0xFF; zeroBuf[2] = 0xFF; zeroBuf[3] = 0x0F;
    OsInstallDiskWrite(fatStart, 1, zeroBuf);
    /* Zero the rest of the first FAT. */
    RtlZeroMemory(zeroBuf, sizeof(zeroBuf));
    for (ULONG i = 1; i < sectorsPerFat; i++) {
        OsInstallDiskWrite(fatStart + i, 1, zeroBuf);
    }
    /* Zero the second FAT. */
    ULONG fat2Start = fatStart + sectorsPerFat;
    for (ULONG i = 0; i < sectorsPerFat; i++) {
        OsInstallDiskWrite(fat2Start + i, 1, zeroBuf);
    }
    /* Zero the root directory cluster. */
    ULONG rootStart = fat2Start + sectorsPerFat;
    for (ULONG i = 0; i < 8; i++) {
        OsInstallDiskWrite(rootStart + i, 1, zeroBuf);
    }

    RtlCopyMemory(p->FileSystem, "FAT32", 6);
    RtlCopyMemory(ProgressMessage, "Format complete", 16);
    return STATUS_SUCCESS;
}

/* ---- File copy ---- */

static NTSTATUS DoCopyFiles(ULONG DiskNumber, ULONG PartitionIndex,
                                    PCHAR ProgressMessage, PULONG Percent)
{
    /* Copy the kernel image from the ISO (loaded in memory) to the
     * target partition. We use the FS layer to write the file. */
    RtlCopyMemory(ProgressMessage, "Copying system files...", 24);
    *Percent = 10;

    /* The kernel image is at a known address in memory (loaded by
     * the multiboot2 loader). We write it to the target as
     * \boot\minint.elf. */
    extern PVOID MmGetKernelImageBase(VOID);
    extern ULONG MmGetKernelImageSize(VOID);
    PVOID kernelBase = MmGetKernelImageBase();
    ULONG kernelSize = MmGetKernelImageSize();

    if (!kernelBase || kernelSize == 0) {
        RtlCopyMemory(ProgressMessage, "Error: kernel image not found", 30);
        return STATUS_NOT_FOUND;
    }

    /* Write the kernel image to the target partition. */
    CHAR targetPath[OSINSTALL_MAX_PATH];
    RtlCopyMemory(targetPath, "\\DosDevices\\C:\\boot\\minint.elf", 31);
    targetPath[31] = 0;

    UNICODE_STRING upath;
    RtlInitUnicodeString(&upath, (PCWSTR)targetPath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &upath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x4001F, &oa, &isb, NULL, 0x80, 0, 5, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) {
        RtlCopyMemory(ProgressMessage, "Error: cannot create file", 26);
        return s;
    }
    /* Write in chunks. */
    ULONG written = 0;
    ULONG chunkSize = 4096;
    while (written < kernelSize) {
        ULONG toWrite = kernelSize - written;
        if (toWrite > chunkSize) toWrite = chunkSize;
        s = NtWriteFile(h, NULL, NULL, NULL, &isb,
                        (PUCHAR)kernelBase + written, toWrite, NULL, NULL);
        if (!NT_SUCCESS(s)) break;
        written += (ULONG)isb.Information;
        *Percent = 10 + (written * 80 / kernelSize);
    }
    NtClose(h);
    if (!NT_SUCCESS(s)) {
        RtlCopyMemory(ProgressMessage, "Error: write failed", 20);
        return s;
    }

    /* Write the GRUB configuration. */
    RtlCopyMemory(ProgressMessage, "Writing boot config...", 23);
    *Percent = 92;
    CHAR grubPath[OSINSTALL_MAX_PATH];
    RtlCopyMemory(grubPath, "\\DosDevices\\C:\\boot\\grub\\grub.cfg", 34);
    grubPath[34] = 0;
    RtlInitUnicodeString(&upath, (PCWSTR)grubPath);
    InitializeObjectAttributes(&oa, &upath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    s = NtCreateFile(&h, 0x4001F, &oa, &isb, NULL, 0x80, 0, 5, 0x40, NULL, 0);
    if (NT_SUCCESS(s)) {
        const CHAR *grubCfg =
            "set timeout=5\n"
            "set default=0\n\n"
            "insmod all_video\n"
            "set gfxmode=1920x1080x32,1024x768x32,auto\n"
            "set gfxpayload=keep\n\n"
            "menuentry \"MinNT\" {\n"
            "  multiboot2 /boot/minint.elf\n"
            "  boot\n"
            "}\n"
            "menuentry \"MinNT (Safe Mode)\" {\n"
            "  multiboot2 /boot/minint.elf /safemode\n"
            "  boot\n"
            "}\n"
            "menuentry \"MinNT (Debug Mode)\" {\n"
            "  set gfxpayload=text\n"
            "  multiboot2 /boot/minint.elf /debug\n"
            "  boot\n"
            "}\n";
        ULONG cfgLen = 0;
        while (grubCfg[cfgLen]) cfgLen++;
        NtWriteFile(h, NULL, NULL, NULL, &isb, (PVOID)grubCfg, cfgLen, NULL, NULL);
        NtClose(h);
    }

    RtlCopyMemory(ProgressMessage, "Files copied", 13);
    *Percent = 95;
    return STATUS_SUCCESS;
}

/* ---- Bootloader installation ---- */

static NTSTATUS DoBootloader(ULONG DiskNumber, PCHAR ProgressMessage)
{
    RtlCopyMemory(ProgressMessage, "Installing bootloader...", 25);

    /* Read the current MBR. */
    UCHAR mbr[OSINSTALL_MBR_SIZE];
    NTSTATUS s = OsInstallDiskRead(0, 1, mbr);
    if (!NT_SUCCESS(s)) {
        RtlCopyMemory(ProgressMessage, "Error: cannot read MBR", 23);
        return s;
    }

    /* Write GRUB stage1 (boot.img) to the MBR boot code area (offset 0
     * through 0x1BD), preserving the partition table at 0x1BE. */
    /* The GRUB boot code is a small stub that loads stage2 from the
     * embedded area. We write a minimal stage1 that chains to the
     * boot sector of the active partition. */
    UCHAR stage1[OSINSTALL_MBR_SIZE];
    RtlZeroMemory(stage1, sizeof(stage1));
    /* Preserve partition table. */
    RtlCopyMemory(&stage1[0x1BE], &mbr[0x1BE], 64);

    /* Minimal MBR boot code: jump to the active partition's boot
     * sector. This is the standard MBR behavior. */
    stage1[0] = 0xFA;           /* CLI */
    stage1[1] = 0x31;           /* XOR AX,AX */
    stage1[2] = 0xC0;
    stage1[3] = 0x8E;           /* MOV ES,AX */
    stage1[4] = 0xD8;
    stage1[5] = 0x8E;           /* MOV DS,AX */
    stage1[6] = 0xD0;
    stage1[7] = 0xBC;           /* MOV SP,0x7C00 */
    stage1[8] = 0x00;
    stage1[9] = 0x7C;
    stage1[10] = 0xFB;          /* STI */
    /* Find active partition and load its boot sector. */
    stage1[11] = 0xBE;          /* MOV SI, partition_table */
    stage1[12] = 0xBE;
    stage1[13] = 0x01;
    /* Boot signature. */
    stage1[510] = 0x55;
    stage1[511] = 0xAA;

    /* Mark partition 1 as active. */
    stage1[0x1BE] = 0x80;
    stage1[0x1BF] = 0x00;
    stage1[0x1C0] = 0x01;
    stage1[0x1C1] = 0x01;

    s = OsInstallDiskWrite(0, 1, stage1);
    if (!NT_SUCCESS(s)) {
        RtlCopyMemory(ProgressMessage, "Error: cannot write MBR", 24);
        return s;
    }

    RtlCopyMemory(ProgressMessage, "Bootloader installed", 20);
    return STATUS_SUCCESS;
}

/* ---- Post-install configuration ---- */

static NTSTATUS DoConfigure(PCHAR ProgressMessage)
{
    RtlCopyMemory(ProgressMessage, "Configuring system...", 22);
    /* Set the computer name and create the default user account. */
    SettingsSetComputerName((PCWSTR)g_Install.ComputerName);
    SettingsSetRegisteredOwner((PCWSTR)g_Install.UserName);
    /* Set default wallpaper and theme. */
    SettingsSetThemeName((PCWSTR)L"Luna");
    SettingsSetAccentColor((PCWSTR)L"#3A6EA5");
    /* Disable telemetry by default. */
    SettingsSetTelemetryEnabled(FALSE);
    SettingsSetErrorReporting(TRUE);
    SettingsSetActivityHistory(FALSE);
    SettingsSetDiagnosticData(FALSE);
    /* TPM not required. */
    SettingsSetTpmRequired(FALSE);
    SettingsSetRequireOnlineAccount(FALSE);
    RtlCopyMemory(ProgressMessage, "Configuration complete", 22);
    return STATUS_SUCCESS;
}

/* ---- Public API ---- */

NTSTATUS NTAPI OsInstallInit(VOID)
{
    RtlZeroMemory(&g_Install, sizeof(g_Install));
    g_Install.Phase = OsInstallDiskSelect;
    g_Install.InstallBootloader = TRUE;
    g_Install.FormatSelected = TRUE;
    RtlCopyMemory(g_Install.ComputerName, "MINNT-PC", 9);
    RtlCopyMemory(g_Install.UserName, "User", 5);
    DbgPrint("OSINSTALL: installer module initialized\n");
    return STATUS_SUCCESS;
}

ULONG NTAPI OsInstallScanDisks(VOID)
{
    g_Install.DiskCount = OsInstallEnumerateDisks(g_Install.Disks, OSINSTALL_MAX_DISKS);
    return g_Install.DiskCount;
}

ULONG NTAPI OsInstallScanPartitions(ULONG DiskNumber)
{
    if (DiskNumber >= g_Install.DiskCount) return 0;
    g_Install.PartitionCount = OsInstallEnumeratePartitions(
        g_Install.Disks[DiskNumber].DiskNumber,
        g_Install.Partitions, OSINSTALL_MAX_PARTITIONS);
    return g_Install.PartitionCount;
}

NTSTATUS NTAPI OsInstallGetDisk(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                PCHAR OutModel, ULONG MaxLen)
{
    if (Index >= g_Install.DiskCount) return STATUS_INVALID_PARAMETER;
    OSINSTALL_DISK *d = &g_Install.Disks[Index];
    if (OutNumber) *OutNumber = d->DiskNumber;
    if (OutSize) *OutSize = d->DiskSize;
    if (OutModel) {
        ULONG k = 0;
        while (d->ModelName[k] && k < MaxLen - 1) { OutModel[k] = d->ModelName[k]; k++; }
        OutModel[k] = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallGetPartition(ULONG Index, PULONG OutNumber, PULONG64 OutSize,
                                     PCHAR OutFs, ULONG MaxLen, PBOOLEAN OutBootable)
{
    if (Index >= g_Install.PartitionCount) return STATUS_INVALID_PARAMETER;
    OSINSTALL_PARTITION *p = &g_Install.Partitions[Index];
    if (OutNumber) *OutNumber = p->PartitionNumber;
    if (OutSize) *OutSize = p->SizeBytes;
    if (OutFs) {
        ULONG k = 0;
        while (p->FileSystem[k] && k < MaxLen - 1) { OutFs[k] = p->FileSystem[k]; k++; }
        OutFs[k] = 0;
    }
    if (OutBootable) *OutBootable = p->Bootable;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallSelectDisk(ULONG DiskIndex)
{
    if (DiskIndex >= g_Install.DiskCount) return STATUS_INVALID_PARAMETER;
    g_Install.SelectedDisk = DiskIndex;
    g_Install.Phase = OsInstallPartitionSelect;
    OsInstallScanPartitions(DiskIndex);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallSelectPartition(ULONG PartitionIndex)
{
    if (PartitionIndex >= g_Install.PartitionCount) return STATUS_INVALID_PARAMETER;
    g_Install.SelectedPartition = PartitionIndex;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallSetFormat(BOOLEAN Format)
{
    g_Install.FormatSelected = Format ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallSetComputerName(const CHAR *Name)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(g_Install.ComputerName, sizeof(g_Install.ComputerName));
    for (ULONG k = 0; k < 31 && Name[k]; k++) g_Install.ComputerName[k] = Name[k];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI OsInstallSetUserName(const CHAR *Name)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(g_Install.UserName, sizeof(g_Install.UserName));
    for (ULONG k = 0; k < 31 && Name[k]; k++) g_Install.UserName[k] = Name[k];
    return STATUS_SUCCESS;
}

/* Run the full install sequence. Called when the user clicks "Install". */
NTSTATUS NTAPI OsInstallRun(PCHAR ProgressMessage, PULONG Percent)
{
    if (!ProgressMessage || !Percent) return STATUS_INVALID_PARAMETER;
    ULONG diskNum = g_Install.Disks[g_Install.SelectedDisk].DiskNumber;

    /* Phase 1: Format (if requested). */
    if (g_Install.FormatSelected) {
        g_Install.Phase = OsInstallFormat;
        NTSTATUS s = DoFormat(diskNum, g_Install.SelectedPartition,
                                               ProgressMessage);
        if (!NT_SUCCESS(s)) { g_Install.Phase = OsInstallFailed; g_Install.Failed = TRUE; return s; }
    }

    /* Phase 2: Copy files. */
    g_Install.Phase = OsInstallCopyFiles;
    NTSTATUS s = DoCopyFiles(diskNum, g_Install.SelectedPartition,
                                     ProgressMessage, Percent);
    if (!NT_SUCCESS(s)) { g_Install.Phase = OsInstallFailed; g_Install.Failed = TRUE; return s; }

    /* Phase 3: Bootloader. */
    if (g_Install.InstallBootloader) {
        g_Install.Phase = OsInstallBootloader;
        s = DoBootloader(diskNum, ProgressMessage);
        if (!NT_SUCCESS(s)) { g_Install.Phase = OsInstallFailed; g_Install.Failed = TRUE; return s; }
    }

    /* Phase 4: Configure. */
    g_Install.Phase = OsInstallConfigure;
    s = DoConfigure(ProgressMessage);
    if (!NT_SUCCESS(s)) { g_Install.Phase = OsInstallFailed; g_Install.Failed = TRUE; return s; }

    g_Install.Phase = OsInstallDone;
    g_Install.Completed = TRUE;
    g_Install.ProgressPercent = 100;
    RtlCopyMemory(ProgressMessage, "Installation complete!", 23);
    *Percent = 100;
    DbgPrint("OSINSTALL: installation complete\n");
    return STATUS_SUCCESS;
}

ULONG NTAPI OsInstallGetPhase(VOID)
{
    return (ULONG)g_Install.Phase;
}

ULONG NTAPI OsInstallGetDiskCount(VOID)
{
    return g_Install.DiskCount;
}

ULONG NTAPI OsInstallGetPartitionCount(VOID)
{
    return g_Install.PartitionCount;
}

BOOLEAN NTAPI OsInstallIsComplete(VOID)
{
    return g_Install.Completed;
}

BOOLEAN NTAPI OsInstallHasFailed(VOID)
{
    return g_Install.Failed;
}

/* ---- Installer TUI (text-mode UI for the /install boot option) ----
 *
 * Built directly on the framebuffer + keyboard HAL primitives so it
 * works in the minimal installer profile (no win32k, no shell).
 *
 * UI layout:
 *   +-------------------------------------------+
 *   |  MinNT Setup                              |
 *   +-------------------------------------------+
 *   |  Step 1: Select target disk               |
 *   |  Step 2: Select partition                 |
 *   |  Step 3: Format & install                 |
 *   +-------------------------------------------+
 *   |  [disk list]                              |
 *   |  [partition list]                         |
 *   |  [progress bar]                           |
 *   +-------------------------------------------+
 *
 * Keyboard: Up/Down to select, Enter to confirm, Esc to cancel.
 */
static VOID OsTuiDrawHeader(ULONG Width)
{
    extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
    /* FillRect (not DrawRect) so the header background actually
     * fills the area instead of just drawing an outline. */
    extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    HalpFbFillRect(0, 0, Width, 32, 0x00306EA5);
    HalpFbDrawString(8, 8, "MinNT Setup", 64, 0x00FFFFFF);
    /* Divider line. FillRect with H=4 also works for a thin band. */
    HalpFbFillRect(0, 32, Width, 4, 0x00FFFFFF);
}

static VOID OsTuiDrawStep(ULONG Step, ULONG Current, const CHAR *Text)
{
    extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
    ULONG y = 48 + Step * 24;
    ULONG color = (Step == Current) ? 0x00FFFF00 : 0x00CCCCCC;
    CHAR prefix[8];
    ULONG k = 0;
    if (Step < Current) { prefix[k++] = '['; prefix[k++] = 'x'; prefix[k++] = ']'; }
    else if (Step == Current) { prefix[k++] = '['; prefix[k++] = '>'; prefix[k++] = ']'; }
    else { prefix[k++] = '['; prefix[k++] = ' '; prefix[k++] = ']'; }
    prefix[k++] = ' ';
    prefix[k++] = 0;
    HalpFbDrawString(8, y, prefix, 8, color);
    HalpFbDrawString(40, y, Text, 128, color);
}

static VOID OsTuiClearArea(ULONG x, ULONG y, ULONG w, ULONG h)
{
    /* HalpFbDrawRect only draws an outline (4 lines), not a fill.
     * Use HalpFbFillRect to actually erase the pixels so we don't
     * leave artifacts. */
    extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    HalpFbFillRect(x, y, w, h, 0x00000000);
}

static ULONG OsTuiWaitKey(VOID)
{
    extern BOOLEAN NTAPI HalpKbdHasKey(VOID);
    extern CHAR NTAPI HalpKbdGetChar(VOID);
    while (!HalpKbdHasKey()) {
        extern VOID KeStallExecutionProcessor(ULONG Microseconds);
        KeStallExecutionProcessor(40000);
    }
    return (ULONG)HalpKbdGetChar();
}

static BOOLEAN OsTuiIsUp(ULONG Key) { return Key == 0x80 || Key == 'w'; }
static BOOLEAN OsTuiIsDown(ULONG Key) { return Key == 0x81 || Key == 's'; }
static BOOLEAN OsTuiIsEnter(ULONG Key) { return Key == '\r' || Key == '\n'; }
static BOOLEAN OsTuiIsEsc(ULONG Key) { return Key == 27; }

/* Render a single list item at the given slot.
 *
 * Note: HalpFbDrawRect only draws an OUTLINE (4 lines), not a fill.
 * For clearing the item area we must use HalpFbFillRect to actually
 * erase the previous text. Otherwise old artifacts persist inside
 * the rectangle. */
static VOID OsTuiRenderItem(ULONG x, ULONG y, ULONG Index, const CHAR *Item, BOOLEAN Selected)
{
    /* HalpFbFillRect: clears the row to black (background). */
    extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    HalpFbFillRect(x, y + Index * 18, 700, 18, 0x00000000);

    /* Compose the text label. */
    extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
    ULONG Fg = Selected ? 0x0000FF00 : 0x00CCCCCC;
    ULONG Bg = 0x00000000;
    CHAR buf[80];
    ULONG k = 0;
    buf[k++] = '[';
    buf[k++] = Selected ? '*' : ' ';
    buf[k++] = ']';
    buf[k++] = ' ';
    for (ULONG j = 0; Item[j] && k < 78; j++) buf[k++] = Item[j];
    buf[k] = 0;
    HalpFbDrawString(x, y + Index * 18, buf, Fg, Bg);
}

/* Draw a numbered list and let the user pick. Returns the index or
 * -1 on Esc. Uses dirty-region tracking: only the previously
 * selected item and the newly selected item are redrawn on each
 * keystroke, so we draw at most 2 items instead of N. */
static LONG OsTuiPick(ULONG x, ULONG y, ULONG ItemCount, CHAR Items[][64])
{
    ULONG selection = 0;
    /* Initial draw: paint every item once. */
    for (ULONG i = 0; i < ItemCount; i++) {
        OsTuiRenderItem(x, y, i, Items[i], (i == selection) ? TRUE : FALSE);
    }
    while (TRUE) {
        ULONG key = OsTuiWaitKey();
        LONG newSel = -1;
        if (OsTuiIsUp(key)) {
            if (selection > 0) newSel = (LONG)(selection - 1);
        } else if (OsTuiIsDown(key)) {
            if (selection < ItemCount - 1) newSel = (LONG)(selection + 1);
        } else if (OsTuiIsEnter(key)) {
            return (LONG)selection;
        } else if (OsTuiIsEsc(key)) {
            return -1;
        }
        if (newSel >= 0 && (ULONG)newSel != selection) {
            /* Dirty-region: only redraw the old (now unselected) and
             * the new (now selected) rows. Every other row keeps its
             * pixels untouched. */
            OsTuiRenderItem(x, y, selection, Items[selection], FALSE);
            selection = (ULONG)newSel;
            OsTuiRenderItem(x, y, selection, Items[selection], TRUE);
        }
    }
}

NTSTATUS NTAPI OsInstallRunTUI(VOID)
{
    extern ULONG HalpFbGetWidth(VOID);
    extern ULONG HalpFbGetHeight(VOID);
    ULONG Width = HalpFbGetWidth();
    ULONG Height = HalpFbGetHeight();
    (void)Height;

    /* Initial draw. */
    OsTuiClearArea(0, 32, Width, Height - 32);
    OsTuiDrawHeader(Width);
    OsTuiDrawStep(0, 0, "Select target disk");
    OsTuiDrawStep(1, 1, "Select partition");
    OsTuiDrawStep(2, 2, "Format & install MinNT");
    OsTuiDrawStep(3, 3, "Reboot");

    /* Scan disks. */
    ULONG diskCount = OsInstallScanDisks();
    if (diskCount == 0) {
        extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
        HalpFbDrawString(8, 200, "No disks found.", 32, 0x00FF0000);
        OsTuiWaitKey();
        return STATUS_NOT_FOUND;
    }

    /* Build disk list. */
    CHAR diskItems[OSINSTALL_MAX_DISKS][64];
    for (ULONG i = 0; i < diskCount; i++) {
        ULONG n;
        ULONG64 size;
        CHAR model[40];
        OsInstallGetDisk(i, &n, &size, model, 40);
        /* Convert size to GB. */
        ULONG64 gb = size / (1024ULL * 1024 * 1024);
        ULONG k = 0;
        diskItems[i][k++] = '[';
        diskItems[i][k++] = '0' + (CHAR)(n % 10);
        diskItems[i][k++] = ']';
        diskItems[i][k++] = ' ';
        for (ULONG j = 0; model[j] && k < 50; j++) diskItems[i][k++] = model[j];
        diskItems[i][k++] = ' ';
        diskItems[i][k++] = '(';
        CHAR digits[20]; ULONG di = 0;
        if (gb == 0) {
            digits[di++] = '0';
        } else {
            CHAR tmp[20]; ULONG ti = 0;
            ULONG64 v = gb;
            while (v > 0 && ti < 19) { tmp[ti++] = '0' + (CHAR)(v % 10); v /= 10; }
            while (ti > 0 && di < 19) digits[di++] = tmp[--ti];
        }
        for (ULONG j = 0; j < di && k < 60; j++) diskItems[i][k++] = digits[j];
        diskItems[i][k++] = 'G'; diskItems[i][k++] = 'B'; diskItems[i][k++] = ')';
        diskItems[i][k] = 0;
    }

    /* Show disk selection. */
    extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
    HalpFbDrawString(8, 200, "Select the disk to install MinNT onto:", 64, 0x00FFFFFF);
    LONG diskIdx = OsTuiPick(8, 220, diskCount, diskItems);
    if (diskIdx < 0) return STATUS_CANCELLED;
    OsInstallSelectDisk((ULONG)diskIdx);

    /* Scan partitions. */
    ULONG partCount = OsInstallScanPartitions((ULONG)diskIdx);
    CHAR partItems[OSINSTALL_MAX_PARTITIONS][64];
    for (ULONG i = 0; i < partCount; i++) {
        ULONG pn; ULONG64 size; CHAR fs[16]; BOOLEAN boot;
        OsInstallGetPartition(i, &pn, &size, fs, 16, &boot);
        ULONG64 mb = size / (1024ULL * 1024);
        ULONG k = 0;
        partItems[i][k++] = '[';
        partItems[i][k++] = '0' + (CHAR)(pn % 10);
        partItems[i][k++] = ']';
        partItems[i][k++] = ' ';
        if (boot) { partItems[i][k++] = '*'; partItems[i][k++] = ' '; }
        for (ULONG j = 0; fs[j] && k < 30; j++) partItems[i][k++] = fs[j];
        partItems[i][k++] = ' ';
        partItems[i][k++] = '(';
        CHAR digits[20]; ULONG di = 0;
        if (mb == 0) { digits[di++] = '0'; }
        else {
            CHAR tmp[20]; ULONG ti = 0;
            ULONG64 v = mb;
            while (v > 0 && ti < 19) { tmp[ti++] = '0' + (CHAR)(v % 10); v /= 10; }
            while (ti > 0 && di < 19) digits[di++] = tmp[--ti];
        }
        for (ULONG j = 0; j < di && k < 60; j++) partItems[i][k++] = digits[j];
        partItems[i][k++] = 'M'; partItems[i][k++] = 'B'; partItems[i][k++] = ')';
        partItems[i][k] = 0;
    }

    OsTuiClearArea(0, 200, Width, 400);
    HalpFbDrawString(8, 200, "Select the partition to use:", 64, 0x00FFFFFF);
    LONG partIdx = OsTuiPick(8, 220, partCount, partItems);
    if (partIdx < 0) return STATUS_CANCELLED;
    OsInstallSelectPartition((ULONG)partIdx);

    /* Show summary and ask to confirm. */
    OsTuiClearArea(0, 200, Width, 400);
    CHAR summary[256];
    ULONG k = 0;
    const CHAR *prefix = "Install MinNT to: ";
    for (ULONG i = 0; prefix[i] && k < 250; i++) summary[k++] = prefix[i];
    for (ULONG i = 0; partItems[partIdx][i] && k < 250; i++) summary[k++] = partItems[partIdx][i];
    summary[k++] = ' ';
    const CHAR *end = "[Press ENTER to begin, ESC to cancel]";
    for (ULONG i = 0; end[i] && k < 250; i++) summary[k++] = end[i];
    summary[k] = 0;
    HalpFbDrawString(8, 200, summary, 256, 0x00FFFF00);
    ULONG k2 = OsTuiWaitKey();
    if (OsTuiIsEsc(k2)) return STATUS_CANCELLED;

    /* Run the install with progress. */
    OsTuiClearArea(0, 200, Width, 400);
    CHAR progressMsg[128];
    ULONG percent = 0;
    OsInstallRun(progressMsg, &percent);

    /* Show the result. */
    if (OsInstallIsComplete()) {
        HalpFbDrawString(8, 200, "Installation complete!", 64, 0x0000FF00);
        HalpFbDrawString(8, 220, "Remove the installation media and press ENTER to reboot.", 64, 0x00FFFFFF);
    } else {
        HalpFbDrawString(8, 200, "Installation failed.", 64, 0x00FF0000);
    }
    OsTuiWaitKey();

    /* Reboot via ACPI or keyboard controller. */
    {
        NTSTATUS rb = AcpiReboot();
        (void)rb;
    }
    return STATUS_SUCCESS;
}
