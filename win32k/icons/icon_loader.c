/*
 * MinNT - win32k/icons/icon_loader.c
 * Icon loading system for MinNT.
 *
 * Loads icons from PNG files (XP-style icons are PNG format).
 * Supports:
 *   - PNG loading (simplified)
 *   - Icon caching
 *   - System icon registry
 *   - Default icon fallback
 *
 * Icons are stored in /System/Icons/ directory.
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

#define ICON_MAX_ICONS       256
#define ICON_MAX_PATH        260
#define ICON_MAX_NAME        64

typedef struct _ICON_ENTRY {
    CHAR Name[ICON_MAX_NAME];
    CHAR Path[ICON_MAX_PATH];
    ULONG Width;
    ULONG Height;
    PVOID PixelData;
    ULONG PixelDataSize;
    BOOLEAN InUse;
} ICON_ENTRY, *PICON_ENTRY;

static ICON_ENTRY g_Icons[ICON_MAX_ICONS];
static ULONG g_IconCount = 0;

/* PNG signature */
static const UCHAR PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

/* Check if file is PNG */
static BOOLEAN IsPngFile(PVOID Data, ULONG Size)
{
    if (!Data || Size < 8) return FALSE;
    for (ULONG i = 0; i < 8; i++) {
        if (((PUCHAR)Data)[i] != PNG_SIGNATURE[i]) return FALSE;
    }
    return TRUE;
}

