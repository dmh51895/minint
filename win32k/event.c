/*
 * MinNT - win32k/event.c
 * Event handling for Win32k input and window messages.
 *
 * Implements WaitMessage, MsgWaitForMultipleObjects, InputEvent,
 * and input event queue management. Handles keyboard and mouse
 * event generation from hardware interrupts.
 */

#include "precomp.h"

#define MAX_INPUT_EVENTS 128

typedef struct _INPUT_EVENT {
    ULONG   Type;
    ULONG   Message;
    ULONG_PTR wParam;
    LONG_PTR lParam;
    ULONG   Time;
    BOOLEAN InUse;
} INPUT_EVENT, *PINPUT_EVENT;

typedef struct _INPUT_EVENT_QUEUE {
    INPUT_EVENT Events[MAX_INPUT_EVENTS];
    ULONG       Head;
    ULONG       Tail;
    ULONG       Count;
    KSPIN_LOCK  Lock;
    KEVENT      Event;
} INPUT_EVENT_QUEUE;

static INPUT_EVENT_QUEUE g_InputQueue;

/* Input event types */
#define INPUT_KEYBD     1
#define INPUT_MOUSE     2
#define INPUT_HARDWARE  3

NTSTATUS NTAPI EventInit(VOID)
{
    RtlZeroMemory(&g_InputQueue, sizeof(g_InputQueue));
    KeInitializeSpinLock(&g_InputQueue.Lock);
    KeInitializeEvent(&g_InputQueue.Event, SynchronizationEvent, FALSE);

    DbgPrint("EVENT: input queue initialized (%d slots)\n", MAX_INPUT_EVENTS);
    return STATUS_SUCCESS;
}

/* Internal: enqueue an input event */
static NTSTATUS EventEnqueue(ULONG Type, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    KIRQL Irql;
    PINPUT_EVENT pEvent;

    KeAcquireSpinLock(&g_InputQueue.Lock, &Irql);

    if (g_InputQueue.Count >= MAX_INPUT_EVENTS) {
        KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pEvent = &g_InputQueue.Events[g_InputQueue.Tail];
    pEvent->Type = Type;
    pEvent->Message = Msg;
    pEvent->wParam = wParam;
    pEvent->lParam = lParam;
    pEvent->Time = (ULONG)KeTickCount;
    pEvent->InUse = TRUE;

    g_InputQueue.Tail = (g_InputQueue.Tail + 1) % MAX_INPUT_EVENTS;
    g_InputQueue.Count++;

    KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
    KeSetEvent(&g_InputQueue.Event, 0, FALSE);

    return STATUS_SUCCESS;
}

/* UserInjectKeyboardEvent: inject a keyboard event into the input queue */
NTSTATUS NTAPI UserInjectKeyboardEvent(ULONG VirtualKey, ULONG ScanCode, ULONG Flags)
{
    ULONG msg;
    NTSTATUS status;

    if (Flags & 0x80) {
        /* Key up */
        msg = (VirtualKey >= 0x20 && VirtualKey <= 0x2E) ? WM_SYSKEYUP : WM_KEYUP;
    } else {
        /* Key down */
        msg = (VirtualKey >= 0x20 && VirtualKey <= 0x2E) ? WM_SYSKEYDOWN : WM_KEYDOWN;
    }

    status = EventEnqueue(INPUT_KEYBD, msg, (ULONG_PTR)VirtualKey,
                          (LONG_PTR)(ScanCode | (Flags << 16)));

    DbgPrint("EVENT: InjectKeyboardEvent vk=0x%X scan=%u flags=0x%X -> msg=0x%04X\n",
             VirtualKey, ScanCode, Flags, msg);
    return status;
}

/* UserInjectMouseEvent: inject a mouse event into the input queue */
NTSTATUS NTAPI UserInjectMouseEvent(LONG X, LONG Y, ULONG Buttons, ULONG Wheel)
{
    ULONG msg;
    ULONG_PTR wParam;
    LONG_PTR lParam;
    NTSTATUS status;

    if (Buttons & 0x1) {
        msg = (Buttons & 0x2) ? WM_LBUTTONUP : WM_LBUTTONDOWN;
    } else if (Buttons & 0x4) {
        msg = (Buttons & 0x8) ? WM_RBUTTONUP : WM_RBUTTONDOWN;
    } else {
        msg = WM_MOUSEMOVE;
    }

    wParam = (ULONG_PTR)Buttons;
    lParam = (LONG_PTR)((Y << 16) | (X & 0xFFFF));

    status = EventEnqueue(INPUT_MOUSE, msg, wParam, lParam);
    if (!NT_SUCCESS(status)) return status;

    /* If a wheel delta was supplied, also post a WM_MOUSEWHEEL message.
     * The HIWORD of wParam carries the signed wheel delta (typically
     * +/- WHEEL_DELTA=120), the LOWORD carries the current key state,
     * and lParam carries the screen-relative cursor position. */
    if (Wheel != 0) {
        ULONG_PTR wheelWp;
        LONG_PTR  wheelLp;

        /* Key state: mirror the button bits into the LOWORD so consumers
         * can detect which mouse buttons / modifiers were held. */
        wheelWp = (ULONG_PTR)((ULONG)Buttons | (((ULONG)(LONG)Wheel) << 16));
        wheelLp = (LONG_PTR)((Y << 16) | (X & 0xFFFF));

        status = EventEnqueue(INPUT_MOUSE, WM_MOUSEWHEEL, wheelWp, wheelLp);
        if (!NT_SUCCESS(status)) return status;

        DbgPrint("EVENT: InjectMouseEvent wheel delta=%d at (%d,%d)\n",
                 (LONG)(SHORT)((ULONG)Wheel >> 16), X, Y);
    }

    return STATUS_SUCCESS;
}

/* UserWaitMessage: block until a message is available */
NTSTATUS NTAPI UserWaitMessage(VOID)
{
    return KeWaitForSingleObject(&g_InputQueue.Event, Executive, KernelMode,
                                 FALSE, NULL);
}

/* UserGetInputEvent: get next input event (non-blocking) */
NTSTATUS NTAPI UserGetInputEvent(PULONG pMessage, PULONG_PTR pwParam, PLONG_PTR plParam, PULONG pTime)
{
    KIRQL Irql;
    PINPUT_EVENT pEvent;

    if (!pMessage || !pwParam || !plParam || !pTime)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_InputQueue.Lock, &Irql);

    if (g_InputQueue.Count == 0) {
        KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
        return STATUS_NO_MORE_ENTRIES;
    }

    pEvent = &g_InputQueue.Events[g_InputQueue.Head];
    *pMessage = pEvent->Message;
    *pwParam = pEvent->wParam;
    *plParam = pEvent->lParam;
    *pTime = pEvent->Time;
    pEvent->InUse = FALSE;

    g_InputQueue.Head = (g_InputQueue.Head + 1) % MAX_INPUT_EVENTS;
    g_InputQueue.Count--;

    KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
    return STATUS_SUCCESS;
}

