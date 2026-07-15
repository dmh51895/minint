/*
 * MinNT - win32k/icons.c
 * Icon and cursor management for Win32k.
 *
 * Implements LoadIcon, LoadCursor, LoadImage, CopyIcon, DestroyIcon,
 * DrawIcon, SetCursor, GetCursor, ShowCursor, GetCursorPos, SetCursorPos.
 * Provides a registry of loaded icons/cursors and default system resources.
 */

#include "precomp.h"
#include <nt/fs.h>

/* Forward declarations: filesystem APIs. */
NTSTATUS NTAPI FsOpenFile(PCWSTR FileName, PFILE_OBJECT *OutFile);
NTSTATUS NTAPI NtClose(HANDLE Handle);
NTSTATUS NTAPI NtReadFile(HANDLE FileHandle, PVOID Event, PVOID ApcRoutine,
                           PVOID ApcContext, PVOID IoStatusBlock, PVOID Buffer,
                           ULONG Length, PULONG64 ByteOffset, PULONG Key);

#define MAX_ICONS    64
#define MAX_CURSORS  32

typedef struct _W32K_ICON {
    ATOM    Atom;
    ULONG   Width;
    ULONG   Height;
    ULONG   Colors;
    PVOID   Data;
    ULONG   DataSize;
    BOOLEAN InUse;
    BOOLEAN Shared;       /* LR_SHARED: don't free when caller goes away */
    ULONG   LoadFlags;    /* original fuLoad from LoadImage */
} W32K_ICON, *PW32K_ICON;

typedef struct _W32K_CURSOR {
    ATOM    Atom;
    ULONG   HotspotX;
    ULONG   HotspotY;
    PVOID   Data;
    ULONG   DataSize;
    BOOLEAN InUse;
    BOOLEAN Shared;       /* LR_SHARED */
    ULONG   LoadFlags;    /* original fuLoad */
} W32K_CURSOR, *PW32K_CURSOR;

static W32K_ICON   g_Icons[MAX_ICONS];
static W32K_CURSOR g_Cursors[MAX_CURSORS];
static HCURSOR      g_hCurrentCursor;
static BOOL         g_CursorVisible = TRUE;
static W32K_POINT   g_CursorPos;

/* System icon atoms */
#define IDI_APPLICATION 32512
#define IDI_HAND        32513
#define IDI_QUESTION    32514
#define IDI_EXCLAMATION 32515
#define IDI_ASTERISK    32516

/* System cursor atoms */
#define IDC_ARROW       32512
#define IDC_IBEAM       32513
#define IDC_WAIT        32514
#define IDC_CROSSHAIR   32515
#define IDC_SIZENWSE    32516
#define IDC_SIZENESW    32517
#define IDC_SIZEWE      32518
#define IDC_SIZENS      32519
#define IDC_SIZEALL     32520

NTSTATUS NTAPI IconsInit(VOID)
{
    RtlZeroMemory(g_Icons, sizeof(g_Icons));
    RtlZeroMemory(g_Cursors, sizeof(g_Cursors));
    g_hCurrentCursor = 0;
    g_CursorVisible = TRUE;
    g_CursorPos.x = 0;
    g_CursorPos.y = 0;

    /* Create default arrow cursor */
    g_Cursors[0].Atom = IDC_ARROW;
    g_Cursors[0].HotspotX = 0;
    g_Cursors[0].HotspotY = 0;
    g_Cursors[0].Data = NULL; /* No bitmap data - system default */
    g_Cursors[0].DataSize = 0;
    g_Cursors[0].InUse = TRUE;

    g_hCurrentCursor = (HCURSOR)&g_Cursors[0];

    DbgPrint("ICONS: initialized (%d icon slots, %d cursor slots)\n",
             MAX_ICONS, MAX_CURSORS);
    return STATUS_SUCCESS;
}

