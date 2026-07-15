/*
 * MinNT - win32k/ex.c
 * Extended window operations for Win32k.
 *
 * Implements GetWindowLong, SetWindowLong, GetClassLong, SetClassLong,
 * GetClassInfo, GetClassName, and property management (SetProp, GetProp,
 * RemoveProp). These are the extended window/class attribute functions.
 */

#include "precomp.h"

#define MAX_PROPS 32
#define MAX_CLASS_LONG 64

/* Window long offsets (GWLP_*) */
#define GWLP_WNDPROC        (-4)
#define GWLP_HINSTANCE       (-6)
#define GWLP_HWNDPARENT      (-8)
#define GWLP_USERDATA        (-21)
#define GWLP_ID              (-12)

/* Class long offsets (GCLP_*) */
#define GCLP_HBRBACKGROUND   (-10)
#define GCLP_HCURSOR         (-12)
#define GCLP_HICON           (-14)
#define GCLP_WNDPROC         (-24)
#define GCLP_HMODULE         (-16)
#define GCLP_CBWNDEXTRA      (-20)
#define GCLP_CBCLSEXTRA      (-18)

/* W32K_PROPERTY and MAX_PROPS are now defined in win32k.h */

/* GetWindowLongPtrW: retrieve window long value */
NTSTATUS NTAPI UserGetWindowLongPtr(ULONG_PTR hWnd, int Index, PLONG_PTR pValue)
{
    WINDOW *pWnd = (WINDOW *)hWnd;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pValue) return STATUS_INVALID_PARAMETER;

    *pValue = 0;

    switch (Index) {
        case GWLP_WNDPROC:
            *pValue = (LONG_PTR)pWnd->lpfnWndProc;
            break;
        case GWLP_HINSTANCE:
            *pValue = (LONG_PTR)pWnd->hInstance;
            break;
        case GWLP_HWNDPARENT:
            *pValue = (LONG_PTR)pWnd->hwndParent;
            break;
        case GWLP_USERDATA:
            /* User data stored after the WINDOW struct in a real implementation.
             * Here we return 0 as no extra storage is allocated. */
            *pValue = 0;
            break;
        case GWLP_ID:
            *pValue = 0;
            break;
        default:
            DbgPrint("EX: GetWindowLongPtr(%p, %d) unknown index\n", (PVOID)hWnd, Index);
            break;
    }

    DbgPrint("EX: GetWindowLongPtr(%p, %d) -> %p\n", (PVOID)hWnd, Index, (PVOID)*pValue);
    return STATUS_SUCCESS;
}

/* SetWindowLongPtrW: set window long value */
NTSTATUS NTAPI UserSetWindowLongPtr(ULONG_PTR hWnd, int Index, LONG_PTR Value, PLONG_PTR pOldValue)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    LONG_PTR oldVal = 0;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pOldValue) return STATUS_INVALID_PARAMETER;

    switch (Index) {
        case GWLP_WNDPROC:
            oldVal = (LONG_PTR)pWnd->lpfnWndProc;
            pWnd->lpfnWndProc = (W32K_WNDPROC)Value;
            break;
        case GWLP_HINSTANCE:
            oldVal = (LONG_PTR)pWnd->hInstance;
            pWnd->hInstance = (ULONG_PTR)Value;
            break;
        case GWLP_HWNDPARENT:
            oldVal = (LONG_PTR)pWnd->hwndParent;
            pWnd->hwndParent = (ULONG_PTR)Value;
            break;
        case GWLP_USERDATA:
            /* Would need extra storage allocation */
            oldVal = 0;
            break;
        case GWLP_ID:
            oldVal = 0;
            break;
        default:
            DbgPrint("EX: SetWindowLongPtr(%p, %d) unknown index\n", (PVOID)hWnd, Index);
            break;
    }

    *pOldValue = oldVal;
    DbgPrint("EX: SetWindowLongPtr(%p, %d, %p) old=%p\n",
             (PVOID)hWnd, Index, (PVOID)Value, (PVOID)oldVal);
    return STATUS_SUCCESS;
}

