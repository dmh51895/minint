/*
 * MinNT - boot/registry.c
 * Subsystem registry + dependency-ordered initialization.
 *
 * Each subsystem registers:
 *   - its name (string)
 *   - its init function
 *   - the names of subsystems it depends on (must init first)
 *   - a phase (0 = core kernel/hal, 1 = drivers, 2 = services, 3 = GUI)
 *
 * At boot time, the dispatcher:
 *   1. Reads the active boot profile (set by /install, /safemode, etc.)
 *   2. Topologically sorts all registered subsystems by their
 *      declared dependencies
 *   3. Walks the sorted list and inits each subsystem whose name is
 *      allowed by the active profile
 *
 * The phase is just a hint for ordering: subsystems declared in
 * phase 1 run before phase 2, but if A in phase 1 depends on B in
 * phase 1 the topo sort still gets it right. The phase is mostly
 * for log output grouping.
 *
 * Adding a new subsystem: call BootRegisterSubsystem() with its name,
 * init function, and dependency list. Update the relevant profile
 * definitions in boot/profile.c. No edits to kiinit.c needed.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define BOOT_MAX_SUBSYSTEMS   96
#define BOOT_MAX_DEPS_PER     8
#define BOOT_MAX_NAME         32

typedef NTSTATUS (NTAPI *BOOT_SUBSYS_INIT)(VOID);

typedef struct _BOOT_SUBSYSTEM_REC {
    CHAR Name[BOOT_MAX_NAME];
    BOOT_SUBSYS_INIT Init;
    /* Dependencies: names of subsystems that must init first.
     * Max BOOT_MAX_DEPS_PER. NULL-terminated via DepCount. */
    const CHAR *Deps[BOOT_MAX_DEPS_PER];
    ULONG DepCount;
    /* Phase: 0 = core, 1 = drivers, 2 = services, 3 = GUI. */
    ULONG Phase;
    /* True if the active profile says we should init this subsystem. */
    BOOLEAN ShouldInit;
    /* True if Init() has been called successfully. */
    BOOLEAN Initialized;
    BOOLEAN InUse;
} BOOT_SUBSYSTEM_REC;

static BOOT_SUBSYSTEM_REC g_Registry[BOOT_MAX_SUBSYSTEMS];

static BOOT_SUBSYSTEM_REC *BootFindRec(const CHAR *Name);

/* Phase names for log output. */
static const CHAR *g_PhaseNames[] = { "Core", "Drivers", "Services", "GUI" };

/* ---- Registration API ---- */

