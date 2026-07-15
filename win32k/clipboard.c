/*
 * MinNT - win32k/clipboard.c
 * Clipboard subsystem for Win32k.
 *
 * Implements OpenClipboard, CloseClipboard, EmptyClipboard,
 * GetClipboardData, SetClipboardData, CountClipboardFormats,
 * EnumClipboardFormats, GetClipboardFormatName, RegisterClipboardFormat.
 *
 * Single-owner model: only one thread can have the clipboard open at a time.
 * Data is stored in a simple array of format+handle pairs.
 */

#include "precomp.h"
#include <nt/ps.h>

#define CLIPBOARD_MAX_FORMATS  64
#define CLIPBOARD_MAX_NAME     128

typedef struct _CLIPBOARD_FORMAT {
    ULONG     FormatId;
    HANDLE    Data;
    ULONG     DataSize;
    BOOLEAN   InUse;
} CLIPBOARD_FORMAT, *PCLIPBOARD_FORMAT;

typedef struct _CLIPBOARD_STATE {
    BOOLEAN           Open;
    ULONG_PTR         OwnerThread;
    ULONG_PTR         HwndOwner;
    ULONG             FormatCount;
    ULONG             NextFormatId;
    CLIPBOARD_FORMAT  Formats[CLIPBOARD_MAX_FORMATS];
    WCHAR             FormatNames[CLIPBOARD_MAX_FORMATS][CLIPBOARD_MAX_NAME];
} CLIPBOARD_STATE;

static CLIPBOARD_STATE g_Clipboard;
KSPIN_LOCK g_ClipboardLock;

/* Predefined clipboard format IDs */
#define CF_TEXT_ID           1
#define CF_BITMAP_ID         2
#define CF_UNICODETEXT_ID    13
#define CF_HDROP_ID          15

/* Well-known format name strings */
static const WCHAR g_wszText[] = L"CF_TEXT";
static const WCHAR g_wszBitmap[] = L"CF_BITMAP";
static const WCHAR g_wszUnicodeText[] = L"CF_UNICODETEXT";
static const WCHAR g_wszHDrop[] = L"CF_HDROP";

VOID NTAPI ClipboardInit(VOID)
{
    RtlZeroMemory(&g_Clipboard, sizeof(g_Clipboard));
    KeInitializeSpinLock(&g_ClipboardLock);
    g_Clipboard.NextFormatId = 100; /* Start custom formats above predefined */

    /* Register built-in format names */
    RtlCopyMemory(g_Clipboard.FormatNames[0], g_wszText,
                  sizeof(g_wszText));
    RtlCopyMemory(g_Clipboard.FormatNames[1], g_wszBitmap,
                  sizeof(g_wszBitmap));
    RtlCopyMemory(g_Clipboard.FormatNames[2], g_wszUnicodeText,
                  sizeof(g_wszUnicodeText));
    RtlCopyMemory(g_Clipboard.FormatNames[3], g_wszHDrop,
                  sizeof(g_wszHDrop));

    DbgPrint("CLIPBOARD: initialized (%d format slots)\n", CLIPBOARD_MAX_FORMATS);
}

/* OpenClipboard: claim ownership for a thread/window */
NTSTATUS NTAPI UserOpenClipboard(ULONG_PTR hWnd)
{
    KIRQL Irql;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    if (g_Clipboard.Open) {
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        DbgPrint("CLIPBOARD: OpenClipboard failed - already open by %p\n",
                 (PVOID)g_Clipboard.OwnerThread);
        return STATUS_DEVICE_BUSY;
    }

    g_Clipboard.Open = TRUE;
    g_Clipboard.OwnerThread = (ULONG_PTR)KeGetCurrentThread();
    g_Clipboard.HwndOwner = hWnd;

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);

    DbgPrint("CLIPBOARD: OpenClipboard owner=%p hwnd=%p\n",
             (PVOID)g_Clipboard.OwnerThread, (PVOID)hWnd);
    return STATUS_SUCCESS;
}

/* CloseClipboard: release ownership */
NTSTATUS NTAPI UserCloseClipboard(VOID)
{
    KIRQL Irql;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);
    g_Clipboard.Open = FALSE;
    g_Clipboard.OwnerThread = 0;
    g_Clipboard.HwndOwner = 0;
    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);

    DbgPrint("CLIPBOARD: CloseClipboard\n");
    return STATUS_SUCCESS;
}