/* GetClassLongPtrW: retrieve class long value */
NTSTATUS NTAPI UserGetClassLongPtr(ULONG_PTR hWnd, int Index, PLONG_PTR pValue)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    PW32K_CLASS_ENTRY pClass;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pValue) return STATUS_INVALID_PARAMETER;

    *pValue = 0;
    pClass = pWnd->pClass;

    switch (Index) {
        case GCLP_HBRBACKGROUND:
            *pValue = pClass ? (LONG_PTR)pClass->hbrBackground : 0;
            break;
        case GCLP_HCURSOR:
            *pValue = pClass ? (LONG_PTR)pClass->hCursor : 0;
            break;
        case GCLP_HICON:
            *pValue = pClass ? (LONG_PTR)pClass->hIcon : 0;
            break;
        case GCLP_WNDPROC:
            *pValue = (LONG_PTR)pWnd->lpfnWndProc;
            break;
        case GCLP_HMODULE:
            *pValue = (LONG_PTR)pWnd->hInstance;
            break;
        case GCLP_CBWNDEXTRA:
            *pValue = 0;  /* no extra window bytes allocated per class */
            break;
        case GCLP_CBCLSEXTRA:
            *pValue = 0;  /* no extra class bytes allocated per class */
            break;
        default:
            DbgPrint("EX: GetClassLongPtr(%p, %d) unknown index\n", (PVOID)hWnd, Index);
            break;
    }

    return STATUS_SUCCESS;
}

/* SetClassLongPtrW: set class long value */
NTSTATUS NTAPI UserSetClassLongPtr(ULONG_PTR hWnd, int Index, LONG_PTR Value, PLONG_PTR pOldValue)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    PW32K_CLASS_ENTRY pClass;
    LONG_PTR oldVal = 0;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pOldValue) return STATUS_INVALID_PARAMETER;

    pClass = pWnd->pClass;

    switch (Index) {
        case GCLP_HBRBACKGROUND:
            oldVal = pClass ? (LONG_PTR)pClass->hbrBackground : 0;
            if (pClass) pClass->hbrBackground = (HBRUSH)Value;
            break;
        case GCLP_HCURSOR:
            oldVal = pClass ? (LONG_PTR)pClass->hCursor : 0;
            if (pClass) pClass->hCursor = (HCURSOR)Value;
            break;
        case GCLP_HICON:
            oldVal = pClass ? (LONG_PTR)pClass->hIcon : 0;
            if (pClass) pClass->hIcon = (HICON)Value;
            break;
        case GCLP_WNDPROC:
            oldVal = (LONG_PTR)pWnd->lpfnWndProc;
            pWnd->lpfnWndProc = (W32K_WNDPROC)Value;
            break;
        case GCLP_HMODULE:
            oldVal = (LONG_PTR)pWnd->hInstance;
            pWnd->hInstance = (ULONG_PTR)Value;
            break;
        default:
            DbgPrint("EX: SetClassLongPtr(%p, %d) unknown index\n", (PVOID)hWnd, Index);
            break;
    }

    *pOldValue = oldVal;
    return STATUS_SUCCESS;
}

