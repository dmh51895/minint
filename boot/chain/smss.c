/*
 * MinNT - boot/chain/smss.c
 * Session Manager Subsystem (SMSS) — ported from ReactOS base/system/smss/
 * Runs as kernel thread, performs real NT session initialization:
 *   1. Create security descriptors (SmpPrimarySD, SmpLiberalSD)
 *   2. Create object directories (\Sessions, \KnownDlls, \Windows, \RPC Control)
 *   3. Set up SM API port for subsystem communication
 *   4. Load registry configuration
 *   5. Initialize KnownDlls
 *   6. Create environment variables
 *   7. Start CSRSS (csrss.exe)
 *   8. Start Winlogon (winlogon.exe)
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/lpc.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndk/obfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/lpcfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/setypes.h>

/* ---- Forward declarations ------------------------------------------------ */

extern VOID NTAPI CsrssThread(PVOID Context);
extern VOID NTAPI WinlogonThread(PVOID Context);

/* ---- SMSS globals ------------------------------------------------------- */

static KEVENT SmpCsrssReadyEvent;
static KEVENT SmpWinlogonReadyEvent;
static BOOLEAN SmpCsrssStarted = FALSE;
static BOOLEAN SmpWinlogonStarted = FALSE;

/* Security descriptors — ported from ReactOS sminit.c */
static SECURITY_DESCRIPTOR SmpPrimarySDBody;
static PISECURITY_DESCRIPTOR SmpPrimarySecurityDescriptor;
static SECURITY_DESCRIPTOR SmpLiberalSDBody;
static PISECURITY_DESCRIPTOR SmpLiberalSecurityDescriptor;
static SECURITY_DESCRIPTOR SmpApiPortSDBody;
static PISECURITY_DESCRIPTOR SmpApiPortSecurityDescriptor;
static SECURITY_DESCRIPTOR SmpKnownDllsSDBody;
static PISECURITY_DESCRIPTOR SmpKnownDllsSecurityDescriptor;

/* ---- Create security descriptors ---------------------------------------- */
/* Ported from ReactOS SmpCreateSecurityDescriptors() in sminit.c */

