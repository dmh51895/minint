/*
 * MinNT - diag/reliability.c
 * Reliability Monitor - tracks system stability over time.
 *
 * Records daily stability metrics:
 *   - Number of crashes (BugCheck events)
 *   - Number of application failures
 *   - Number of failed driver loads
 *   - Number of disk errors
 *   - Number of service failures
 *   - Overall stability index (0.0 - 10.0, 10.0 = perfect)
 *
 * Persisted per-day in the registry under
 * \Registry\Machine\Software\Microsoft\Windows NT\CurrentVersion\Reliability
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/cm.h>

#define MAX_RELIABILITY_DAYS 30

typedef struct _RELIABILITY_RECORD {
    ULONG64 Date;                /* days since 2000-01-01 */
    ULONG  Crashes;
    ULONG  AppFailures;
    ULONG  DriverFailures;
    ULONG  DiskErrors;
    ULONG  ServiceFailures;
    ULONG  TotalFailures;
    /* Stability index scaled by 10: 100 = 10.0 (perfect), 50 = 5.0. */
    ULONG  StabilityIndexX10;
    BOOLEAN InUse;
} RELIABILITY_RECORD, *PRELIABILITY_RECORD;

static RELIABILITY_RECORD g_Reliability[MAX_RELIABILITY_DAYS];
static KSPIN_LOCK g_ReliabilityLock;

/* Convert a tick count (100ns units since boot) to a day index. */
static ULONG64 TickToDayIndex(ULONG64 Tick)
{
    /* 1 day = 86400 seconds = 86400000 ms = 86400000 KeTickCount units. */
    return Tick / 86400000ULL;
}

NTSTATUS NTAPI ReliabilityInit(VOID)
{
    RtlZeroMemory(g_Reliability, sizeof(g_Reliability));
    KeInitializeSpinLock(&g_ReliabilityLock);
    DbgPrint("RELIABILITY: monitor initialized (%d day history)\n", MAX_RELIABILITY_DAYS);
    return STATUS_SUCCESS;
}

/* Find or allocate the record for a given day. */
static PRELIABILITY_RECORD GetOrAllocRecord(ULONG64 Day, BOOLEAN Create)
{
    ULONG i;
    PRELIABILITY_RECORD oldest = NULL;
    ULONG64 oldestDay = (ULONG64)-1;
    for (i = 0; i < MAX_RELIABILITY_DAYS; i++) {
        if (g_Reliability[i].InUse && g_Reliability[i].Date == Day) {
            return &g_Reliability[i];
        }
        if (g_Reliability[i].InUse && g_Reliability[i].Date < oldestDay) {
            oldestDay = g_Reliability[i].Date;
            oldest = &g_Reliability[i];
        }
    }
    if (!Create) return NULL;
    /* No match - find a free slot or evict oldest */
    for (i = 0; i < MAX_RELIABILITY_DAYS; i++) {
        if (!g_Reliability[i].InUse) {
            RtlZeroMemory(&g_Reliability[i], sizeof(RELIABILITY_RECORD));
            g_Reliability[i].InUse = TRUE;
            g_Reliability[i].Date = Day;
            g_Reliability[i].StabilityIndexX10 = 100;
            return &g_Reliability[i];
        }
    }
    /* All slots full - evict oldest */
    if (oldest) {
        RtlZeroMemory(oldest, sizeof(RELIABILITY_RECORD));
        oldest->InUse = TRUE;
        oldest->Date = Day;
        oldest->StabilityIndexX10 = 100;
        return oldest;
    }
    return NULL;
}

/* Recompute the stability index for a record. */
static VOID RecomputeStability(PRELIABILITY_RECORD r)
{
    r->TotalFailures = r->Crashes + r->AppFailures + r->DriverFailures +
                         r->DiskErrors + r->ServiceFailures;
    /* Each failure subtracts 5 from scaled stability (100 = 10.0 baseline).
     * Floor at 0. */
    LONG s = (LONG)100 - (LONG)r->TotalFailures * 5;
    if (s < 0) s = 0;
    r->StabilityIndexX10 = (ULONG)s;
}

