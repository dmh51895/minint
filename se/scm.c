/*
 * MinNT - se/scm.c
 * Service Control Manager (SCM).
 *
 * Manages persistent Win32-style services:
 *   - Service database (name, display name, type, start mode, status)
 *   - Start/Stop services
 *   - Auto-start during boot, demand-start on request
 *   - Persistent storage in the registry under
 *     \Registry\Machine\System\CurrentControlSet\Services\<name>
 *
 * Each service has:
 *   - Name           (registry key name)
 *   - DisplayName    (human-readable)
 *   - Type           (kernel driver, file system driver, Win32 service)
 *   - StartType      (boot, system, auto, demand, disabled)
 *   - CurrentState   (stopped, start pending, running, stop pending)
 *   - ProcessId      (PID of hosting process, 0 if not running)
 *
 * For MinNT, services are kernel-mode routines registered via
 * ScmRegisterService(). They run in the system process (PsInitialSystemProcess)
 * on a dedicated thread created by the SCM.
 */

#include <nt/ke.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <nt/rtl.h>
#include <nt/ob.h>

#define MAX_SERVICES 64
#define MAX_SERVICE_NAME 64
#define MAX_DISPLAY_NAME 128
#define SCM_KEY_PATH L"\\Registry\\Machine\\System\\CurrentControlSet\\Services"

/* Service types (Win32 dwServiceType) */
#define SERVICE_KERNEL_DRIVER       0x00000001
#define SERVICE_FILE_SYSTEM_DRIVER  0x00000002
#define SERVICE_WIN32_OWN_PROCESS    0x00000010
#define SERVICE_WIN32_SHARE_PROCESS  0x00000020
#define SERVICE_KERNEL_FILE_SYSTEM   0x00000040  /* NT-style */

/* Service start types */
#define SERVICE_BOOT_START      0x00
#define SERVICE_SYSTEM_START    0x01
#define SERVICE_AUTO_START      0x02
#define SERVICE_DEMAND_START    0x03
#define SERVICE_DISABLED        0x04

/* Service current states */
#define SERVICE_STOPPED          0x01
#define SERVICE_START_PENDING    0x02
#define SERVICE_STOP_PENDING     0x03
#define SERVICE_RUNNING          0x04

typedef struct _SCM_SERVICE {
    CHAR     Name[MAX_SERVICE_NAME];
    WCHAR    DisplayName[MAX_DISPLAY_NAME];
    ULONG    ServiceType;
    ULONG    StartType;
    ULONG    CurrentState;
    ULONG_PTR ProcessId;
    PETHREAD Thread;            /* SCM-owned thread for the service */
    PVOID    StartContext;       /* passed to StartRoutine */
    VOID   (*StartRoutine)(PVOID); /* service entry point */
    BOOLEAN  InUse;
} SCM_SERVICE, *PSCM_SERVICE;

static SCM_SERVICE g_Services[MAX_SERVICES];
static KSPIN_LOCK g_ScmLock;
static BOOLEAN g_ScmInitialized = FALSE;

/* Forward decls for kernel-mode helpers */
extern NTSTATUS NTAPI CmCreateKey(PUNICODE_STRING, ULONG, PCM_KEY_NODE *);
extern NTSTATUS NTAPI CmSetValue(PCM_KEY_NODE, PUNICODE_STRING, ULONG, PVOID, ULONG);
extern NTSTATUS NTAPI CmQueryValue(PCM_KEY_NODE, PUNICODE_STRING, PULONG, PVOID, ULONG, PULONG);
extern PCM_KEY_NODE NTAPI CmGetRootKey(VOID);

NTSTATUS NTAPI ScmInitSystem(VOID)
{
    RtlZeroMemory(g_Services, sizeof(g_Services));
    KeInitializeSpinLock(&g_ScmLock);
    g_ScmInitialized = TRUE;
    DbgPrint("SCM: Service Control Manager initialized (%d service slots)\n", MAX_SERVICES);
    return STATUS_SUCCESS;
}

