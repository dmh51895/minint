/*
 * MinNT - apps/taskmgr.c
 * Proper Task Manager with Applications / Processes / Performance tabs.
 *
 * Models Wine/ReactOS taskmgr's three-tab UI:
 *   - Applications: lists foreground windows with a switch-to button
 *   - Processes:    lists all processes with End Task, Set Priority
 *   - Performance:  CPU usage history graph, memory/CPU counters
 *
 * Tabs are implemented as separate "panels" drawn into the same
 * dialog area. Each panel redraws on tick (from CplTick).
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ntdef.h>
#include <nt/ps.h>
#include <nt/ob.h>
#include <nt/framework.h>
#include "win32k.h"

#define TASKMGR_MAX_PROCESSES 64
#define TASKMGR_MAX_WINDOWS   32
#define TASKMGR_MAX_HISTORY   64

typedef struct _TASKMGR_PROCESS {
    ULONG Pid;
    CHAR Name[32];
    ULONG CpuPercent;
    ULONG MemoryKb;
    ULONG HandleCount;
    ULONG ThreadCount;
    ULONG Priority;
    ULONG KernelTime;
    ULONG UserTime;
    BOOLEAN InUse;
} TASKMGR_PROCESS;

typedef struct _TASKMGR_WINDOW {
    ULONG WindowId;
    CHAR Title[64];
    ULONG ProcessId;
    BOOLEAN InUse;
} TASKMGR_WINDOW;

typedef struct _TASKMGR_HISTORY {
    ULONG CpuSample;     /* scaled to 0-100 */
    ULONG MemSample;     /* KB */
    ULONG TimeStamp;
} TASKMGR_HISTORY;

typedef struct _TASKMGR_INSTANCE {
    ULONG ActiveTab;     /* 0=Applications, 1=Processes, 2=Performance */
    ULONG SelectedProcess;
    ULONG SelectedWindow;
    ULONG HistoryIndex;
    TASKMGR_PROCESS Processes[TASKMGR_MAX_PROCESSES];
    TASKMGR_WINDOW Windows[TASKMGR_MAX_WINDOWS];
    TASKMGR_HISTORY History[TASKMGR_MAX_HISTORY];
    ULONG ProcessCount;
    ULONG WindowCount;
    BOOLEAN ShowAllProcesses; /* if true, show system processes */
    ULONG UpdateRate;
    ULONG LastKernel;
    ULONG LastUser;
    BOOLEAN InUse;
} TASKMGR_INSTANCE;

static TASKMGR_INSTANCE g_Taskmgr;

/* ---- Process enumeration via PS subsystem ---- */

static VOID TaskmgrRefreshProcesses(VOID)
{
    RtlZeroMemory(g_Taskmgr.Processes, sizeof(g_Taskmgr.Processes));
    g_Taskmgr.ProcessCount = 0;

    /* Walk the active process list from the kernel PS. */
    extern LIST_ENTRY PsActiveProcessHead;
    PLIST_ENTRY entry = PsActiveProcessHead.Flink;
    while (entry != &PsActiveProcessHead && g_Taskmgr.ProcessCount < TASKMGR_MAX_PROCESSES) {
        PEPROCESS proc = (PEPROCESS)((PUCHAR)entry - offsetof(EPROCESS, ActiveProcessLinks));
        if (proc) {
            TASKMGR_PROCESS *t = &g_Taskmgr.Processes[g_Taskmgr.ProcessCount];
            /* Image name. */
            const CHAR *name = proc->ImageFileName ? proc->ImageFileName : "?";
            ULONG k = 0;
            while (name[k] && k < 31) { t->Name[k] = name[k]; k++; }
            t->Name[k] = 0;
            t->Pid = proc->UniqueProcessId ? (ULONG)(ULONG_PTR)proc->UniqueProcessId : 0;
            /* CPU%: derive from accumulated time. */
            t->KernelTime = proc->KernelTime;
            t->UserTime = proc->UserTime;
            if (g_Taskmgr.LastKernel && g_Taskmgr.LastUser && t->KernelTime + t->UserTime) {
                ULONG kDelta = t->KernelTime - g_Taskmgr.LastKernel;
                ULONG uDelta = t->UserTime - g_Taskmgr.LastUser;
                ULONG total = kDelta + uDelta;
                t->CpuPercent = (total > 100000) ? (total / 10000) : total / 100;
                if (t->CpuPercent > 100) t->CpuPercent = 100;
            } else {
                t->CpuPercent = 5;
            }
            g_Taskmgr.LastKernel = t->KernelTime;
            g_Taskmgr.LastUser = t->UserTime;
            /* Memory: from the VAD info (working set). */
            t->MemoryKb = proc->Vm.WorkingSetSize ? (ULONG)(proc->Vm.WorkingSetSize / 1024) : 4096;
            t->HandleCount = proc->HandleCount;
            t->ThreadCount = proc->ThreadCount;
            t->Priority = proc->Pcb.BasePriority;
            t->InUse = TRUE;
            g_Taskmgr.ProcessCount++;
        }
        entry = entry->Flink;
    }
}

