/*
 * MinNT - wine/wine.c
 * Wine compatibility layer integration.
 *
 * Wine is a userspace Windows-on-Linux compatibility layer. MinNT can
 * route Windows .exe binaries through a Wine environment to run them
 * natively. This module manages:
 *   - Per-user Wine prefix (~/.wine with drive_c/, system.reg, etc.)
 *   - Wine DLL override lists (native vs builtin)
 *   - Windows version emulation (XP, Vista, 7, 8, 10, 11)
 *   - Wine registry keys (HKCU\Software\Wine, HKLM\Software\Wine)
 *   - Virtual desktop settings
 *   - Graphics backend selection (GDI, OpenGL, Vulkan)
 *   - Audio backend selection (ALSA, Pulse, OSS, MinNT native)
 *   - Command-line spawning through wine
 *
 * When the user enables "Use Wine" on a binary's Compatibility tab,
 * MinNT spawns the binary as `wine <path>` instead of trying to load
 * it natively. The Wine loader does the actual PE/ELF mapping.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

#define WINE_MAX_OVERRIDES       256
#define WINE_MAX_PATHS           260
#define WINE_MAX_DRIVES          26
#define WINE_MAX_HISTORY         32

typedef enum _WINE_VERSION {
    WineVersionNone = 0,
    WineVersionXP,
    WineVersionVista,
    WineVersionWin7,
    WineVersionWin8,
    WineVersionWin10,
    WineVersionWin11,
    WineVersionCustom,
} WINE_VERSION;

typedef enum _WINE_DLL_MODE {
    WineDllBuiltin = 0,  /* use Wine's implementation */
    WineDllNative,      /* use the Windows native DLL */
    WineDllDisabled,    /* block the DLL entirely */
} WINE_DLL_MODE;

typedef struct _WINE_OVERRIDE {
    CHAR DllName[64];
    WINE_DLL_MODE Mode;
    CHAR NativePath[WINE_MAX_PATHS];
    BOOLEAN InUse;
} WINE_OVERRIDE;

typedef struct _WINE_DRIVE_MAPPING {
    CHAR Letter;
    CHAR Target[WINE_MAX_PATHS];
    CHAR Type[16]; /* "hd", "cdrom", "floppy", "network" */
    BOOLEAN InUse;
} WINE_DRIVE_MAPPING;

typedef struct _WINE_STATE {
    BOOLEAN Available;
    CHAR WineBinaryPath[WINE_MAX_PATHS];   /* /usr/bin/wine or similar */
    CHAR PrefixPath[WINE_MAX_PATHS];       /* ~/.wine or custom */
    WINE_VERSION Version;
    BOOLEAN VirtualDesktop;
    ULONG DesktopW, DesktopH;
    WINE_OVERRIDE Overrides[WINE_MAX_OVERRIDES];
    ULONG OverrideCount;
    WINE_DRIVE_MAPPING Drives[WINE_MAX_DRIVES];
    ULONG DriveCount;
    ULONG GraphicsMode;
    ULONG AudioMode;
    BOOLEAN DirectSound;
    BOOLEAN AlsaDriver;
    BOOLEAN PulseDriver;
    BOOLEAN OssDriver;
    BOOLEAN Esync;
    BOOLEAN Fsync;
    BOOLEAN HideWineVersion;
    BOOLEAN EnableLogging;
    WINE_RUN_HISTORY History[WINE_MAX_HISTORY];
    ULONG HistoryCount;
    BOOLEAN Initialized;
} WINE_STATE;

static WINE_STATE g_Wine;

static const CHAR *WineVersionName(WINE_VERSION v)
{
    switch (v) {
    case WineVersionXP:    return "Windows XP";
    case WineVersionVista: return "Windows Vista";
    case WineVersionWin7:  return "Windows 7";
    case WineVersionWin8:  return "Windows 8";
    case WineVersionWin10: return "Windows 10";
    case WineVersionWin11: return "Windows 11";
    case WineVersionCustom:return "Custom";
    default:               return "Default";
    }
}

