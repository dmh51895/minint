/*
 * MinNT - win32k/capture.c
 * Mouse capture and tracking for Win32k.
 *
 * Implements SetCapture, ReleaseCapture, GetCapture, SetCaptureFlags,
 * and mouse tracking (TrackMouseEvent). Manages per-thread capture state
 * and provides hit-testing support for non-client areas.
 */

#include "precomp.h"

#define MAX_CAPTURE_THREADS 32

typedef struct _CAPTURE_INFO {
    ULONG_PTR  HwndCapture;
    ULONG_PTR  HwndTrack;
    ULONG      ThreadId;
    ULONG      Flags;
    BOOLEAN    InUse;
    BOOLEAN    TrackHover;
    BOOLEAN    TrackLeave;
    ULONG      HoverTime;
    W32K_POINT LastMousePos;
} CAPTURE_INFO, *PCAPTURE_INFO;

static CAPTURE_INFO g_CaptureTable[MAX_CAPTURE_THREADS];

/* Internal: find or create capture entry for current thread */
static PCAPTURE_INFO CaptureFindEntry(ULONG ThreadId, BOOLEAN Create)
{
    ULONG i;
    ULONG freeSlot = MAX_CAPTURE_THREADS;

    for (i = 0; i < MAX_CAPTURE_THREADS; i++) {
        if (g_CaptureTable[i].InUse && g_CaptureTable[i].ThreadId == ThreadId) {
            return &g_CaptureTable[i];
        }
        if (!g_CaptureTable[i].InUse && freeSlot == MAX_CAPTURE_THREADS) {
            freeSlot = i;
        }
    }

    if (Create && freeSlot < MAX_CAPTURE_THREADS) {
        RtlZeroMemory(&g_CaptureTable[freeSlot], sizeof(CAPTURE_INFO));
        g_CaptureTable[freeSlot].ThreadId = ThreadId;
        g_CaptureTable[freeSlot].InUse = TRUE;
        return &g_CaptureTable[freeSlot];
    }

    return NULL;
}

/* UserSetCapture: redirect all mouse input to specified window */
NTSTATUS NTAPI UserSetCapture2(ULONG_PTR hWnd)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;
    ULONG_PTR oldCapture;

    entry = CaptureFindEntry(tid, TRUE);
    if (!entry) return STATUS_INSUFFICIENT_RESOURCES;

    oldCapture = entry->HwndCapture;
    entry->HwndCapture = hWnd;

    DbgPrint("CAPTURE: SetCapture hwnd=%p old=%p tid=%u\n",
             (PVOID)hWnd, (PVOID)oldCapture, tid);
    return STATUS_SUCCESS;
}

/* UserReleaseCapture: stop capturing mouse input */
NTSTATUS NTAPI UserReleaseCapture(VOID)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    entry = CaptureFindEntry(tid, FALSE);
    if (!entry) return STATUS_NOT_FOUND;

    DbgPrint("CAPTURE: ReleaseCapture old=%p tid=%u\n",
             (PVOID)entry->HwndCapture, tid);
    entry->HwndCapture = 0;
    return STATUS_SUCCESS;
}

/* UserGetCapture: get current capture window */
NTSTATUS NTAPI UserGetCapture(PULONG_PTR pHwnd)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    if (!pHwnd) return STATUS_INVALID_PARAMETER;

    entry = CaptureFindEntry(tid, FALSE);
    *pHwnd = entry ? entry->HwndCapture : 0;

    return STATUS_SUCCESS;
}

/* UserSetCaptureFlags: set capture behavior flags */
NTSTATUS NTAPI UserSetCaptureFlags(ULONG_PTR hWnd, ULONG Flags)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    entry = CaptureFindEntry(tid, TRUE);
    if (!entry) return STATUS_INSUFFICIENT_RESOURCES;

    entry->HwndCapture = hWnd;
    entry->Flags = Flags;

    DbgPrint("CAPTURE: SetCaptureFlags hwnd=%p flags=0x%X\n",
             (PVOID)hWnd, Flags);
    return STATUS_SUCCESS;
}

/* UserTrackMouseEvent: set up mouse tracking for a window */
NTSTATUS NTAPI UserTrackMouseEvent(ULONG_PTR HwndTrack, ULONG HoverTime)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    entry = CaptureFindEntry(tid, TRUE);
    if (!entry) return STATUS_INSUFFICIENT_RESOURCES;

    entry->HwndTrack = HwndTrack;
    entry->TrackHover = TRUE;
    entry->TrackLeave = TRUE;
    entry->HoverTime = (HoverTime == (ULONG)-1) ? 400 : HoverTime;

    DbgPrint("CAPTURE: TrackMouseEvent hwnd=%p hover=%u\n",
             (PVOID)HwndTrack, entry->HoverTime);
    return STATUS_SUCCESS;
}

/* UserGetCaptureWindow: find which window should receive mouse message */
NTSTATUS NTAPI UserGetCaptureWindow(ULONG_PTR *pHwnd, ULONG *pThreadId)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    if (!pHwnd) return STATUS_INVALID_PARAMETER;

    entry = CaptureFindEntry(tid, FALSE);
    if (entry && entry->HwndCapture) {
        *pHwnd = entry->HwndCapture;
        if (pThreadId) *pThreadId = tid;
        return STATUS_SUCCESS;
    }

    *pHwnd = 0;
    if (pThreadId) *pThreadId = 0;
    return STATUS_NOT_FOUND;
}

/* UserUpdateMousePosition: update tracked position for hover detection */
NTSTATUS NTAPI UserUpdateMousePosition(LONG x, LONG y)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    entry = CaptureFindEntry(tid, FALSE);
    if (!entry) return STATUS_SUCCESS;

    entry->LastMousePos.x = x;
    entry->LastMousePos.y = y;

    return STATUS_SUCCESS;
}

/* UserReleaseCaptureForWindow: release capture only if it matches */
NTSTATUS NTAPI UserReleaseCaptureForWindow(ULONG_PTR hWnd)
{
    ULONG_PTR tid = (ULONG_PTR)PsGetCurrentThreadId();
    PCAPTURE_INFO entry;

    entry = CaptureFindEntry(tid, FALSE);
    if (!entry) return STATUS_NOT_FOUND;

    if (entry->HwndCapture == hWnd) {
        entry->HwndCapture = 0;
        DbgPrint("CAPTURE: Released capture for hwnd=%p\n", (PVOID)hWnd);
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_FOUND;
}
