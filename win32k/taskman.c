/*
 * MinNT - win32k/taskman.c
 * Task manager and window enumeration for Win32k.
 */

#include "precomp.h"

#define MAX_TASKS 64

typedef struct _TASK_ENTRY {
    ULONG_PTR Hwnd;
    ULONG     ProcessId;
    ULONG     ThreadId;
    WCHAR     Title[128];
    BOOLEAN   Visible;
    BOOLEAN   InUse;
} TASK_ENTRY, *PTASK_ENTRY;

static TASK_ENTRY g_TaskTable[MAX_TASKS];

NTSTATUS NTAPI TaskManInit(VOID)
{
    RtlZeroMemory(g_TaskTable, sizeof(g_TaskTable));
    DbgPrint("TASKMAN: initialized (%d task slots)\n", MAX_TASKS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserRegisterTaskEntry(ULONG_PTR Hwnd, ULONG ProcessId, ULONG ThreadId, PCWSTR Title)
{
    ULONG i, titleLen;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!g_TaskTable[i].InUse) {
            g_TaskTable[i].Hwnd = Hwnd;
            g_TaskTable[i].ProcessId = ProcessId;
            g_TaskTable[i].ThreadId = ThreadId;
            titleLen = 0;
            if (Title) {
                while (titleLen < 127 && Title[titleLen]) titleLen++;
                RtlCopyMemory(g_TaskTable[i].Title, Title, (titleLen + 1) * sizeof(WCHAR));
            }
            g_TaskTable[i].Title[titleLen] = 0;
            g_TaskTable[i].Visible = TRUE;
            g_TaskTable[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserUnregisterTaskEntry(ULONG_PTR Hwnd)
{
    ULONG i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].Hwnd == Hwnd) {
            g_TaskTable[i].InUse = FALSE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserEnumWindows(PULONG pCount, PULONG_PTR pHwnds, ULONG MaxCount)
{
    ULONG i, count = 0;
    if (!pCount) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse) {
            if (pHwnds && count < MaxCount) {
                pHwnds[count] = g_TaskTable[i].Hwnd;
            }
            count++;
        }
    }
    *pCount = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserEnumTaskWindows(ULONG ProcessId, PULONG pCount, PULONG_PTR pHwnds, ULONG MaxCount)
{
    ULONG i, count = 0;
    if (!pCount) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].ProcessId == ProcessId) {
            if (pHwnds && count < MaxCount) {
                pHwnds[count] = g_TaskTable[i].Hwnd;
            }
            count++;
        }
    }
    *pCount = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetWindowTitle(ULONG_PTR Hwnd, PWCHAR Title, ULONG MaxLen)
{
    ULONG i, len;
    if (!Title || MaxLen == 0) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].Hwnd == Hwnd) {
            len = 0;
            while (len < 127 && g_TaskTable[i].Title[len]) len++;
            if (len >= MaxLen) return STATUS_BUFFER_TOO_SMALL;
            RtlCopyMemory(Title, g_TaskTable[i].Title, (len + 1) * sizeof(WCHAR));
            return STATUS_SUCCESS;
        }
    }
    Title[0] = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserSetWindowTitle(ULONG_PTR Hwnd, PCWSTR Title)
{
    ULONG i, len;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].Hwnd == Hwnd) {
            len = 0;
            if (Title) {
                while (len < 127 && Title[len]) len++;
                RtlCopyMemory(g_TaskTable[i].Title, Title, (len + 1) * sizeof(WCHAR));
            }
            g_TaskTable[i].Title[len] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetWindowThreadProcessId(ULONG_PTR Hwnd, PULONG pProcessId, PULONG pThreadId)
{
    ULONG i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].Hwnd == Hwnd) {
            if (pProcessId) *pProcessId = g_TaskTable[i].ProcessId;
            if (pThreadId) *pThreadId = g_TaskTable[i].ThreadId;
            return STATUS_SUCCESS;
        }
    }
    if (pProcessId) *pProcessId = 0;
    if (pThreadId) *pThreadId = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserIsWindowVisible(ULONG_PTR Hwnd, PBOOL pVisible)
{
    ULONG i;
    if (!pVisible) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < MAX_TASKS; i++) {
        if (g_TaskTable[i].InUse && g_TaskTable[i].Hwnd == Hwnd) {
            *pVisible = g_TaskTable[i].Visible;
            return STATUS_SUCCESS;
        }
    }
    *pVisible = FALSE;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI UserGetWindowTextW(ULONG_PTR Hwnd, PWCHAR Buffer, int MaxCount)
{
    return UserGetWindowTitle(Hwnd, Buffer, (ULONG)MaxCount);
}