NTSTATUS NTAPI BootRegisterSubsystemEx(const CHAR *Name, BOOT_SUBSYS_INIT Init,
                                       const CHAR **Deps, ULONG DepCount,
                                       ULONG Phase)
{
    if (!Name || !Init) return STATUS_INVALID_PARAMETER;
    if (DepCount > BOOT_MAX_DEPS_PER) return STATUS_INVALID_PARAMETER;

    /* Reject duplicates. */
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (g_Registry[i].InUse) {
            BOOLEAN eq = TRUE;
            for (ULONG k = 0; k < BOOT_MAX_NAME; k++) {
                if (g_Registry[i].Name[k] != Name[k]) { eq = FALSE; break; }
                if (Name[k] == 0) break;
            }
            if (eq) return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    /* Find a free slot. */
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (!g_Registry[i].InUse) {
            RtlZeroMemory(&g_Registry[i], sizeof(BOOT_SUBSYSTEM_REC));
            for (ULONG k = 0; k < BOOT_MAX_NAME - 1 && Name[k]; k++) g_Registry[i].Name[k] = Name[k];
            g_Registry[i].Name[BOOT_MAX_NAME - 1] = 0;
            g_Registry[i].Init = Init;
            for (ULONG k = 0; k < DepCount; k++) g_Registry[i].Deps[k] = Deps[k];
            g_Registry[i].DepCount = DepCount;
            g_Registry[i].Phase = Phase;
            g_Registry[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

/* Old-style registration helper used by the framework module. */
NTSTATUS NTAPI BootRegisterSubsystem(const CHAR *Name, NTSTATUS (*Init)(VOID))
{
    return BootRegisterSubsystemEx(Name, Init, NULL, 0, 2);
}

NTSTATUS NTAPI BootMarkSubsystemInitialized(const CHAR *Name)
{
    BOOT_SUBSYSTEM_REC *r = BootFindRec(Name);
    if (!r) return STATUS_NOT_FOUND;
    r->Initialized = TRUE;
    return STATUS_SUCCESS;
}

/* ---- Lookup helpers ---- */

static BOOT_SUBSYSTEM_REC *BootFindRec(const CHAR *Name)
{
    if (!Name) return NULL;
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (!g_Registry[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < BOOT_MAX_NAME; k++) {
            if (g_Registry[i].Name[k] != Name[k]) { eq = FALSE; break; }
            if (Name[k] == 0) break;
        }
        if (eq) return &g_Registry[i];
    }
    return NULL;
}

/* ---- Profile gating ---- */

static NTSTATUS BootMarkShouldInit(VOID)
{
    /* For each registered subsystem, decide if the active profile
     * allows it. We mark `ShouldInit` and skip ones that don't. */
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (!g_Registry[i].InUse) continue;
        g_Registry[i].ShouldInit = BootProfileAllowsSubsystem(g_Registry[i].Name);
    }
    return STATUS_SUCCESS;
}

/* ---- Topological sort ---- */

/* Kahn's algorithm: repeatedly pick a node whose dependencies are
 * already satisfied (and is registered/in-scope), and emit it. */
static NTSTATUS BootTopoSort(ULONG *OutOrder, ULONG *OutCount)
{
    /* Build the in-degree count for each subsystem that should init. */
    ULONG n = 0;
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (g_Registry[i].InUse && g_Registry[i].ShouldInit) n++;
    }
    if (n == 0) { *OutCount = 0; return STATUS_SUCCESS; }

    /* Each subsystem tracks how many unsatisfied dependencies. */
    ULONG indegree[BOOT_MAX_SUBSYSTEMS];
    RtlZeroMemory(indegree, sizeof(indegree));
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        if (!g_Registry[i].InUse || !g_Registry[i].ShouldInit) continue;
        ULONG d = 0;
        for (ULONG k = 0; k < g_Registry[i].DepCount; k++) {
            const CHAR *depName = g_Registry[i].Deps[k];
            BOOT_SUBSYSTEM_REC *dep = BootFindRec(depName);
            /* If the dep is registered and should init, count it as
             * a dependency we must satisfy. If the dep is registered
             * but NOT in this profile, that's still a dependency we
             * must satisfy - we expect the dep will be initialized
             * elsewhere (e.g. core HAL always runs). */
            if (dep) d++;
        }
        indegree[i] = d;
    }

    /* Process queue: nodes with indegree 0. Order within a phase is
     * the registration order. We prefer lower phase first so that
     * core drivers start before services and GUI. */
    ULONG emitted = 0;
    BOOLEAN progress = TRUE;
    while (progress && emitted < n) {
        progress = FALSE;
        /* Find a node with indegree 0 and the lowest phase. */
        ULONG bestIdx = (ULONG)-1;
        ULONG bestPhase = (ULONG)-1;
        for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
            if (!g_Registry[i].InUse || !g_Registry[i].ShouldInit) continue;
            if (g_Registry[i].Initialized) continue;
            if (indegree[i] != 0) continue;
            if (g_Registry[i].Phase < bestPhase) {
                bestPhase = g_Registry[i].Phase;
                bestIdx = i;
            }
        }
        if (bestIdx == (ULONG)-1) break;
        /* Emit. */
        OutOrder[emitted++] = bestIdx;
        g_Registry[bestIdx].Initialized = TRUE;  /* mark so we don't re-emit */
        progress = TRUE;
        /* Decrement indegree of anything that depended on us. */
        for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
            if (!g_Registry[i].InUse || !g_Registry[i].ShouldInit) continue;
            if (g_Registry[i].Initialized) continue;
            for (ULONG k = 0; k < g_Registry[i].DepCount; k++) {
                if (g_Registry[i].Deps[k] &&
                    BootFindRec(g_Registry[i].Deps[k]) == &g_Registry[bestIdx]) {
                    indegree[i]--;
                    break;
                }
            }
        }
    }
    /* Reset Initialized flags so subsequent boots work. */
    for (ULONG i = 0; i < BOOT_MAX_SUBSYSTEMS; i++) {
        g_Registry[i].Initialized = FALSE;
    }
    if (emitted != n) {
        DbgPrint("BOOTREGISTRY: dependency cycle, %u/%u subsystems ordered\n", emitted, n);
        return STATUS_UNSUCCESSFUL;
    }
    *OutCount = emitted;
    return STATUS_SUCCESS;
}

