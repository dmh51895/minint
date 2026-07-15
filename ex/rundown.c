/*
 * MinNT - ex/rundown.c
 * Executive rundown protection (EX_RUNDOWN_REF).
 *
 * Rundown protection prevents new acquires from succeeding once
 * "rundown" has begun. This is used to safely wait for outstanding
 * references to drain before freeing a structure.
 *
 * State machine:
 *   - Counted acquires: ExAcquireRundownProtection increments ref
 *   - ExReleaseRundownProtection decrements
 *   - ExWaitForRundownProtectionAwait: blocks until count is 0
 *   - ExRundownCompleted: marks the structure as in rundown (no new
 *     acquires allowed)
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/framework.h>

NTSTATUS NTAPI ExInitializeRundownProtection(PRUNDOWN_REF Ref)
{
    if (!Ref) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Ref, sizeof(*Ref));
    return STATUS_SUCCESS;
}

VOID NTAPI ExAcquireRundownProtection(PRUNDOWN_REF Ref)
{
    if (!Ref) return;
    /* Increment count atomically, but refuse if rundown completed. */
    ULONG count = (ULONG)__sync_add_and_fetch((volatile ULONG *)&Ref->Count, 1);
    /* Count went from 0 to 1. If we previously marked the rundown,
     * decrement and fail. */
    if (Ref->RundownCompleted) {
        if (count == 1) {
            /* We're the first acquire-after-rundown: wake any waiter. */
            KeSetEvent(&Ref->RundownEvent, 0, FALSE);
        }
        __sync_sub_and_fetch((volatile ULONG *)&Ref->Count, 1);
        /* Caller would normally wait on a flag. MinNT returns without
         * blocking; the caller is responsible for skipping work. */
    }
}

VOID NTAPI ExReleaseRundownProtection(PRUNDOWN_REF Ref)
{
    if (!Ref) return;
    ULONG count = (ULONG)__sync_sub_and_fetch((volatile ULONG *)&Ref->Count, 1);
    if (count == 0 && Ref->RundownCompleted) {
        /* Last reference released after rundown; wake waiters. */
        KeSetEvent(&Ref->RundownEvent, 0, FALSE);
    }
}

VOID NTAPI ExWaitForRundownProtectionRelease(PRUNDOWN_REF Ref)
{
    if (!Ref) return;
    if (Ref->Count == 0) return;
    /* Initialize the event on first use. */
    if (!Ref->EventInitialized) {
        KeInitializeEvent(&Ref->RundownEvent, NotificationEvent, FALSE);
        Ref->EventInitialized = TRUE;
    }
    KeWaitForSingleObject(&Ref->RundownEvent, 0, FALSE, FALSE, NULL);
}

NTSTATUS NTAPI ExWaitForRundownProtectionReleaseAsync(PRUNDOWN_REF Ref, PKEVENT Event)
{
    if (!Ref || !Event) return STATUS_INVALID_PARAMETER;
    KeWaitForSingleObject(Event, 0, FALSE, FALSE, NULL);
    return STATUS_SUCCESS;
}

VOID NTAPI ExRundownCompleted(PRUNDOWN_REF Ref)
{
    if (!Ref) return;
    Ref->RundownCompleted = TRUE;
    /* If count is already zero, wake waiters immediately. */
    if (Ref->Count == 0) {
        if (!Ref->EventInitialized) {
            KeInitializeEvent(&Ref->RundownEvent, NotificationEvent, FALSE);
            Ref->EventInitialized = TRUE;
        }
        KeSetEvent(&Ref->RundownEvent, 0, FALSE);
    }
}

BOOLEAN NTAPI ExAcquireRundownProtectionEx(PRUNDOWN_REF Ref, ULONG Count)
{
    if (!Ref || Count == 0) return FALSE;
    for (ULONG i = 0; i < Count; i++) ExAcquireRundownProtection(Ref);
    return !Ref->RundownCompleted;
}

ULONG NTAPI ExGetRundownProtectionCount(PRUNDOWN_REF Ref)
{
    return Ref ? Ref->Count : 0;
}
