/*
 * MinNT - boot/safemode.c
 * Safe Mode boot - minimal driver subset.
 *
 * Triggered by /safemode boot flag or BCD entry. Skips loading of
 * non-essential drivers and runs only core HAL + filesystem + win32k.
 *
 * Provides:
 *   - Detection of safe mode boot flag
 *   - Conditional driver initialization based on safe mode state
 *   - Visual indicator in the boot status display
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/cm.h>

static BOOLEAN g_SafeModeActive = FALSE;
static BOOLEAN g_BootFlagsDetected = FALSE;

NTSTATUS NTAPI SafeModeInit(VOID)
{
    /* Detect safe mode by reading the boot command line from the
     * registry: \Registry\Machine\System\CurrentControlSet\Control\BootFlags */
    {
        PCM_KEY_NODE key;
        UNICODE_STRING keyPath;
        UNICODE_STRING valueName;
        WCHAR pathBuf[128];
        WCHAR nameBuf[32];
        const WCHAR *prefix = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control";
        const WCHAR *valName = L"SafeBoot";
        NTSTATUS status;
        ULONG actualLen;
        ULONG safeBootVal = 0;
        ULONG i;

        for (i = 0; prefix[i]; i++) pathBuf[i] = prefix[i];
        pathBuf[i] = 0;
        keyPath.Buffer = pathBuf;
        keyPath.Length = (USHORT)(i * sizeof(WCHAR));
        keyPath.MaximumLength = sizeof(pathBuf);

        status = CmOpenKey(&keyPath, 0, &key);
        if (NT_SUCCESS(status)) {
            for (i = 0; valName[i]; i++) nameBuf[i] = valName[i];
            nameBuf[i] = 0;
            valueName.Buffer = nameBuf;
            valueName.Length = (USHORT)(i * sizeof(WCHAR));
            valueName.MaximumLength = sizeof(nameBuf);
            status = CmQueryValue(key, &valueName, NULL, &safeBootVal,
                                   sizeof(safeBootVal), &actualLen);
            if (NT_SUCCESS(status) && safeBootVal != 0) {
                g_SafeModeActive = TRUE;
            }
        }
    }

    g_BootFlagsDetected = TRUE;

    if (g_SafeModeActive) {
        DbgPrint("SAFEMODE: ACTIVE - minimal driver set\n");
    } else {
        DbgPrint("SAFEMODE: inactive - normal boot\n");
    }

    return STATUS_SUCCESS;
}

/* Get current safe mode state. */
BOOLEAN NTAPI SafeModeIsActive(VOID)
{
    return g_SafeModeActive;
}

/* Manually enable/disable safe mode (used by Recovery environment). */
NTSTATUS NTAPI SafeModeSet(BOOLEAN Enable)
{
    g_SafeModeActive = Enable;
    DbgPrint("SAFEMODE: manually set to %s\n", Enable ? "ACTIVE" : "inactive");
    return STATUS_SUCCESS;
}

/* Get boot flag detection status. */
BOOLEAN NTAPI SafeModeBootFlagsDetected(VOID)
{
    return g_BootFlagsDetected;
}

/* Get list of drivers allowed in safe mode. These are the minimal
 * set needed for the system to function. */
NTSTATUS NTAPI SafeModeGetAllowedDrivers(ULONG MaxCount, PCHAR *pNames)
{
    static const CHAR *allowed[] = {
        "HAL", "PS", "CM", "IO", "OB", "KE", "MM", "EX",
        "FS", "FAT32", "NTFS", "Win32k", "VGA", "kbd", "mouse",
        NULL
    };
    ULONG i;
    for (i = 0; i < MaxCount && allowed[i]; i++) {
        ULONG j = 0;
        while (allowed[i][j] && j < 63) pNames[i][j] = allowed[i][j], j++;
        pNames[i][j] = 0;
    }
    return STATUS_SUCCESS;
}
