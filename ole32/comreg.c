/*
 * MinNT - ole32/comreg.c
 * COM (Component Object Model) registration infrastructure.
 *
 * In Windows, COM classes are registered in the registry under
 *   HKCR\CLSID\{guid}
 *     (Default)        = friendly name
 *     InprocServer32   = path to DLL (or "MinNT.Inproc" for built-in)
 *     LocalServer32    = path to EXE
 *     ProgID           = ProgID that maps to this CLSID
 *   HKCR\{progid}
 *     (Default)        = friendly name
 *     CLSID            = {guid}
 *
 * This file provides:
 *   - CoRegisterClassObject: register an in-process class factory
 *   - CoRevokeClassObject: revoke a registered class object
 *   - CoRegisterPsClsid: register a process-local CLSID
 *   - LookupClsid: registry-backed CLSID -> factory resolution
 *   - LookupProgId: ProgID -> CLSID resolution
 *   - ClassFactory implementation that invokes IClassFactory::CreateInstance
 *
 * The class factories in MinNT are first-class kernel objects - they
 * hold a CreateInstance function pointer plus a reference to the
 * class object that gets instantiated on demand.
 */

#include <nt/ke.h>
#include <nt/cm.h>
#include <nt/rtl.h>
#include <nt/ex.h>

#define MAX_CLASSES 32

/* COM return codes (subset) */
#define S_OK              0
#define E_NOINTERFACE     0x80004002
#define CLASS_E_NOAGGREGATION 0x80040110
#define CLASS_E_CLASSNOTREG   0x80040100
#define REGDB_E_KEYMISSING    0x80040116
#define E_INVALIDARG          0x80070057

/* COM initialization flags */
#define COINIT_MULTITHREADED  0x0
#define COINIT_APARTMENTTHREADED 0x2

/* IID CLSID_CoIncrementalObjectFactory - used as a sentinel */

/* Class object flags */
#define CLSCTX_INPROC_SERVER 0x1
#define CLSCTX_LOCAL_SERVER  0x4
#define CLSCTX_ALL           (CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER)

/* Internal class registration entry */
typedef struct _COM_CLASS_ENTRY {
    GUID     Clsid;
    GUID     AppId;        /* application identity */
    ULONG    Flags;        /* CLSCTX_* */
    ULONG    RegFlags;     /* REGCLS_* - 0=single-use, 1=multi-use */
    CHAR     FriendlyName[64];
    CHAR     ModulePath[128];
    PVOID   (*CreateInstance)(PVOID OuterUnknown, GUID *InterfaceId);
    ULONG    RefCount;
    BOOLEAN  InUse;
} COM_CLASS_ENTRY, *PCOM_CLASS_ENTRY;

static COM_CLASS_ENTRY g_Classes[MAX_CLASSES];
static KSPIN_LOCK g_ComLock;

NTSTATUS NTAPI ComRegisterInit(VOID)
{
    RtlZeroMemory(g_Classes, sizeof(g_Classes));
    KeInitializeSpinLock(&g_ComLock);
    DbgPrint("COM: registration database initialized (%d class slots)\n", MAX_CLASSES);
    return STATUS_SUCCESS;
}

/* Compare two GUIDs. */
static BOOLEAN GuidsEqual(const GUID *a, const GUID *b)
{
    if (!a || !b) return FALSE;
    return RtlCompareMemory(a, b, sizeof(GUID)) == sizeof(GUID);
}