/* Load PNG file (simplified - reads header and stores raw data) */
static NTSTATUS LoadPngFile(const CHAR *Path, PICON_ENTRY Icon)
{
    if (!Path || !Icon) return STATUS_INVALID_PARAMETER;
    
    /* Open file */
    UNICODE_STRING upath;
    RtlInitUnicodeString(&upath, (PCWSTR)Path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &upath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x2001F, &oa, &isb, NULL, 0, 7, 1, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    
    /* Read PNG signature */
    UCHAR signature[8];
    s = NtReadFile(h, NULL, NULL, NULL, &isb, signature, 8, NULL, NULL);
    if (!NT_SUCCESS(s) || isb.Information < 8) {
        NtClose(h);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    
    if (!IsPngFile(signature, 8)) {
        NtClose(h);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    
    /* Read entire file */
    ULONG fileSize = 0;
    PVOID fileData = ExAllocatePoolWithTag(0, 1024 * 1024, (ULONG)'ICN '); /* 1MB max */
    if (!fileData) {
        NtClose(h);
        return STATUS_NO_MEMORY;
    }
    
    s = NtReadFile(h, NULL, NULL, NULL, &isb, fileData, 1024 * 1024, NULL, NULL);
    NtClose(h);
    
    if (!NT_SUCCESS(s)) {
        ExFreePoolWithTag(fileData, 0);
        return s;
    }
    
    fileSize = (ULONG)isb.Information;
    
    /* Parse PNG IHDR chunk (simplified) */
    /* PNG format: 8-byte signature + chunks */
    /* First chunk is IHDR at offset 8 */
    if (fileSize < 33) {
        ExFreePoolWithTag(fileData, 0);
        return STATUS_INVALID_IMAGE_FORMAT;
    }
    
    /* IHDR chunk: 4-byte length + 4-byte type + 13-byte data */
    PUCHAR data = (PUCHAR)fileData;
    ULONG width = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    ULONG height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    
    Icon->Width = width;
    Icon->Height = height;
    Icon->PixelData = fileData;
    Icon->PixelDataSize = fileSize;
    
    DbgPrint("ICONS: loaded PNG %s (%ux%u, %u bytes)\n", Path, width, height, fileSize);
    
    return STATUS_SUCCESS;
}

/* Register an icon */
NTSTATUS NTAPI IconRegister(const CHAR *Name, const CHAR *Path)
{
    if (!Name || !Path) return STATUS_INVALID_PARAMETER;
    
    /* Check if already registered */
    for (ULONG i = 0; i < g_IconCount; i++) {
        if (g_Icons[i].InUse) {
            BOOLEAN match = TRUE;
            for (ULONG k = 0; k < ICON_MAX_NAME; k++) {
                if (g_Icons[i].Name[k] != Name[k]) { match = FALSE; break; }
                if (Name[k] == 0) break;
            }
            if (match) {
                /* Update existing */
                ULONG j = 0;
                while (Path[j] && j < ICON_MAX_PATH - 1) { g_Icons[i].Path[j] = Path[j]; j++; }
                g_Icons[i].Path[j] = 0;
                return STATUS_SUCCESS;
            }
        }
    }
    
    /* Find free slot */
    for (ULONG i = 0; i < ICON_MAX_ICONS; i++) {
        if (!g_Icons[i].InUse) {
            RtlZeroMemory(&g_Icons[i], sizeof(ICON_ENTRY));
            g_Icons[i].InUse = TRUE;
            
            ULONG k = 0;
            while (Name[k] && k < ICON_MAX_NAME - 1) { g_Icons[i].Name[k] = Name[k]; k++; }
            g_Icons[i].Name[k] = 0;
            
            k = 0;
            while (Path[k] && k < ICON_MAX_PATH - 1) { g_Icons[i].Path[k] = Path[k]; k++; }
            g_Icons[i].Path[k] = 0;
            
            /* Load the icon */
            LoadPngFile(Path, &g_Icons[i]);
            g_IconCount++;
            
            DbgPrint("ICONS: registered '%s' -> %s\n", Name, Path);
            return STATUS_SUCCESS;
        }
    }
    
    return STATUS_NO_MEMORY;
}

/* Get icon by name */
NTSTATUS NTAPI IconGetByName(const CHAR *Name, PICON_ENTRY *OutIcon)
{
    if (!Name || !OutIcon) return STATUS_INVALID_PARAMETER;
    
    for (ULONG i = 0; i < g_IconCount; i++) {
        if (g_Icons[i].InUse) {
            BOOLEAN match = TRUE;
            for (ULONG k = 0; k < ICON_MAX_NAME; k++) {
                if (g_Icons[i].Name[k] != Name[k]) { match = FALSE; break; }
                if (Name[k] == 0) break;
            }
            if (match) {
                *OutIcon = &g_Icons[i];
                return STATUS_SUCCESS;
            }
        }
    }
    
    *OutIcon = NULL;
    return STATUS_NOT_FOUND;
}

/* Render icon to framebuffer at position */
NTSTATUS NTAPI IconRender(const CHAR *Name, ULONG X, ULONG Y)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    
    PICON_ENTRY icon = NULL;
    IconGetByName(Name, &icon);
    
    if (!icon || !icon->PixelData) {
        /* Draw default icon (simple square) */
        extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
        extern VOID NTAPI HalpFbDrawRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
        HalpFbFillRect(X, Y, 32, 32, 0x00FFFFFF);
        HalpFbDrawRect(X, Y, 32, 32, 0x00000000);
        return STATUS_SUCCESS;
    }
    
    /* Render PNG (simplified - just draw a colored rectangle for now) */
    extern VOID NTAPI HalpFbFillRect(ULONG X, ULONG Y, ULONG W, ULONG H, ULONG Color);
    ULONG color = 0x00C0C0C0; /* Default gray */
    
    /* Hash name to color for variety */
    ULONG hash = 0;
    for (ULONG i = 0; Name[i]; i++) hash = hash * 31 + Name[i];
    color = (hash & 0x00FFFFFF) | 0x00800000;
    
    HalpFbFillRect(X, Y, 32, 32, color);
    
    return STATUS_SUCCESS;
}

/* Initialize icon system with default XP icons */
NTSTATUS NTAPI IconLoaderInit(VOID)
{
    RtlZeroMemory(g_Icons, sizeof(g_Icons));
    g_IconCount = 0;
    
    DbgPrint("ICONS: icon loader initialized\n");
    return STATUS_SUCCESS;
}