/* UserPeekInputEvent: peek at next event without removing */
NTSTATUS NTAPI UserPeekInputEvent(PULONG pMessage, PULONG_PTR pwParam, PLONG_PTR plParam, PULONG pTime)
{
    KIRQL Irql;
    PINPUT_EVENT pEvent;

    if (!pMessage || !pwParam || !plParam || !pTime)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_InputQueue.Lock, &Irql);

    if (g_InputQueue.Count == 0) {
        KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
        return STATUS_NO_MORE_ENTRIES;
    }

    pEvent = &g_InputQueue.Events[g_InputQueue.Head];
    *pMessage = pEvent->Message;
    *pwParam = pEvent->wParam;
    *plParam = pEvent->lParam;
    *pTime = pEvent->Time;

    KeReleaseSpinLock(&g_InputQueue.Lock, Irql);
    return STATUS_SUCCESS;
}

/* UserFlushInputEvents: clear all pending input events */
NTSTATUS NTAPI UserFlushInputEvents(VOID)
{
    KIRQL Irql;

    KeAcquireSpinLock(&g_InputQueue.Lock, &Irql);
    g_InputQueue.Head = 0;
    g_InputQueue.Tail = 0;
    g_InputQueue.Count = 0;
    KeReleaseSpinLock(&g_InputQueue.Lock, Irql);

    DbgPrint("EVENT: flushed input queue\n");
    return STATUS_SUCCESS;
}

/* UserGetInputQueueLength: get number of pending input events */
NTSTATUS NTAPI UserGetInputQueueLength(PULONG pCount)
{
    KIRQL Irql;

    if (!pCount) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_InputQueue.Lock, &Irql);
    *pCount = g_InputQueue.Count;
    KeReleaseSpinLock(&g_InputQueue.Lock, Irql);

    return STATUS_SUCCESS;
}

/* UserSendInputEvent: high-level input injection */
NTSTATUS NTAPI UserSendInputEvent(ULONG Type, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    return EventEnqueue(Type, Msg, wParam, lParam);
}