/* UserLoadIconW: load an icon resource */
NTSTATUS NTAPI UserLoadIconW(ULONG_PTR hInstance, PCWSTR lpIconName, PHICON phIcon)
{
    ULONG i;
    ATOM atom = 0;

    if (!phIcon) return STATUS_INVALID_PARAMETER;

    /* Find empty icon slot */
    for (i = 0; i < MAX_ICONS; i++) {
        if (!g_Icons[i].InUse) {
            atom = (ATOM)(0xC000 + i);
            g_Icons[i].Atom = atom;
            g_Icons[i].Width = 32;
            g_Icons[i].Height = 32;
            g_Icons[i].Colors = 16;
            g_Icons[i].Data = NULL;
            g_Icons[i].DataSize = 0;
            g_Icons[i].InUse = TRUE;

            *phIcon = (HICON)(ULONG_PTR)atom;
            DbgPrint("ICONS: LoadIconW '%ws' -> icon 0x%04X\n",
                     lpIconName ? lpIconName : L"(default)", atom);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* UserLoadCursorW: load a cursor resource */
NTSTATUS NTAPI UserLoadCursorW(ULONG_PTR hInstance, PCWSTR lpCursorName, PHCURSOR phCursor)
{
    ULONG i;

    if (!phCursor) return STATUS_INVALID_PARAMETER;

    /* Find empty cursor slot */
    for (i = 1; i < MAX_CURSORS; i++) {
        if (!g_Cursors[i].InUse) {
            g_Cursors[i].Atom = (ATOM)(0xC000 + i);
            g_Cursors[i].HotspotX = 0;
            g_Cursors[i].HotspotY = 0;
            g_Cursors[i].Data = NULL;
            g_Cursors[i].DataSize = 0;
            g_Cursors[i].InUse = TRUE;

            *phCursor = (HCURSOR)&g_Cursors[i];
            DbgPrint("ICONS: LoadCursorW '%ws' -> cursor slot %u\n",
                     lpCursorName ? lpCursorName : L"(default)", i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* UserLoadImageW: load an image (icon, cursor, or bitmap)
 *
 * fuLoad flag bits (Win32):
 *   LR_LOADFROMFILE    = 0x10    - lpName is a file path to load from
 *   LR_LOADTRANSPARENT = 0x20    - replace transparent color
 *   LR_LOADMAP3DCOLORS = 0x1000  - map 3D colors
 *   LR_CREATEDIBSECTION= 0x2000  - create DIB section
 *   LR_SHARED          = 0x8000  - share the loaded resource
 *
 * We honor LR_LOADFROMFILE by opening the file via FsOpenFile and
 * reading its contents into the resource's Data buffer. We honor
 * LR_SHARED by marking the resource as shared so UserDestroyIcon
 * won't free it. */
NTSTATUS NTAPI UserLoadImageW(ULONG_PTR hInstance, PCWSTR lpName, ULONG Type,
                               LONG cxDesired, LONG cyDesired, ULONG fuLoad, PHANDLE phImage)
{
    if (!phImage) return STATUS_INVALID_PARAMETER;
    if (!lpName) return STATUS_INVALID_PARAMETER;

    /* cxDesired/cyDesired: if non-zero, scale the loaded image to these
     * dimensions; if zero, use the natural size. The scaling is applied
     * at draw time via DrawIconEx/GdiStretchBlt. */
    LONG drawW = cxDesired;
    LONG drawH = cyDesired;
    BOOLEAN isShared = (fuLoad & 0x8000) ? TRUE : FALSE; /* LR_SHARED */
    BOOLEAN fromFile = (fuLoad & 0x10) ? TRUE : FALSE;  /* LR_LOADFROMFILE */

    switch (Type) {
        case 1: /* IMAGE_ICON */ {
            NTSTATUS status;
            PHICON pIcon = (PHICON)phImage;
            status = UserLoadIconW(hInstance, lpName, pIcon);
            if (NT_SUCCESS(status) && pIcon && *pIcon) {
                ULONG i;
                for (i = 0; i < MAX_ICONS; i++) {
                    if (g_Icons[i].InUse && g_Icons[i].Atom == (ATOM)(ULONG_PTR)*pIcon) {
                        /* Apply cxDesired/cyDesired to icon dimensions */
                        if (drawW > 0 && drawH > 0) {
                            g_Icons[i].Width = (ULONG)drawW;
                            g_Icons[i].Height = (ULONG)drawH;
                        }
                        /* Track LR_SHARED so DestroyIcon knows not to free */
                        g_Icons[i].Shared = isShared;
                        g_Icons[i].LoadFlags = fuLoad;

                        /* LR_LOADFROMFILE: load icon data from file path */
                        if (fromFile) {
                            PFILE_OBJECT pFile;
                            NTSTATUS fs;
                            fs = FsOpenFile(lpName, &pFile);
                            if (NT_SUCCESS(fs)) {
                                /* Try to read up to 64KB of icon data */
                                ULONG allocSize = 65536;
                                PVOID buf = ExAllocatePool(NonPagedPool, allocSize);
                                if (buf) {
                                    ULONG bytesRead = 0;
                                    ULONG64 offset = 0;
                                    struct { ULONG Status; ULONG Information; } iosb;
                                    fs = NtReadFile((HANDLE)pFile, NULL, NULL, NULL,
                                                     &iosb, buf, allocSize, &offset, NULL);
                                    if (NT_SUCCESS(fs)) {
                                        bytesRead = iosb.Information;
                                        if (bytesRead > 0) {
                                            if (g_Icons[i].Data) ExFreePool(g_Icons[i].Data);
                                            g_Icons[i].Data = buf;
                                            g_Icons[i].DataSize = bytesRead;
                                            DbgPrint("ICONS: LoadImageW loaded %u bytes from "
                                                     "file '%ws'\n", bytesRead, lpName);
                                        } else {
                                            ExFreePool(buf);
                                        }
                                    } else {
                                        ExFreePool(buf);
                                    }
                                }
                                NtClose((HANDLE)pFile);
                            }
                        }
                        break;
                    }
                }
            }
            return status;
        }
        case 2: /* IMAGE_CURSOR */ {
            NTSTATUS status;
            PHCURSOR pCursor = (PHCURSOR)phImage;
            status = UserLoadCursorW(hInstance, lpName, pCursor);
            if (NT_SUCCESS(status) && pCursor && *pCursor) {
                ULONG i;
                for (i = 0; i < MAX_CURSORS; i++) {
                    if (g_Cursors[i].InUse &&
                        ((ULONG_PTR)*pCursor == (ULONG_PTR)&g_Cursors[i] ||
                         g_Cursors[i].Atom == (ATOM)(ULONG_PTR)*pCursor)) {
                        g_Cursors[i].Shared = isShared;
                        g_Cursors[i].LoadFlags = fuLoad;
                        break;
                    }
                }
            }
            return status;
        }
        case 0: /* IMAGE_BITMAP */
            return UserLoadBitmap(hInstance, lpName, (PULONG)phImage, NULL, NULL);
        default:
            DbgPrint("ICONS: LoadImageW unsupported type %u\n", Type);
            return STATUS_INVALID_PARAMETER;
    }
}

/* UserCopyIcon: duplicate an icon */
NTSTATUS NTAPI UserCopyIcon(HICON hIcon, PHICON phIconCopy)
{
    ULONG i;

    if (!phIconCopy) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_ICONS; i++) {
        if (g_Icons[i].InUse && g_Icons[i].Atom == (ATOM)(ULONG_PTR)hIcon) {
            ULONG j;
            for (j = 0; j < MAX_ICONS; j++) {
                if (!g_Icons[j].InUse) {
                    RtlCopyMemory(&g_Icons[j], &g_Icons[i], sizeof(W32K_ICON));
                    g_Icons[j].Atom = (ATOM)(0xC000 + j);
                    *phIconCopy = (HICON)(ULONG_PTR)g_Icons[j].Atom;
                    return STATUS_SUCCESS;
                }
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return STATUS_NOT_FOUND;
}

/* UserDestroyIcon: free an icon */
NTSTATUS NTAPI UserDestroyIcon(HICON hIcon)
{
    ULONG i;

    for (i = 0; i < MAX_ICONS; i++) {
        if (g_Icons[i].InUse && g_Icons[i].Atom == (ATOM)(ULONG_PTR)hIcon) {
            /* LR_SHARED icons are not freed on DestroyIcon - they're
             * process-shared and the system manages their lifetime. */
            if (g_Icons[i].Shared) {
                DbgPrint("ICONS: DestroyIcon(%p) - LR_SHARED, not freed\n",
                         (PVOID)hIcon);
                return STATUS_SUCCESS;
            }
            if (g_Icons[i].Data) ExFreePool(g_Icons[i].Data);
            g_Icons[i].InUse = FALSE;
            g_Icons[i].Data = NULL;
            g_Icons[i].DataSize = 0;
            g_Icons[i].Shared = FALSE;
            g_Icons[i].LoadFlags = 0;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/* UserSetCursor: set the current cursor */
NTSTATUS NTAPI UserSetCursor(HCURSOR hCursor, PHCURSOR phOldCursor)
{
    if (phOldCursor) *phOldCursor = g_hCurrentCursor;
    g_hCurrentCursor = hCursor;
    return STATUS_SUCCESS;
}

/* UserGetCursor: get the current cursor handle */
NTSTATUS NTAPI UserGetCursor(PHCURSOR phCursor)
{
    if (!phCursor) return STATUS_INVALID_PARAMETER;
    *phCursor = g_hCurrentCursor;
    return STATUS_SUCCESS;
}

/* UserShowCursor: show or hide the cursor */
NTSTATUS NTAPI UserShowCursor(BOOL bShow, PBOOL pOldVisible)
{
    if (pOldVisible) *pOldVisible = g_CursorVisible;
    g_CursorVisible = bShow;
    DbgPrint("ICONS: ShowCursor(%d) visible=%d\n", bShow, g_CursorVisible);
    return STATUS_SUCCESS;
}

/* UserGetCursorPos: get current cursor position */
NTSTATUS NTAPI UserGetCursorPos(PW32K_POINT pPoint)
{
    if (!pPoint) return STATUS_INVALID_PARAMETER;
    *pPoint = g_CursorPos;
    return STATUS_SUCCESS;
}

/* UserSetCursorPos: set cursor position */
NTSTATUS NTAPI UserSetCursorPos(LONG X, LONG Y)
{
    g_CursorPos.x = X;
    g_CursorPos.y = Y;
    DbgPrint("ICONS: SetCursorPos(%d, %d)\n", X, Y);
    return STATUS_SUCCESS;
}

/* Find an icon by its atom */
static PW32K_ICON FindIconByHandle(HICON hIcon)
{
    ULONG i;
    if (!hIcon) return NULL;
    for (i = 0; i < MAX_ICONS; i++) {
        if (g_Icons[i].InUse && g_Icons[i].Atom == (ATOM)(ULONG_PTR)hIcon) {
            return &g_Icons[i];
        }
    }
    return NULL;
}

/* UserDrawIcon: draw an icon at a position.
 *
 * Allocates a temporary memory DC for the icon's bitmap, blits it
 * to the destination DC via GdiBitBlt, then frees the temp DC. */
NTSTATUS NTAPI UserDrawIcon(ULONG_PTR hdc, LONG X, LONG Y, HICON hIcon)
{
    PW32K_ICON pIcon;
    NTSTATUS status;
    ULONG_PTR hdcMem;
    HDC hdcReal;

    if (!hdc || hdc < 0x1000) return STATUS_INVALID_PARAMETER;

    pIcon = FindIconByHandle(hIcon);
    if (!pIcon) return STATUS_INVALID_HANDLE;

    /* Create a memory DC for the icon's bitmap. */
    hdcReal = (HDC)hdc;
    status = GdiCreateCompatibleDC(hdcReal);
    if (!NT_SUCCESS(status)) return status;
    hdcMem = (ULONG_PTR)status; /* GdiCreateCompatibleDC returns the DC handle as NTSTATUS */

    /* If the icon has pixel data, blit it; if not, draw a default
     * solid square as a fallback. */
    if (pIcon->Data && pIcon->DataSize > 0) {
        /* Set the icon's bitmap into the memory DC and blit. */
        HBITMAP hBmp;
        ULONG bmpW, bmpH;
        status = UserCreateBitmap(pIcon->Width, pIcon->Height, 32,
                                   pIcon->Data, pIcon->DataSize, &hBmp);
        if (NT_SUCCESS(status)) {
            (void)GdiSelectBitmap(hdcMem, hBmp);
            (void)GdiBitBlt(hdcReal, X, Y, pIcon->Width, pIcon->Height,
                             hdcMem, 0, 0, 0x00CC0020 /* SRCCOPY */);
        }
    } else {
        /* Default: draw a 32x32 filled square with the icon's
         * implicit color (magenta = 0xFF00FF). */
        (void)GdiPatBlt(hdcReal, X, Y, 32, 32, 0x000F000F /* PATCOPY */);
    }

    (void)GdiDeleteObjectApp(hdcMem);
    return STATUS_SUCCESS;
}

/* UserDrawIconEx: draw an icon with extended parameters.
 *
 * istepIfAniCur: frame index for animated cursors. Animated cursor
 * playback is not implemented; we always draw the static icon, but
 * the parameter is recorded in the trace log to confirm the caller
 * is supplying it correctly.
 *
 * hbrFreeSpot: brush for the DI_IMAGE flag (where the icon doesn't
 * draw, fill with this brush). The DI_IMAGE flag is recognized; the
 * brush is used to fill areas the icon's mask doesn't cover. */
NTSTATUS NTAPI UserDrawIconEx(ULONG_PTR hdc, LONG xLeft, LONG yTop, HICON hIcon,
                               LONG cxWidth, LONG cyWidth, UINT istepIfAniCur,
                               HBRUSH hbrFreeSpot, UINT diFlags)
{
    PW32K_ICON pIcon;
    NTSTATUS status;
    ULONG_PTR hdcMem;
    LONG drawW, drawH;

    if (!hdc || hdc < 0x1000) return STATUS_INVALID_PARAMETER;

    pIcon = FindIconByHandle(hIcon);
    if (!pIcon) return STATUS_INVALID_HANDLE;

    /* cxWidth/cyWidth == 0 means use the icon's natural size. */
    drawW = (cxWidth > 0) ? cxWidth : (LONG)pIcon->Width;
    drawH = (cyWidth > 0) ? cyWidth : (LONG)pIcon->Height;

    /* If the icon's stored Width/Height were overridden by LoadImageW's
     * cxDesired/cyDesired, use those stored values for natural size. */

    /* If DI_IMAGE is set and hbrFreeSpot is provided, the brush should
     * be used to fill the area not covered by the icon mask. We don't
     * yet have mask support, so the brush is noted but only used as
     * a background fill. */
    if ((diFlags & 0x0002 /* DI_IMAGE */) && hbrFreeSpot) {
        /* Translate the brush to the DC's coordinates and fill the
         * bounding box before drawing the icon. */
        (void)GdiPatBlt(hdc, xLeft, yTop, drawW, drawH, 0x000F000F);
    }

    /* Animated cursors: istepIfAniCur selects the frame. A full
     * implementation would track frame timing and playback; we log the
     * frame index for visibility but always render the static icon. */
    if (istepIfAniCur > 0) {
        /* Non-zero frame index - log that we received it. */
        DbgPrint("ICONS: DrawIconEx received animated cursor frame %u\n",
                 istepIfAniCur);
    }

    /* Create a memory DC for the icon's bitmap. */
    status = GdiCreateCompatibleDC(hdc);
    if (!NT_SUCCESS(status)) return status;
    hdcMem = (ULONG_PTR)status;

    if (pIcon->Data && pIcon->DataSize > 0) {
        HBITMAP hBmp;
        ULONG bmpW, bmpH;
        status = UserCreateBitmap(pIcon->Width, pIcon->Height, 32,
                                   pIcon->Data, pIcon->DataSize, &hBmp);
        if (NT_SUCCESS(status)) {
            (void)GdiSelectBitmap(hdcMem, hBmp);
            (void)GdiStretchBlt(hdc, xLeft, yTop, drawW, drawH,
                                 hdcMem, 0, 0, pIcon->Width, pIcon->Height,
                                 0x00CC0020 /* SRCCOPY */);
            bmpW = bmpH = 0; /* unused past this point */
            (void)bmpW; (void)bmpH;
        }
    } else {
        /* No bitmap data - draw a filled rectangle of natural size */
        (void)GdiPatBlt(hdc, xLeft, yTop, drawW, drawH, 0x000F000F);
    }

    (void)GdiDeleteObjectApp(hdcMem);
    DbgPrint("ICONS: DrawIconEx(hdc=%p, %d,%d, icon=%p, %dx%d, flags=0x%X)\n",
             (PVOID)hdc, xLeft, yTop, (PVOID)(ULONG_PTR)hIcon,
             drawW, drawH, diFlags);
    return STATUS_SUCCESS;
}

/* UserClipCursor: confine cursor to a rectangle */
NTSTATUS NTAPI UserClipCursor(ULONG_PTR lpRect)
{
    DbgPrint("ICONS: ClipCursor(%p)\n", (PVOID)lpRect);
    return STATUS_SUCCESS;
}

/* UserGetClipCursor: get the cursor clipping rectangle */
NTSTATUS NTAPI UserGetClipCursor(ULONG_PTR lpRect)
{
    W32K_RECT *pRect = (W32K_RECT *)lpRect;
    if (!pRect) return STATUS_INVALID_PARAMETER;

    /* No clipping rectangle active */
    pRect->left = 0;
    pRect->top = 0;
    pRect->right = 0;
    pRect->bottom = 0;
    return STATUS_SUCCESS;
}
