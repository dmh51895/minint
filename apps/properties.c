/*
 * MinNT - apps/properties.c
 * File properties dialog with Compatibility tab.
 *
 * Right-click on a file -> Properties. The dialog shows multiple
 * tabs: General, Compatibility, Version, Security, Details.
 *
 * The Compatibility tab lets the user mark a binary to be run
 * through Wine, set the emulated Windows version, edit per-DLL
 * overrides, configure virtual desktop, graphics, and audio
 * settings. The settings are stored under
 * HKCU\Software\MinNT\Compatibility\<path-hash>.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/cm.h>
#include <nt/ntdef.h>
#include <nt/ps.h>
#include <ndk/obfuncs.h>
#include <nt/io.h>
#include <nt/framework.h>

#define PROPS_MAX_HASH          32
#define PROPS_MAX_OVERRIDES     64
#define PROPS_MAX_PATH          260
#define PROPS_REG_PREFIX        "\\Registry\\User\\.DEFAULT\\Software\\MinNT\\Compatibility\\"

typedef enum _PROPS_TAB {
    PropsTabGeneral = 0,
    PropsTabCompatibility,
    PropsTabVersion,
    PropsTabSecurity,
    PropsTabDetails,
    PropsTabMax,
} PROPS_TAB;

typedef struct _PROPS_OVERRIDE {
    CHAR DllName[64];
    ULONG Mode;
    CHAR NativePath[PROPS_MAX_PATH];
    BOOLEAN InUse;
} PROPS_OVERRIDE;

typedef struct _PROPS_FILE {
    CHAR Path[PROPS_MAX_PATH];
    CHAR Hash[PROPS_MAX_HASH];
    UCHAR UseWine;
    ULONG WindowsVersion;       /* see WINE_VERSION */
    UCHAR VirtualDesktop;
    ULONG DesktopW, DesktopH;
    ULONG GraphicsMode;
    ULONG AudioMode;
    UCHAR DirectSound;
    UCHAR Esync, Fsync;
    PROPS_OVERRIDE Overrides[PROPS_MAX_OVERRIDES];
    ULONG OverrideCount;
    CHAR RunAsAdmin;
    CHAR DisableThemes;
    CHAR LowerMemMode;
    CHAR HighDpiAware;
    UCHAR InUse;
} PROPS_FILE;

static PROPS_FILE g_Props[8];

/* Compute a simple hash of the file path. */
static VOID PropsHash(const CHAR *Path, PCHAR OutHash)
{
    ULONG h = 5381;
    for (ULONG i = 0; Path[i]; i++) h = h * 33 + (ULONG)(UCHAR)Path[i];
    ULONG k = 0;
    CHAR digits[] = "0123456789ABCDEF";
    for (LONG shift = 28; shift >= 0; shift -= 4) {
        UCHAR nib = (h >> shift) & 0xF;
        OutHash[k++] = digits[nib];
    }
    OutHash[k] = 0;
}

static PROPS_FILE *PropsFind(const CHAR *Path)
{
    for (ULONG i = 0; i < 8; i++) {
        if (g_Props[i].InUse) {
            BOOLEAN eq = TRUE;
            for (ULONG k = 0; k < PROPS_MAX_PATH; k++) {
                if (g_Props[i].Path[k] != Path[k]) { eq = FALSE; break; }
                if (Path[k] == 0) break;
            }
            if (eq) return &g_Props[i];
        }
    }
    return NULL;
}

static PROPS_FILE *PropsAlloc(const CHAR *Path)
{
    for (ULONG i = 0; i < 8; i++) {
        if (!g_Props[i].InUse) {
            RtlZeroMemory(&g_Props[i], sizeof(PROPS_FILE));
            g_Props[i].InUse = 1;
            for (ULONG k = 0; k < PROPS_MAX_PATH - 1 && Path[k]; k++) g_Props[i].Path[k] = Path[k];
            PropsHash(Path, g_Props[i].Hash);
            /* Defaults. */
            g_Props[i].WindowsVersion = 0; /* unchanged */
            g_Props[i].VirtualDesktop = 0;
            g_Props[i].GraphicsMode = 0;
            g_Props[i].AudioMode = 3;
            g_Props[i].DirectSound = 1;
            return &g_Props[i];
        }
    }
    return NULL;
}