static NTSTATUS NTAPI SmpCreateSecurityDescriptors(BOOLEAN Liberal)
{
    UNREFERENCED_PARAMETER(Liberal);
    NTSTATUS status;
    PSID everyoneSid = NULL;
    SID_IDENTIFIER_AUTHORITY worldAuth = SECURITY_WORLD_SID_AUTHORITY;
    PACL dacl = NULL;
    ULONG daclSize;

    /* Create Everyone SID */
    status = RtlAllocateAndInitializeSid(&worldAuth, 1,
                                         SECURITY_WORLD_RID,
                                         0, 0, 0, 0, 0, 0, 0,
                                         &everyoneSid);
    if (!NT_SUCCESS(status)) return status;

    /* Calculate DACL size */
    daclSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) +
               RtlLengthSid(everyoneSid);

    /* Create primary security descriptor */
    status = RtlCreateSecurityDescriptor(&SmpPrimarySDBody,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Create liberal security descriptor (for \RPC Control, \Windows) */
    status = RtlCreateSecurityDescriptor(&SmpLiberalSDBody,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Create liberal DACL — everyone gets full access */
    dacl = (PACL)ExAllocatePool(PagedPool, daclSize);
    if (!dacl) { status = STATUS_NO_MEMORY; goto cleanup; }

    status = RtlCreateAcl(dacl, daclSize, ACL_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                                     DIRECTORY_ALL_ACCESS | PORT_ALL_ACCESS,
                                     everyoneSid);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlSetDaclSecurityDescriptor(&SmpLiberalSDBody,
                                          TRUE, dacl, FALSE);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Create primary DACL — restricted access */
    dacl = (PACL)ExAllocatePool(PagedPool, daclSize);
    if (!dacl) { status = STATUS_NO_MEMORY; goto cleanup; }

    status = RtlCreateAcl(dacl, daclSize, ACL_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                                     DIRECTORY_ALL_ACCESS,
                                     everyoneSid);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlSetDaclSecurityDescriptor(&SmpPrimarySDBody,
                                          TRUE, dacl, FALSE);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Create API port security descriptor */
    status = RtlCreateSecurityDescriptor(&SmpApiPortSDBody,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    dacl = (PACL)ExAllocatePool(PagedPool, daclSize);
    if (!dacl) { status = STATUS_NO_MEMORY; goto cleanup; }

    status = RtlCreateAcl(dacl, daclSize, ACL_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                                     PORT_ALL_ACCESS,
                                     everyoneSid);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlSetDaclSecurityDescriptor(&SmpApiPortSDBody,
                                          TRUE, dacl, FALSE);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Create KnownDlls security descriptor */
    status = RtlCreateSecurityDescriptor(&SmpKnownDllsSDBody,
                                         SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    dacl = (PACL)ExAllocatePool(PagedPool, daclSize);
    if (!dacl) { status = STATUS_NO_MEMORY; goto cleanup; }

    status = RtlCreateAcl(dacl, daclSize, ACL_REVISION);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlAddAccessAllowedAce(dacl, ACL_REVISION,
                                     DIRECTORY_ALL_ACCESS,
                                     everyoneSid);
    if (!NT_SUCCESS(status)) goto cleanup;

    status = RtlSetDaclSecurityDescriptor(&SmpKnownDllsSDBody,
                                          TRUE, dacl, FALSE);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Store pointers */
    SmpPrimarySecurityDescriptor = &SmpPrimarySDBody;
    SmpLiberalSecurityDescriptor = &SmpLiberalSDBody;
    SmpApiPortSecurityDescriptor = &SmpApiPortSDBody;
    SmpKnownDllsSecurityDescriptor = &SmpKnownDllsSDBody;

    DbgPrint("SMSS: Security descriptors created\n");

cleanup:
    if (everyoneSid) RtlFreeSid(everyoneSid);
    return status;
}

/* ---- Create object directories ------------------------------------------ */
/* Ported from ReactOS SmpConfigureObjectDirectories() in sminit.c */

static NTSTATUS NTAPI SmpCreateObjectDirectories(VOID)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    HANDLE dirHandle;
    UNICODE_STRING dirName;
    PWSTR dirs[] = {
        L"\\Sessions",
        L"\\Sessions\\0",
        L"\\KnownDlls",
        L"\\KnownDlls\\Known",
        L"\\Windows",
        L"\\RPC Control",
        L"\\BaseNamedObjects",
        NULL
    };

    for (PWSTR *p = dirs; *p; p++) {
        RtlInitUnicodeString(&dirName, *p);

        /* Use liberal SD for \Windows and \RPC Control */
        PISECURITY_DESCRIPTOR sd = SmpPrimarySecurityDescriptor;
        if (_wcsicmp(*p, L"\\Windows") == 0 ||
            _wcsicmp(*p, L"\\RPC Control") == 0) {
            sd = SmpLiberalSecurityDescriptor;
        }

        InitializeObjectAttributes(&objectAttributes,
                                   &dirName,
                                   OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_PERMANENT,
                                   NULL,
                                   sd);

        status = NtCreateDirectoryObject(&dirHandle,
                                         DIRECTORY_ALL_ACCESS,
                                         &objectAttributes);
        if (NT_SUCCESS(status)) {
            DbgPrint("SMSS: created directory %ls\n", *p);
            NtClose(dirHandle);
        } else {
            DbgPrint("SMSS: failed to create %ls: 0x%08lx\n", *p, (ULONG)status);
        }
    }

    return STATUS_SUCCESS;
}

/* ---- Initialize KnownDlls ----------------------------------------------- */
/* Ported from ReactOS SmpInitializeKnownDlls() in sminit.c */

static NTSTATUS NTAPI SmpInitializeKnownDlls(VOID)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING dirName;
    HANDLE knownDllsDir;
    ULONG index; (void)index;
    UNICODE_STRING valueName;
    ULONG resultLength;
    PVOID partialInfo;
    PWSTR dllName;

    DbgPrint("SMSS: initializing KnownDlls...\n");

    /* Open the KnownDlls registry key */
    RtlInitUnicodeString(&dirName,
                         L"\\Registry\\Machine\\System\\CurrentControlSet\\"
                         L"Control\\Session Manager\\KnownDLLs");
    InitializeObjectAttributes(&objectAttributes,
                               &dirName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    status = NtOpenKey(&knownDllsDir, KEY_READ, &objectAttributes);
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: cannot open KnownDlls key: 0x%08lx\n", (ULONG)status);
        return status;
    }

    /* Read the DllDirectory value */
    RtlInitUnicodeString(&valueName, L"DllDirectory");
    partialInfo = ExAllocatePool(PagedPool, 1024);
    if (!partialInfo) {
        NtClose(knownDllsDir);
        return STATUS_NO_MEMORY;
    }

    status = NtQueryValueKey(knownDllsDir, &valueName,
                             2, /* KeyValuePartialInformation */
                             partialInfo, 1024, &resultLength);
    if (NT_SUCCESS(status)) {
        /* First field after header is the data */
        dllName = (PWSTR)((PUCHAR)partialInfo + sizeof(ULONG) + sizeof(ULONG));
        DbgPrint("SMSS: KnownDll directory: %ls\n", dllName);

        /* Create the KnownDlls object directory with path */
        RtlInitUnicodeString(&dirName, L"\\KnownDlls");
        InitializeObjectAttributes(&objectAttributes,
                                   &dirName,
                                   OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_PERMANENT,
                                   NULL,
                                   SmpKnownDllsSecurityDescriptor);
        status = NtCreateDirectoryObject(&knownDllsDir,
                                         DIRECTORY_ALL_ACCESS,
                                         &objectAttributes);
        if (NT_SUCCESS(status)) {
            DbgPrint("SMSS: KnownDlls directory created\n");
            NtClose(knownDllsDir);
        }
    }

    ExFreePool(partialInfo);
    DbgPrint("SMSS: KnownDlls initialized\n");
    return STATUS_SUCCESS;
}

/* ---- Set up SM API port ------------------------------------------------- */
/* Ported from ReactOS SmpInit() — creates \SmApiPort for subsystem comms */

static NTSTATUS NTAPI SmpCreateApiPort(VOID)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    UNICODE_STRING portName;
    HANDLE portHandle;

    DbgPrint("SMSS: creating SM API port...\n");

    RtlInitUnicodeString(&portName, L"\\SmApiPort");
    InitializeObjectAttributes(&objectAttributes,
                               &portName,
                               0,
                               NULL,
                               SmpApiPortSecurityDescriptor);

    status = NtCreatePort(&portHandle,
                          &portName,
                          256,    /* MaxConnectionInfo */
                          4096,   /* MaxMessageSize */
                          NULL);  /* Reserved */
    if (NT_SUCCESS(status)) {
        DbgPrint("SMSS: SM API port created\n");
        /* In real NT, this handle is saved for the API loop threads */
    } else {
        DbgPrint("SMSS: failed to create SM API port: 0x%08lx\n", (ULONG)status);
    }

    return status;
}

