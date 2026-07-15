/*
 * MinNT - win32k/validate.c
 * Window handle validation for Win32k.
 */

#include "precomp.h"

#define MAX_VALID_HANDLES 256

typedef struct _VALID_HANDLE_ENTRY {
    PVOID   Object;
    ULONG   Type;
    ULONG   Access;
    ULONG   RefCount;
    BOOLEAN InUse;
} VALID_HANDLE_ENTRY, *PVALID_HANDLE_ENTRY;

static VALID_HANDLE_ENTRY g_ValidHandles[MAX_VALID_HANDLES];

NTSTATUS NTAPI ValidateInit(VOID)
{
    RtlZeroMemory(g_ValidHandles, sizeof(g_ValidHandles));
    DbgPrint("VALIDATE: initialized (%d handle slots)\n", MAX_VALID_HANDLES);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHandle(ULONG_PTR Handle, ULONG ObjectType, PBOOL pValid)
{
    ULONG i;
    if (!pValid) return STATUS_INVALID_PARAMETER;

    *pValid = FALSE;
    for (i = 0; i < MAX_VALID_HANDLES; i++) {
        if (g_ValidHandles[i].InUse &&
            (ULONG_PTR)g_ValidHandles[i].Object == Handle &&
            g_ValidHandles[i].Type == ObjectType) {
            *pValid = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserRegisterHandle(PVOID Object, ULONG ObjectType, ULONG Access, PULONG pHandle)
{
    ULONG i;
    if (!Object || !pHandle) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_VALID_HANDLES; i++) {
        if (!g_ValidHandles[i].InUse) {
            g_ValidHandles[i].Object = Object;
            g_ValidHandles[i].Type = ObjectType;
            g_ValidHandles[i].Access = Access;
            g_ValidHandles[i].RefCount = 1;
            g_ValidHandles[i].InUse = TRUE;
            *pHandle = i + 1;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserUnregisterHandle(ULONG Handle)
{
    ULONG idx = Handle - 1;
    if (idx >= MAX_VALID_HANDLES) return STATUS_INVALID_PARAMETER;
    if (!g_ValidHandles[idx].InUse) return STATUS_INVALID_HANDLE;

    g_ValidHandles[idx].InUse = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserLockHandle(ULONG Handle, PVOID *pObject)
{
    ULONG idx = Handle - 1;
    if (!pObject) return STATUS_INVALID_PARAMETER;
    if (idx >= MAX_VALID_HANDLES) return STATUS_INVALID_PARAMETER;
    if (!g_ValidHandles[idx].InUse) return STATUS_INVALID_HANDLE;

    *pObject = g_ValidHandles[idx].Object;
    g_ValidHandles[idx].RefCount++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserUnlockHandle(ULONG Handle)
{
    ULONG idx = Handle - 1;

    /* Validate the handle range first. */
    if (idx >= MAX_VALID_HANDLES) return STATUS_INVALID_PARAMETER;

    /* The entry must currently be in use. */
    if (!g_ValidHandles[idx].InUse) return STATUS_INVALID_HANDLE;

    /* Guard against unbalanced unlock calls that would underflow RefCount. */
    if (g_ValidHandles[idx].RefCount == 0) {
        DbgPrint("VALIDATE: UserUnlockHandle(%u) refcount already zero\n", Handle);
        return STATUS_INVALID_HANDLE;
    }

    g_ValidHandles[idx].RefCount--;

    /* When the last reference is dropped, release the slot so it can be
     * recycled by a subsequent UserRegisterHandle call. The Object pointer
     * is cleared to avoid dangling references. */
    if (g_ValidHandles[idx].RefCount == 0) {
        DbgPrint("VALIDATE: UserUnlockHandle(%u) refcount -> 0, releasing slot\n", Handle);
        g_ValidHandles[idx].Object = NULL;
        g_ValidHandles[idx].Type = 0;
        g_ValidHandles[idx].Access = 0;
        g_ValidHandles[idx].InUse = FALSE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHwnd(ULONG_PTR Hwnd, PWINDOW *ppWnd)
{
    if (!ppWnd) return STATUS_INVALID_PARAMETER;

    if (!Hwnd || Hwnd < 0x1000) {
        *ppWnd = NULL;
        return STATUS_INVALID_HANDLE;
    }

    *ppWnd = (WINDOW *)Hwnd;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHdc(ULONG_PTR Hdc, PVOID *ppDc)
{
    if (!ppDc) return STATUS_INVALID_PARAMETER;

    if (!Hdc || Hdc < 0x1000) {
        *ppDc = NULL;
        return STATUS_INVALID_HANDLE;
    }

    *ppDc = (PVOID)Hdc;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHbrush(ULONG_PTR Hbrush, PVOID *ppBrush)
{
    if (!ppBrush) return STATUS_INVALID_PARAMETER;
    *ppBrush = (PVOID)Hbrush;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHpen(ULONG_PTR Hpen, PVOID *ppPen)
{
    if (!ppPen) return STATUS_INVALID_PARAMETER;
    *ppPen = (PVOID)Hpen;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHfont(ULONG_PTR Hfont, PVOID *ppFont)
{
    if (!ppFont) return STATUS_INVALID_PARAMETER;
    *ppFont = (PVOID)Hfont;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserValidateHrgn(ULONG_PTR Hrgn, PVOID *ppRgn)
{
    if (!ppRgn) return STATUS_INVALID_PARAMETER;
    *ppRgn = (PVOID)Hrgn;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserReferenceObject(ULONG_PTR Handle, PVOID *pObj)
{
    return UserLockHandle((ULONG)Handle, pObj);
}

NTSTATUS NTAPI UserDereferenceObject(ULONG_PTR Handle)
{
    return UserUnlockHandle((ULONG)Handle);
}