/* ---- Initialize everything in dependency order ---- */

NTSTATUS NTAPI BootRegistryInit(VOID)
{
    RtlZeroMemory(g_Registry, sizeof(g_Registry));

    /* Build the registry from the boot profile + subsystems list. We
     * use a registration helper below so each subsystem is just one
     * BootRegister call. */
    extern NTSTATUS NTAPI MmInitSystemWrap(VOID);
    extern NTSTATUS NTAPI ExInitializePoolManager(VOID);
    extern NTSTATUS NTAPI ObInitSystem(VOID);
    extern NTSTATUS NTAPI IoInitSystem(VOID);
    extern NTSTATUS NTAPI IoInitNullDriver(VOID);
    extern NTSTATUS NTAPI CmInitSystem(VOID);
    extern NTSTATUS NTAPI SeInitSystem(VOID);
    extern NTSTATUS NTAPI FsInitSystem(VOID);
    extern NTSTATUS NTAPI LpcInitSystem(VOID);
    extern NTSTATUS NTAPI UsbInitSystem(VOID);
    extern NTSTATUS NTAPI GpuInitializeSubsystem(VOID);
    extern NTSTATUS NTAPI AhciInitSystem(VOID);
    extern NTSTATUS NTAPI PartitionScan(VOID);
    extern NTSTATUS NTAPI DriverEntry(VOID);
    extern NTSTATUS NTAPI BootChainInit(VOID);
    extern NTSTATUS NTAPI OsInstallInit(VOID);
    extern NTSTATUS NTAPI ScmAutoStart(VOID);
    extern NTSTATUS NTAPI ScmInitSystem(VOID);
    extern NTSTATUS NTAPI RecycleBinInit(VOID);
    extern NTSTATUS NTAPI NetConnectionsInit(VOID);
    extern NTSTATUS NTAPI ComRegisterInit(VOID);
    extern NTSTATUS NTAPI SafeModeInit(VOID);
    extern NTSTATUS NTAPI BootCfgInit(VOID);
    extern NTSTATUS NTAPI DisplayTopologyInit(VOID);
    extern NTSTATUS NTAPI NsInit(VOID);
    extern NTSTATUS NTAPI EtwInit(VOID);
    extern NTSTATUS NTAPI ReliabilityInit(VOID);
    extern NTSTATUS NTAPI SpoolerInit(VOID);
    extern NTSTATUS NTAPI TouchInputInit(VOID);
    extern NTSTATUS NTAPI ImeInit(VOID);
    extern NTSTATUS NTAPI GamepadInit(VOID);
    extern NTSTATUS NTAPI MmcInit(VOID);
    extern NTSTATUS NTAPI AcpiInit(VOID);
    extern NTSTATUS NTAPI WmiInit(VOID);
    extern NTSTATUS NTAPI RpcInit(VOID);
    extern NTSTATUS NTAPI ReparseInit(VOID);
    extern NTSTATUS NTAPI VssInit(VOID);
    extern NTSTATUS NTAPI PnpInit(VOID);
    extern NTSTATUS NTAPI KdInit(ULONG, ULONG, ULONG_PTR);
    extern NTSTATUS NTAPI NotepadInit(VOID);
    extern NTSTATUS NTAPI CalculatorInit(VOID);
    extern NTSTATUS NTAPI TermInit(VOID);
    extern NTSTATUS NTAPI SafeUsbInit(VOID);
    extern NTSTATUS NTAPI QuotaInit(VOID);
    extern NTSTATUS NTAPI RoamingProfileInit(VOID);
    extern NTSTATUS NTAPI SyncInit(VOID);
    extern NTSTATUS NTAPI HidInit(VOID);
    extern NTSTATUS NTAPI TsfInit(VOID);
    extern NTSTATUS NTAPI MediaInit(VOID);
    extern NTSTATUS NTAPI InstInit(VOID);
    extern NTSTATUS NTAPI WineInit(VOID);
    extern NTSTATUS NTAPI WinebootInitModule(VOID);
    extern NTSTATUS NTAPI WinebootInitDefault(VOID);
    extern NTSTATUS NTAPI PropertiesInit(VOID);
    extern NTSTATUS NTAPI TaskmgrInit(VOID);
    extern NTSTATUS NTAPI TpmInit(VOID);
    extern NTSTATUS NTAPI SteamInputInit(VOID);
    extern NTSTATUS NTAPI RemapInit(VOID);
    extern NTSTATUS NTAPI TouchpadInit(VOID);
    extern NTSTATUS NTAPI ControllerNameInit(VOID);
    extern NTSTATUS NTAPI CcInitSystem(VOID);
    extern NTSTATUS NTAPI RtwInitSystem(VOID);

    /* ---- Dependency declarations ---- */
    static const CHAR *DepsMm[]   = { "HAL", "Ke" };
    static const CHAR *DepsEx[]   = { "Mm" };
    static const CHAR *DepsOb[]   = { "Ex" };
    static const CHAR *DepsIo[]   = { "Ob", "Mm" };
    static const CHAR *DepsCm[]   = { "Io" };
    static const CHAR *DepsSe[]   = { "Ob" };
    static const CHAR *DepsPs[]   = { "Ob", "Mm" };
    static const CHAR *DepsFs[]   = { "Io" };
    static const CHAR *DepsLpc[]  = { "Ob" };
    static const CHAR *DepsAhci[] = { "Io", "Partition" };
    static const CHAR *DepsPartition[] = { "Io" };
    static const CHAR *DepsGpu[]  = { "Io" };
    static const CHAR *DepsScm[]  = { "Ob", "Io" };
    static const CHAR *DepsNullDriver[] = { "Io" };
    static const CHAR *DepsCc[]   = { "Io", "Cm" };
    static const CHAR *DepsCcMore[] = { "Cc" };
    static const CHAR *DepsRecycleBin[] = { "Fs" };
    static const CHAR *DepsNetwork[] = { "Fs" };
    static const CHAR *DepsComreg[] = { "Fs" };
    static const CHAR *DepsSafeMode[] = { "Fs" };
    static const CHAR *DepsBootCfg[] = { "Fs" };
    static const CHAR *DepsDisplayTopology[] = { "Fs" };
    static const CHAR *DepsNamespace[] = { "Fs" };
    static const CHAR *DepsEtw[] = { "Fs" };
    static const CHAR *DepsReliability[] = { "Fs" };
    static const CHAR *DepsSpooler[] = { "Fs" };
    static const CHAR *DepsTouch[] = { "Fs" };
    static const CHAR *DepsIme[] = { "Fs" };
    static const CHAR *DepsGamepad[] = { "Fs" };
    static const CHAR *DepsMmc[] = { "Fs" };
    static const CHAR *DepsAcpi[] = { "Fs" };
    static const CHAR *DepsWmi[] = { "Fs" };
    static const CHAR *DepsRpc[] = { "Fs" };
    static const CHAR *DepsReparse[] = { "Fs" };
    static const CHAR *DepsVss[] = { "Fs" };
    static const CHAR *DepsPnp[] = { "Fs" };
    static const CHAR *DepsKd[] = { "Fs" };
    static const CHAR *DepsApps[] = { "Fs" };
    static const CHAR *DepsSafeUsb[] = { "Fs" };
    static const CHAR *DepsQuota[] = { "Fs" };
    static const CHAR *DepsProfile[] = { "Fs" };
    static const CHAR *DepsSync[] = { "Fs" };
    static const CHAR *DepsHid[] = { "Fs" };
    static const CHAR *DepsTsf[] = { "Fs" };
    static const CHAR *DepsMedia[] = { "Fs" };
    static const CHAR *DepsInst[] = { "Fs" };
    static const CHAR *DepsOsInst[] = { "Fs", "Ahci" };
    static const CHAR *DepsWine[] = { "Fs" };
    static const CHAR *DepsProperties[] = { "Fs" };
    static const CHAR *DepsTaskmgr[] = { "Fs" };
    static const CHAR *DepsTpm[] = { "Fs" };
    static const CHAR *DepsSteamInput[] = { "Fs" };
    static const CHAR *DepsCtrlName[] = { "Fs" };
    static const CHAR *DepsUsb[] = { "Io" };
    static const CHAR *DepsWin32k[] = { "Fs" };  /* framebuffer+keyboard init'd outside */
    static const CHAR *DepsBootChain[] = { "Win32k", "Ps" };
    static const CHAR *DepsRtw[] = { "Io" };

    /* ---- Init-function wrappers for subsystems whose real Init has
     *      a non-(VOID) signature. ---- */
    NTSTATUS NTAPI WrapCcInit(VOID) { CcInitSystem(); return STATUS_SUCCESS; }
    NTSTATUS NTAPI WrapKdInit(VOID) { return KdInit(6, 0, 0x100000); }

    /* ---- Phase 0: Core HAL/Kernel (placeholder; HAL is already running). ---- */
    BootRegisterSubsystemEx("HAL", NULL, NULL, 0, 0);

    /* ---- Phase 0: Memory + Object Manager + Pool ---- */
    BootRegisterSubsystemEx("Mm",         MmInitSystemWrap, DepsMm, 2, 0);
    BootRegisterSubsystemEx("Ex",         ExInitializePoolManager, DepsEx, 1, 0);
    BootRegisterSubsystemEx("Ob",         ObInitSystem, DepsOb, 1, 0);

    /* ---- Phase 1: I/O + Drivers + Services ---- */
    BootRegisterSubsystemEx("Io",         IoInitSystem, DepsIo, 2, 1);
    BootRegisterSubsystemEx("NullDriver", IoInitNullDriver, DepsNullDriver, 1, 1);
    BootRegisterSubsystemEx("Cm",         CmInitSystem, DepsCm, 1, 1);
    BootRegisterSubsystemEx("Cc",         WrapCcInit, DepsCc, 1, 1);
    BootRegisterSubsystemEx("Se",         SeInitSystem, DepsSe, 1, 1);
    BootRegisterSubsystemEx("Ps",         PsInitSystem, DepsPs, 2, 1);
    BootRegisterSubsystemEx("Scm",        ScmInitSystem, DepsScm, 1, 1);
    BootRegisterSubsystemEx("Lpc",        LpcInitSystem, DepsLpc, 1, 1);
    BootRegisterSubsystemEx("Usb",        UsbInitSystem, DepsUsb, 1, 1);
    BootRegisterSubsystemEx("Gpu",        GpuInitializeSubsystem, DepsGpu, 1, 1);
    BootRegisterSubsystemEx("Ahci",       AhciInitSystem, DepsAhci, 2, 1);
    BootRegisterSubsystemEx("Partition",  PartitionScan, DepsPartition, 1, 1);
    BootRegisterSubsystemEx("Rtw",        RtwInitSystem, DepsRtw, 1, 1);

    /* ---- Phase 1: Filesystem + dependents ---- */
    BootRegisterSubsystemEx("Fs",          FsInitSystem, DepsFs, 1, 1);
    BootRegisterSubsystemEx("RecycleBin",  RecycleBinInit, DepsRecycleBin, 1, 2);
    BootRegisterSubsystemEx("Network",     NetConnectionsInit, DepsNetwork, 1, 2);
    BootRegisterSubsystemEx("Comreg",      ComRegisterInit, DepsComreg, 1, 2);
    BootRegisterSubsystemEx("SafeMode",    SafeModeInit, DepsSafeMode, 1, 2);
    BootRegisterSubsystemEx("BootCfg",     BootCfgInit, DepsBootCfg, 1, 2);
    BootRegisterSubsystemEx("DisplayTopology", DisplayTopologyInit, DepsDisplayTopology, 1, 2);
    BootRegisterSubsystemEx("Namespace",   NsInit, DepsNamespace, 1, 2);
    BootRegisterSubsystemEx("Etw",         EtwInit, DepsEtw, 1, 2);
    BootRegisterSubsystemEx("Reliability", ReliabilityInit, DepsReliability, 1, 2);
    BootRegisterSubsystemEx("Spooler",     SpoolerInit, DepsSpooler, 1, 2);
    BootRegisterSubsystemEx("Touch",       TouchInputInit, DepsTouch, 1, 2);
    BootRegisterSubsystemEx("Ime",         ImeInit, DepsIme, 1, 2);
    BootRegisterSubsystemEx("Gamepad",     GamepadInit, DepsGamepad, 1, 2);
    BootRegisterSubsystemEx("Mmc",         MmcInit, DepsMmc, 1, 2);
    BootRegisterSubsystemEx("Acpi",        AcpiInit, DepsAcpi, 1, 2);
    BootRegisterSubsystemEx("Wmi",         WmiInit, DepsWmi, 1, 2);
    BootRegisterSubsystemEx("Rpc",         RpcInit, DepsRpc, 1, 2);
    BootRegisterSubsystemEx("Reparse",     ReparseInit, DepsReparse, 1, 2);
    BootRegisterSubsystemEx("Vss",         VssInit, DepsVss, 1, 2);
    BootRegisterSubsystemEx("Pnp",         PnpInit, DepsPnp, 1, 2);
    BootRegisterSubsystemEx("Kd",          WrapKdInit, DepsKd, 1, 2);
    BootRegisterSubsystemEx("Apps",        NotepadInit, DepsApps, 1, 2);
    BootRegisterSubsystemEx("SafeUsb",     SafeUsbInit, DepsSafeUsb, 1, 2);
    BootRegisterSubsystemEx("Quota",       QuotaInit, DepsQuota, 1, 2);
    BootRegisterSubsystemEx("Profile",     RoamingProfileInit, DepsProfile, 1, 2);
    BootRegisterSubsystemEx("Sync",        SyncInit, DepsSync, 1, 2);
    BootRegisterSubsystemEx("Hid",         HidInit, DepsHid, 1, 2);
    BootRegisterSubsystemEx("Tsf",         TsfInit, DepsTsf, 1, 2);
    BootRegisterSubsystemEx("Media",       MediaInit, DepsMedia, 1, 2);
    BootRegisterSubsystemEx("Installer",   InstInit, DepsInst, 1, 2);
    BootRegisterSubsystemEx("OsInstall",   OsInstallInit, DepsOsInst, 2, 2);
    BootRegisterSubsystemEx("Wine",        WineInit, DepsWine, 1, 2);
    BootRegisterSubsystemEx("Properties",  PropertiesInit, DepsProperties, 1, 2);
    BootRegisterSubsystemEx("Taskmgr",     TaskmgrInit, DepsTaskmgr, 1, 2);
    BootRegisterSubsystemEx("Tpm",         TpmInit, DepsTpm, 1, 2);
    BootRegisterSubsystemEx("SteamInput",  SteamInputInit, DepsSteamInput, 1, 2);
    BootRegisterSubsystemEx("CtrlName",    ControllerNameInit, DepsCtrlName, 1, 2);

    /* ---- Phase 3: GUI + Boot chain ---- */
    BootRegisterSubsystemEx("Win32k",     DriverEntry, DepsWin32k, 1, 3);
    BootRegisterSubsystemEx("BootChain",  BootChainInit, DepsBootChain, 2, 3);

    /* Mark profile-allowed status. */
    BootMarkShouldInit();

    /* Topo sort. */
    ULONG order[BOOT_MAX_SUBSYSTEMS];
    ULONG orderCount = 0;
    NTSTATUS s = BootTopoSort(order, &orderCount);
    if (!NT_SUCCESS(s)) {
        DbgPrint("BOOTREGISTRY: topo sort failed: 0x%x\n", s);
        return s;
    }
    DbgPrint("BOOTREGISTRY: %u subsystems in dependency order:\n", orderCount);
    for (ULONG i = 0; i < orderCount; i++) {
        BOOT_SUBSYSTEM_REC *r = &g_Registry[order[i]];
        DbgPrint("  [%u] %s (phase %s)\n", i, r->Name, g_PhaseNames[r->Phase]);
    }

    /* Run them. We don't initialize HAL/Ke here because they were
     * done before the registry was even built (they need to be ready
     * for DbgPrint to work). Anything else in order gets its Init
     * called. */
    for (ULONG i = 0; i < orderCount; i++) {
        BOOT_SUBSYSTEM_REC *r = &g_Registry[order[i]];
        if (!r->Init) continue;  /* HAL placeholder */
        if (r->Init()) {
            DbgPrint("BOOTREGISTRY: %s init FAILED\n", r->Name);
            return STATUS_UNSUCCESSFUL;
        }
        r->Initialized = TRUE;
    }
    return STATUS_SUCCESS;
}

/* ---- Lookup APIs (the same surface BootProfileAllowsSubsystem
 * provides, but the registry-driven version). ---- */
