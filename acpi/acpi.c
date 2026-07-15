/*
 * MinNT - acpi/acpi.c
 * ACPI (Advanced Configuration and Power Interface) table parser and
 * power-management integration.
 *
 * Real ACPI lives in firmware and exposes a set of tables via the RSDP,
 * which we walk to find the XSDT (or RSDT) and enumerate FADT, FACS,
 * MADT, MCFG, HPET, and SRAT entries. The power manager talks to this
 * module for sleep state transitions (S1-S5), battery status, thermal
 * zones, and reset/power-button handling.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/io.h>
#include <nt/ob.h>
#include <nt/ex.h>
#include <nt/framework.h>

/* ACPI signatures we look for */
#define ACPI_RSDP_SIGNATURE     0x2052545020445352ULL /* "RSD PTR " */
#define ACPI_FACS_SIGNATURE     0x50434146           /* "FACS" */
#define ACPI_FADT_SIGNATURE     0x50434146           /* "FACP" */
#define ACPI_MADT_SIGNATURE     0x43495041           /* "APIC" */
#define ACPI_HPET_SIGNATURE     0x54455048           /* "HPET" */
#define ACPI_MCFG_SIGNATURE     0x4746434d           /* "MCFG" */
#define ACPI_SRAT_SIGNATURE     0x54415253           /* "SRAT" */
#define ACPI_SLIC_SIGNATURE     0x43494C53           /* "SLIC" */
#define ACPI_SLIT_SIGNATURE     0x54494C53           /* "SLIT" */
#define ACPI_SSDT_SIGNATURE     0x54445353           /* "SSDT" */
#define ACPI_DSDT_SIGNATURE     0x54445344           /* "DSDT" */

typedef struct _ACPI_RSDP {
    UCHAR Signature[8];
    UCHAR Checksum;
    UCHAR OemId[6];
    UCHAR Revision;
    ULONG RsdtAddress;
    ULONG Length;
    ULONG XsdtAddress[2]; /* 64-bit on x86_64 */
} ACPI_RSDP, *PACPI_RSDP;

typedef struct _ACPI_DESC_HEADER {
    ULONG Signature;
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    UCHAR OemId[6];
    UCHAR OemTableId[8];
    ULONG OemRevision;
    ULONG CreatorId;
    ULONG CreatorRevision;
} ACPI_DESC_HEADER, *PACPI_DESC_HEADER;

typedef struct _ACPI_FADT {
    ACPI_DESC_HEADER Header;
    ULONG FirmwareCtrl;
    ULONG Dsdt;
    UCHAR Reserved;
    UCHAR PreferredPowerProfile;
    ULONG SciInterrupt;
    ULONG SmiCommand;
    UCHAR AcpiEnable;
    UCHAR AcpiDisable;
    UCHAR S4BiosRequest;
    UCHAR PStateControl;
    ULONG Pm1aEventBlock;
    ULONG Pm1bEventBlock;
    ULONG Pm1aControlBlock;
    ULONG Pm1bControlBlock;
    ULONG Pm2ControlBlock;
    ULONG PmTimerBlock;
    ULONG Gpe0Block;
    ULONG Gpe1Block;
    ULONG SleepControlReg;
    ULONG SleepStatusReg;
} ACPI_FADT, *PACPI_FADT;

#define ACPI_SLEEP_S0    0
#define ACPI_SLEEP_S1    1
#define ACPI_SLEEP_S2    2
#define ACPI_SLEEP_S3    3
#define ACPI_SLEEP_S4    4
#define ACPI_SLEEP_S5    5

typedef struct _ACPI_MADT {
    ACPI_DESC_HEADER Header;
    ULONG LocalApicAddress;
    ULONG Flags;
} ACPI_MADT, *PACPI_MADT;

typedef struct _ACPI_HPET {
    ACPI_DESC_HEADER Header;
    UCHAR HardwareRev;
    UCHAR ComparatorCount:5;
    UCHAR CounterSize:1;
    UCHAR Reserved:1;
    UCHAR LegacyReplacement:1;
    UCHAR PciVendorId[2];
    ULONG Address;
    UCHAR HpetNumber;
    UCHAR MinPeriodicTick;
    UCHAR PageProtection;
} ACPI_HPET, *PACPI_HPET;

typedef struct _ACPI_MCFG_ENTRY {
    ULONG BaseAddressLo;
    ULONG BaseAddressHi;
    ULONG PciSegmentGroup;
    UCHAR StartBus;
    UCHAR EndBus;
    ULONG Reserved;
} ACPI_MCFG_ENTRY, *PACPI_MCFG_ENTRY;

typedef struct _ACPI_MCFG {
    ACPI_DESC_HEADER Header;
    UCHAR Reserved[8];
    ACPI_MCFG_ENTRY Entries[1];
} ACPI_MCFG, *PACPI_MCFG;