/* EmptyClipboard: clear all data */
NTSTATUS NTAPI UserEmptyClipboard(VOID)
{
    KIRQL Irql;
    ULONG i;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    if (!g_Clipboard.Open) {
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_DEVICE_BUSY;
    }

    for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.Formats[i].InUse && g_Clipboard.Formats[i].Data) {
            ExFreePool(g_Clipboard.Formats[i].Data);
            g_Clipboard.Formats[i].Data = NULL;
            g_Clipboard.Formats[i].DataSize = 0;
        }
        g_Clipboard.Formats[i].InUse = FALSE;
    }
    g_Clipboard.FormatCount = 0;

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    DbgPrint("CLIPBOARD: EmptyClipboard\n");
    return STATUS_SUCCESS;
}

/* SetClipboardData: store data in a format */
NTSTATUS NTAPI UserSetClipboardData(ULONG Format, HANDLE hMem, ULONG Size)
{
    KIRQL Irql;
    ULONG i, slot;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    if (!g_Clipboard.Open) {
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_DEVICE_BUSY;
    }

    /* Find existing slot for this format or an empty one */
    slot = CLIPBOARD_MAX_FORMATS;
    for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.Formats[i].InUse && g_Clipboard.Formats[i].FormatId == Format) {
            slot = i;
            break;
        }
    }
    if (slot == CLIPBOARD_MAX_FORMATS) {
        for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
            if (!g_Clipboard.Formats[i].InUse) { slot = i; break; }
        }
    }

    if (slot == CLIPBOARD_MAX_FORMATS) {
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Free old data if reusing slot */
    if (g_Clipboard.Formats[slot].Data) {
        ExFreePool(g_Clipboard.Formats[slot].Data);
    }

    g_Clipboard.Formats[slot].FormatId = Format;
    g_Clipboard.Formats[slot].DataSize = Size;
    g_Clipboard.Formats[slot].InUse = TRUE;

    if (hMem && Size > 0) {
        g_Clipboard.Formats[slot].Data = ExAllocatePool(NonPagedPool, Size);
        if (g_Clipboard.Formats[slot].Data) {
            RtlCopyMemory(g_Clipboard.Formats[slot].Data, (PVOID)hMem, Size);
        } else {
            g_Clipboard.Formats[slot].InUse = FALSE;
            W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
            return STATUS_NO_MEMORY;
        }
    } else {
        g_Clipboard.Formats[slot].Data = NULL;
    }

    g_Clipboard.FormatCount++;
    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);

    DbgPrint("CLIPBOARD: SetClipboardData fmt=%u size=%u slot=%u\n",
             Format, Size, slot);
    return STATUS_SUCCESS;
}

/* GetClipboardData: retrieve data for a format */
NTSTATUS NTAPI UserGetClipboardData(ULONG Format, PVOID *ppData, PULONG pSize)
{
    KIRQL Irql;
    ULONG i;

    if (!ppData || !pSize) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.Formats[i].InUse && g_Clipboard.Formats[i].FormatId == Format) {
            *ppData = g_Clipboard.Formats[i].Data;
            *pSize = g_Clipboard.Formats[i].DataSize;
            W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
            return STATUS_SUCCESS;
        }
    }

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    *ppData = NULL;
    *pSize = 0;
    return STATUS_NOT_FOUND;
}

/* CountClipboardFormats: return number of available formats */
NTSTATUS NTAPI UserCountClipboardFormats(PULONG pCount)
{
    KIRQL Irql;

    if (!pCount) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);
    *pCount = g_Clipboard.FormatCount;
    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);

    return STATUS_SUCCESS;
}

/* EnumClipboardFormats: iterate through available formats */
NTSTATUS NTAPI UserEnumClipboardFormats(ULONG PrevFormat, PULONG pNextFormat)
{
    KIRQL Irql;
    ULONG i;
    BOOLEAN foundPrev = FALSE;

    if (!pNextFormat) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    if (PrevFormat == 0) {
        /* Return first format */
        for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
            if (g_Clipboard.Formats[i].InUse) {
                *pNextFormat = g_Clipboard.Formats[i].FormatId;
                W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
                return STATUS_SUCCESS;
            }
        }
    } else {
        /* Find next format after PrevFormat */
        foundPrev = FALSE;
        for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
            if (g_Clipboard.Formats[i].InUse) {
                if (foundPrev) {
                    *pNextFormat = g_Clipboard.Formats[i].FormatId;
                    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
                    return STATUS_SUCCESS;
                }
                if (g_Clipboard.Formats[i].FormatId == PrevFormat) {
                    foundPrev = TRUE;
                }
            }
        }
    }

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    *pNextFormat = 0;
    return STATUS_NO_MORE_ENTRIES;
}

