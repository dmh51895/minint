/*
 * MinNT - input/ctrlname.c
 * Controller FriendlyName registry management.
 *
 * Mirrors the structure Windows uses for HID joystick devices:
 *   HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\
 *       PrivateProperties\Joystick\OEM\VID_xxxx&PID_xxxx
 *           OEMName  (REG_SZ)     - Friendly name
 *           OEMData  (REG_BINARY) - Vendor-specific data
 *
 * Applications that need to identify a controller (e.g. games that
 * look for "Xbox 360 For Windows (Controller)") read this registry
 * tree. We store the entries in memory and persist them via Cm.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/cm.h>
#include <nt/framework.h>

#define CTRLNAME_MAX_ENTRIES     64
#define CTRLNAME_NAME_MAX        128
#define CTRLNAME_OEMDATA_MAX     64
#define CTRLNAME_KEY_MAX         128

typedef struct _CTRLNAME_ENTRY {
    USHORT Vid;
    USHORT Pid;
    CHAR Name[CTRLNAME_NAME_MAX];
    UCHAR OemData[CTRLNAME_OEMDATA_MAX];
    ULONG OemDataLength;
    BOOLEAN InUse;
} CTRLNAME_ENTRY;

static CTRLNAME_ENTRY g_Entries[CTRLNAME_MAX_ENTRIES];

static CTRLNAME_ENTRY *CtrlnameFind(USHORT Vid, USHORT Pid)
{
    for (ULONG i = 0; i < CTRLNAME_MAX_ENTRIES; i++) {
        if (g_Entries[i].InUse &&
            g_Entries[i].Vid == Vid && g_Entries[i].Pid == Pid)
            return &g_Entries[i];
    }
    return NULL;
}

static CTRLNAME_ENTRY *CtrlnameAlloc(USHORT Vid, USHORT Pid)
{
    for (ULONG i = 0; i < CTRLNAME_MAX_ENTRIES; i++) {
        if (!g_Entries[i].InUse) {
            RtlZeroMemory(&g_Entries[i], sizeof(CTRLNAME_ENTRY));
            g_Entries[i].InUse = TRUE;
            g_Entries[i].Vid = Vid;
            g_Entries[i].Pid = Pid;
            return &g_Entries[i];
        }
    }
    return NULL;
}

static VOID CtrlnameBuildKey(USHORT Vid, USHORT Pid, PCHAR Buffer, ULONG MaxLen)
{
    /* Format: \\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\
     * MediaProperties\\PrivateProperties\\Joystick\\OEM\\VID_xxxx&PID_xxxx */
    const CHAR *prefix = "\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\VID_";
    ULONG k = 0;
    for (ULONG i = 0; prefix[i] && k < MaxLen - 32; i++) Buffer[k++] = prefix[i];
    /* VID as 4 hex chars. */
    for (LONG shift = 12; shift >= 0 && k < MaxLen - 32; shift -= 4) {
        UCHAR nib = (Vid >> shift) & 0xF;
        Buffer[k++] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    Buffer[k++] = '&';
    Buffer[k++] = 'P'; Buffer[k++] = 'I'; Buffer[k++] = 'D'; Buffer[k++] = '_';
    for (LONG shift = 12; shift >= 0 && k < MaxLen - 32; shift -= 4) {
        UCHAR nib = (Pid >> shift) & 0xF;
        Buffer[k++] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    Buffer[k] = 0;
}

/* Write the OEMName value into the registry for the device. */
static NTSTATUS CtrlnamePersist(CTRLNAME_ENTRY *e)
{
    CHAR keyPath[CTRLNAME_KEY_MAX];
    CtrlnameBuildKey(e->Vid, e->Pid, keyPath, sizeof(keyPath));

    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;

    /* OEMName. */
    if (e->Name[0]) {
        CHAR vname[16];
        ULONG k = 0;
        vname[k++] = 'O'; vname[k++] = 'E'; vname[k++] = 'M';
        vname[k++] = 'N'; vname[k++] = 'a'; vname[k++] = 'm'; vname[k++] = 'e';
        vname[k] = 0;
        UNICODE_STRING uvalue;
        RtlInitUnicodeString(&uvalue, (PCWSTR)vname);
        WCHAR wideBuf[CTRLNAME_NAME_MAX];
        ULONG i = 0;
        while (e->Name[i] && i < CTRLNAME_NAME_MAX - 1) { wideBuf[i] = (WCHAR)(UCHAR)e->Name[i]; i++; }
        wideBuf[i] = 0;
        CmSetValue(key, REG_SZ, &uvalue, wideBuf, (i + 1) * sizeof(WCHAR));
    }

    /* OEMData. */
    if (e->OemDataLength > 0) {
        CHAR vname[16];
        ULONG k = 0;
        vname[k++] = 'O'; vname[k++] = 'E'; vname[k++] = 'M';
        vname[k++] = 'D'; vname[k++] = 'a'; vname[k++] = 't'; vname[k++] = 'a';
        vname[k] = 0;
        UNICODE_STRING uvalue;
        RtlInitUnicodeString(&uvalue, (PCWSTR)vname);
        CmSetValue(key, REG_BINARY, &uvalue, e->OemData, e->OemDataLength);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ControllerSetName(USHORT Vid, USHORT Pid, const CHAR *FriendlyName)
{
    if (!FriendlyName) return STATUS_INVALID_PARAMETER;
    CTRLNAME_ENTRY *e = CtrlnameFind(Vid, Pid);
    if (!e) e = CtrlnameAlloc(Vid, Pid);
    if (!e) return STATUS_NO_MEMORY;
    RtlZeroMemory(e->Name, CTRLNAME_NAME_MAX);
    ULONG i = 0;
    while (FriendlyName[i] && i < CTRLNAME_NAME_MAX - 1) { e->Name[i] = FriendlyName[i]; i++; }
    e->Name[i] = 0;
    CtrlnamePersist(e);
    DbgPrint("CTRLNAME: set VID=%04x PID=%04x -> '%s'\n", Vid, Pid, e->Name);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ControllerGetName(USHORT Vid, USHORT Pid, PCHAR OutName, ULONG MaxLen)
{
    if (!OutName || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    CTRLNAME_ENTRY *e = CtrlnameFind(Vid, Pid);
    if (!e) { OutName[0] = 0; return STATUS_NOT_FOUND; }
    ULONG i = 0;
    while (e->Name[i] && i < MaxLen - 1) { OutName[i] = e->Name[i]; i++; }
    OutName[i] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ControllerSetOemData(USHORT Vid, USHORT Pid, PUCHAR Data, ULONG Length)
{
    if (!Data || Length == 0) return STATUS_INVALID_PARAMETER;
    if (Length > CTRLNAME_OEMDATA_MAX) return STATUS_INVALID_PARAMETER;
    CTRLNAME_ENTRY *e = CtrlnameFind(Vid, Pid);
    if (!e) e = CtrlnameAlloc(Vid, Pid);
    if (!e) return STATUS_NO_MEMORY;
    RtlCopyMemory(e->OemData, Data, Length);
    e->OemDataLength = Length;
    CtrlnamePersist(e);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ControllerGetOemData(USHORT Vid, USHORT Pid, PUCHAR OutData, ULONG MaxLen, PULONG OutLength)
{
    if (!OutData || !OutLength) return STATUS_INVALID_PARAMETER;
    CTRLNAME_ENTRY *e = CtrlnameFind(Vid, Pid);
    if (!e) { *OutLength = 0; return STATUS_NOT_FOUND; }
    ULONG got = e->OemDataLength;
    if (got > MaxLen) got = MaxLen;
    RtlCopyMemory(OutData, e->OemData, got);
    *OutLength = got;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ControllerRemoveEntry(USHORT Vid, USHORT Pid)
{
    CTRLNAME_ENTRY *e = CtrlnameFind(Vid, Pid);
    if (!e) return STATUS_NOT_FOUND;
    RtlZeroMemory(e, sizeof(CTRLNAME_ENTRY));
    return STATUS_SUCCESS;
}

ULONG NTAPI ControllerEnumerate(PULONG Vids, PULONG Pids, PCHAR Names, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < CTRLNAME_MAX_ENTRIES && n < MaxCount; i++) {
        if (g_Entries[i].InUse) {
            if (Vids) Vids[n] = g_Entries[i].Vid;
            if (Pids) Pids[n] = g_Entries[i].Pid;
            if (Names) {
                ULONG k = 0;
                while (g_Entries[i].Name[k] && k < 127) { Names[n * 128 + k] = g_Entries[i].Name[k]; k++; }
                Names[n * 128 + k] = 0;
            }
            n++;
        }
    }
    return n;
}

NTSTATUS NTAPI ControllerNameInit(VOID)
{
    RtlZeroMemory(g_Entries, sizeof(g_Entries));
    /* Seed the common Xbox/PS4/Switch names so the registry has them
     * before any controller is even plugged in. */
    ControllerSetName(0x045E, 0x028E, "Xbox 360 Controller");
    ControllerSetName(0x045E, 0x02D1, "Xbox One Controller");
    ControllerSetName(0x045E, 0x02EA, "Xbox One S Controller");
    ControllerSetName(0x045E, 0x0B05, "Xbox One Elite Controller");
    ControllerSetName(0x045E, 0x0B13, "Xbox Series X Controller");
    ControllerSetName(0x054C, 0x05C4, "PS4 Controller");
    ControllerSetName(0x054C, 0x09CC, "PS4 DualShock 4");
    ControllerSetName(0x054C, 0x0BA0, "PS4 Wireless Adapter");
    ControllerSetName(0x054C, 0x0CE6, "PS5 DualSense");
    ControllerSetName(0x054C, 0x0DF2, "PS5 DualSense Edge");
    ControllerSetName(0x057E, 0x2009, "Switch Pro Controller");
    ControllerSetName(0x28DE, 0x1001, "Steam Controller");
    ControllerSetName(0x28DE, 0x1101, "Steam Controller");
    ControllerSetName(0x28DE, 0x1201, "Steam Controller");
    /* Seed default OEMData (the "01 00 00 00 00 00 00 00" empty body
     * Windows uses when no per-device data is available). */
    UCHAR defaultOemData[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    for (USHORT v = 0x045E; v <= 0x045E; v++) {
        ControllerSetOemData(v, 0x028E, defaultOemData, sizeof(defaultOemData));
    }
    DbgPrint("CTRLNAME: controller friendly name registry initialized\n");
    return STATUS_SUCCESS;
}