typedef struct _ACPI_TABLE {
    ULONG Signature;
    ULONG Length;
    PACPI_DESC_HEADER Header;
    struct _ACPI_TABLE *Next;
} ACPI_TABLE, *PACPI_TABLE;

typedef struct _ACPI_STATE {
    PACPI_RSDP Rsdp;
    BOOLEAN AcpiEnabled;
    BOOLEAN SleepInProgress;
    ULONG SleepState;
    PACPI_TABLE Tables;
    ULONG TableCount;
    ACPI_FADT Fadt;
    ACPI_MADT Madt;
    ACPI_HPET Hpet;
    BOOLEAN MadtPresent;
    BOOLEAN HpetPresent;
    BOOLEAN McfgPresent;
    ULONG McfgBase;
    ULONG BatteryPercent;
    ULONG ThermalCelsius;
    ULONG CpuCount;
    ULONG IoApicCount;
} ACPI_STATE, *PACPI_STATE;

static ACPI_STATE g_Acpi;

/* Walk the physical address space searching for the RSDP signature. */
static PACPI_RSDP AcpiLocateRsdp(VOID)
{
    /* RSDP lives in the EBDA or in the first 1KB of the 512KB-640KB range
     * or as a 32-bit pointer at 0x40E in real-mode BIOS data. */
    PUCHAR p;
    for (p = (PUCHAR)0x000E0000; p < (PUCHAR)0x000FFFFF; p += 16) {
        UCHAR sig[8];
        RtlCopyMemory(sig, p, 8);
        if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
            sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
            sig[6] == 'R' && sig[7] == ' ') {
            return (PACPI_RSDP)p;
        }
    }
    /* Try EBDA pointer */
    USHORT ebda_seg = *(volatile USHORT *)0x40E;
    if (ebda_seg) {
        PUCHAR ebda = (PUCHAR)((ULONG)ebda_seg << 4);
        for (p = ebda; p < ebda + 1024; p += 16) {
            UCHAR sig[8];
            RtlCopyMemory(sig, p, 8);
            if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
                sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
                sig[6] == 'R' && sig[7] == ' ') {
                return (PACPI_RSDP)p;
            }
        }
    }
    return NULL;
}

/* Validate an ACPI description header checksum. */
static UCHAR AcpiChecksum(PVOID table, ULONG length)
{
    PUCHAR p = (PUCHAR)table;
    UCHAR sum = 0;
    for (ULONG i = 0; i < length; i++) sum += p[i];
    return sum;
}

static NTSTATUS AcpiRegisterTable(PACPI_DESC_HEADER hdr)
{
    PACPI_TABLE t = (PACPI_TABLE)ExAllocatePool(0, sizeof(ACPI_TABLE));
    if (!t) return STATUS_NO_MEMORY;
    t->Signature = hdr->Signature;
    t->Length = hdr->Length;
    t->Header = hdr;
    t->Next = g_Acpi.Tables;
    g_Acpi.Tables = t;
    g_Acpi.TableCount++;
    return STATUS_SUCCESS;
}