/* Record a crash (bugcheck). */
NTSTATUS NTAPI ReliabilityRecordCrash(VOID)
{
    KIRQL irql;
    ULONG64 day = TickToDayIndex((ULONG64)KeTickCount);
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    {
        PRELIABILITY_RECORD r = GetOrAllocRecord(day, TRUE);
        if (r) {
            r->Crashes++;
            RecomputeStability(r);
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return STATUS_SUCCESS;
}

/* Record an application failure. */
NTSTATUS NTAPI ReliabilityRecordAppFailure(VOID)
{
    KIRQL irql;
    ULONG64 day = TickToDayIndex((ULONG64)KeTickCount);
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    {
        PRELIABILITY_RECORD r = GetOrAllocRecord(day, TRUE);
        if (r) {
            r->AppFailures++;
            RecomputeStability(r);
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return STATUS_SUCCESS;
}

/* Record a driver load failure. */
NTSTATUS NTAPI ReliabilityRecordDriverFailure(VOID)
{
    KIRQL irql;
    ULONG64 day = TickToDayIndex((ULONG64)KeTickCount);
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    {
        PRELIABILITY_RECORD r = GetOrAllocRecord(day, TRUE);
        if (r) {
            r->DriverFailures++;
            RecomputeStability(r);
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return STATUS_SUCCESS;
}

/* Record a disk error. */
NTSTATUS NTAPI ReliabilityRecordDiskError(VOID)
{
    KIRQL irql;
    ULONG64 day = TickToDayIndex((ULONG64)KeTickCount);
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    {
        PRELIABILITY_RECORD r = GetOrAllocRecord(day, TRUE);
        if (r) {
            r->DiskErrors++;
            RecomputeStability(r);
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return STATUS_SUCCESS;
}

/* Record a service failure. */
NTSTATUS NTAPI ReliabilityRecordServiceFailure(VOID)
{
    KIRQL irql;
    ULONG64 day = TickToDayIndex((ULONG64)KeTickCount);
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    {
        PRELIABILITY_RECORD r = GetOrAllocRecord(day, TRUE);
        if (r) {
            r->ServiceFailures++;
            RecomputeStability(r);
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return STATUS_SUCCESS;
}

/* Get the overall stability index (average over the last MAX_RELIABILITY_DAYS).
 * Returns scaled value (100 = 10.0 perfect). */
ULONG NTAPI ReliabilityGetIndex(VOID)
{
    ULONG sum = 0;
    ULONG count = 0;
    ULONG i;
    KIRQL irql;
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    for (i = 0; i < MAX_RELIABILITY_DAYS; i++) {
        if (g_Reliability[i].InUse) {
            sum += g_Reliability[i].StabilityIndexX10;
            count++;
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    if (count == 0) return 100;
    return sum / count;
}

/* Enumerate daily records. */
ULONG NTAPI ReliabilityEnumDays(ULONG MaxCount, PULONG64 pDates,
                                  PULONG pCrashes, PULONG pAppFailures,
                                  PULONG pDriverFailures, PULONG pDiskErrors,
                                  PULONG pServiceFailures)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ReliabilityLock, &irql);
    for (i = 0; i < MAX_RELIABILITY_DAYS && n < MaxCount; i++) {
        if (g_Reliability[i].InUse) {
            if (pDates) pDates[n] = g_Reliability[i].Date;
            if (pCrashes) pCrashes[n] = g_Reliability[i].Crashes;
            if (pAppFailures) pAppFailures[n] = g_Reliability[i].AppFailures;
            if (pDriverFailures) pDriverFailures[n] = g_Reliability[i].DriverFailures;
            if (pDiskErrors) pDiskErrors[n] = g_Reliability[i].DiskErrors;
            if (pServiceFailures) pServiceFailures[n] = g_Reliability[i].ServiceFailures;
            n++;
        }
    }
    KeReleaseSpinLock(&g_ReliabilityLock, &irql);
    return n;
}
