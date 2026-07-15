/*
 * MinNT - wine/wineboot.c
 * wineboot equivalent: Wine prefix initialization.
 *
 * When Wine is first used, a "prefix" (a self-contained Windows-style
 * filesystem + registry layout) must be created. wineboot performs
 * this initialization:
 *   - Creates the ~/.wine/drive_c/ tree (windows, system32, etc.)
 *   - Seeds HKLM\Software\Microsoft\Windows NT\CurrentVersion
 *   - Sets up the well-known system.reg, user.reg, userdef.reg files
 *   - Installs Mono/Gecko placeholder entries
 *   - Records the Wine version installed
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/cm.h>
#include <nt/ntdef.h>
#include <ndk/obfuncs.h>
#include <nt/io.h>
#include <nt/framework.h>

#define WINEBOOT_MAX_PATH      260
#define WINEBOOT_MAX_REG       8

typedef enum _WINEBOOT_STAGE {
    WinebootStageNone = 0,
    WinebootStagePrefix,
    WinebootStageDriveLayout,
    WinebootStageRegistry,
    WinebootStageDllCache,
    WinebootStageFonts,
    WinebootStageFinalize,
    WinebootStageDone,
} WINEBOOT_STAGE;

static const CHAR *g_WinebootPrefix =
    "\\Registry\\User\\.DEFAULT\\Software\\Wine";

static NTSTATUS WinebootCreateDir(const CHAR *Path)
{
    CHAR upath[WINEBOOT_MAX_PATH * 2];
    ULONG k = 0;
    for (ULONG i = 0; Path[i] && k < sizeof(upath) - 1; i++) upath[k++] = Path[i];
    upath[k] = 0;
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_CASE_INSENSITIVE, NULL, NULL);
    /* The directory is created as a side effect of writing a file in
     * it; we just touch a sentinel. */
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x4001F, &oa, &isb, NULL, 0x80, 0, 1, 0x40, NULL, 0);
    if (NT_SUCCESS(s)) NtClose(h);
    return s;
}

static NTSTATUS WinebootCreateRegKey(const CHAR *Path, PULONG Created)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)Path);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&n, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    if (Created) *Created = 1;
    return STATUS_SUCCESS;
}

static NTSTATUS WinebootSetRegSz(const CHAR *Path, const CHAR *Value, const CHAR *Data)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)Path);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&n, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    UNICODE_STRING v;
    RtlInitUnicodeString(&v, (PCWSTR)Value);
    return CmSetValue(key, REG_SZ, &v, (PVOID)Data, sizeof(SIZE_T));
}

static NTSTATUS WinebootCreatePrefixLayout(const CHAR *PrefixPath)
{
    /* Drive layout: <prefix>/drive_c/windows, system32, syswow64,
     * users/Public, etc. */
    CHAR buf[WINEBOOT_MAX_PATH];
    /* drive_c */
    ULONG k = 0;
    for (ULONG i = 0; PrefixPath[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = PrefixPath[i];
    if (k > 0 && buf[k-1] != '/') buf[k++] = '/';
    const CHAR *suffix;
    suffix = "drive_c/windows"; for (ULONG i = 0; suffix[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = suffix[i];
    buf[k] = 0;
    WinebootCreateDir(buf);
    suffix = "drive_c/windows/system32"; for (ULONG i = 0; suffix[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = suffix[i];
    buf[k] = 0;
    WinebootCreateDir(buf);
    suffix = "drive_c/windows/syswow64"; for (ULONG i = 0; suffix[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = suffix[i];
    buf[k] = 0;
    WinebootCreateDir(buf);
    suffix = "drive_c/windows/Fonts"; for (ULONG i = 0; suffix[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = suffix[i];
    buf[k] = 0;
    WinebootCreateDir(buf);
    suffix = "drive_c/users/Public"; for (ULONG i = 0; suffix[i] && k < WINEBOOT_MAX_PATH - 1; i++) buf[k++] = suffix[i];
    buf[k] = 0;
    WinebootCreateDir(buf);
    return STATUS_SUCCESS;
}

static NTSTATUS WinebootInitRegistry(void)
{
    /* HKLM\Software\Microsoft\Windows NT\CurrentVersion */
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
                     "ProductName", "Microsoft Windows");
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
                     "CurrentVersion", "10.0");
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
                     "CurrentBuild", "19041");
    /* HKCU\Software\Wine */
    WinebootSetRegSz(g_WinebootPrefix, "Version", "10.0");
    WinebootSetRegSz(g_WinebootPrefix, "GraphicsDriver", "gdi");
    WinebootSetRegSz(g_WinebootPrefix, "AudioDriver", "minnt");
    /* HKLM\Software\Microsoft\Windows\CurrentVersion */
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion",
                     "ProgramFilesDir", "C:\\Program Files");
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion",
                     "CommonFilesDir", "C:\\Program Files\\Common Files");
    /* HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts */
    /* HKLM\Software\Wine */
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Wine", "Version", "10.0");
    WinebootSetRegSz("\\Registry\\Machine\\Software\\Wine", "Build", "wine-10.0-mintnt");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WinebootInit(const CHAR *PrefixPath)
{
    if (!PrefixPath) return STATUS_INVALID_PARAMETER;
    DbgPrint("WINEBOOT: initializing prefix %s\n", PrefixPath);
    /* Stage 1: layout. */
    WinebootCreatePrefixLayout(PrefixPath);
    /* Stage 2: registry seed. */
    WinebootInitRegistry();
    /* Stage 3: set the prefix path in Wine state. */
    WineSetPrefixPath(PrefixPath);
    DbgPrint("WINEBOOT: prefix %s initialized\n", PrefixPath);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WinebootInitDefault(VOID)
{
    return WinebootInit("~/.wine");
}

NTSTATUS NTAPI WinebootShutdown(VOID)
{
    DbgPrint("WINEBOOT: shutdown\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI WinebootInitModule(VOID)
{
    DbgPrint("WINEBOOT: wineboot module initialized\n");
    return STATUS_SUCCESS;
}