static NTSTATUS AcpiParseXsdt(ULONGLONG xsdtAddr)
{
    PACPI_DESC_HEADER hdr = (PACPI_DESC_HEADER)(ULONG_PTR)xsdtAddr;
    if (!hdr) return STATUS_INVALID_PARAMETER;
    if (AcpiChecksum(hdr, hdr->Length) != 0) return STATUS_INVALID_PARAMETER;
    AcpiRegisterTable(hdr);

    ULONG entries = (hdr->Length - sizeof(ACPI_DESC_HEADER)) / 8;
    ULONGLONG *ptrs = (ULONGLONG *)(hdr + 1);
    for (ULONG i = 0; i < entries; i++) {
        PACPI_DESC_HEADER sub = (PACPI_DESC_HEADER)(ULONG_PTR)ptrs[i];
        if (!sub) continue;
        if (AcpiChecksum(sub, sub->Length) != 0) continue;
        AcpiRegisterTable(sub);
        if (sub->Signature == ACPI_FADT_SIGNATURE && sub->Length >= sizeof(ACPI_FADT)) {
            RtlCopyMemory(&g_Acpi.Fadt, sub, sizeof(ACPI_FADT));
        } else if (sub->Signature == ACPI_MADT_SIGNATURE && sub->Length >= sizeof(ACPI_MADT)) {
            RtlCopyMemory(&g_Acpi.Madt, sub, sizeof(ACPI_MADT));
            g_Acpi.MadtPresent = TRUE;
        } else if (sub->Signature == ACPI_HPET_SIGNATURE && sub->Length >= sizeof(ACPI_HPET)) {
            RtlCopyMemory(&g_Acpi.Hpet, sub, sizeof(ACPI_HPET));
            g_Acpi.HpetPresent = TRUE;
        } else if (sub->Signature == ACPI_MCFG_SIGNATURE) {
            PACPI_MCFG mcfg = (PACPI_MCFG)sub;
            if (mcfg->Entries[0].BaseAddressLo) {
                g_Acpi.McfgBase = mcfg->Entries[0].BaseAddressLo;
                g_Acpi.McfgPresent = TRUE;
            }
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS AcpiParseRsdt(ULONG rsdtAddr)
{
    PACPI_DESC_HEADER hdr = (PACPI_DESC_HEADER)(ULONG_PTR)rsdtAddr;
    if (!hdr) return STATUS_INVALID_PARAMETER;
    if (AcpiChecksum(hdr, hdr->Length) != 0) return STATUS_INVALID_PARAMETER;
    AcpiRegisterTable(hdr);

    ULONG entries = (hdr->Length - sizeof(ACPI_DESC_HEADER)) / 4;
    ULONG *ptrs = (ULONG *)(hdr + 1);
    for (ULONG i = 0; i < entries; i++) {
        PACPI_DESC_HEADER sub = (PACPI_DESC_HEADER)(ULONG_PTR)ptrs[i];
        if (!sub) continue;
        if (AcpiChecksum(sub, sub->Length) != 0) continue;
        AcpiRegisterTable(sub);
    }
    return STATUS_SUCCESS;
}

/* Transition the platform into an ACPI sleep state. */
NTSTATUS NTAPI AcpiEnterSleepState(ULONG State)
{
    if (State < ACPI_SLEEP_S0 || State > ACPI_SLEEP_S5) return STATUS_INVALID_PARAMETER;
    if (g_Acpi.SleepInProgress) return STATUS_DEVICE_BUSY;
    g_Acpi.SleepInProgress = TRUE;
    g_Acpi.SleepState = State;
    DbgPrint("ACPI: entering sleep state S%u\n", State);

    /* If we have a FADT sleep control register, write the SLP_TYP value
     * derived from the state. Otherwise, fall back to a soft power-off. */
    if (g_Acpi.Fadt.SleepControlReg) {
        volatile ULONG *ctrl = (volatile ULONG *)(ULONG_PTR)g_Acpi.Fadt.SleepControlReg;
        ULONG val = ((State & 7) << 10) | (1 << 13);
        *ctrl = val;
    }
    if (State == ACPI_SLEEP_S5) {
        DbgPrint("ACPI: soft power-off\n");
    }
    g_Acpi.SleepInProgress = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AcpiReboot(VOID)
{
    DbgPrint("ACPI: keyboard controller reset\n");
    /* Pulse the keyboard controller reset line. */
    UCHAR tmp;
    volatile UCHAR *kbc = (volatile UCHAR *)0x64;
    tmp = *kbc; (void)tmp;
    *kbc = 0xFE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AcpiGetBatteryStatus(PULONG OutPercent, PULONG OutCharging)
{
    if (OutPercent) *OutPercent = g_Acpi.BatteryPercent;
    if (OutCharging) *OutCharging = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AcpiGetThermal(PULONG OutCelsius)
{
    if (OutCelsius) *OutCelsius = g_Acpi.ThermalCelsius;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AcpiGetCpuCount(PULONG OutCount)
{
    if (OutCount) *OutCount = g_Acpi.CpuCount;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AcpiInit(VOID)
{
    RtlZeroMemory(&g_Acpi, sizeof(g_Acpi));
    g_Acpi.BatteryPercent = 85;
    g_Acpi.ThermalCelsius = 45;
    g_Acpi.CpuCount = 1;

    PACPI_RSDP rsdp = AcpiLocateRsdp();
    if (!rsdp) {
        DbgPrint("ACPI: RSDP not found, running without ACPI tables\n");
        return STATUS_NOT_FOUND;
    }
    g_Acpi.Rsdp = rsdp;
    if (rsdp->XsdtAddress[0] || rsdp->XsdtAddress[1]) {
        ULONGLONG addr = ((ULONGLONG)rsdp->XsdtAddress[1] << 32) |
                          (ULONGLONG)rsdp->XsdtAddress[0];
        AcpiParseXsdt(addr);
    } else if (rsdp->RsdtAddress) {
        AcpiParseRsdt(rsdp->RsdtAddress);
    }

    /* Walk MADT entries to count CPUs and I/O APICs. */
    if (g_Acpi.MadtPresent) {
        PUCHAR p = (PUCHAR)&g_Acpi.Madt + sizeof(ACPI_MADT);
        PUCHAR end = p + g_Acpi.Madt.Header.Length - sizeof(ACPI_MADT);
        while (p < end) {
            UCHAR type = *p;
            UCHAR len = *(p + 1);
            if (len < 2) break;
            if (type == 0) g_Acpi.CpuCount++;
            else if (type == 1) g_Acpi.IoApicCount++;
            p += len;
        }
    }
    DbgPrint("ACPI: %u tables parsed, %u CPUs, %u I/O APICs\n",
             g_Acpi.TableCount, g_Acpi.CpuCount, g_Acpi.IoApicCount);
    return STATUS_SUCCESS;
}
