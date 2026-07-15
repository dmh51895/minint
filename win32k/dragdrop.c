/*
 * MinNT - win32k/dragdrop.c
 * Drag and drop support for Win32k.
 *
 * Implements DoDragDrop, RegisterDragDrop, RevokeDragDrop,
 * DragEnter, DragOver, DragLeave, Drop. Provides OLE-style
 * drag and drop through IDropTarget/IDropSource interfaces.
 */

#include "precomp.h"

#define MAX_DROPTARGETS 32

/* Standard OLE drop effect flags (from winuser.h / objidl.h). */
#define DROPEFFECT_NONE   0
#define DROPEFFECT_COPY   1
#define DROPEFFECT_MOVE   2
#define DROPEFFECT_LINK   4

typedef struct _W32K_DROPSOURCE {
    ULONG_PTR  Hwnd;
    ULONG      DataFormat;
    ULONG      Effect;
    BOOLEAN    InUse;
} W32K_DROPSOURCE, *PW32K_DROPSOURCE;

typedef struct _W32K_DROPTARGET {
    ULONG_PTR  Hwnd;
    ULONG_PTR  Callback;
    ULONG      AllowedEffects;
    BOOLEAN    InUse;
} W32K_DROPTARGET, *PW32K_DROPTARGET;

typedef struct _W32K_DRAGSTATE {
    BOOLEAN    Active;
    ULONG_PTR  HwndSource;
    ULONG_PTR  HwndTarget;
    ULONG      DropEffect;
    ULONG      DataFormat;       /* clipboard format being dragged */
    ULONG      AllowedEffects;   /* effects source permits */
    LONG       MouseX;
    LONG       MouseY;
    ULONG      KeyState;
} W32K_DRAGSTATE;

static W32K_DROPSOURCE g_DropSources[MAX_DROPTARGETS];
static W32K_DROPTARGET g_DropTargets[MAX_DROPTARGETS];
static W32K_DRAGSTATE  g_DragState;

NTSTATUS NTAPI DragDropInit(VOID)
{
    RtlZeroMemory(g_DropSources, sizeof(g_DropSources));
    RtlZeroMemory(g_DropTargets, sizeof(g_DropTargets));
    RtlZeroMemory(&g_DragState, sizeof(g_DragState));

    DbgPrint("DRAGDROP: initialized (%d target slots)\n", MAX_DROPTARGETS);
    return STATUS_SUCCESS;
}