static NTSTATUS OpenServiceKey(const CHAR *Name, PCM_KEY_NODE *OutKey)
{
    UNICODE_STRING keyPath;
    NTSTATUS status;
    ULONG nameLen;
    WCHAR pathBuf[512];

    /* Build "\\Registry\\Machine\\System\\CurrentControlSet\\Services\\<Name>" */
    nameLen = 0;
    while (Name[nameLen]) nameLen++;

    {
        /* Use simple concat */
        const WCHAR *prefix = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
        ULONG prefixLen = 0;
        while (prefix[prefixLen]) prefixLen++;
        if ((prefixLen + nameLen) * sizeof(WCHAR) > sizeof(pathBuf))
            return STATUS_BUFFER_TOO_SMALL;
        for (ULONG i = 0; i < prefixLen; i++) pathBuf[i] = prefix[i];
        for (ULONG i = 0; i < nameLen; i++)
            pathBuf[prefixLen + i] = (WCHAR)(UCHAR)Name[i];
        pathBuf[prefixLen + nameLen] = 0;

        keyPath.Buffer = pathBuf;
        keyPath.Length = (USHORT)((prefixLen + nameLen) * sizeof(WCHAR));
        keyPath.MaximumLength = sizeof(pathBuf);
    }

    status = CmCreateKey(&keyPath, 0, OutKey);
    return status;
}

static NTSTATUS WriteServiceToRegistry(PSCM_SERVICE svc)
{
    PCM_KEY_NODE key;
    UNICODE_STRING valueName;
    NTSTATUS status;
    ULONG dword;

    status = OpenServiceKey(svc->Name, &key);
    if (!NT_SUCCESS(status)) return status;

    /* DisplayName (REG_SZ) */
    {
        UNICODE_STRING vn;
        WCHAR nameBuf[16];
        const WCHAR *src = L"DisplayName";
        ULONG len = 0;
        while (src[len]) { nameBuf[len] = src[len]; len++; }
        nameBuf[len] = 0;
        vn.Buffer = nameBuf;
        vn.Length = (USHORT)(len * sizeof(WCHAR));
        vn.MaximumLength = sizeof(nameBuf);
        status = CmSetValue(key, &vn, 1, svc->DisplayName,
                            (ULONG)(MAX_DISPLAY_NAME * sizeof(WCHAR)));
        if (!NT_SUCCESS(status)) return status;
    }

    /* ServiceType (REG_DWORD) */
    {
        UNICODE_STRING vn;
        WCHAR nameBuf[16];
        const WCHAR *src = L"Type";
        ULONG len = 0;
        while (src[len]) { nameBuf[len] = src[len]; len++; }
        nameBuf[len] = 0;
        vn.Buffer = nameBuf;
        vn.Length = (USHORT)(len * sizeof(WCHAR));
        vn.MaximumLength = sizeof(nameBuf);
        dword = svc->ServiceType;
        status = CmSetValue(key, &vn, 4, &dword, sizeof(dword));
        if (!NT_SUCCESS(status)) return status;
    }

    /* Start (REG_DWORD) */
    {
        UNICODE_STRING vn;
        WCHAR nameBuf[16];
        const WCHAR *src = L"Start";
        ULONG len = 0;
        while (src[len]) { nameBuf[len] = src[len]; len++; }
        nameBuf[len] = 0;
        vn.Buffer = nameBuf;
        vn.Length = (USHORT)(len * sizeof(WCHAR));
        vn.MaximumLength = sizeof(nameBuf);
        dword = svc->StartType;
        status = CmSetValue(key, &vn, 4, &dword, sizeof(dword));
        if (!NT_SUCCESS(status)) return status;
    }

    return STATUS_SUCCESS;
}