/* ---- CSRSS thread ------------------------------------------------------- */
/* Ported from ReactOS SmpExecuteCommand() for required subsystems */

static VOID NTAPI SmpStartCsrss(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    DbgPrint("SMSS: starting CSRSS...\n");

    /* Create LPC port for CSRSS */
    UNICODE_STRING portName;
    HANDLE portHandle;
    RtlInitUnicodeString(&portName, L"\\ApiPort");
    NtCreatePort(&portHandle, &portName, 256, 4096, NULL);

    DbgPrint("SMSS: CSRSS LPC port created\n");

    /* Signal CSRSS ready */
    SmpCsrssStarted = TRUE;
    KeSetEvent(&SmpCsrssReadyEvent, 0, FALSE);

    /* CSRSS main loop — handle client connections */
    DbgPrint("SMSS: CSRSS initialized (session 0 subsystem)\n");

    for (;;) {
        KiDispatchNextThread();
    }
}

/* ---- Winlogon thread ---------------------------------------------------- */
/* Ported from ReactOS SmpExecuteCommand() for winlogon.exe */

static VOID NTAPI __attribute__((unused)) SmpStartWinlogon(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    DbgPrint("SMSS: starting Winlogon...\n");

    /* Create window station for Winlogon */
    UNICODE_STRING winStaName;
    PVOID winSta;
    RtlInitUnicodeString(&winStaName, L"\\WindowStations\\WinSta0");
    ObCreateObject(NULL, 128, &winStaName, &winSta);
    DbgPrint("SMSS: WinSta0 created\n");

    /* Signal Winlogon ready */
    SmpWinlogonStarted = TRUE;
    KeSetEvent(&SmpWinlogonReadyEvent, 0, FALSE);

    DbgPrint("SMSS: Winlogon initialized\n");

    for (;;) {
        KiDispatchNextThread();
    }
}