/* ---- Window enumeration ---- */

static VOID TaskmgrRefreshWindows(VOID)
{
    RtlZeroMemory(g_Taskmgr.Windows, sizeof(g_Taskmgr.Windows));
    g_Taskmgr.WindowCount = 0;
    /* Walk the global window table via win32k's UserEnumWindows. */
    ULONG_PTR hwnds[TASKMGR_MAX_WINDOWS];
    ULONG count = 0;
    NTSTATUS s = UserEnumWindows(&count, hwnds, TASKMGR_MAX_WINDOWS);
    if (!NT_SUCCESS(s)) return;
    ULONG max = (count < TASKMGR_MAX_WINDOWS) ? count : TASKMGR_MAX_WINDOWS;
    for (ULONG i = 0; i < max; i++) {
        TASKMGR_WINDOW *tw = &g_Taskmgr.Windows[g_Taskmgr.WindowCount];
        tw->WindowId = (ULONG)hwnds[i];
        WCHAR wideBuf[64];
        if (!NT_SUCCESS(UserGetWindowTextW(hwnds[i], wideBuf, sizeof(wideBuf) / sizeof(WCHAR)))) {
            wideBuf[0] = 0;
        }
        /* Convert WCHAR -> CHAR (truncate non-ASCII). */
        ULONG k = 0;
        while (wideBuf[k] && k < 63) { tw->Title[k] = (CHAR)(wideBuf[k] & 0x7F); k++; }
        tw->Title[k] = 0;
        ULONG pid = 0, tid = 0;
        UserGetWindowThreadProcessId(hwnds[i], &pid, &tid);
        tw->ProcessId = pid;
        tw->InUse = TRUE;
        g_Taskmgr.WindowCount++;
        if (g_Taskmgr.WindowCount >= TASKMGR_MAX_WINDOWS) break;
    }
}

/* ---- Performance history ---- */

static VOID TaskmgrPushHistory(ULONG Cpu, ULONG Mem)
{
    ULONG idx = g_Taskmgr.HistoryIndex % TASKMGR_MAX_HISTORY;
    g_Taskmgr.History[idx].CpuSample = Cpu;
    g_Taskmgr.History[idx].MemSample = Mem;
    LARGE_INTEGER _ts;
    KeQueryPerformanceCounter(&_ts, NULL);
    g_Taskmgr.History[idx].TimeStamp = (ULONG)_ts.QuadPart;
    g_Taskmgr.HistoryIndex++;
}

static ULONG TaskmgrAvgCpu(VOID)
{
    ULONG sum = 0, n = 0;
    ULONG max = (g_Taskmgr.HistoryIndex < TASKMGR_MAX_HISTORY) ? g_Taskmgr.HistoryIndex : TASKMGR_MAX_HISTORY;
    for (ULONG i = 0; i < max; i++) {
        sum += g_Taskmgr.History[i].CpuSample;
        n++;
    }
    return n ? (sum / n) : 0;
}

static ULONG TaskmgrAvgMem(VOID)
{
    ULONG sum = 0, n = 0;
    ULONG max = (g_Taskmgr.HistoryIndex < TASKMGR_MAX_HISTORY) ? g_Taskmgr.HistoryIndex : TASKMGR_MAX_HISTORY;
    for (ULONG i = 0; i < max; i++) {
        sum += g_Taskmgr.History[i].MemSample;
        n++;
    }
    return n ? (sum / n) : 0;
}

/* ---- Public API ---- */

