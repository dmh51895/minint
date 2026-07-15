/*
 * MinNT - boot/bootcfg.c
 * Boot Configuration editor.
 *
 * Manages the list of boot entries (each pointing to an OS image),
 * the default entry, and the boot menu timeout. Persisted in the
 * registry under HKLM\System\CurrentControlSet\Control\BootEntries.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/cm.h>

#define MAX_BOOT_ENTRIES 16

typedef struct _BOOT_ENTRY {
    ULONG Id;
    WCHAR FriendlyName[64];
    WCHAR OsLoaderPath[260];
    ULONG TimeoutSecs;            /* display menu timeout */
    BOOLEAN IsDefault;
    BOOLEAN IsCurrent;
    BOOLEAN InUse;
} BOOT_ENTRY, *PBOOT_ENTRY;

static BOOT_ENTRY g_BootEntries[MAX_BOOT_ENTRIES];
static KSPIN_LOCK g_BootLock;
static ULONG g_BootDefaultId = 0;
static ULONG g_BootTimeoutSecs = 30;

NTSTATUS NTAPI BootCfgInit(VOID)
{
    RtlZeroMemory(g_BootEntries, sizeof(g_BootEntries));
    KeInitializeSpinLock(&g_BootLock);

    /* Register the default boot entry for MinNT */
    {
        BOOT_ENTRY *e = &g_BootEntries[0];
        e->Id = 0;
        RtlCopyMemory(e->FriendlyName, L"MinNT 6.1", 17);
        e->FriendlyName[16] = 0;
        RtlCopyMemory(e->OsLoaderPath, L"\\EFI\\MinNT\\bootmgfw.efi", 47);
        e->OsLoaderPath[46] = 0;
        e->TimeoutSecs = 30;
        e->IsDefault = TRUE;
        e->IsCurrent = TRUE;
        e->InUse = TRUE;
        g_BootDefaultId = 0;
    }

    /* Add a Safe Mode entry */
    {
        BOOT_ENTRY *e = &g_BootEntries[1];
        e->Id = 1;
        RtlCopyMemory(e->FriendlyName, L"MinNT Safe Mode", 31);
        e->FriendlyName[30] = 0;
        RtlCopyMemory(e->OsLoaderPath, L"\\EFI\\MinNT\\bootmgfw.efi /safemode", 71);
        e->OsLoaderPath[70] = 0;
        e->TimeoutSecs = 30;
        e->IsDefault = FALSE;
        e->IsCurrent = FALSE;
        e->InUse = TRUE;
    }

    /* Add a Recovery entry */
    {
        BOOT_ENTRY *e = &g_BootEntries[2];
        e->Id = 2;
        RtlCopyMemory(e->FriendlyName, L"MinNT Recovery", 30);
        e->FriendlyName[29] = 0;
        RtlCopyMemory(e->OsLoaderPath, L"\\EFI\\MinNT\\bootmgfw.efi /recovery", 72);
        e->OsLoaderPath[71] = 0;
        e->TimeoutSecs = 10;
        e->IsDefault = FALSE;
        e->IsCurrent = FALSE;
        e->InUse = TRUE;
    }

    DbgPrint("BOOTCFG: %d boot entries initialized\n", 3);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI BootCfgAddEntry(const WCHAR *FriendlyName,
                                const WCHAR *OsLoaderPath,
                                ULONG TimeoutSecs)
{
    ULONG i;
    KIRQL irql;
    if (!FriendlyName || !OsLoaderPath) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_BootLock, &irql);
    for (i = 0; i < MAX_BOOT_ENTRIES; i++) {
        if (!g_BootEntries[i].InUse) {
            RtlZeroMemory(&g_BootEntries[i], sizeof(BOOT_ENTRY));
            g_BootEntries[i].Id = i;
            {
                ULONG j = 0;
                while (FriendlyName[j] && j < 63) g_BootEntries[i].FriendlyName[j] = FriendlyName[j], j++;
                g_BootEntries[i].FriendlyName[j] = 0;
            }
            {
                ULONG j = 0;
                while (OsLoaderPath[j] && j < 259) g_BootEntries[i].OsLoaderPath[j] = OsLoaderPath[j], j++;
                g_BootEntries[i].OsLoaderPath[j] = 0;
            }
            g_BootEntries[i].TimeoutSecs = TimeoutSecs;
            g_BootEntries[i].InUse = TRUE;
            KeReleaseSpinLock(&g_BootLock, &irql);
            DbgPrint("BOOTCFG: added entry '%ws'\n", FriendlyName);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_BootLock, &irql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI BootCfgRemoveEntry(ULONG Id)
{
    KIRQL irql;
    if (Id >= MAX_BOOT_ENTRIES || !g_BootEntries[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_BootLock, &irql);
    RtlZeroMemory(&g_BootEntries[Id], sizeof(BOOT_ENTRY));
    KeReleaseSpinLock(&g_BootLock, &irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI BootCfgSetDefault(ULONG Id)
{
    ULONG i;
    KIRQL irql;
    if (Id >= MAX_BOOT_ENTRIES || !g_BootEntries[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_BootLock, &irql);
    for (i = 0; i < MAX_BOOT_ENTRIES; i++) {
        if (g_BootEntries[i].InUse) {
            g_BootEntries[i].IsDefault = (i == Id);
        }
    }
    g_BootDefaultId = Id;
    KeReleaseSpinLock(&g_BootLock, &irql);
    DbgPrint("BOOTCFG: default entry set to %u\n", Id);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI BootCfgSetTimeout(ULONG TimeoutSecs)
{
    KIRQL irql;
    if (TimeoutSecs > 300) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_BootLock, &irql);
    g_BootTimeoutSecs = TimeoutSecs;
    KeReleaseSpinLock(&g_BootLock, &irql);
    return STATUS_SUCCESS;
}

ULONG NTAPI BootCfgGetDefault(VOID)
{
    ULONG id;
    KIRQL irql;
    KeAcquireSpinLock(&g_BootLock, &irql);
    id = g_BootDefaultId;
    KeReleaseSpinLock(&g_BootLock, &irql);
    return id;
}

ULONG NTAPI BootCfgGetTimeout(VOID)
{
    ULONG t;
    KIRQL irql;
    KeAcquireSpinLock(&g_BootLock, &irql);
    t = g_BootTimeoutSecs;
    KeReleaseSpinLock(&g_BootLock, &irql);
    return t;
}

ULONG NTAPI BootCfgEnumEntries(ULONG MaxCount, ULONG *pIds, PCHAR *pNames,
                                PBOOLEAN pIsDefault, PULONG pTimeouts)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_BootLock, &irql);
    for (i = 0; i < MAX_BOOT_ENTRIES && n < MaxCount; i++) {
        if (g_BootEntries[i].InUse) {
            if (pIds) pIds[n] = g_BootEntries[i].Id;
            if (pNames) {
                ULONG j = 0;
                while (g_BootEntries[i].FriendlyName[j] && j < 63) pNames[n][j] = (CHAR)g_BootEntries[i].FriendlyName[j], j++;
                pNames[n][j] = 0;
            }
            if (pIsDefault) pIsDefault[n] = g_BootEntries[i].IsDefault;
            if (pTimeouts) pTimeouts[n] = g_BootEntries[i].TimeoutSecs;
            n++;
        }
    }
    KeReleaseSpinLock(&g_BootLock, &irql);
    return n;
}

/* Get entry details by ID. */
NTSTATUS NTAPI BootCfgGetEntry(ULONG Id, ULONG *pTimeout, PBOOLEAN pIsDefault)
{
    KIRQL irql;
    if (Id >= MAX_BOOT_ENTRIES || !g_BootEntries[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_BootLock, &irql);
    if (pTimeout) *pTimeout = g_BootEntries[Id].TimeoutSecs;
    if (pIsDefault) *pIsDefault = g_BootEntries[Id].IsDefault;
    KeReleaseSpinLock(&g_BootLock, &irql);
    return STATUS_SUCCESS;
}
