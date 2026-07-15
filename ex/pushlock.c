/*
 * MinNT - ex/pushlock.c
 * Push locks (EX_PUSH_LOCK).
 *
 * A push lock is an in-memory synchronization primitive optimized for
 * the common case where the lock is uncontended. It uses a single
 * ULONG_PTR and a sequence-of-acquires pattern: the holder increments
 * a sequence counter, which both readers and writers must observe.
 *
 * In MinNT we implement two flavours:
 *   - Push lock (shared/exclusive): multiple shared readers, single
 *     exclusive writer
 *   - Auto-boost push lock: same plus priority boost for the holder
 *
 * The implementation uses a 32-bit packed value:
 *   bits  0..15: shared count (0..65535)
 *   bit      16: exclusive flag
 *   bit      17: waiting-writers flag
 *   bits 18..31: must be 0 for the inline path; if set, fall back to
 *                 a stack-allocated wait block
 */

#include <nt/ke.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/framework.h>

#define PUSHLOCK_EXCLUSIVE_BIT   16
#define PUSHLOCK_WAITING_BIT     17
#define PUSHLOCK_SHARED_MASK      0xFFFF

NTSTATUS NTAPI ExInitializePushLock(PPUSH_LOCK Lock)
{
    if (!Lock) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Lock, sizeof(*Lock));
    return STATUS_SUCCESS;
}

VOID NTAPI ExAcquirePushLockShared(PPUSH_LOCK Lock)
{
    if (!Lock) return;
    /* Fast path: uncontended shared acquire. Increment the shared
     * count if the lock isn't currently exclusive. */
    ULONG state = Lock->State;
    for (;;) {
        if ((state & (1 << PUSHLOCK_EXCLUSIVE_BIT)) == 0 &&
            (state & PUSHLOCK_SHARED_MASK) < PUSHLOCK_SHARED_MASK) {
            ULONG newState = (state + 1) & PUSHLOCK_SHARED_MASK;
            /* Atomically: if state still matches, replace with newState. */
            ULONG prev = (ULONG)__sync_val_compare_and_swap(
                (volatile ULONG *)&Lock->State, state, newState);
            if (prev == state) return;
            state = prev;
        } else {
            /* Contended: spin briefly. */
            for (volatile ULONG spin = 0; spin < 100; spin++) { __asm__ __volatile__("pause"); }
            state = Lock->State;
        }
    }
}

VOID NTAPI ExReleasePushLockShared(PPUSH_LOCK Lock)
{
    if (!Lock) return;
    ULONG state = Lock->State;
    for (;;) {
        ULONG count = state & PUSHLOCK_SHARED_MASK;
        if (count == 0) return;  /* not held */
        ULONG newState = state - 1;
        ULONG prev = (ULONG)__sync_val_compare_and_swap(
            (volatile ULONG *)&Lock->State, state, newState);
        if (prev == state) {
            /* If there are waiting writers, wake them. */
            if ((newState & ((1 << PUSHLOCK_WAITING_BIT) | PUSHLOCK_SHARED_MASK)) ==
                (1 << PUSHLOCK_WAITING_BIT)) {
                KeSetEvent(&Lock->WakeEvent, 0, FALSE);
            }
            return;
        }
        state = prev;
    }
}

VOID NTAPI ExAcquirePushLockExclusive(PPUSH_LOCK Lock)
{
    if (!Lock) return;
    ULONG state = Lock->State;
    for (;;) {
        if (state == 0) {
            ULONG newState = (1 << PUSHLOCK_EXCLUSIVE_BIT) | (1 << PUSHLOCK_WAITING_BIT);
            ULONG prev = (ULONG)__sync_val_compare_and_swap(
                (volatile ULONG *)&Lock->State, state, newState);
            if (prev == state) return;
            state = prev;
        } else {
            /* Mark that a writer is waiting (so shared acquires don't
             * keep entering). Then wait. */
            ULONG newState = state | (1 << PUSHLOCK_WAITING_BIT);
            ULONG prev = (ULONG)__sync_val_compare_and_swap(
                (volatile ULONG *)&Lock->State, state, newState);
            if (prev == state) {
                /* Spin briefly before yielding. */
                for (volatile ULONG spin = 0; spin < 1000; spin++) {
                    __asm__ __volatile__("pause");
                    if (Lock->State == 0) break;
                }
                if (Lock->State != 0) {
                    KeWaitForSingleObject(&Lock->WakeEvent, 0, FALSE, FALSE, NULL);
                }
                state = Lock->State;
            } else {
                state = prev;
            }
        }
    }
}

VOID NTAPI ExReleasePushLockExclusive(PPUSH_LOCK Lock)
{
    if (!Lock) return;
    /* Clear exclusive + waiting bits. */
    ULONG state = Lock->State;
    for (;;) {
        ULONG newState = state & ~((1 << PUSHLOCK_EXCLUSIVE_BIT) | (1 << PUSHLOCK_WAITING_BIT));
        ULONG prev = (ULONG)__sync_val_compare_and_swap(
            (volatile ULONG *)&Lock->State, state, newState);
        if (prev == state) {
            if ((newState & PUSHLOCK_SHARED_MASK) > 0 || (newState & (1 << PUSHLOCK_WAITING_BIT))) {
                KeSetEvent(&Lock->WakeEvent, 0, FALSE);
            }
            return;
        }
        state = prev;
    }
}

/* Legacy ERESOURCE-style exclusive acquire for callers that need
 * recursive semantics. */
VOID NTAPI ExAcquirePushLockExclusiveRecursive(PPUSH_LOCK Lock, ULONG Count)
{
    /* MinNT simplification: just acquire exclusive. */
    ExAcquirePushLockExclusive(Lock);
}

ULONG NTAPI ExGetPushLockState(PPUSH_LOCK Lock)
{
    return Lock ? Lock->State : 0;
}