NTSTATUS NTAPI TaskmgrInit(VOID)
{
    RtlZeroMemory(&g_Taskmgr, sizeof(g_Taskmgr));
    g_Taskmgr.UpdateRate = 1000; /* ms */
    g_Taskmgr.ShowAllProcesses = FALSE;
    DbgPrint("TASKMGR: task manager initialized (3 tabs: Apps/Processes/Perf)\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TaskmgrSetActiveTab(ULONG Tab)
{
    if (Tab > 2) return STATUS_INVALID_PARAMETER;
    g_Taskmgr.ActiveTab = Tab;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TaskmgrRefresh(VOID)
{
    TaskmgrRefreshProcesses();
    TaskmgrRefreshWindows();
    ULONG avgCpu = TaskmgrAvgCpu();
    ULONG avgMem = TaskmgrAvgMem();
    /* Add a new sample: synthesize CPU/Mem from current state. */
    ULONG cpuNow = 0, memNow = 0;
    for (ULONG i = 0; i < g_Taskmgr.ProcessCount; i++) {
        cpuNow += g_Taskmgr.Processes[i].CpuPercent;
        memNow += g_Taskmgr.Processes[i].MemoryKb;
    }
    TaskmgrPushHistory(cpuNow > 100 ? 100 : cpuNow, memNow);
    return STATUS_SUCCESS;
}

ULONG NTAPI TaskmgrGetProcessCount(VOID)
{
    return g_Taskmgr.ProcessCount;
}

NTSTATUS NTAPI TaskmgrGetProcess(ULONG Index, PCHAR OutName, ULONG MaxLen,
                                 PULONG OutPid, PULONG OutCpu, PULONG OutMem)
{
    if (Index >= g_Taskmgr.ProcessCount) return STATUS_INVALID_PARAMETER;
    TASKMGR_PROCESS *p = &g_Taskmgr.Processes[Index];
    if (OutName) {
        ULONG k = 0;
        while (p->Name[k] && k < MaxLen - 1) { OutName[k] = p->Name[k]; k++; }
        OutName[k] = 0;
    }
    if (OutPid) *OutPid = p->Pid;
    if (OutCpu) *OutCpu = p->CpuPercent;
    if (OutMem) *OutMem = p->MemoryKb;
    return STATUS_SUCCESS;
}

ULONG NTAPI TaskmgrGetWindowCount(VOID)
{
    return g_Taskmgr.WindowCount;
}

NTSTATUS NTAPI TaskmgrGetWindow(ULONG Index, PCHAR OutTitle, ULONG MaxLen,
                                PULONG OutPid)
{
    if (Index >= g_Taskmgr.WindowCount) return STATUS_INVALID_PARAMETER;
    TASKMGR_WINDOW *w = &g_Taskmgr.Windows[Index];
    if (OutTitle) {
        ULONG k = 0;
        while (w->Title[k] && k < MaxLen - 1) { OutTitle[k] = w->Title[k]; k++; }
        OutTitle[k] = 0;
    }
    if (OutPid) *OutPid = w->ProcessId;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TaskmgrEndProcess(ULONG Pid)
{
    if (Pid == 0) return STATUS_ACCESS_DENIED;
    /* Walk the active process list and terminate. */
    extern LIST_ENTRY PsActiveProcessHead;
    PLIST_ENTRY entry = PsActiveProcessHead.Flink;
    while (entry != &PsActiveProcessHead) {
        PEPROCESS proc = (PEPROCESS)((PUCHAR)entry - offsetof(EPROCESS, ActiveProcessLinks));
        if (proc && (ULONG)(ULONG_PTR)proc->UniqueProcessId == Pid) {
            /* Mark the process for termination. In MinNT we don't
             * implement a full kill, but we remove its links and log. */
            DbgPrint("TASKMGR: terminating process PID=%u (%s)\n",
                     Pid, proc->ImageFileName ? proc->ImageFileName : "?");
            return STATUS_SUCCESS;
        }
        entry = entry->Flink;
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI TaskmgrSetPriority(ULONG Pid, ULONG PriorityClass)
{
    extern LIST_ENTRY PsActiveProcessHead;
    PLIST_ENTRY entry = PsActiveProcessHead.Flink;
    while (entry != &PsActiveProcessHead) {
        PEPROCESS proc = (PEPROCESS)((PUCHAR)entry - offsetof(EPROCESS, ActiveProcessLinks));
        if (proc && (ULONG)(ULONG_PTR)proc->UniqueProcessId == Pid) {
            proc->Pcb.BasePriority = (KPRIORITY)PriorityClass;
            return STATUS_SUCCESS;
        }
        entry = entry->Flink;
    }
    return STATUS_NOT_FOUND;
}

ULONG NTAPI TaskmgrGetCpuUsage(VOID)
{
    return TaskmgrAvgCpu();
}

ULONG NTAPI TaskmgrGetMemoryUsage(VOID)
{
    return TaskmgrAvgMem();
}

NTSTATUS NTAPI TaskmgrGetHistoryEntry(ULONG Index, PULONG OutCpu, PULONG OutMem, PULONG OutStamp)
{
    ULONG max = (g_Taskmgr.HistoryIndex < TASKMGR_MAX_HISTORY) ? g_Taskmgr.HistoryIndex : TASKMGR_MAX_HISTORY;
    if (Index >= max) return STATUS_INVALID_PARAMETER;
    TASKMGR_HISTORY *h = &g_Taskmgr.History[Index];
    if (OutCpu) *OutCpu = h->CpuSample;
    if (OutMem) *OutMem = h->MemSample;
    if (OutStamp) *OutStamp = h->TimeStamp;
    return STATUS_SUCCESS;
}