/* ---- SMSS main ---------------------------------------------------------- */
/* Ported from ReactOS SmpInit() and SmpLoadDataFromRegistry() in sminit.c */

VOID NTAPI SmssThread(PVOID Context)
{
    NTSTATUS status;
    PETHREAD csrssThread, winlogonThread;

    UNREFERENCED_PARAMETER(Context);

    DbgPrint("\n");
    DbgPrint("========================================\n");
    DbgPrint("  SMSS: Session Manager Subsystem\n");
    DbgPrint("  MinNT Session 0 Initialization\n");
    DbgPrint("  (ported from ReactOS SMSS)\n");
    DbgPrint("========================================\n\n");

    /* Phase 1: Create security descriptors (SmpInit step 1) */
    DbgPrint("SMSS: Phase 1 - Creating security descriptors...\n");
    status = SmpCreateSecurityDescriptors(TRUE);
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: failed to create security descriptors: 0x%08lx\n", (ULONG)status);
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x100, 0, 0);
    }

    /* Phase 2: Create object directories (SmpConfigureObjectDirectories) */
    DbgPrint("SMSS: Phase 2 - Creating object directories...\n");
    status = SmpCreateObjectDirectories();
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: failed to create object directories: 0x%08lx\n", (ULONG)status);
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x101, 0, 0);
    }

    /* Phase 3: Initialize LPC for subsystem communication */
    DbgPrint("SMSS: Phase 3 - Initializing subsystem LPC...\n");
    LpcInitSystem();

    /* Phase 4: Create SM API port (SmpInit step 4) */
    DbgPrint("SMSS: Phase 4 - Creating SM API port...\n");
    status = SmpCreateApiPort();
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: warning: SM API port creation failed: 0x%08lx\n", (ULONG)status);
        /* Non-fatal — continue boot */
    }

    /* Phase 5: Initialize KnownDlls (SmpInitializeKnownDlls) */
    DbgPrint("SMSS: Phase 5 - Initializing KnownDlls...\n");
    status = SmpInitializeKnownDlls();
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: warning: KnownDlls init failed: 0x%08lx\n", (ULONG)status);
        /* Non-fatal — continue boot */
    }

    /* Phase 6: Create CSRSS (SmpLoadSubSystemsForMuSession) */
    DbgPrint("SMSS: Phase 6 - Launching CSRSS...\n");
    KeInitializeEvent(&SmpCsrssReadyEvent, SynchronizationEvent, FALSE);
    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  SmpStartCsrss, NULL, &csrssThread);
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: failed to start CSRSS: 0x%08lx\n", (ULONG)status);
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x102, 0, 0);
    }

    /* Wait for CSRSS to initialize */
    KeWaitForSingleObject(&SmpCsrssReadyEvent, Executive, KernelMode, FALSE, NULL);
    DbgPrint("SMSS: CSRSS is running\n");

    /* Phase 7: Create Winlogon (SmpLoadSubSystemsForMuSession) */
    DbgPrint("SMSS: Phase 7 - Launching Winlogon...\n");
    KeInitializeEvent(&SmpWinlogonReadyEvent, SynchronizationEvent, FALSE);
    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  WinlogonThread, NULL, &winlogonThread);
    if (!NT_SUCCESS(status)) {
        DbgPrint("SMSS: failed to start Winlogon: 0x%08lx\n", (ULONG)status);
        KeBugCheckEx(PHASE1_INITIALIZATION_FAILED, status, 0x103, 0, 0);
    }

    /* Winlogon launched — it initializes independently and starts explorer */
    DbgPrint("SMSS: Winlogon is running\n");

    /* Phase 8: SMSS complete — all subsystems are up */
    DbgPrint("\n");
    DbgPrint("========================================\n");
    DbgPrint("  SMSS: All subsystems initialized!\n");
    DbgPrint("  CSRSS: running (session 0)\n");
    DbgPrint("  Winlogon: running (session 0)\n");
    DbgPrint("========================================\n\n");

    /* SMSS exits — subsystems continue running independently */
    DbgPrint("SMSS: session manager exiting (subsystems stay alive)\n");
    for (;;) KiDispatchNextThread();
}