/* GetClassNameW: retrieve window class name */
NTSTATUS NTAPI UserGetClassName(ULONG_PTR hWnd, PWCHAR ClassName, int MaxCount)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    PW32K_CLASS_ENTRY pClass;
    ULONG nameLen;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!ClassName || MaxCount <= 0) return STATUS_INVALID_PARAMETER;

    /* Copy from the actual class entry. */
    pClass = pWnd->pClass;
    if (!pClass || !pClass->inuse) {
        /* No class - fallback to "Window" */
        if (MaxCount >= 7) {
            RtlCopyMemory(ClassName, L"Window", 7 * sizeof(WCHAR));
        } else {
            RtlCopyMemory(ClassName, L"W", 2 * sizeof(WCHAR));
        }
        return STATUS_SUCCESS;
    }

    nameLen = 0;
    while (nameLen < 63 && pClass->szClassName[nameLen]) nameLen++;
    nameLen++; /* include null terminator */

    if ((ULONG)MaxCount < nameLen) {
        /* Buffer too small - copy what fits */
        ULONG copyLen = (ULONG)MaxCount - 1;
        RtlCopyMemory(ClassName, pClass->szClassName, copyLen * sizeof(WCHAR));
        ClassName[copyLen] = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(ClassName, pClass->szClassName, nameLen * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/* SetPropW: add a property to a window */
NTSTATUS NTAPI UserSetProp(ULONG_PTR hWnd, ATOM Atom, HANDLE Value)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    ULONG i;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    /* Check if property already exists - if so, update Value. */
    for (i = 0; i < MAX_PROPS; i++) {
        if (pWnd->Props[i].InUse && pWnd->Props[i].Atom == Atom) {
            pWnd->Props[i].Value = Value;
            DbgPrint("EX: SetProp(%p, atom=0x%04X, val=%p) updated slot %u\n",
                     (PVOID)hWnd, Atom, Value, i);
            return STATUS_SUCCESS;
        }
    }

    /* Find a free slot. */
    for (i = 0; i < MAX_PROPS; i++) {
        if (!pWnd->Props[i].InUse) {
            pWnd->Props[i].Atom = Atom;
            pWnd->Props[i].Value = Value;
            pWnd->Props[i].InUse = TRUE;
            DbgPrint("EX: SetProp(%p, atom=0x%04X, val=%p) -> slot %u\n",
                     (PVOID)hWnd, Atom, Value, i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* GetPropW: retrieve a property from a window */
NTSTATUS NTAPI UserGetProp(ULONG_PTR hWnd, ATOM Atom, PHANDLE pValue)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    ULONG i;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;
    if (!pValue) return STATUS_INVALID_PARAMETER;

    /* Default: not found. */
    *pValue = NULL;

    /* Scan the per-window property list for an entry matching Atom.
     * The Props array lives inside the WINDOW struct (see win32k.h) and
     * each slot is identified by its Atom value with InUse == TRUE. */
    for (i = 0; i < MAX_PROPS; i++) {
        if (pWnd->Props[i].InUse && pWnd->Props[i].Atom == Atom) {
            *pValue = pWnd->Props[i].Value;
            DbgPrint("EX: GetProp(%p, atom=0x%04X) -> %p\n",
                     (PVOID)hWnd, Atom, (PVOID)*pValue);
            return STATUS_SUCCESS;
        }
    }

    DbgPrint("EX: GetProp(%p, atom=0x%04X) not found\n", (PVOID)hWnd, Atom);
    return STATUS_NOT_FOUND;
}

/* RemovePropW: remove a property from a window */
NTSTATUS NTAPI UserRemoveProp(ULONG_PTR hWnd, ATOM Atom)
{
    WINDOW *pWnd = (WINDOW *)hWnd;
    ULONG i;

    if (!pWnd || (ULONG_PTR)pWnd < 0x1000) return STATUS_INVALID_HANDLE;

    for (i = 0; i < MAX_PROPS; i++) {
        if (pWnd->Props[i].InUse && pWnd->Props[i].Atom == Atom) {
            pWnd->Props[i].Atom = 0;
            pWnd->Props[i].Value = NULL;
            pWnd->Props[i].InUse = FALSE;
            DbgPrint("EX: RemoveProp(%p, atom=0x%04X) freed slot %u\n",
                     (PVOID)hWnd, Atom, i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/* GetWindowLongW: 32-bit wrapper for GetWindowLongPtr */
NTSTATUS NTAPI UserGetWindowLong(ULONG_PTR hWnd, int Index, PULONG pValue)
{
    LONG_PTR val = 0;
    NTSTATUS status;

    if (!pValue) return STATUS_INVALID_PARAMETER;

    status = UserGetWindowLongPtr(hWnd, Index, &val);
    *pValue = (ULONG)val;
    return status;
}

/* SetWindowLongW: 32-bit wrapper for SetWindowLongPtr */
NTSTATUS NTAPI UserSetWindowLong(ULONG_PTR hWnd, int Index, ULONG Value, PULONG pOldValue)
{
    LONG_PTR oldVal = 0;
    NTSTATUS status;

    if (!pOldValue) return STATUS_INVALID_PARAMETER;

    status = UserSetWindowLongPtr(hWnd, Index, (LONG_PTR)Value, &oldVal);
    *pOldValue = (ULONG)oldVal;
    return status;
}