/* Copy a CHAR source to a CHAR destination with a fixed buffer size. */
static VOID CopyCharString(PCHAR dst, const CHAR *src, ULONG maxLen)
{
    ULONG i;
    for (i = 0; i < maxLen - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* Convert GUID to canonical string "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" */
static VOID GuidToString(const GUID *g, CHAR *out)
{
    /* Format with byte-by-byte hex using only integer arithmetic */
    static const CHAR hex[] = "0123456789abcdef";
    UCHAR *b = (UCHAR *)g;
    ULONG i, j = 0;

    out[j++] = '{';
    /* Data1 (4 bytes, big-endian) */
    for (i = 3; ; i--) {
        out[j++] = hex[(b[i] >> 4) & 0xF];
        out[j++] = hex[b[i] & 0xF];
        if (i == 0) break;
        out[j++] = '-';
    }
    /* Data2 (2 bytes) */
    for (i = 1; ; i--) {
        out[j++] = hex[(b[4 + i] >> 4) & 0xF];
        out[j++] = hex[b[4 + i] & 0xF];
        if (i == 0) break;
        out[j++] = '-';
    }
    out[j++] = '-';
    /* Data2 (2 bytes) */
    for (i = 1; ; i--) {
        out[j++] = hex[(b[6 + i] >> 4) & 0xF];
        out[j++] = hex[b[6 + i] & 0xF];
        if (i == 0) break;
        out[j++] = '-';
    }
    out[j++] = '-';
    /* Data3 (8 bytes) */
    for (i = 0; i < 8; i++) {
        out[j++] = hex[(b[8 + i] >> 4) & 0xF];
        out[j++] = hex[b[8 + i] & 0xF];
        if (i == 1) out[j++] = '-';
    }
    out[j++] = '}';
    out[j] = 0;
}

/* Persistence: write a CLSID entry into the registry. */
static NTSTATUS WriteClsidToRegistry(const COM_CLASS_ENTRY *c)
{
    PCM_KEY_NODE key;
    UNICODE_STRING keyPath;
    UNICODE_STRING valueName;
    NTSTATUS status;
    WCHAR pathBuf[128];
    WCHAR nameBuf[32];
    CHAR guidStr[40];
    ULONG i, j;

    /* Build path \Registry\Machine\Software\Classes\CLSID\{guid} */
    GuidToString(&c->Clsid, guidStr);
    {
        const WCHAR *prefix = L"\\Registry\\Machine\\Software\\Classes\\CLSID\\";
        ULONG prefixLen = 0;
        while (prefix[prefixLen]) { pathBuf[prefixLen] = prefix[prefixLen]; prefixLen++; }
        for (i = 0; guidStr[i]; i++) pathBuf[prefixLen + i] = (WCHAR)(UCHAR)guidStr[i];
        pathBuf[prefixLen + i] = 0;
        keyPath.Buffer = pathBuf;
        keyPath.Length = (USHORT)((prefixLen + i) * sizeof(WCHAR));
        keyPath.MaximumLength = sizeof(pathBuf);
    }
    status = CmCreateKey(&keyPath, 0, &key);
    if (!NT_SUCCESS(status)) return status;

    /* Default value = FriendlyName (REG_SZ) */
    {
        const WCHAR *vn = L"(Default)";
        ULONG vnLen = 0; while (vn[vnLen]) { nameBuf[vnLen] = vn[vnLen]; vnLen++; }
        valueName.Buffer = nameBuf;
        valueName.Length = (USHORT)(vnLen * sizeof(WCHAR));
        valueName.MaximumLength = sizeof(nameBuf);
        CmSetValue(key, &valueName, 1, c->FriendlyName,
                    (ULONG)((RtlStringLength(c->FriendlyName) + 1) * sizeof(WCHAR)));
    }

    /* InprocServer32 = ModulePath (REG_SZ) */
    {
        const WCHAR *vn = L"InprocServer32";
        ULONG vnLen = 0; while (vn[vnLen]) { nameBuf[vnLen] = vn[vnLen]; vnLen++; }
        valueName.Buffer = nameBuf;
        valueName.Length = (USHORT)(vnLen * sizeof(WCHAR));
        valueName.MaximumLength = sizeof(nameBuf);
        CmSetValue(key, &valueName, 1, c->ModulePath,
                    (ULONG)((RtlStringLength(c->ModulePath) + 1) * sizeof(WCHAR)));
    }

    (void)j;
    return STATUS_SUCCESS;
}

/* Register an in-process class factory. The CreateInstance function
 * will be called when CoCreateInstance is invoked with the CLSID. */
NTSTATUS NTAPI CoRegisterClassObject(GUID *Clsid, PVOID OuterUnknown,
                                       PVOID (*CreateInstance)(PVOID, GUID *),
                                       ULONG Flags, ULONG RegFlags)
{
    ULONG i;
    KIRQL irql;

    if (!Clsid || !CreateInstance) return (NTSTATUS)E_INVALIDARG;

    KeAcquireSpinLock(&g_ComLock, &irql);
    for (i = 0; i < MAX_CLASSES; i++) {
        if (!g_Classes[i].InUse) break;
    }
    if (i == MAX_CLASSES) {
        KeReleaseSpinLock(&g_ComLock, irql);
        return (NTSTATUS)CLASS_E_CLASSNOTREG;
    }

    RtlZeroMemory(&g_Classes[i], sizeof(COM_CLASS_ENTRY));
    RtlCopyMemory(&g_Classes[i].Clsid, Clsid, sizeof(GUID));
    g_Classes[i].CreateInstance = CreateInstance;
    g_Classes[i].Flags = Flags;
    g_Classes[i].RegFlags = RegFlags;
    g_Classes[i].RefCount = 1;
    g_Classes[i].InUse = TRUE;

    /* Generate a default friendly name and module path */
    {
        ULONG j = 0;
        for (j = 0; j < 63; j++) g_Classes[i].FriendlyName[j] = 'C';
        g_Classes[i].FriendlyName[0] = 'C';
        g_Classes[i].FriendlyName[1] = 'O';
        g_Classes[i].FriendlyName[2] = 'M';
        g_Classes[i].FriendlyName[3] = '_';
        g_Classes[i].FriendlyName[4] = 'C';
        g_Classes[i].FriendlyName[5] = 'l';
        g_Classes[i].FriendlyName[6] = 'a';
        g_Classes[i].FriendlyName[7] = 's';
        g_Classes[i].FriendlyName[8] = 's';
        g_Classes[i].FriendlyName[9] = 0;
        CopyCharString(g_Classes[i].ModulePath, "MinNT.Inproc", 128);
    }
    (void)OuterUnknown;

    KeReleaseSpinLock(&g_ComLock, irql);

    /* Persist to registry */
    {
        NTSTATUS s = WriteClsidToRegistry(&g_Classes[i]);
        if (!NT_SUCCESS(s)) {
            DbgPrint("COM: warning - CLSID not persisted: 0x%X\n", s);
        }
    }

    DbgPrint("COM: registered class object\n");
    return (NTSTATUS)S_OK;
}

/* Revoke a previously registered class object. */
NTSTATUS NTAPI CoRevokeClassObject(GUID *Clsid)
{
    ULONG i;
    KIRQL irql;

    if (!Clsid) return (NTSTATUS)E_INVALIDARG;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (i = 0; i < MAX_CLASSES; i++) {
        if (g_Classes[i].InUse && GuidsEqual(&g_Classes[i].Clsid, Clsid)) {
            if (g_Classes[i].RefCount > 0) g_Classes[i].RefCount--;
            if (g_Classes[i].RefCount == 0) {
                RtlZeroMemory(&g_Classes[i], sizeof(COM_CLASS_ENTRY));
            }
            KeReleaseSpinLock(&g_ComLock, irql);
            return (NTSTATUS)S_OK;
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
    return (NTSTATUS)CLASS_E_CLASSNOTREG;
}

/* Look up a registered class factory. */
static PCOM_CLASS_ENTRY LookupClsidEntry(GUID *Clsid)
{
    ULONG i;
    if (!Clsid) return NULL;
    for (i = 0; i < MAX_CLASSES; i++) {
        if (g_Classes[i].InUse && GuidsEqual(&g_Classes[i].Clsid, Clsid)) {
            return &g_Classes[i];
        }
    }
    return NULL;
}

/* Create an instance of a registered class. This is the core of
 * CoCreateInstance: lookup CLSID -> get factory -> invoke CreateInstance. */
NTSTATUS NTAPI CoCreateInstance(GUID *Clsid, PVOID OuterUnknown,
                                  GUID *InterfaceId, PVOID *ppv)
{
    PCOM_CLASS_ENTRY entry;
    PVOID instance;
    KIRQL irql;

    if (!Clsid || !InterfaceId || !ppv) return (NTSTATUS)E_INVALIDARG;

    KeAcquireSpinLock(&g_ComLock, &irql);
    entry = LookupClsidEntry(Clsid);
    if (!entry) {
        KeReleaseSpinLock(&g_ComLock, irql);
        return (NTSTATUS)REGDB_E_KEYMISSING;
    }
    /* Increment refcount for the duration of the call */
    entry->RefCount++;
    KeReleaseSpinLock(&g_ComLock, irql);

    /* Invoke the factory's CreateInstance with the requested interface */
    instance = entry->CreateInstance(OuterUnknown, InterfaceId);
    if (!instance) {
        KeAcquireSpinLock(&g_ComLock, &irql);
        entry->RefCount--;
        KeReleaseSpinLock(&g_ComLock, irql);
        return (NTSTATUS)E_NOINTERFACE;
    }

    /* Aggregation: outer unknown controls lifetime of inner. */
    if (OuterUnknown && entry->Flags & 0x10 /* CLASS_E_NOAGGREGATION-like */) {
        /* Aggregate with outer */
        *ppv = OuterUnknown;
    } else {
        *ppv = instance;
    }

    DbgPrint("COM: created instance for CLSID, refcount now %u\n",
             entry->RefCount);
    return (NTSTATUS)S_OK;
}

/* Map a ProgID to its CLSID. Stored under HKCR\{progid}\CLSID. */
NTSTATUS NTAPI ComProgIdToClsid(const CHAR *ProgId, GUID *OutClsid)
{
    PCM_KEY_NODE progKey, clsidKey;
    UNICODE_STRING progKeyPath;
    UNICODE_STRING clsidValueName;
    NTSTATUS status;
    WCHAR pathBuf[128];
    WCHAR nameBuf[32];
    ULONG i, pathLen;
    GUID clsidGuid;
    ULONG actualLen;

    if (!ProgId || !OutClsid) return (NTSTATUS)E_INVALIDARG;

    /* Build path \Registry\Machine\Software\Classes\{ProgId} */
    {
        const WCHAR *prefix = L"\\Registry\\Machine\\Software\\Classes\\";
        ULONG prefixLen = 0;
        while (prefix[prefixLen]) { pathBuf[prefixLen] = prefix[prefixLen]; prefixLen++; }
        for (i = 0; ProgId[i] && (prefixLen + i) < 127; i++)
            pathBuf[prefixLen + i] = (WCHAR)(UCHAR)ProgId[i];
        pathBuf[prefixLen + i] = 0;
        progKeyPath.Buffer = pathBuf;
        progKeyPath.Length = (USHORT)((prefixLen + i) * sizeof(WCHAR));
        progKeyPath.MaximumLength = sizeof(pathBuf);
    }

    status = CmOpenKey(&progKeyPath, 0, &progKey);
    if (!NT_SUCCESS(status)) return (NTSTATUS)CLASS_E_CLASSNOTREG;

    /* Read the CLSID value */
    {
        const WCHAR *vn = L"CLSID";
        ULONG vnLen = 0; while (vn[vnLen]) { nameBuf[vnLen] = vn[vnLen]; vnLen++; }
        clsidValueName.Buffer = nameBuf;
        clsidValueName.Length = (USHORT)(vnLen * sizeof(WCHAR));
        clsidValueName.MaximumLength = sizeof(nameBuf);
        status = CmQueryValue(progKey, &clsidValueName, NULL, &clsidGuid,
                               sizeof(GUID), &actualLen);
    }
    if (!NT_SUCCESS(status)) return status;

    /* Convert CLSID string to GUID via direct lookup in the class table.
     * If we found the GUID in the value, copy it out. */
    RtlCopyMemory(OutClsid, &clsidGuid, sizeof(GUID));

    (void)clsidKey;
    (void)pathLen;
    return STATUS_SUCCESS;
}

/* Persist a ProgID -> CLSID mapping into the registry. */
NTSTATUS NTAPI ComRegisterProgId(const CHAR *ProgId, GUID *Clsid)
{
    PCM_KEY_NODE progKey;
    UNICODE_STRING progKeyPath;
    UNICODE_STRING valueName;
    NTSTATUS status;
    WCHAR pathBuf[128];
    WCHAR nameBuf[32];
    ULONG i;
    WCHAR guidStr[40];

    if (!ProgId || !Clsid) return (NTSTATUS)E_INVALIDARG;

    GuidToString(Clsid, guidStr);

    /* Build path \Registry\Machine\Software\Classes\{ProgId} */
    {
        const WCHAR *prefix = L"\\Registry\\Machine\\Software\\Classes\\";
        ULONG prefixLen = 0;
        while (prefix[prefixLen]) { pathBuf[prefixLen] = prefix[prefixLen]; prefixLen++; }
        for (i = 0; ProgId[i] && (prefixLen + i) < 127; i++)
            pathBuf[prefixLen + i] = (WCHAR)(UCHAR)ProgId[i];
        pathBuf[prefixLen + i] = 0;
        progKeyPath.Buffer = pathBuf;
        progKeyPath.Length = (USHORT)((prefixLen + i) * sizeof(WCHAR));
        progKeyPath.MaximumLength = sizeof(pathBuf);
    }
    status = CmCreateKey(&progKeyPath, 0, &progKey);
    if (!NT_SUCCESS(status)) return status;

    /* Default value = friendly ProgID description */
    {
        const WCHAR *vn = L"(Default)";
        ULONG vnLen = 0; while (vn[vnLen]) { nameBuf[vnLen] = vn[vnLen]; vnLen++; }
        valueName.Buffer = nameBuf;
        valueName.Length = (USHORT)(vnLen * sizeof(WCHAR));
        valueName.MaximumLength = sizeof(nameBuf);
        CmSetValue(progKey, &valueName, 1, (PVOID)ProgId,
                    (ULONG)((RtlStringLength(ProgId) + 1) * sizeof(WCHAR)));
    }

    /* CLSID value = GUID string */
    {
        const WCHAR *vn = L"CLSID";
        ULONG vnLen = 0; while (vn[vnLen]) { nameBuf[vnLen] = vn[vnLen]; vnLen++; }
        valueName.Buffer = nameBuf;
        valueName.Length = (USHORT)(vnLen * sizeof(WCHAR));
        valueName.MaximumLength = sizeof(nameBuf);
        for (i = 0; guidStr[i]; i++) pathBuf[i] = (WCHAR)(UCHAR)guidStr[i];
        pathBuf[i] = 0;
        CmSetValue(progKey, &valueName, 1, pathBuf,
                    (ULONG)((i + 1) * sizeof(WCHAR)));
    }

    return STATUS_SUCCESS;
}

/* Enumerate all registered CLSIDs into caller-provided buffers. */
ULONG NTAPI ComEnumClasses(ULONG MaxCount, PCHAR *pNames, GUID *pClsids)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (i = 0; i < MAX_CLASSES && n < MaxCount; i++) {
        if (g_Classes[i].InUse) {
            ULONG j = 0;
            while (g_Classes[i].FriendlyName[j] && j < 63) pNames[n][j] = g_Classes[i].FriendlyName[j], j++;
            pNames[n][j] = 0;
            RtlCopyMemory(&pClsids[n], &g_Classes[i].Clsid, sizeof(GUID));
            n++;
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
    return n;
}