static VOID PropsBuildKey(PROPS_FILE *p, PCHAR Buffer, ULONG MaxLen)
{
    ULONG k = 0;
    const CHAR *prefix = PROPS_REG_PREFIX;
    for (ULONG i = 0; prefix[i] && k < MaxLen - 40; i++) Buffer[k++] = prefix[i];
    for (ULONG i = 0; p->Hash[i] && k < MaxLen - 1; i++) Buffer[k++] = p->Hash[i];
    Buffer[k] = 0;
}

static NTSTATUS PropsSave(PROPS_FILE *p)
{
    CHAR keyPath[PROPS_MAX_PATH];
    PropsBuildKey(p, keyPath, sizeof(keyPath));
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;

    CHAR vname[64];
    UNICODE_STRING v;
    ULONG val;

    /* UseWine */
    val = p->UseWine;
    RtlCopyMemory(vname, "UseWine\0", 8);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* WindowsVersion */
    val = p->WindowsVersion;
    RtlCopyMemory(vname, "WinVersion\0", 12);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* VirtualDesktop */
    val = p->VirtualDesktop;
    RtlCopyMemory(vname, "VirtualDesktop\0", 16);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* GraphicsMode */
    val = p->GraphicsMode;
    RtlCopyMemory(vname, "GraphicsMode\0", 14);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* AudioMode */
    val = p->AudioMode;
    RtlCopyMemory(vname, "AudioMode\0", 11);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* Esync / Fsync */
    val = p->Esync;
    RtlCopyMemory(vname, "Esync\0", 7);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    val = p->Fsync;
    RtlCopyMemory(vname, "Fsync\0", 7);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_DWORD, &v, &val, sizeof(val));
    /* Path (for lookup). */
    RtlCopyMemory(vname, "Path\0", 6);
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, REG_SZ, &v, p->Path, sizeof(p->Path));
    DbgPrint("PROPS: saved compatibility for %s (UseWine=%u)\n", p->Path, p->UseWine);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsOpen(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    PROPS_FILE *p = PropsFind(Path);
    if (!p) p = PropsAlloc(Path);
    if (!p) return STATUS_NO_MEMORY;
    PropsSave(p);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsSetUseWine(const CHAR *Path, BOOLEAN Enabled)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) p = PropsAlloc(Path);
    if (!p) return STATUS_NO_MEMORY;
    p->UseWine = Enabled ? 1 : 0;
    PropsSave(p);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsGetUseWine(const CHAR *Path, PBOOLEAN OutEnabled)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) { *OutEnabled = FALSE; return STATUS_NOT_FOUND; }
    *OutEnabled = p->UseWine ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsSetWindowsVersion(const CHAR *Path, ULONG Version)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) p = PropsAlloc(Path);
    if (!p) return STATUS_NO_MEMORY;
    p->WindowsVersion = Version;
    PropsSave(p);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsGetWindowsVersion(const CHAR *Path, PULONG OutVersion)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) { *OutVersion = 0; return STATUS_NOT_FOUND; }
    *OutVersion = p->WindowsVersion;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsSetVirtualDesktop(const CHAR *Path, BOOLEAN Enabled, ULONG Width, ULONG Height)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) p = PropsAlloc(Path);
    if (!p) return STATUS_NO_MEMORY;
    p->VirtualDesktop = Enabled ? 1 : 0;
    p->DesktopW = Width;
    p->DesktopH = Height;
    PropsSave(p);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsGetVirtualDesktop(const CHAR *Path, PBOOLEAN OutEnabled, PULONG OutW, PULONG OutH)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) { *OutEnabled = FALSE; *OutW = 0; *OutH = 0; return STATUS_NOT_FOUND; }
    if (OutEnabled) *OutEnabled = p->VirtualDesktop ? TRUE : FALSE;
    if (OutW) *OutW = p->DesktopW;
    if (OutH) *OutH = p->DesktopH;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PropsSetDllOverride(const CHAR *Path, const CHAR *DllName, ULONG Mode, const CHAR *NativePath)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) p = PropsAlloc(Path);
    if (!p) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < PROPS_MAX_OVERRIDES; i++) {
        if (!p->Overrides[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 64; k++) {
            if (p->Overrides[i].DllName[k] != DllName[k]) { eq = FALSE; break; }
            if (DllName[k] == 0) break;
        }
        if (eq) {
            p->Overrides[i].Mode = Mode;
            if (NativePath) {
                RtlZeroMemory(p->Overrides[i].NativePath, PROPS_MAX_PATH);
                for (ULONG k = 0; k < PROPS_MAX_PATH - 1 && NativePath[k]; k++) p->Overrides[i].NativePath[k] = NativePath[k];
            }
            return STATUS_SUCCESS;
        }
    }
    for (ULONG i = 0; i < PROPS_MAX_OVERRIDES; i++) {
        if (!p->Overrides[i].InUse) {
            p->Overrides[i].InUse = 1;
            p->Overrides[i].Mode = Mode;
            for (ULONG k = 0; k < 63 && DllName[k]; k++) p->Overrides[i].DllName[k] = DllName[k];
            if (NativePath) {
                for (ULONG k = 0; k < PROPS_MAX_PATH - 1 && NativePath[k]; k++) p->Overrides[i].NativePath[k] = NativePath[k];
            }
            p->OverrideCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI PropsRemoveDllOverride(const CHAR *Path, const CHAR *DllName)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) return STATUS_NOT_FOUND;
    for (ULONG i = 0; i < PROPS_MAX_OVERRIDES; i++) {
        if (!p->Overrides[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 64; k++) {
            if (p->Overrides[i].DllName[k] != DllName[k]) { eq = FALSE; break; }
            if (DllName[k] == 0) break;
        }
        if (eq) {
            RtlZeroMemory(&p->Overrides[i], sizeof(PROPS_OVERRIDE));
            if (p->OverrideCount) p->OverrideCount--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

ULONG NTAPI PropsEnumDllOverrides(const CHAR *Path, PCHAR Names, ULONG MaxCount)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) return 0;
    ULONG n = 0;
    for (ULONG i = 0; i < PROPS_MAX_OVERRIDES && n < MaxCount; i++) {
        if (p->Overrides[i].InUse) {
            ULONG k = 0;
            while (p->Overrides[i].DllName[k] && k < 63) { Names[n * 64 + k] = p->Overrides[i].DllName[k]; k++; }
            Names[n * 64 + k] = 0;
            n++;
        }
    }
    return n;
}

/* Apply compatibility settings for an executable. Called by the
 * shell/explorer before launching a binary through Wine. */
NTSTATUS NTAPI PropsApplyForExecutable(const CHAR *Path)
{
    PROPS_FILE *p = PropsFind(Path);
    if (!p) return STATUS_SUCCESS; /* no overrides => run natively */
    if (!p->UseWine) return STATUS_SUCCESS;
    /* Apply Wine version. */
    WineSetVersion(p->WindowsVersion);
    if (p->VirtualDesktop) WineSetVirtualDesktop(TRUE, p->DesktopW, p->DesktopH);
    WineSetGraphicsMode(p->GraphicsMode);
    WineSetAudioMode(p->AudioMode);
    WineSetEsync(p->Esync ? TRUE : FALSE);
    WineSetFsync(p->Fsync ? TRUE : FALSE);
    /* Apply per-DLL overrides. */
    for (ULONG i = 0; i < PROPS_MAX_OVERRIDES; i++) {
        if (p->Overrides[i].InUse) {
            WineSetDllOverride(p->Overrides[i].DllName, (ULONG)p->Overrides[i].Mode, p->Overrides[i].NativePath);
        }
    }
    DbgPrint("PROPS: applied Wine compatibility for %s (v=%u, virt=%u)\n",
             Path, p->WindowsVersion, p->VirtualDesktop);
    return STATUS_SUCCESS;
}

/* Spawn an executable, routing through Wine if its compatibility
 * settings have UseWine enabled, or if the binary is x86/ARM and
 * therefore needs Wine to run. */
NTSTATUS NTAPI PropsLaunch(const CHAR *Path, const CHAR *Args)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    BOOLEAN shouldRoute = FALSE;
    WineShouldRoute(Path, &shouldRoute);
    if (shouldRoute) {
        PropsApplyForExecutable(Path);
        return WineRunExecutable(Path, Args);
    }
    /* Otherwise: native spawn via PsCreateSystemProcess. */
    extern NTSTATUS NTAPI PsCreateSystemProcess(const CHAR *ImageName, PEPROCESS *OutProcess);
    PEPROCESS proc = NULL;
    return PsCreateSystemProcess(Path, &proc);
}

NTSTATUS NTAPI PropertiesInit(VOID)
{
    RtlZeroMemory(g_Props, sizeof(g_Props));
    DbgPrint("PROPERTIES: file properties + Wine compatibility tab initialized\n");
    return STATUS_SUCCESS;
}
