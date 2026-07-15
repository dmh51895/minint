/*
 * MinNT - win32k/timers.c
 * Timer management for Win32k.
 */

#include "precomp.h"

#define MAX_TIMERS 64

typedef struct _W32K_TIMER {
    ULONG_PTR Hwnd;
    ULONG_PTR TimerId;
    ULONG     Elapse;
    ULONG     Interval;
    ULONG_PTR Callback;
    ULONG     TimeStamp;
    BOOLEAN   Active;
    BOOLEAN   InUse;
} W32K_TIMER, *PW32K_TIMER;

KSPIN_LOCK g_TimerLock;
static W32K_TIMER g_Timers[MAX_TIMERS];

NTSTATUS NTAPI TimersInit(VOID)
{
    RtlZeroMemory(g_Timers, sizeof(g_Timers));
    KeInitializeSpinLock(&g_TimerLock);
    DbgPrint("TIMERS: initialized (%d timer slots)\n", MAX_TIMERS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserSetTimer(ULONG_PTR hWnd, ULONG_PTR TimerId, ULONG Elapse,
                             ULONG_PTR TimerFunc)
{
    KIRQL Irql;
    ULONG i;

    W32K_LOCK_SPIN(g_TimerLock, Irql);

    /* Check if timer already exists, update it */
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_Timers[i].InUse && g_Timers[i].Hwnd == hWnd && g_Timers[i].TimerId == TimerId) {
            g_Timers[i].Elapse = Elapse;
            g_Timers[i].Interval = Elapse;
            g_Timers[i].Callback = TimerFunc;
            g_Timers[i].TimeStamp = (ULONG)KeTickCount;
            g_Timers[i].Active = TRUE;
            W32K_UNLOCK_SPIN(g_TimerLock, Irql);
            DbgPrint("TIMERS: SetTimer updated hwnd=%p id=%p elapse=%u\n",
                     (PVOID)hWnd, (PVOID)TimerId, Elapse);
            return STATUS_SUCCESS;
        }
    }

    /* Create new timer */
    for (i = 0; i < MAX_TIMERS; i++) {
        if (!g_Timers[i].InUse) {
            g_Timers[i].Hwnd = hWnd;
            g_Timers[i].TimerId = TimerId;
            g_Timers[i].Elapse = Elapse;
            g_Timers[i].Interval = Elapse;
            g_Timers[i].Callback = TimerFunc;
            g_Timers[i].TimeStamp = (ULONG)KeTickCount;
            g_Timers[i].Active = TRUE;
            g_Timers[i].InUse = TRUE;
            W32K_UNLOCK_SPIN(g_TimerLock, Irql);
            DbgPrint("TIMERS: SetTimer hwnd=%p id=%p elapse=%u -> slot %u\n",
                     (PVOID)hWnd, (PVOID)TimerId, Elapse, i);
            return STATUS_SUCCESS;
        }
    }

    W32K_UNLOCK_SPIN(g_TimerLock, Irql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserKillTimer(ULONG_PTR hWnd, ULONG_PTR TimerId)
{
    KIRQL Irql;
    ULONG i;

    W32K_LOCK_SPIN(g_TimerLock, Irql);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_Timers[i].InUse && g_Timers[i].Hwnd == hWnd && g_Timers[i].TimerId == TimerId) {
            g_Timers[i].Active = FALSE;
            g_Timers[i].InUse = FALSE;
            W32K_UNLOCK_SPIN(g_TimerLock, Irql);
            DbgPrint("TIMERS: KillTimer hwnd=%p id=%p\n", (PVOID)hWnd, (PVOID)TimerId);
            return STATUS_SUCCESS;
        }
    }
    W32K_UNLOCK_SPIN(g_TimerLock, Irql);
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserCheckTimers(VOID)
{
    KIRQL Irql;
    ULONG i;
    ULONG now = (ULONG)KeTickCount;

    W32K_LOCK_SPIN(g_TimerLock, Irql);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_Timers[i].InUse && g_Timers[i].Active) {
            if ((now - g_Timers[i].TimeStamp) >= g_Timers[i].Elapse) {
                g_Timers[i].TimeStamp = now;
                W32K_UNLOCK_SPIN(g_TimerLock, Irql);
                if (g_Timers[i].Callback) {
                    ((VOID (NTAPI *)(HWND, ULONG, ULONG_PTR, LONG_PTR))g_Timers[i].Callback)(
                        (HWND)g_Timers[i].Hwnd, WM_TIMER, g_Timers[i].TimerId, 0);
                } else {
                    UserPostMessage(g_Timers[i].Hwnd, WM_TIMER, g_Timers[i].TimerId, 0);
                }
                W32K_LOCK_SPIN(g_TimerLock, Irql);
            }
        }
    }
    W32K_UNLOCK_SPIN(g_TimerLock, Irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetSystemTimeAsFileTime(PULONG pLowPart, PULONG pHighPart)
{
    LARGE_INTEGER li;
    KeQuerySystemTime(&li);
    if (pLowPart) *pLowPart = li.LowPart;
    if (pHighPart) *pHighPart = li.HighPart;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetTickCount(PULONG pTicks)
{
    if (!pTicks) return STATUS_INVALID_PARAMETER;
    *pTicks = (ULONG)KeTickCount;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserQueryPerformanceCounter(PLONG64 pCounter, PLONG64 pFrequency)
{
    if (pCounter) *pCounter = (LONG64)KeTickCount * 10000; /* 100ns units */
    if (pFrequency) *pFrequency = 10000000LL; /* 100MHz */
    return STATUS_SUCCESS;
}
