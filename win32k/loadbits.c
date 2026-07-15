/*
 * MinNT - win32k/loadbits.c
 * Bitmap loading and management for Win32k.
 */

#include "precomp.h"

#define MAX_BITMAPS 32

typedef struct _W32K_BITMAP {
    ATOM        Atom;
    ULONG       Width;
    ULONG       Height;
    ULONG       BitsPerPixel;
    PVOID       Scan0;
    ULONG       Stride;
    ULONG       DataSize;
    BOOLEAN     InUse;
    ULONG_PTR   hInstance;  /* Module the bitmap was loaded from (0 = kernel32 default) */
    ULONG_PTR   ResourceName; /* Original lpBitmapName (or resource ID if MAKEINTRESOURCE) */
    BOOLEAN     IsResource;   /* TRUE if loaded as a module resource rather than stock */
} W32K_BITMAP, *PW32K_BITMAP;

static W32K_BITMAP g_Bitmaps[MAX_BITMAPS];

NTSTATUS NTAPI LoadBitsInit(VOID)
{
    RtlZeroMemory(g_Bitmaps, sizeof(g_Bitmaps));
    DbgPrint("LOADBITS: initialized (%d bitmap slots)\n", MAX_BITMAPS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLoadBitmap(ULONG_PTR hInstance, PCWSTR lpBitmapName,
                               PULONG phBitmap, PULONG pWidth, PULONG pHeight)
{
    ULONG i;
    BOOLEAN isResource = FALSE;
    ULONG_PTR resourceId = 0;
    ULONG_PTR effectiveInstance;

    if (!phBitmap) return STATUS_INVALID_PARAMETER;

    /* Resolve the owning module. hInstance == 0 means "use the default
     * instance" — historically kernel32 / the executable image. */
    effectiveInstance = hInstance ? hInstance : (ULONG_PTR)0x00010000;

    /* Determine whether lpBitmapName is a MAKEINTRESOURCE-style identifier
     * (value < 0x10000) or a pointer to a string name. MAKEINTRESOURCE
     * packs the integer ID in the low 16 bits with the high bits zero. */
    if (((ULONG_PTR)lpBitmapName) < 0x10000) {
        isResource = TRUE;
        resourceId = (ULONG_PTR)lpBitmapName;
    } else if (lpBitmapName) {
        /* String resource name — record the pointer for later lookup. */
        resourceId = (ULONG_PTR)lpBitmapName;
    }

    for (i = 0; i < MAX_BITMAPS; i++) {
        if (!g_Bitmaps[i].InUse) {
            g_Bitmaps[i].Atom = (ATOM)(0xD000 + i);
            g_Bitmaps[i].Width = 64;
            g_Bitmaps[i].Height = 64;
            g_Bitmaps[i].BitsPerPixel = 32;
            g_Bitmaps[i].Stride = 64 * 4;
            g_Bitmaps[i].DataSize = 64 * 64 * 4;
            g_Bitmaps[i].Scan0 = ExAllocatePool(NonPagedPool, g_Bitmaps[i].DataSize);
            if (g_Bitmaps[i].Scan0) {
                RtlZeroMemory(g_Bitmaps[i].Scan0, g_Bitmaps[i].DataSize);
            }
            /* Record the owning instance so the bitmap can be freed or
             * looked up against the correct module later. */
            g_Bitmaps[i].hInstance = effectiveInstance;
            g_Bitmaps[i].ResourceName = resourceId;
            g_Bitmaps[i].IsResource = isResource || (lpBitmapName != NULL);
            g_Bitmaps[i].InUse = TRUE;
            *phBitmap = g_Bitmaps[i].Atom;
            if (pWidth) *pWidth = g_Bitmaps[i].Width;
            if (pHeight) *pHeight = g_Bitmaps[i].Height;

            if (isResource) {
                DbgPrint("LOADBITS: LoadBitmap hInst=%p resid=%u -> %u (%ux%u)\n",
                         (PVOID)effectiveInstance, (ULONG)resourceId,
                         *phBitmap, g_Bitmaps[i].Width, g_Bitmaps[i].Height);
            } else {
                DbgPrint("LOADBITS: LoadBitmap hInst=%p '%ws' -> %u (%ux%u)\n",
                         (PVOID)effectiveInstance,
                         lpBitmapName ? lpBitmapName : L"(default)",
                         *phBitmap, g_Bitmaps[i].Width, g_Bitmaps[i].Height);
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserCreateBitmap(ULONG Width, ULONG Height, ULONG BitsPerPixel,
                                 PVOID pData, ULONG DataSize, PULONG phBitmap)
{
    ULONG i, stride, totalSize;
    if (!phBitmap) return STATUS_INVALID_PARAMETER;

    stride = ((Width * BitsPerPixel / 8 + 3) & ~3);
    totalSize = stride * Height;

    for (i = 0; i < MAX_BITMAPS; i++) {
        if (!g_Bitmaps[i].InUse) {
            g_Bitmaps[i].Atom = (ATOM)(0xD000 + i);
            g_Bitmaps[i].Width = Width;
            g_Bitmaps[i].Height = Height;
            g_Bitmaps[i].BitsPerPixel = BitsPerPixel;
            g_Bitmaps[i].Stride = stride;
            g_Bitmaps[i].DataSize = totalSize;
            g_Bitmaps[i].Scan0 = ExAllocatePool(NonPagedPool, totalSize);
            if (!g_Bitmaps[i].Scan0) return STATUS_NO_MEMORY;
            if (pData && DataSize >= totalSize) {
                RtlCopyMemory(g_Bitmaps[i].Scan0, pData, totalSize);
            } else {
                RtlZeroMemory(g_Bitmaps[i].Scan0, totalSize);
            }
            g_Bitmaps[i].InUse = TRUE;
            *phBitmap = g_Bitmaps[i].Atom;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserDeleteBitmap(ULONG hBitmap)
{
    ULONG i;
    for (i = 0; i < MAX_BITMAPS; i++) {
        if (g_Bitmaps[i].InUse && g_Bitmaps[i].Atom == (ATOM)hBitmap) {
            if (g_Bitmaps[i].Scan0) ExFreePool(g_Bitmaps[i].Scan0);
            g_Bitmaps[i].InUse = FALSE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetBitmapSize(ULONG hBitmap, PULONG pWidth, PULONG pHeight, PULONG pStride)
{
    ULONG i;
    for (i = 0; i < MAX_BITMAPS; i++) {
        if (g_Bitmaps[i].InUse && g_Bitmaps[i].Atom == (ATOM)hBitmap) {
            if (pWidth) *pWidth = g_Bitmaps[i].Width;
            if (pHeight) *pHeight = g_Bitmaps[i].Height;
            if (pStride) *pStride = g_Bitmaps[i].Stride;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetBitmapBits(ULONG hBitmap, PVOID pBuffer, ULONG BufferSize,
                                  PULONG pBytesRead)
{
    ULONG i, copySize;
    for (i = 0; i < MAX_BITMAPS; i++) {
        if (g_Bitmaps[i].InUse && g_Bitmaps[i].Atom == (ATOM)hBitmap) {
            copySize = g_Bitmaps[i].DataSize;
            if (copySize > BufferSize) copySize = BufferSize;
            if (pBuffer && g_Bitmaps[i].Scan0) {
                RtlCopyMemory(pBuffer, g_Bitmaps[i].Scan0, copySize);
            }
            if (pBytesRead) *pBytesRead = copySize;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserSetBitmapBits(ULONG hBitmap, PVOID pBuffer, ULONG DataSize)
{
    ULONG i, copySize;
    for (i = 0; i < MAX_BITMAPS; i++) {
        if (g_Bitmaps[i].InUse && g_Bitmaps[i].Atom == (ATOM)hBitmap) {
            copySize = (DataSize < g_Bitmaps[i].DataSize) ? DataSize : g_Bitmaps[i].DataSize;
            if (pBuffer && g_Bitmaps[i].Scan0) {
                RtlCopyMemory(g_Bitmaps[i].Scan0, pBuffer, copySize);
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}