/* RegisterClipboardFormatW: register a custom format name */
NTSTATUS NTAPI UserRegisterClipboardFormatW(PCWSTR lpFormatName, PULONG lpFormat)
{
    KIRQL Irql;
    ULONG i, nameLen;

    if (!lpFormatName || !lpFormat) return STATUS_INVALID_PARAMETER;

    nameLen = 0;
    while (nameLen < CLIPBOARD_MAX_NAME - 1 && lpFormatName[nameLen]) nameLen++;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    /* Check if already registered */
    for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.FormatNames[i][0] != 0) {
            BOOLEAN match = TRUE;
            ULONG j;
            for (j = 0; j < nameLen; j++) {
                if (g_Clipboard.FormatNames[i][j] != lpFormatName[j]) {
                    match = FALSE;
                    break;
                }
            }
            if (match && g_Clipboard.FormatNames[i][nameLen] == 0) {
                *lpFormat = (i < 4) ? (i + 1) : (100 + i);
                W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
                return STATUS_SUCCESS;
            }
        }
    }

    /* Find empty slot */
    for (i = 4; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.FormatNames[i][0] == 0) {
            RtlCopyMemory(g_Clipboard.FormatNames[i], lpFormatName,
                          nameLen * sizeof(WCHAR));
            g_Clipboard.FormatNames[i][nameLen] = 0;
            *lpFormat = 100 + i;
            W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
            DbgPrint("CLIPBOARD: RegisterClipboardFormat '%ws' -> %u\n",
                     lpFormatName, *lpFormat);
            return STATUS_SUCCESS;
        }
    }

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

/* GetClipboardFormatNameW: get name for a format */
NTSTATUS NTAPI UserGetClipboardFormatNameW(ULONG Format, PWCHAR lpBuf, int cchMax)
{
    KIRQL Irql;
    ULONG nameLen;

    if (!lpBuf || cchMax <= 0) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    /* Built-in formats */
    if (Format == CF_TEXT_ID) {
        nameLen = 7;
        if ((ULONG)cchMax <= nameLen) { W32K_UNLOCK_SPIN(g_ClipboardLock, Irql); return STATUS_BUFFER_TOO_SMALL; }
        RtlCopyMemory(lpBuf, g_wszText, nameLen * sizeof(WCHAR));
        lpBuf[nameLen] = 0;
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_SUCCESS;
    }
    if (Format == CF_BITMAP_ID) {
        nameLen = 10;
        if ((ULONG)cchMax <= nameLen) { W32K_UNLOCK_SPIN(g_ClipboardLock, Irql); return STATUS_BUFFER_TOO_SMALL; }
        RtlCopyMemory(lpBuf, g_wszBitmap, nameLen * sizeof(WCHAR));
        lpBuf[nameLen] = 0;
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_SUCCESS;
    }
    if (Format == CF_UNICODETEXT_ID) {
        nameLen = 15;
        if ((ULONG)cchMax <= nameLen) { W32K_UNLOCK_SPIN(g_ClipboardLock, Irql); return STATUS_BUFFER_TOO_SMALL; }
        RtlCopyMemory(lpBuf, g_wszUnicodeText, nameLen * sizeof(WCHAR));
        lpBuf[nameLen] = 0;
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_SUCCESS;
    }
    if (Format == CF_HDROP_ID) {
        nameLen = 8;
        if ((ULONG)cchMax <= nameLen) { W32K_UNLOCK_SPIN(g_ClipboardLock, Irql); return STATUS_BUFFER_TOO_SMALL; }
        RtlCopyMemory(lpBuf, g_wszHDrop, nameLen * sizeof(WCHAR));
        lpBuf[nameLen] = 0;
        W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
        return STATUS_SUCCESS;
    }

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    lpBuf[0] = 0;
    return STATUS_NOT_FOUND;
}

/* IsClipboardFormatAvailable: quick check for format presence */
NTSTATUS NTAPI UserIsClipboardFormatAvailable(ULONG Format, PULONG pAvailable)
{
    KIRQL Irql;
    ULONG i;

    if (!pAvailable) return STATUS_INVALID_PARAMETER;

    W32K_LOCK_SPIN(g_ClipboardLock, Irql);

    *pAvailable = 0;
    for (i = 0; i < CLIPBOARD_MAX_FORMATS; i++) {
        if (g_Clipboard.Formats[i].InUse && g_Clipboard.Formats[i].FormatId == Format) {
            *pAvailable = 1;
            break;
        }
    }

    W32K_UNLOCK_SPIN(g_ClipboardLock, Irql);
    return STATUS_SUCCESS;
}