/* RegisterDragDrop: associate a window with an IDropTarget */
NTSTATUS NTAPI UserRegisterDragDrop(ULONG_PTR Hwnd, ULONG_PTR Callback)
{
    ULONG i;

    for (i = 0; i < MAX_DROPTARGETS; i++) {
        if (!g_DropTargets[i].InUse) {
            g_DropTargets[i].Hwnd = Hwnd;
            g_DropTargets[i].Callback = Callback;
            g_DropTargets[i].AllowedEffects = 0x7; /* DROPEFFECT_COPY|MOVE|LINK */
            g_DropTargets[i].InUse = TRUE;

            DbgPrint("DRAGDROP: RegisterDragDrop hwnd=%p -> slot %u\n", (PVOID)Hwnd, i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* RevokeDragDrop: remove drop target registration */
NTSTATUS NTAPI UserRevokeDragDrop(ULONG_PTR Hwnd)
{
    ULONG i;

    for (i = 0; i < MAX_DROPTARGETS; i++) {
        if (g_DropTargets[i].InUse && g_DropTargets[i].Hwnd == Hwnd) {
            g_DropTargets[i].InUse = FALSE;

            DbgPrint("DRAGDROP: RevokeDragDrop hwnd=%p\n", (PVOID)Hwnd);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/* DoDragDrop: initiate a drag and drop operation */
NTSTATUS NTAPI UserDoDragDrop(ULONG_PTR HwndSource, ULONG DataFormat,
                               ULONG AllowedEffects, PULONG pEffect)
{
    ULONG targetEffects = 0;
    ULONG filtered;
    BOOLEAN haveTarget = FALSE;
    ULONG i;

    if (!pEffect) return STATUS_INVALID_PARAMETER;

    g_DragState.Active = TRUE;
    g_DragState.HwndSource = HwndSource;
    g_DragState.HwndTarget = 0;
    g_DragState.DropEffect = 0;
    g_DragState.KeyState = 0;
    g_DragState.DataFormat = DataFormat;
    /* Record what the source allows so subsequent DragOver/Drop calls can
     * keep intersecting against the target's accepted effects. */
    g_DragState.AllowedEffects = AllowedEffects;

    /* Look up the target registration to determine what effects the
     * target accepts. If no target window has been registered yet we
     * fall back to DROPEFFECT_NONE. */
    for (i = 0; i < MAX_DROPTARGETS; i++) {
        if (g_DropTargets[i].InUse) {
            targetEffects = g_DropTargets[i].AllowedEffects;
            g_DragState.HwndTarget = g_DropTargets[i].Hwnd;
            haveTarget = TRUE;
            break;
        }
    }

    /* The resulting effect is the intersection of what the source allows
     * (AllowedEffects) and what the target accepts (targetEffects). */
    filtered = AllowedEffects & targetEffects;

    /* Pick a preferred effect from the intersection, honouring the
     * conventional precedence COPY > MOVE > LINK. */
    if (filtered & DROPEFFECT_COPY) {
        g_DragState.DropEffect = DROPEFFECT_COPY;
    } else if (filtered & DROPEFFECT_MOVE) {
        g_DragState.DropEffect = DROPEFFECT_MOVE;
    } else if (filtered & DROPEFFECT_LINK) {
        g_DragState.DropEffect = DROPEFFECT_LINK;
    } else {
        g_DragState.DropEffect = DROPEFFECT_NONE;
    }

    *pEffect = g_DragState.DropEffect;

    DbgPrint("DRAGDROP: DoDragDrop src=%p fmt=%u allowed=0x%X target=%s "
             "targetFx=0x%X -> effect=0x%X\n",
             (PVOID)HwndSource, DataFormat, AllowedEffects,
             haveTarget ? "yes" : "no",
             targetEffects, *pEffect);

    g_DragState.Active = FALSE;
    return STATUS_SUCCESS;
}

/* UserDragEnter: notify target of drag entering */
NTSTATUS NTAPI UserDragEnter(ULONG_PTR HwndTarget, ULONG KeyState, LONG X, LONG Y, PULONG pEffect)
{
    if (!pEffect) return STATUS_INVALID_PARAMETER;

    g_DragState.HwndTarget = HwndTarget;
    g_DragState.MouseX = X;
    g_DragState.MouseY = Y;
    g_DragState.KeyState = KeyState;

    /* Find target's allowed effects */
    {
        ULONG i;
        for (i = 0; i < MAX_DROPTARGETS; i++) {
            if (g_DropTargets[i].InUse && g_DropTargets[i].Hwnd == HwndTarget) {
                *pEffect = g_DropTargets[i].AllowedEffects & 0x7;
                DbgPrint("DRAGDROP: DragEnter hwnd=%p effect=0x%X\n",
                         (PVOID)HwndTarget, *pEffect);
                return STATUS_SUCCESS;
            }
        }
    }

    *pEffect = 0;
    return STATUS_NOT_FOUND;
}

/* UserDragOver: update drag feedback as mouse moves */
NTSTATUS NTAPI UserDragOver(ULONG KeyState, LONG X, LONG Y, PULONG pEffect)
{
    if (!pEffect) return STATUS_INVALID_PARAMETER;

    g_DragState.MouseX = X;
    g_DragState.MouseY = Y;
    g_DragState.KeyState = KeyState;

    *pEffect = g_DragState.DropEffect;
    return STATUS_SUCCESS;
}

/* UserDragLeave: notify target that drag left */
NTSTATUS NTAPI UserDragLeave(VOID)
{
    g_DragState.HwndTarget = 0;
    DbgPrint("DRAGDROP: DragLeave\n");
    return STATUS_SUCCESS;
}

/* UserDrop: complete the drop operation */
NTSTATUS NTAPI UserDrop(LONG X, LONG Y, PULONG pEffect)
{
    if (!pEffect) return STATUS_INVALID_PARAMETER;

    g_DragState.MouseX = X;
    g_DragState.MouseY = Y;

    *pEffect = g_DragState.DropEffect;

    DbgPrint("DRAGDROP: Drop at (%d,%d) effect=%u\n", X, Y, *pEffect);

    g_DragState.Active = FALSE;
    g_DragState.HwndTarget = 0;
    return STATUS_SUCCESS;
}

/* UserIsDragDropActive: check if a drag operation is in progress */
NTSTATUS NTAPI UserIsDragDropActive(PULONG pActive)
{
    if (!pActive) return STATUS_INVALID_PARAMETER;

    *pActive = g_DragState.Active ? 1 : 0;
    return STATUS_SUCCESS;
}

/* UserGetDragDropData: get the data source for current drag */
NTSTATUS NTAPI UserGetDragDropData(PULONG pHwndSource, PULONG pFormat)
{
    if (!pHwndSource || !pFormat) return STATUS_INVALID_PARAMETER;

    *pHwndSource = (ULONG)g_DragState.HwndSource;
    *pFormat = g_DragState.DataFormat;
    return STATUS_SUCCESS;
}