/* Register a new service. startRoutine is called when the service is started. */
NTSTATUS NTAPI ScmRegisterService(const CHAR *Name, const WCHAR *DisplayName,
                                    ULONG ServiceType, ULONG StartType,
                                    VOID (*StartRoutine)(PVOID), PVOID StartContext)
{
    ULONG i, nameLen;
    KIRQL irql;

    if (!Name || !StartRoutine) return STATUS_INVALID_PARAMETER;
    nameLen = 0; while (Name[nameLen]) nameLen++;
    if (nameLen >= MAX_SERVICE_NAME) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_ScmLock, &irql);
    for (i = 0; i < MAX_SERVICES; i++) {
        if (!g_Services[i].InUse) break;
    }
    if (i == MAX_SERVICES) {
        KeReleaseSpinLock(&g_ScmLock, irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Initialize service record */
    {
        SCM_SERVICE *svc = &g_Services[i];
        RtlZeroMemory(svc, sizeof(*svc));
        for (ULONG j = 0; j < nameLen; j++) svc->Name[j] = Name[j];
        svc->Name[nameLen] = 0;
        if (DisplayName) {
            ULONG j = 0;
            while (DisplayName[j] && j < MAX_DISPLAY_NAME - 1) {
                svc->DisplayName[j] = DisplayName[j];
                j++;
            }
            svc->DisplayName[j] = 0;
        } else {
            for (ULONG j = 0; j < nameLen; j++) svc->DisplayName[j] = (WCHAR)(UCHAR)Name[j];
            svc->DisplayName[nameLen] = 0;
        }
        svc->ServiceType = ServiceType;
        svc->StartType = StartType;
        svc->CurrentState = SERVICE_STOPPED;
        svc->StartRoutine = StartRoutine;
        svc->StartContext = StartContext;
        svc->InUse = TRUE;
    }
    KeReleaseSpinLock(&g_ScmLock, irql);

    /* Persist to registry */
    {
        NTSTATUS s = WriteServiceToRegistry(&g_Services[i]);
        if (!NT_SUCCESS(s)) {
            DbgPrint("SCM: warning - failed to persist service '%s' to registry\n", Name);
        }
    }

    DbgPrint("SCM: registered service '%s' (type=%u start=%u)\n",
             Name, ServiceType, StartType);
    return STATUS_SUCCESS;
}

/* Service thread entry point: invokes the service's StartRoutine and
 * blocks waiting for stop signal. */
static VOID NTAPI ScmServiceThread(PVOID Context)
{
    PSCM_SERVICE svc = (PSCM_SERVICE)Context;
    if (!svc) return;
    svc->CurrentState = SERVICE_START_PENDING;
    /* Run the service entry point. It is expected to block (e.g. on a
     * KeWaitForSingleObject) until a stop is requested. */
    svc->StartRoutine(svc->StartContext);
    svc->CurrentState = SERVICE_STOPPED;
    DbgPrint("SCM: service '%s' thread exiting\n", svc->Name);
}

/* Start a service. Returns STATUS_SUCCESS if started, STATUS_INVALID_HANDLE
 * if not found, STATUS_INVALID_DEVICE_REQUEST if already running. */
NTSTATUS NTAPI ScmStartService(const CHAR *Name)
{
    ULONG i;
    KIRQL irql;
    NTSTATUS status;
    PSCM_SERVICE svc = NULL;

    KeAcquireSpinLock(&g_ScmLock, &irql);
    for (i = 0; i < MAX_SERVICES; i++) {
        if (g_Services[i].InUse) {
            ULONG j = 0; while (Name[j] && g_Services[i].Name[j] == Name[j] && g_Services[i].Name[j]) j++;
            if (Name[j] == 0 && g_Services[i].Name[j] == 0) {
                svc = &g_Services[i];
                break;
            }
        }
    }
    if (!svc) {
        KeReleaseSpinLock(&g_ScmLock, irql);
        return STATUS_INVALID_HANDLE;
    }
    if (svc->CurrentState != SERVICE_STOPPED) {
        KeReleaseSpinLock(&g_ScmLock, irql);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    /* Mark as start pending before releasing the lock. */
    svc->CurrentState = SERVICE_START_PENDING;
    KeReleaseSpinLock(&g_ScmLock, irql);

    /* Create a system thread to host the service. */
    status = PsCreateSystemThread(PsInitialSystemProcess, ScmServiceThread,
                                    svc, &svc->Thread);
    if (!NT_SUCCESS(status)) {
        DbgPrint("SCM: failed to create thread for '%s': 0x%X\n", Name, status);
        svc->CurrentState = SERVICE_STOPPED;
        return status;
    }

    /* Wait briefly for the service to transition to RUNNING.
     * Real implementations use an event signaled by the service. */
    {
        ULONG ticks = 0;
        while (ticks++ < 100) {
            if (svc->CurrentState == SERVICE_RUNNING) break;
            KeStallExecutionProcessor(1000);
        }
        if (svc->CurrentState == SERVICE_START_PENDING) {
            /* Service hasn't explicitly transitioned; assume running. */
            svc->CurrentState = SERVICE_RUNNING;
        }
    }

    DbgPrint("SCM: started service '%s'\n", Name);
    return STATUS_SUCCESS;
}

/* Stop a service. Currently signals nothing - the service is expected
 * to check its own shutdown flag and return from StartRoutine. */
NTSTATUS NTAPI ScmStopService(const CHAR *Name)
{
    ULONG i;
    KIRQL irql;
    PSCM_SERVICE svc = NULL;

    KeAcquireSpinLock(&g_ScmLock, &irql);
    for (i = 0; i < MAX_SERVICES; i++) {
        if (g_Services[i].InUse) {
            ULONG j = 0; while (Name[j] && g_Services[i].Name[j] == Name[j] && g_Services[i].Name[j]) j++;
            if (Name[j] == 0 && g_Services[i].Name[j] == 0) {
                svc = &g_Services[i];
                break;
            }
        }
    }
    if (!svc) {
        KeReleaseSpinLock(&g_ScmLock, irql);
        return STATUS_INVALID_HANDLE;
    }
    if (svc->CurrentState == SERVICE_STOPPED) {
        KeReleaseSpinLock(&g_ScmLock, irql);
        return STATUS_SUCCESS;
    }
    svc->CurrentState = SERVICE_STOP_PENDING;
    KeReleaseSpinLock(&g_ScmLock, irql);

    /* The StartRoutine is expected to observe the state change and
     * return. We don't have a signal mechanism; mark STOPPED after
     * a grace period. */
    {
        ULONG ticks = 0;
        while (ticks++ < 1000) {
            if (svc->CurrentState == SERVICE_STOPPED) break;
            KeStallExecutionProcessor(1000);
        }
        if (svc->CurrentState != SERVICE_STOPPED) {
            svc->CurrentState = SERVICE_STOPPED;
        }
    }

    DbgPrint("SCM: stopped service '%s'\n", Name);
    return STATUS_SUCCESS;
}

/* Auto-start services during boot. Iterates all registered services
 * and starts those with StartType == SERVICE_AUTO_START. */
NTSTATUS NTAPI ScmAutoStart(VOID)
{
    ULONG i;
    KIRQL irql;
    NTSTATUS overall = STATUS_SUCCESS;

    DbgPrint("SCM: auto-starting services\n");
    for (i = 0; i < MAX_SERVICES; i++) {
        BOOLEAN shouldStart = FALSE;
        KeAcquireSpinLock(&g_ScmLock, &irql);
        if (g_Services[i].InUse &&
            g_Services[i].StartType == SERVICE_AUTO_START &&
            g_Services[i].CurrentState == SERVICE_STOPPED) {
            shouldStart = TRUE;
        }
        KeReleaseSpinLock(&g_ScmLock, irql);
        if (shouldStart) {
            NTSTATUS s = ScmStartService(g_Services[i].Name);
            if (!NT_SUCCESS(s)) overall = s;
        }
    }
    return overall;
}

/* Enumerate services. pNames/pTypes/pStates must each be arrays of
 * size >= MaxCount. Returns count written. */
ULONG NTAPI ScmEnumServices(ULONG MaxCount, PCHAR *pNames, PULONG pTypes,
                              PULONG pStates)
{
    ULONG i, n = 0;
    KIRQL irql;

    KeAcquireSpinLock(&g_ScmLock, &irql);
    for (i = 0; i < MAX_SERVICES && n < MaxCount; i++) {
        if (g_Services[i].InUse) {
            ULONG j;
            for (j = 0; g_Services[i].Name[j] && j < MAX_SERVICE_NAME - 1; j++) {
                pNames[n][j] = g_Services[i].Name[j];
            }
            pNames[n][j] = 0;
            pTypes[n] = g_Services[i].ServiceType;
            pStates[n] = g_Services[i].CurrentState;
            n++;
        }
    }
    KeReleaseSpinLock(&g_ScmLock, irql);
    return n;
}

/* Look up a single service's state. */
NTSTATUS NTAPI ScmQueryServiceStatus(const CHAR *Name, PULONG pState)
{
    ULONG i;
    if (!Name || !pState) return STATUS_INVALID_PARAMETER;
    *pState = SERVICE_STOPPED;
    for (i = 0; i < MAX_SERVICES; i++) {
        if (g_Services[i].InUse) {
            ULONG j = 0; while (Name[j] && g_Services[i].Name[j] == Name[j] && g_Services[i].Name[j]) j++;
            if (Name[j] == 0 && g_Services[i].Name[j] == 0) {
                *pState = g_Services[i].CurrentState;
                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_INVALID_HANDLE;
}

/* Look up the registered StartRoutine for a service. Used by the Services
 * control panel to invoke start/stop without going through ScmStartService. */
NTSTATUS NTAPI ScmGetServiceInfo(const CHAR *Name, PULONG pType, PULONG pState,
                                    PULONG pStartType, PVOID *pStartRoutine,
                                    PVOID *pStartContext)
{
    ULONG i;
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < MAX_SERVICES; i++) {
        if (g_Services[i].InUse) {
            ULONG j = 0; while (Name[j] && g_Services[i].Name[j] == Name[j] && g_Services[i].Name[j]) j++;
            if (Name[j] == 0 && g_Services[i].Name[j] == 0) {
                if (pType) *pType = g_Services[i].ServiceType;
                if (pState) *pState = g_Services[i].CurrentState;
                if (pStartType) *pStartType = g_Services[i].StartType;
                if (pStartRoutine) *pStartRoutine = g_Services[i].StartRoutine;
                if (pStartContext) *pStartContext = g_Services[i].StartContext;
                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_INVALID_HANDLE;
}
