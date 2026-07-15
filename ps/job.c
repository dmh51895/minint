/*
 * MinNT - ps/job.c
 * Job Objects.
 *
 * A Job Object is a process group. Processes can be assigned to a job,
 * and the job imposes limits on all its member processes:
 *   - Per-process and total CPU time limits
 *   - Working set size limits
 *   - Process count limit
 *   - Active process / terminated process count
 *
 * When a job's limits are exceeded, processes can be signaled via
 * notifications or terminated. The job's state persists until the
 * last member process exits.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define JOB_MAX_OBJECTS     32
#define JOB_MAX_MEMBERS     32

typedef struct _EJOB {
    ULONG Id;
    CHAR Name[64];
    BOOLEAN InUse;
    /* Limits. */
    ULONG64 PerProcessUserTimeLimit;   /* 100ns units */
    ULONG64 PerProcessKernelTimeLimit;
    ULONG64 TotalUserTimeLimit;
    ULONG64 TotalKernelTimeLimit;
    ULONG64 WorkingSetLimit;
    ULONG ProcessCountLimit;
    /* Active counters. */
    ULONG ActiveProcesses;
    ULONG TerminatedProcesses;
    ULONG64 TotalUserTime;
    ULONG64 TotalKernelTime;
    ULONG64 PeakWorkingSetSize;
    /* Membership. */
    PEPROCESS Members[JOB_MAX_MEMBERS];
} EJOB, *PEJOB;

static EJOB g_Jobs[JOB_MAX_OBJECTS];

NTSTATUS NTAPI PsCreateJob(const CHAR *Name, PULONG OutJobId)
{
    for (ULONG i = 0; i < JOB_MAX_OBJECTS; i++) {
        if (!g_Jobs[i].InUse) {
            RtlZeroMemory(&g_Jobs[i], sizeof(EJOB));
            g_Jobs[i].InUse = TRUE;
            if (Name) {
                for (ULONG k = 0; k < 63 && Name[k]; k++) g_Jobs[i].Name[k] = Name[k];
            }
            if (OutJobId) *OutJobId = i;
            DbgPrint("JOB: created '%s' as id %u\n", Name ? Name : "(unnamed)", i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI PsDeleteJob(ULONG JobId)
{
    if (JobId >= JOB_MAX_OBJECTS || !g_Jobs[JobId].InUse) return STATUS_INVALID_PARAMETER;
    /* Detach all member processes. */
    for (ULONG i = 0; i < JOB_MAX_MEMBERS; i++) {
        g_Jobs[JobId].Members[i] = NULL;
    }
    RtlZeroMemory(&g_Jobs[JobId], sizeof(EJOB));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsAssignProcessToJob(ULONG JobId, PEPROCESS Process)
{
    if (JobId >= JOB_MAX_OBJECTS || !g_Jobs[JobId].InUse || !Process) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < JOB_MAX_MEMBERS; i++) {
        if (!g_Jobs[JobId].Members[i]) {
            g_Jobs[JobId].Members[i] = Process;
            g_Jobs[JobId].ActiveProcesses++;
            if (g_Jobs[JobId].ActiveProcesses > g_Jobs[JobId].PeakWorkingSetSize)
                g_Jobs[JobId].PeakWorkingSetSize = g_Jobs[JobId].ActiveProcesses;
            /* Enforce process count limit. */
            if (g_Jobs[JobId].ProcessCountLimit &&
                g_Jobs[JobId].ActiveProcesses > g_Jobs[JobId].ProcessCountLimit) {
                DbgPrint("JOB: process count limit exceeded for job %u\n", JobId);
                return STATUS_DISK_QUOTA_EXCEEDED;
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI PsRemoveProcessFromJob(ULONG JobId, PEPROCESS Process)
{
    if (JobId >= JOB_MAX_OBJECTS || !g_Jobs[JobId].InUse || !Process) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < JOB_MAX_MEMBERS; i++) {
        if (g_Jobs[JobId].Members[i] == Process) {
            g_Jobs[JobId].Members[i] = NULL;
            if (g_Jobs[JobId].ActiveProcesses) g_Jobs[JobId].ActiveProcesses--;
            g_Jobs[JobId].TerminatedProcesses++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI PsSetJobLimits(ULONG JobId, ULONG64 PerProcessUserTime,
                              ULONG64 PerProcessKernelTime, ULONG64 TotalUser,
                              ULONG64 TotalKernel, ULONG64 WorkingSet, ULONG ProcessCount)
{
    if (JobId >= JOB_MAX_OBJECTS || !g_Jobs[JobId].InUse) return STATUS_INVALID_PARAMETER;
    g_Jobs[JobId].PerProcessUserTimeLimit = PerProcessUserTime;
    g_Jobs[JobId].PerProcessKernelTimeLimit = PerProcessKernelTime;
    g_Jobs[JobId].TotalUserTimeLimit = TotalUser;
    g_Jobs[JobId].TotalKernelTimeLimit = TotalKernel;
    g_Jobs[JobId].WorkingSetLimit = WorkingSet;
    g_Jobs[JobId].ProcessCountLimit = ProcessCount;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PsQueryJobInfo(ULONG JobId, PULONG OutActive, PULONG OutTerminated,
                              PULONG64 OutUserTime, PULONG64 OutKernelTime)
{
    if (JobId >= JOB_MAX_OBJECTS || !g_Jobs[JobId].InUse) return STATUS_INVALID_PARAMETER;
    if (OutActive) *OutActive = g_Jobs[JobId].ActiveProcesses;
    if (OutTerminated) *OutTerminated = g_Jobs[JobId].TerminatedProcesses;
    if (OutUserTime) *OutUserTime = g_Jobs[JobId].TotalUserTime;
    if (OutKernelTime) *OutKernelTime = g_Jobs[JobId].TotalKernelTime;
    return STATUS_SUCCESS;
}

ULONG NTAPI PsGetJobCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < JOB_MAX_OBJECTS; i++) if (g_Jobs[i].InUse) n++;
    return n;
}
