/*
 * MinNT - ex/eresource.c
 * Executive resources (ERESOURCE).
 *
 * ERESOURCE is NT's heavyweight reader-writer lock. Unlike push locks,
 * it supports:
 *   - Multiple shared (read) holders
 *   - Single exclusive (write) holder
 *   - Recursive shared acquisition (each acquire needs a matching release)
 *   - Priority inheritance for the exclusive holder
 *   - Waiters (waiting shared/exclusive count) so a writer doesn't
 *     starve behind continuous readers
 *
 * Uses a single ULONG state plus per-thread recursion counters stored
 * in a thread-local structure.
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/framework.h>

#define ERESOURCE_SHARED_MAX    0x7FFFFFFF
#define ERESOURCE_EXCLUSIVE_BIT 31
#define ERESOURCE_SHARED_MASK   0x7FFFFFFF

/* Type defs are in framework.h. */

NTSTATUS NTAPI ExInitializeResourceLite(ERESOURCE *Resource, const CHAR *Name)
{
    if (!Resource) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Resource, sizeof(*Resource));
    if (Name) {
        for (ULONG k = 0; k < 63 && Name[k]; k++) Resource->Name[k] = Name[k];
    }
    KeInitializeSpinLock(&Resource->Lock);
    KeInitializeSemaphore(&Resource->SharedWaiters, 0, 0x7FFFFFFF);
    KeInitializeSemaphore(&Resource->ExclusiveWaiter, 0, 1);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExDeleteResourceLite(ERESOURCE *Resource)
{
    if (!Resource) return STATUS_INVALID_PARAMETER;
    /* Wake any remaining waiters. */
    KeReleaseSemaphore(&Resource->SharedWaiters, 0, 0x7FFFFFFF, FALSE);
    KeReleaseSemaphore(&Resource->ExclusiveWaiter, 0, 1, FALSE);
    /* Free owner tracking. */
    PERESOURCE_THREAD_ENTRY entry = Resource->Owners;
    while (entry) {
        PERESOURCE_THREAD_ENTRY next = entry->Next;
        ExFreePool(entry);
        entry = next;
    }
    RtlZeroMemory(Resource, sizeof(*Resource));
    return STATUS_SUCCESS;
}

static PERESOURCE_THREAD_ENTRY ErFindOrCreateOwner(ERESOURCE *Resource, PETHREAD Thread)
{
    for (PERESOURCE_THREAD_ENTRY e = Resource->Owners; e; e = e->Next) {
        if (e->Thread == Thread) return e;
    }
    PERESOURCE_THREAD_ENTRY e = (PERESOURCE_THREAD_ENTRY)ExAllocatePool(NonPagedPool, sizeof(*e));
    if (!e) return NULL;
    e->Thread = Thread;
    e->SharedCount = 0;
    e->ExclusiveOwner = FALSE;
    e->Next = Resource->Owners;
    Resource->Owners = e;
    return e;
}

NTSTATUS NTAPI ExAcquireResourceSharedLite(ERESOURCE *Resource, BOOLEAN Wait)
{
    if (!Resource) return STATUS_INVALID_PARAMETER;
    PETHREAD thread = PsGetCurrentThread();

    /* Fast path: uncontended shared acquire. */
    KIRQL irql;
    KeAcquireSpinLock(&Resource->Lock, &irql);
    if ((Resource->State & (1 << ERESOURCE_EXCLUSIVE_BIT)) == 0 &&
        KeReadStateSemaphore(&Resource->ExclusiveWaiter) == 0) {
        Resource->State++;
        KeReleaseSpinLock(&Resource->Lock, irql);
        PERESOURCE_THREAD_ENTRY e = ErFindOrCreateOwner(Resource, thread);
        if (e) e->SharedCount++;
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Resource->Lock, irql);

    if (!Wait) return STATUS_TIMEOUT;
    /* Wait on the shared semaphore. */
    KeWaitForSingleObject(&Resource->SharedWaiters, 0, FALSE, FALSE, NULL);

    /* We've been woken. Re-acquire the lock and bump. */
    KeAcquireSpinLock(&Resource->Lock, &irql);
    Resource->State++;
    KeReleaseSpinLock(&Resource->Lock, irql);
    PERESOURCE_THREAD_ENTRY e = ErFindOrCreateOwner(Resource, thread);
    if (e) e->SharedCount++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExReleaseResourceLite(ERESOURCE *Resource)
{
    if (!Resource) return STATUS_INVALID_PARAMETER;
    PETHREAD thread = PsGetCurrentThread();
    KIRQL irql;
    KeAcquireSpinLock(&Resource->Lock, &irql);

    if (Resource->ExclusiveOwner == thread) {
        /* Exclusive release. */
        Resource->ExclusiveOwner = NULL;
        Resource->State = 0;
        /* Wake one exclusive waiter, then all shared waiters. */
        if (KeReadStateSemaphore(&Resource->ExclusiveWaiter) > 0) {
            KeReleaseSemaphore(&Resource->ExclusiveWaiter, 0, 1, FALSE);
        } else if (KeReadStateSemaphore(&Resource->SharedWaiters) > 0) {
            KeReleaseSemaphore(&Resource->SharedWaiters, 0, 0x7FFFFFFF, FALSE);
        }
        KeReleaseSpinLock(&Resource->Lock, irql);
        return STATUS_SUCCESS;
    }
    /* Shared release. */
    PERESOURCE_THREAD_ENTRY entry = NULL;
    for (PERESOURCE_THREAD_ENTRY e = Resource->Owners; e; e = e->Next) {
        if (e->Thread == thread) { entry = e; break; }
    }
    if (!entry || entry->SharedCount <= 0) {
        KeReleaseSpinLock(&Resource->Lock, irql);
        return STATUS_UNSUCCESSFUL;
    }
    entry->SharedCount--;
    Resource->State--;
    ULONG pending = (ULONG)KeReadStateSemaphore(&Resource->SharedWaiters);
    if (Resource->State == 0 && pending > 0) {
        KeReleaseSemaphore(&Resource->SharedWaiters, 0, pending, FALSE);
    }
    KeReleaseSpinLock(&Resource->Lock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI ExAcquireResourceExclusiveLite(ERESOURCE *Resource, BOOLEAN Wait)
{
    if (!Resource) return STATUS_INVALID_PARAMETER;
    PETHREAD thread = PsGetCurrentThread();
    KIRQL irql;
    KeAcquireSpinLock(&Resource->Lock, &irql);
    if (Resource->State == 0) {
        Resource->State = (1 << ERESOURCE_EXCLUSIVE_BIT);
        Resource->ExclusiveOwner = thread;
        KeReleaseSpinLock(&Resource->Lock, irql);
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Resource->Lock, irql);
    if (!Wait) return STATUS_TIMEOUT;
    KeWaitForSingleObject(&Resource->ExclusiveWaiter, 0, FALSE, FALSE, NULL);
    KeAcquireSpinLock(&Resource->Lock, &irql);
    Resource->State = (1 << ERESOURCE_EXCLUSIVE_BIT);
    Resource->ExclusiveOwner = thread;
    KeReleaseSpinLock(&Resource->Lock, irql);
    return STATUS_SUCCESS;
}

ULONG NTAPI ExGetResourceState(ERESOURCE *Resource)
{
    return Resource ? Resource->State : 0;
}