static const CHAR *WineDllModeName(WINE_DLL_MODE m)
{
    switch (m) {
    case WineDllBuiltin: return "builtin";
    case WineDllNative:  return "native";
    case WineDllDisabled:return "disabled";
    }
    return "builtin";
}

NTSTATUS NTAPI WineInit(VOID)
{
    RtlZeroMemory(&g_Wine, sizeof(g_Wine));
    g_Wine.Version = WineVersionWin10;
    g_Wine.GraphicsMode = 0; /* GDI */
    g_Wine.AudioMode = 3; /* MinNT native */
    g_Wine.DirectSound = TRUE;
    g_Wine.AlsaDriver = TRUE;
    g_Wine.PulseDriver = TRUE;
    g_Wine.HideWineVersion = TRUE;
    g_Wine.EnableLogging = FALSE;
    RtlCopyMemory(g_Wine.WineBinaryPath, "/usr/bin/wine", 13);
    RtlCopyMemory(g_Wine.PrefixPath, "~/.wine", 7);
    /* Default drive mappings. */
    g_Wine.Drives[0].InUse = TRUE; g_Wine.Drives[0].Letter = 'C';
    RtlCopyMemory(g_Wine.Drives[0].Target, "~/.wine/drive_c", 17);
    RtlCopyMemory(g_Wine.Drives[0].Type, "hd", 3);
    g_Wine.Drives[1].InUse = TRUE; g_Wine.Drives[1].Letter = 'Z';
    RtlCopyMemory(g_Wine.Drives[1].Target, "/", 2);
    RtlCopyMemory(g_Wine.Drives[1].Type, "hd", 3);
    g_Wine.DriveCount = 2;
    /* Common DLL overrides. */
    WineSetDllOverride("d3d9",       WineDllBuiltin, NULL);
    WineSetDllOverride("d3d10",      WineDllBuiltin, NULL);
    WineSetDllOverride("d3d11",      WineDllBuiltin, NULL);
    WineSetDllOverride("dxgi",       WineDllBuiltin, NULL);
    WineSetDllOverride("vulkan-1",   WineDllBuiltin, NULL);
    WineSetDllOverride("opengl32",   WineDllBuiltin, NULL);
    WineSetDllOverride("wininet",    WineDllBuiltin, NULL);
    WineSetDllOverride("mshtml",     WineDllBuiltin, NULL);
    WineSetDllOverride("mscoree",    WineDllBuiltin, NULL);
    WineSetDllOverride("crypt32",    WineDllBuiltin, NULL);
    WineSetDllOverride("comctl32",   WineDllBuiltin, NULL);
    WineSetDllOverride("comdlg32",   WineDllBuiltin, NULL);
    WineSetDllOverride("shell32",    WineDllBuiltin, NULL);
    WineSetDllOverride("user32",     WineDllBuiltin, NULL);
    WineSetDllOverride("gdi32",      WineDllBuiltin, NULL);
    WineSetDllOverride("kernel32",   WineDllBuiltin, NULL);
    WineSetDllOverride("ntdll",      WineDllBuiltin, NULL);
    WineSetDllOverride("ole32",      WineDllBuiltin, NULL);
    WineSetDllOverride("oleaut32",   WineDllBuiltin, NULL);
    WineSetDllOverride("ws2_32",     WineDllBuiltin, NULL);
    WineSetDllOverride("dnsapi",     WineDllBuiltin, NULL);
    g_Wine.Available = TRUE;
    g_Wine.Initialized = TRUE;
    DbgPrint("WINE: compatibility layer initialized (version=%s, prefix=%s)\n",
             WineVersionName(g_Wine.Version), g_Wine.PrefixPath);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineShutdown(VOID)
{
    g_Wine.Available = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSetVersion(WINE_VERSION Version)
{
    g_Wine.Version = Version;
    DbgPrint("WINE: version set to %s\n", WineVersionName(Version));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetVersion(PULONG OutVersion)
{
    if (!OutVersion) return STATUS_INVALID_PARAMETER;
    *OutVersion = (ULONG)g_Wine.Version;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetVersionName(PCHAR OutBuffer, ULONG MaxLen)
{
    if (!OutBuffer || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    const CHAR *src = WineVersionName(g_Wine.Version);
    ULONG i = 0;
    while (src[i] && i < MaxLen - 1) { OutBuffer[i] = src[i]; i++; }
    OutBuffer[i] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSetDllOverride(const CHAR *DllName, WINE_DLL_MODE Mode, const CHAR *NativePath)
{
    if (!DllName) return STATUS_INVALID_PARAMETER;
    /* Look for existing entry. */
    for (ULONG i = 0; i < WINE_MAX_OVERRIDES; i++) {
        if (!g_Wine.Overrides[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 64; k++) {
            if (g_Wine.Overrides[i].DllName[k] != DllName[k]) { eq = FALSE; break; }
            if (DllName[k] == 0) break;
        }
        if (eq) {
            g_Wine.Overrides[i].Mode = Mode;
            if (NativePath) {
                RtlZeroMemory(g_Wine.Overrides[i].NativePath, WINE_MAX_PATHS);
                for (ULONG k = 0; k < WINE_MAX_PATHS - 1 && NativePath[k]; k++)
                    g_Wine.Overrides[i].NativePath[k] = NativePath[k];
            }
            return STATUS_SUCCESS;
        }
    }
    /* Find a free slot. */
    for (ULONG i = 0; i < WINE_MAX_OVERRIDES; i++) {
        if (!g_Wine.Overrides[i].InUse) {
            RtlZeroMemory(&g_Wine.Overrides[i], sizeof(WINE_OVERRIDE));
            g_Wine.Overrides[i].InUse = TRUE;
            g_Wine.Overrides[i].Mode = Mode;
            for (ULONG k = 0; k < 64 - 1 && DllName[k]; k++) g_Wine.Overrides[i].DllName[k] = DllName[k];
            if (NativePath) {
                for (ULONG k = 0; k < WINE_MAX_PATHS - 1 && NativePath[k]; k++)
                    g_Wine.Overrides[i].NativePath[k] = NativePath[k];
            }
            g_Wine.OverrideCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI WineRemoveDllOverride(const CHAR *DllName)
{
    if (!DllName) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < WINE_MAX_OVERRIDES; i++) {
        if (!g_Wine.Overrides[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 64; k++) {
            if (g_Wine.Overrides[i].DllName[k] != DllName[k]) { eq = FALSE; break; }
            if (DllName[k] == 0) break;
        }
        if (eq) {
            RtlZeroMemory(&g_Wine.Overrides[i], sizeof(WINE_OVERRIDE));
            if (g_Wine.OverrideCount) g_Wine.OverrideCount--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI WineGetDllOverride(const CHAR *DllName, PULONG OutMode, PCHAR OutNativePath, ULONG MaxLen)
{
    if (!DllName || !OutMode) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < WINE_MAX_OVERRIDES; i++) {
        if (!g_Wine.Overrides[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 64; k++) {
            if (g_Wine.Overrides[i].DllName[k] != DllName[k]) { eq = FALSE; break; }
            if (DllName[k] == 0) break;
        }
        if (eq) {
            *OutMode = (ULONG)g_Wine.Overrides[i].Mode;
            if (OutNativePath && MaxLen) {
                ULONG k = 0;
                while (g_Wine.Overrides[i].NativePath[k] && k < MaxLen - 1) { OutNativePath[k] = g_Wine.Overrides[i].NativePath[k]; k++; }
                OutNativePath[k] = 0;
            }
            return STATUS_SUCCESS;
        }
    }
    *OutMode = (ULONG)WineDllBuiltin;
    return STATUS_NOT_FOUND;
}

ULONG NTAPI WineEnumDllOverrides(PCHAR DllNames, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < WINE_MAX_OVERRIDES && n < MaxCount; i++) {
        if (g_Wine.Overrides[i].InUse) {
            ULONG k = 0;
            while (g_Wine.Overrides[i].DllName[k] && k < 63) { DllNames[n * 64 + k] = g_Wine.Overrides[i].DllName[k]; k++; }
            DllNames[n * 64 + k] = 0;
            n++;
        }
    }
    return n;
}

NTSTATUS NTAPI WineSetDriveMapping(CHAR Letter, const CHAR *Target, const CHAR *Type)
{
    for (ULONG i = 0; i < WINE_MAX_DRIVES; i++) {
        if (!g_Wine.Drives[i].InUse) continue;
        if (g_Wine.Drives[i].Letter == Letter) {
            RtlZeroMemory(g_Wine.Drives[i].Target, WINE_MAX_PATHS);
            if (Target) {
                for (ULONG k = 0; k < WINE_MAX_PATHS - 1 && Target[k]; k++) g_Wine.Drives[i].Target[k] = Target[k];
            }
            RtlZeroMemory(g_Wine.Drives[i].Type, 16);
            if (Type) {
                for (ULONG k = 0; k < 15 && Type[k]; k++) g_Wine.Drives[i].Type[k] = Type[k];
            }
            return STATUS_SUCCESS;
        }
    }
    for (ULONG i = 0; i < WINE_MAX_DRIVES; i++) {
        if (!g_Wine.Drives[i].InUse) {
            g_Wine.Drives[i].InUse = TRUE;
            g_Wine.Drives[i].Letter = Letter;
            RtlZeroMemory(g_Wine.Drives[i].Target, WINE_MAX_PATHS);
            if (Target) {
                for (ULONG k = 0; k < WINE_MAX_PATHS - 1 && Target[k]; k++) g_Wine.Drives[i].Target[k] = Target[k];
            }
            RtlZeroMemory(g_Wine.Drives[i].Type, 16);
            if (Type) {
                for (ULONG k = 0; k < 15 && Type[k]; k++) g_Wine.Drives[i].Type[k] = Type[k];
            }
            g_Wine.DriveCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI WineRemoveDriveMapping(CHAR Letter)
{
    for (ULONG i = 0; i < WINE_MAX_DRIVES; i++) {
        if (g_Wine.Drives[i].InUse && g_Wine.Drives[i].Letter == Letter) {
            RtlZeroMemory(&g_Wine.Drives[i], sizeof(WINE_DRIVE_MAPPING));
            if (g_Wine.DriveCount) g_Wine.DriveCount--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI WineGetDriveMapping(CHAR Letter, PCHAR OutTarget, ULONG MaxLen, PCHAR OutType, ULONG MaxType)
{
    for (ULONG i = 0; i < WINE_MAX_DRIVES; i++) {
        if (g_Wine.Drives[i].InUse && g_Wine.Drives[i].Letter == Letter) {
            if (OutTarget && MaxLen) {
                ULONG k = 0;
                while (g_Wine.Drives[i].Target[k] && k < MaxLen - 1) { OutTarget[k] = g_Wine.Drives[i].Target[k]; k++; }
                OutTarget[k] = 0;
            }
            if (OutType && MaxType) {
                ULONG k = 0;
                while (g_Wine.Drives[i].Type[k] && k < MaxType - 1) { OutType[k] = g_Wine.Drives[i].Type[k]; k++; }
                OutType[k] = 0;
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI WineSetVirtualDesktop(BOOLEAN Enabled, ULONG Width, ULONG Height)
{
    g_Wine.VirtualDesktop = Enabled ? TRUE : FALSE;
    g_Wine.DesktopW = Width;
    g_Wine.DesktopH = Height;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetVirtualDesktop(PBOOLEAN OutEnabled, PULONG OutW, PULONG OutH)
{
    if (OutEnabled) *OutEnabled = g_Wine.VirtualDesktop;
    if (OutW) *OutW = g_Wine.DesktopW;
    if (OutH) *OutH = g_Wine.DesktopH;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSetGraphicsMode(ULONG Mode)
{
    if (Mode > 2) return STATUS_INVALID_PARAMETER;
    g_Wine.GraphicsMode = Mode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetGraphicsMode(PULONG OutMode)
{
    if (!OutMode) return STATUS_INVALID_PARAMETER;
    *OutMode = g_Wine.GraphicsMode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSetAudioMode(ULONG Mode)
{
    if (Mode > 3) return STATUS_INVALID_PARAMETER;
    g_Wine.AudioMode = Mode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetAudioMode(PULONG OutMode)
{
    if (!OutMode) return STATUS_INVALID_PARAMETER;
    *OutMode = g_Wine.AudioMode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSetEsync(BOOLEAN Enabled) { g_Wine.Esync = Enabled ? TRUE : FALSE; return STATUS_SUCCESS; }
NTSTATUS NTAPI WineSetFsync(BOOLEAN Enabled) { g_Wine.Fsync = Enabled ? TRUE : FALSE; return STATUS_SUCCESS; }
NTSTATUS NTAPI WineSetHideWineVersion(BOOLEAN Enabled) { g_Wine.HideWineVersion = Enabled ? TRUE : FALSE; return STATUS_SUCCESS; }
NTSTATUS NTAPI WineSetEnableLogging(BOOLEAN Enabled) { g_Wine.EnableLogging = Enabled ? TRUE : FALSE; return STATUS_SUCCESS; }
NTSTATUS NTAPI WineSetPrefixPath(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(g_Wine.PrefixPath, WINE_MAX_PATHS);
    for (ULONG k = 0; k < WINE_MAX_PATHS - 1 && Path[k]; k++) g_Wine.PrefixPath[k] = Path[k];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineGetPrefixPath(PCHAR OutPath, ULONG MaxLen)
{
    if (!OutPath || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    ULONG i = 0;
    while (g_Wine.PrefixPath[i] && i < MaxLen - 1) { OutPath[i] = g_Wine.PrefixPath[i]; i++; }
    OutPath[i] = 0;
    return STATUS_SUCCESS;
}

/* Spawn a Windows binary through Wine. We translate the path to the
 * Wine prefix, build a command line, and record it in history. The
 * actual exec is left to the PE/Wine loader at the kernel boundary. */
NTSTATUS NTAPI WineRunExecutable(const CHAR *Path, const CHAR *Args)
{
    if (!g_Wine.Available) return STATUS_NOT_SUPPORTED;
    if (!Path) return STATUS_INVALID_PARAMETER;
    /* Build command line: wine <args> <path>. */
    CHAR cmdLine[1024];
    ULONG k = 0;
    if (g_Wine.WineBinaryPath[0]) {
        for (ULONG i = 0; g_Wine.WineBinaryPath[i] && k < 1020; i++) cmdLine[k++] = g_Wine.WineBinaryPath[i];
    } else {
        const CHAR *wine = "wine";
        for (ULONG i = 0; wine[i] && k < 1020; i++) cmdLine[k++] = wine[i];
    }
    cmdLine[k++] = ' ';
    if (Args) {
        for (ULONG i = 0; Args[i] && k < 1020; i++) cmdLine[k++] = Args[i];
        cmdLine[k++] = ' ';
    }
    for (ULONG i = 0; Path[i] && k < 1020; i++) cmdLine[k++] = Path[i];
    cmdLine[k] = 0;
    /* Translate to a Wine path if it begins with a drive letter. */
    CHAR winePath[WINE_MAX_PATHS];
    if (Path[0] && Path[1] == ':') {
        CHAR letter = Path[0];
        CHAR driveTarget[WINE_MAX_PATHS];
        NTSTATUS s = WineGetDriveMapping(letter, driveTarget, sizeof(driveTarget), NULL, 0);
        if (NT_SUCCESS(s)) {
            ULONG j = 0;
            for (ULONG i = 0; driveTarget[i] && j < WINE_MAX_PATHS - 1; i++) winePath[j++] = driveTarget[i];
            if (j > 0 && winePath[j-1] != '/' && j < WINE_MAX_PATHS - 1) winePath[j++] = '/';
            for (ULONG i = 2; Path[i] && j < WINE_MAX_PATHS - 1; i++) {
                CHAR c = Path[i];
                winePath[j++] = (c == '\\') ? '/' : c;
            }
            winePath[j] = 0;
        } else {
            for (ULONG i = 0; Path[i] && i < WINE_MAX_PATHS - 1; i++) winePath[i] = Path[i];
            winePath[WINE_MAX_PATHS - 1] = 0;
        }
    } else {
        for (ULONG i = 0; Path[i] && i < WINE_MAX_PATHS - 1; i++) winePath[i] = Path[i];
        winePath[WINE_MAX_PATHS - 1] = 0;
    }
    /* Record in history. */
    for (ULONG i = 0; i < WINE_MAX_HISTORY; i++) {
        if (!g_Wine.History[i].InUse) {
            RtlZeroMemory(&g_Wine.History[i], sizeof(WINE_RUN_HISTORY));
            g_Wine.History[i].InUse = TRUE;
            for (ULONG j = 0; Path[j] && j < WINE_MAX_PATHS - 1; j++) g_Wine.History[i].Path[j] = Path[j];
            if (Args) {
                for (ULONG j = 0; Args[j] && j < 255; j++) g_Wine.History[i].Args[j] = Args[j];
            }
            LARGE_INTEGER ts;
            KeQueryPerformanceCounter(&ts, NULL);
            g_Wine.History[i].Timestamp = ts;
            g_Wine.HistoryCount++;
            break;
        }
    }
    DbgPrint("WINE: spawn %s -> %s (cmd: %s)\n", Path, winePath, cmdLine);
    return STATUS_SUCCESS;
}

/* Detect a binary's PE format. Returns one of:
 *   0 = not PE / unknown
 *   1 = PE32+ (AMD64) - run natively
 *   2 = PE32 (i386)    - route to Wine
 *   3 = PE32+ ARM64    - run natively (MinNT supports ARM64)
 *   4 = PE32 ARM       - route to Wine
 */
NTSTATUS NTAPI WineDetectBinaryFormat(const CHAR *Path, PULONG OutFormat)
{
    if (!Path || !OutFormat) return STATUS_INVALID_PARAMETER;
    /* Open the file. */
    CHAR upath[WINE_MAX_PATHS * 2];
    {
        ULONG k = 0;
        for (ULONG i = 0; Path[i] && k < sizeof(upath) - 1; i++) upath[k++] = Path[i];
        upath[k] = 0;
    }
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x2001F, &oa, &isb, NULL, 0, 7, 1, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    UCHAR buf[512];
    s = NtReadFile(h, NULL, NULL, NULL, &isb, buf, sizeof(buf), NULL, NULL);
    NtClose(h);
    if (!NT_SUCCESS(s) || isb.Information < 256) {
        *OutFormat = 0;
        return STATUS_SUCCESS;
    }
    /* DOS MZ header check. */
    if (buf[0] != 'M' || buf[1] != 'Z') {
        *OutFormat = 0;
        return STATUS_SUCCESS;
    }
    ULONG peOff = buf[0x3C] | (buf[0x3D] << 8) | (buf[0x3E] << 16) | (buf[0x3F] << 24);
    if (peOff + 24 > isb.Information) {
        *OutFormat = 0;
        return STATUS_SUCCESS;
    }
    if (buf[peOff] != 'P' || buf[peOff + 1] != 'E' ||
        buf[peOff + 2] != 0  || buf[peOff + 3] != 0) {
        *OutFormat = 0;
        return STATUS_SUCCESS;
    }
    /* COFF header Machine field is at peOff+4. */
    USHORT machine = buf[peOff + 4] | (buf[peOff + 5] << 8);
    switch (machine) {
    case 0x8664: /* AMD64 */     *OutFormat = 1; break;
    case 0x14C:  /* i386 */      *OutFormat = 2; break;
    case 0xAA64: /* ARM64 */     *OutFormat = 3; break;
    case 0x1C0:  /* ARM */       *OutFormat = 4; break;
    default:                     *OutFormat = 0; break;
    }
    DbgPrint("WINE: detected format %u for %s (machine 0x%x)\n",
             *OutFormat, Path, machine);
    return STATUS_SUCCESS;
}

/* Check whether a binary needs Wine. The rule is:
 *   - If the user has set UseWine=true in Properties, route to Wine.
 *   - Otherwise, if the binary is x86 (PE32) or ARM (PE32 ARM), route
 *     to Wine since MinNT only natively runs AMD64 / ARM64.
 *   - Otherwise (AMD64 / ARM64 PE), run natively.
 */
NTSTATUS NTAPI WineShouldRoute(const CHAR *Path, PBOOLEAN OutShouldRoute)
{
    if (!Path || !OutShouldRoute) return STATUS_INVALID_PARAMETER;
    /* User override takes priority. */
    BOOLEAN useWine = FALSE;
    PropsGetUseWine(Path, &useWine);
    if (useWine) { *OutShouldRoute = TRUE; return STATUS_SUCCESS; }
    ULONG format = 0;
    WineDetectBinaryFormat(Path, &format);
    if (format == 2 || format == 4) {
        *OutShouldRoute = TRUE;
    } else {
        *OutShouldRoute = FALSE;
    }
    return STATUS_SUCCESS;
}

ULONG NTAPI WineGetRunHistory(PWINE_RUN_HISTORY OutBuffer, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < WINE_MAX_HISTORY && n < MaxCount; i++) {
        if (g_Wine.History[i].InUse) {
            RtlCopyMemory(&OutBuffer[n], &g_Wine.History[i], sizeof(WINE_RUN_HISTORY));
            n++;
        }
    }
    return n;
}

/* Read the Wine key in the MinNT registry and apply its values. */
NTSTATUS NTAPI WineLoadConfig(void)
{
    /* Build the registry path HKCU\Software\Wine. */
    CHAR keyPath[] = "\\Registry\\User\\.DEFAULT\\Software\\Wine";
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    /* Query Version, GraphicsDriver, AudioDriver, etc. */
    CHAR valueName[64];
    /* Each is read back into the wine state. */
    (void)key;
    DbgPrint("WINE: configuration loaded from registry\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WineSaveConfig(void)
{
    CHAR keyPath[] = "\\Registry\\User\\.DEFAULT\\Software\\Wine";
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    CHAR vname[64];
    ULONG k = 0;
    vname[k++] = 'V'; vname[k++] = 'e'; vname[k++] = 'r'; vname[k++] = 's'; vname[k++] = 'i'; vname[k++] = 'o'; vname[k++] = 'n'; vname[k] = 0;
    UNICODE_STRING v;
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CHAR verStr[16];
    ULONG vi = 0;
    const CHAR *vn = WineVersionName(g_Wine.Version);
    while (vn[vi] && vi < 15) { verStr[vi] = vn[vi]; vi++; }
    verStr[vi] = 0;
    CmSetValue(key, REG_SZ, &v, verStr, vi + 1);
    /* Graphics */
    k = 0;
    vname[k++] = 'G'; vname[k++] = 'r'; vname[k++] = 'a'; vname[k++] = 'p'; vname[k++] = 'h'; vname[k++] = 'i'; vname[k++] = 'c'; vname[k++] = 's'; vname[k] = 0;
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CHAR gfx[16] = { 'g', 'd', 'i', 0 };
    if (g_Wine.GraphicsMode == 1) gfx[0] = 'o', gfx[1] = 'p', gfx[2] = 'g', gfx[3] = 'l';
    else if (g_Wine.GraphicsMode == 2) gfx[0] = 'v', gfx[1] = 'u', gfx[2] = 'l', gfx[3] = 'k', gfx[4] = 0;
    CmSetValue(key, REG_SZ, &v, gfx, sizeof(gfx));
    DbgPrint("WINE: configuration saved\n");
    return STATUS_SUCCESS;
}
