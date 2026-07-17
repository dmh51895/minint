/*
 * MinNT - win32k/file_picker.c
 * File picker dialog for selecting files.
 *
 * Used by Settings → Personalization → Browse wallpaper button
 * and other file selection needs.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/fs.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

#define FP_MAX_PATH        260
#define FP_MAX_ENTRIES     64
#define FP_DIALOG_W        600
#define FP_DIALOG_H        450

typedef struct _FP_ENTRY {
    CHAR Name[64];
    BOOLEAN IsDirectory;
    BOOLEAN InUse;
} FP_ENTRY;

static FP_ENTRY g_FpEntries[FP_MAX_ENTRIES];
static ULONG g_FpEntryCount = 0;
static CHAR g_FpCurrentDir[FP_MAX_PATH] = "C:\\";
static CHAR g_FpSelectedPath[FP_MAX_PATH] = {0};
static BOOLEAN g_FpDialogActive = FALSE;

/* List directory contents */
static NTSTATUS FpListDirectory(const CHAR *Path)
{
    g_FpEntryCount = 0;
    
    /* Add parent directory entry */
    if (Path[0] != 'C' || Path[2] != '\\' || Path[3] != 0) {
        FP_ENTRY *parent = &g_FpEntries[g_FpEntryCount++];
        RtlZeroMemory(parent, sizeof(FP_ENTRY));
        parent->IsDirectory = TRUE;
        parent->InUse = TRUE;
        ULONG k = 0;
        parent->Name[k++] = '.';
        parent->Name[k++] = '.';
        parent->Name[k] = 0;
    }
    
    /* Open directory */
    UNICODE_STRING upath;
    RtlInitUnicodeString(&upath, (PCWSTR)Path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &upath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x2001F, &oa, &isb, NULL, 0, 7, 1, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    
    /* Read directory entries (simplified) */
    UCHAR buf[4096];
    s = NtReadFile(h, NULL, NULL, NULL, &isb, buf, sizeof(buf), NULL, NULL);
    NtClose(h);
    
    if (!NT_SUCCESS(s)) return s;
    
    /* Parse directory entries (simplified) */
    /* In a real implementation, this would parse FAT32/NTFS directory structures */
    /* For now, add some sample entries */
    
    /* Add common directories */
    const CHAR *commonDirs[] = {
        "Windows", "Users", "Program Files", "System32", "Web", NULL
    };
    
    for (ULONG i = 0; commonDirs[i] && g_FpEntryCount < FP_MAX_ENTRIES; i++) {
        FP_ENTRY *entry = &g_FpEntries[g_FpEntryCount++];
        RtlZeroMemory(entry, sizeof(FP_ENTRY));
        entry->IsDirectory = TRUE;
        entry->InUse = TRUE;
        ULONG k = 0;
        while (commonDirs[i][k] && k < 63) { entry->Name[k] = commonDirs[i][k]; k++; }
        entry->Name[k] = 0;
    }
    
    /* Add common files */
    const CHAR *commonFiles[] = {
        "windows_xp_bliss-wide.jpg", "wallpaper.bmp", "desktop.png", NULL
    };
    
    for (ULONG i = 0; commonFiles[i] && g_FpEntryCount < FP_MAX_ENTRIES; i++) {
        FP_ENTRY *entry = &g_FpEntries[g_FpEntryCount++];
        RtlZeroMemory(entry, sizeof(FP_ENTRY));
        entry->IsDirectory = FALSE;
        entry->InUse = TRUE;
        ULONG k = 0;
        while (commonFiles[i][k] && k < 63) { entry->Name[k] = commonFiles[i][k]; k++; }
        entry->Name[k] = 0;
    }
    
    return STATUS_SUCCESS;
}

/* Draw file picker dialog */
NTSTATUS NTAPI FpDraw(VOID)
{
    if (!g_FpDialogActive) return STATUS_UNSUCCESSFUL;
    
    extern volatile ULONG *HalpFbGetBase(VOID);
    extern ULONG HalpFbGetWidth(VOID);
    extern ULONG HalpFbGetHeight(VOID);
    extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    extern VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    extern VOID HalpFbDrawString(ULONG X, ULONG Y, const CHAR *Str, ULONG Fg, ULONG Bg);
    
    ULONG fbWidth = HalpFbGetWidth();
    ULONG fbHeight = HalpFbGetHeight();
    
    ULONG dx = (fbWidth - FP_DIALOG_W) / 2;
    ULONG dy = (fbHeight - FP_DIALOG_H) / 2;
    
    /* Draw dialog background */
    HalpFbFillRect(dx, dy, FP_DIALOG_W, FP_DIALOG_H, 0x00F0F0F0);
    HalpFbDrawRect(dx, dy, FP_DIALOG_W, FP_DIALOG_H, 0x00000000);
    
    /* Draw title bar */
    HalpFbFillRect(dx, dy, FP_DIALOG_W, 24, 0x00316AC5);
    HalpFbDrawString(dx + 8, dy + 4, "Select Wallpaper", 0x00FFFFFF, 0x00316AC5);
    
    /* Draw current path */
    HalpFbDrawString(dx + 8, dy + 30, "Path:", 0x00000000, 0x00F0F0F0);
    HalpFbDrawString(dx + 50, dy + 30, g_FpCurrentDir, 0x00000000, 0x00F0F0F0);
    
    /* Draw file list */
    ULONG listY = dy + 60;
    for (ULONG i = 0; i < g_FpEntryCount && i < 20; i++) {
        if (g_FpEntries[i].InUse) {
            const CHAR *prefix = g_FpEntries[i].IsDirectory ? "[DIR] " : "      ";
            CHAR line[128];
            ULONG k = 0;
            while (prefix[k] && k < 127) { line[k] = prefix[k]; k++; }
            for (ULONG j = 0; g_FpEntries[i].Name[j] && k < 127; j++) { line[k] = g_FpEntries[i].Name[j]; k++; }
            line[k] = 0;
            HalpFbDrawString(dx + 8, listY + i * 16, line, 0x00000000, 0x00F0F0F0);
        }
    }
    
    /* Draw buttons */
    ULONG btnY = dy + FP_DIALOG_H - 40;
    HalpFbFillRect(dx + FP_DIALOG_W - 200, btnY, 80, 24, 0x00D6D2D0);
    HalpFbDrawRect(dx + FP_DIALOG_W - 200, btnY, 80, 24, 0x00000000);
    HalpFbDrawString(dx + FP_DIALOG_W - 190, btnY + 4, "OK", 0x00000000, 0x00D6D2D0);
    
    HalpFbFillRect(dx + FP_DIALOG_W - 110, btnY, 80, 24, 0x00D6D2D0);
    HalpFbDrawRect(dx + FP_DIALOG_W - 110, btnY, 80, 24, 0x00000000);
    HalpFbDrawString(dx + FP_DIALOG_W - 100, btnY + 4, "Cancel", 0x00000000, 0x00D6D2D0);
    
    return STATUS_SUCCESS;
}

/* Handle click in file picker */
NTSTATUS NTAPI FpHandleClick(ULONG MX, ULONG MY)
{
    if (!g_FpDialogActive) return STATUS_UNSUCCESSFUL;
    
    extern ULONG HalpFbGetWidth(VOID);
    extern ULONG HalpFbGetHeight(VOID);
    
    ULONG fbWidth = HalpFbGetWidth();
    ULONG fbHeight = HalpFbGetHeight();
    
    ULONG dx = (fbWidth - FP_DIALOG_W) / 2;
    ULONG dy = (fbHeight - FP_DIALOG_H) / 2;
    
    /* Check if click is outside dialog */
    if (MX < dx || MX > dx + FP_DIALOG_W || MY < dy || MY > dy + FP_DIALOG_H) {
        g_FpDialogActive = FALSE;
        return STATUS_SUCCESS;
    }
    
    /* Check OK button */
    if (MX >= dx + FP_DIALOG_W - 200 && MX <= dx + FP_DIALOG_W - 120 &&
        MY >= dy + FP_DIALOG_H - 40 && MY <= dy + FP_DIALOG_H - 16) {
        /* OK - use selected path */
        RtlCopyMemory(g_FpSelectedPath, g_FpCurrentDir, FP_MAX_PATH);
        g_FpDialogActive = FALSE;
        return STATUS_SUCCESS;
    }
    
    /* Check Cancel button */
    if (MX >= dx + FP_DIALOG_W - 110 && MX <= dx + FP_DIALOG_W - 30 &&
        MY >= dy + FP_DIALOG_H - 40 && MY <= dy + FP_DIALOG_H - 16) {
        g_FpDialogActive = FALSE;
        return STATUS_SUCCESS;
    }
    
    /* Check file list clicks */
    ULONG listY = dy + 60;
    ULONG index = (MY - listY) / 16;
    if (index < g_FpEntryCount && g_FpEntries[index].InUse) {
        if (g_FpEntries[index].IsDirectory) {
            /* Navigate into directory */
            if (g_FpEntries[index].Name[0] == '.' && g_FpEntries[index].Name[1] == '.') {
                /* Parent directory */
                ULONG len = 0;
                while (g_FpCurrentDir[len]) len++;
                if (len > 3) {
                    /* Remove last directory */
                    while (len > 0 && g_FpCurrentDir[len-1] != '\\') len--;
                    if (len > 3) len--;
                    g_FpCurrentDir[len] = 0;
                }
            } else {
                /* Append directory name */
                ULONG len = 0;
                while (g_FpCurrentDir[len]) len++;
                if (len > 0 && g_FpCurrentDir[len-1] != '\\') {
                    g_FpCurrentDir[len++] = '\\';
                }
                ULONG k = 0;
                while (g_FpEntries[index].Name[k] && len < FP_MAX_PATH - 1) {
                    g_FpCurrentDir[len++] = g_FpEntries[index].Name[k++];
                }
                g_FpCurrentDir[len] = 0;
            }
            FpListDirectory(g_FpCurrentDir);
            FpDraw();
        } else {
            /* Select file */
            RtlCopyMemory(g_FpSelectedPath, g_FpCurrentDir, FP_MAX_PATH);
            ULONG len = 0;
            while (g_FpSelectedPath[len]) len++;
            if (len > 0 && g_FpSelectedPath[len-1] != '\\') {
                g_FpSelectedPath[len++] = '\\';
            }
            ULONG k = 0;
            while (g_FpEntries[index].Name[k] && len < FP_MAX_PATH - 1) {
                g_FpSelectedPath[len++] = g_FpEntries[index].Name[k++];
            }
            g_FpSelectedPath[len] = 0;
        }
    }
    
    return STATUS_SUCCESS;
}

/* Open file picker dialog */
NTSTATUS NTAPI FpOpen(const CHAR *StartPath)
{
    if (StartPath) {
        ULONG k = 0;
        while (StartPath[k] && k < FP_MAX_PATH - 1) { g_FpCurrentDir[k] = StartPath[k]; k++; }
        g_FpCurrentDir[k] = 0;
    } else {
        RtlCopyMemory(g_FpCurrentDir, "C:\\", 4);
    }
    
    FpListDirectory(g_FpCurrentDir);
    g_FpDialogActive = TRUE;
    FpDraw();
    
    return STATUS_SUCCESS;
}

/* Get selected path */
NTSTATUS NTAPI FpGetSelected(PCHAR OutPath, ULONG MaxLen)
{
    if (!OutPath || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    
    ULONG k = 0;
    while (g_FpSelectedPath[k] && k < MaxLen - 1) { OutPath[k] = g_FpSelectedPath[k]; k++; }
    OutPath[k] = 0;
    
    return STATUS_SUCCESS;
}

/* Check if dialog is active */
BOOLEAN NTAPI FpIsActive(VOID)
{
    return g_FpDialogActive;
}

/* Initialize file picker */
NTSTATUS NTAPI FpInit(VOID)
{
    RtlZeroMemory(g_FpEntries, sizeof(g_FpEntries));
    g_FpEntryCount = 0;
    RtlCopyMemory(g_FpCurrentDir, "C:\\", 4);
    g_FpDialogActive = FALSE;
    DbgPrint("FILEPICKER: initialized\n");
    return STATUS_SUCCESS;
}