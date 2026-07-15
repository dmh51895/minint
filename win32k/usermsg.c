/*
 * MinNT - win32k/usermsg.c
 * Win32k.sys USER message pump — Phase 2 of the win32k USER port.
 *
 * Phase 2 (Message pump): UserPeekMessage, UserGetMessage, UserPostMessage,
 *                          UserTranslateMessage, UserDispatchMessage
 *
 * Same philosophy as gdikernel.c: WORKS OR IT DOESN'T.
 *
 * Simplification, stated up front the way the GDI phases do it: this is ONE
 * systemwide message queue, not a per-thread/per-window one. UserGetDC and
 * UserReleaseDC already ignore hWnd and hand back a generic DC for the exact
 * same reason — there's no window object and no thread-to-queue association
 * to key off yet. What IS real here: a genuine circular queue, a genuine
 * KEVENT that GetMessage actually blocks on via KeWaitForSingleObject (no
 * busy-polling), genuine VK -> WM_CHAR translation, and a genuine
 * HWND -> WNDPROC dispatch table. The dispatch table just starts out empty,
 * because nothing creates windows yet — Phase 3's UserCreateWindowEx is
 * what calls Win32kRegisterWindowProc() to populate it.
 */

#include "win32k.h"
#include <nt/dispatcher.h>

#define W32_MSGQUEUE_SIZE 256

typedef struct _W32MSGQUEUE {
    W32K_MSG   Messages[W32_MSGQUEUE_SIZE];
    ULONG      Head;
    ULONG      Tail;
    ULONG      Count;
    KSPIN_LOCK Lock;
    KEVENT     MessageEvent;
} W32MSGQUEUE;

static W32MSGQUEUE g_MsgQueue;

#define W32K_MAX_WINDOWS 64

typedef struct _W32K_WNDENTRY {
    HWND         hwnd;
    W32K_WNDPROC wndproc;
    BOOLEAN      InUse;
} W32K_WNDENTRY;

static W32K_WNDENTRY g_WndTable[W32K_MAX_WINDOWS];

/* Called once from DriverEntry, right after Win32kInitTable(). */
VOID NTAPI Win32kInitMessageQueue(VOID)
{
    RtlZeroMemory(&g_MsgQueue, sizeof(g_MsgQueue));
    KeInitializeSpinLock(&g_MsgQueue.Lock);
    KeInitializeEvent(&g_MsgQueue.MessageEvent, SynchronizationEvent, FALSE);

    RtlZeroMemory(&g_WndTable, sizeof(g_WndTable));

    DbgPrint("WIN32K: message queue initialized (%d slots, %d window slots)\n",
             W32_MSGQUEUE_SIZE, W32K_MAX_WINDOWS);
}

/* ---- Queue internals — caller must hold g_MsgQueue.Lock for Find ------- */

static BOOLEAN Win32kMsgMatchesFilter(ULONG Message, ULONG MsgFilterMin, ULONG MsgFilterMax)
{
    if (MsgFilterMin == 0 && MsgFilterMax == 0)
        return TRUE;
    return (Message >= MsgFilterMin && Message <= MsgFilterMax);
}

/* Scans oldest-to-newest for the first message matching the window filter
 * (hWnd) and the message-id range filter. On a hit, copies it to *Out and,
 * if Remove is set, closes the gap by shifting every older entry forward
 * one slot — real GetMessage/PeekMessage can return a message from the
 * middle of the queue, not just the head. */
static BOOLEAN Win32kMsgQueueFind(HWND hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax, BOOLEAN Remove, W32K_MSG *Out)
{
    ULONG i, idx;
    HWND MsgHwnd;

    for (i = 0; i < g_MsgQueue.Count; i++) {
        idx = (g_MsgQueue.Head + i) % W32_MSGQUEUE_SIZE;
        MsgHwnd = g_MsgQueue.Messages[idx].hwnd;

        if (!Win32kMsgMatchesFilter(g_MsgQueue.Messages[idx].message, MsgFilterMin, MsgFilterMax))
            continue;

        /* hWnd filter: hWnd == 0 means "any window". Otherwise only accept
         * messages targeted at hWnd, plus broadcast/hardware messages
         * (hwnd == 0) which are delivered to every window's queue. */
        if (hWnd != 0 && MsgHwnd != hWnd && MsgHwnd != 0)
            continue;

        *Out = g_MsgQueue.Messages[idx];

        if (Remove) {
            ULONG j, dst, src;
            for (j = i; j > 0; j--) {
                dst = (g_MsgQueue.Head + j) % W32_MSGQUEUE_SIZE;
                src = (g_MsgQueue.Head + j - 1) % W32_MSGQUEUE_SIZE;
                g_MsgQueue.Messages[dst] = g_MsgQueue.Messages[src];
            }
            g_MsgQueue.Head = (g_MsgQueue.Head + 1) % W32_MSGQUEUE_SIZE;
            g_MsgQueue.Count--;
        }

        return TRUE;
    }

    return FALSE;
}

/* Internal enqueue, shared by UserPostMessage and UserTranslateMessage
 * (which posts the WM_CHAR it synthesizes). Not a syscall itself. */
static NTSTATUS Win32kQueuePostMessage(HWND hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    KIRQL OldIrql;
    W32K_MSG *pMsg;

    KeAcquireSpinLock(&g_MsgQueue.Lock, &OldIrql);

    if (g_MsgQueue.Count >= W32_MSGQUEUE_SIZE) {
        KeReleaseSpinLock(&g_MsgQueue.Lock, OldIrql);
        DbgPrint("WIN32K: message queue full, dropping 0x%04X for hwnd=%p\n", Msg, (PVOID)hWnd);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pMsg = &g_MsgQueue.Messages[g_MsgQueue.Tail];
    pMsg->hwnd    = hWnd;
    pMsg->message = Msg;
    pMsg->wParam  = wParam;
    pMsg->lParam  = lParam;
    pMsg->time    = (ULONG)KeTickCount;
    pMsg->pt.x    = 0;
    pMsg->pt.y    = 0;

    g_MsgQueue.Tail = (g_MsgQueue.Tail + 1) % W32_MSGQUEUE_SIZE;
    g_MsgQueue.Count++;

    KeReleaseSpinLock(&g_MsgQueue.Lock, OldIrql);

    /* SynchronizationEvent: wakes exactly one blocked GetMessage, auto-resets */
    KeSetEvent(&g_MsgQueue.MessageEvent, 0, FALSE);

    return STATUS_SUCCESS;
}

/* ---- UserPostMessage (0x100E) ------------------------------------------ */

NTSTATUS APIENTRY UserPostMessage(ULONG_PTR hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    NTSTATUS Status = Win32kQueuePostMessage((HWND)hWnd, Msg, wParam, lParam);

    DbgPrint("WIN32K: UserPostMessage(%p, 0x%04X, %p, %p) -> 0x%08X\n",
             (PVOID)hWnd, Msg, (PVOID)wParam, (PVOID)lParam, Status);

    return Status;
}

/* ---- UserGetMessage (0x1006) --------------------------------------------
 * Genuinely blocks: if nothing matches yet, waits on g_MsgQueue.MessageEvent
 * and re-checks after waking — the same double-check loop any real
 * dispatcher consumer needs, since a message could be filtered out or
 * claimed by another waiter between the wake and the re-check. */

NTSTATUS APIENTRY UserGetMessage(PW32K_MSG Msg, ULONG_PTR hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax)
{
    KIRQL OldIrql;
    BOOLEAN Found;
    W32K_MSG Local;

    if (!Msg) return STATUS_INVALID_PARAMETER;

    for (;;) {
        KeAcquireSpinLock(&g_MsgQueue.Lock, &OldIrql);
        Found = Win32kMsgQueueFind((HWND)hWnd, MsgFilterMin, MsgFilterMax, TRUE, &Local);
        KeReleaseSpinLock(&g_MsgQueue.Lock, OldIrql);

        if (Found) {
            *Msg = Local;
            DbgPrint("WIN32K: UserGetMessage -> hwnd=%p msg=0x%04X\n",
                     (PVOID)Local.hwnd, Local.message);
            return STATUS_SUCCESS;
        }

        KeWaitForSingleObject(&g_MsgQueue.MessageEvent, Executive, KernelMode, FALSE, NULL);
    }
}

/* ---- UserPeekMessage (0x1001) — never blocks ---------------------------- */

NTSTATUS APIENTRY UserPeekMessage(PW32K_MSG Msg, ULONG_PTR hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax, ULONG RemoveMsg)
{
    KIRQL OldIrql;
    BOOLEAN Found;
    W32K_MSG Local;

    if (!Msg) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_MsgQueue.Lock, &OldIrql);
    Found = Win32kMsgQueueFind((HWND)hWnd, MsgFilterMin, MsgFilterMax,
                                (RemoveMsg & PM_REMOVE) ? TRUE : FALSE, &Local);
    KeReleaseSpinLock(&g_MsgQueue.Lock, OldIrql);

    if (!Found) {
        DbgPrint("WIN32K: UserPeekMessage -> no message\n");
        return STATUS_NO_MORE_ENTRIES;
    }

    *Msg = Local;
    DbgPrint("WIN32K: UserPeekMessage -> hwnd=%p msg=0x%04X (remove=%d)\n",
             (PVOID)Local.hwnd, Local.message, (RemoveMsg & PM_REMOVE) ? 1 : 0);
    return STATUS_SUCCESS;
}

/* ---- UserTranslateMessage (0x100D) --------------------------------------
 * Real VK -> char mapping for the common keys. No shift/capslock state is
 * tracked yet (there's no GetKeyboardState array to track it in), so
 * letters translate to lowercase unconditionally — a documented shortcut,
 * not a silent one. */

static UCHAR Win32kVkToChar(ULONG Vk)
{
    if (Vk >= VK_0 && Vk <= VK_9) return (UCHAR)Vk;                /* '0'-'9' alias VK_0-VK_9 */
    if (Vk >= VK_A && Vk <= VK_Z) return (UCHAR)(Vk - VK_A + 'a'); /* no shift-state yet */

    switch (Vk) {
        case VK_SPACE:  return ' ';
        case VK_RETURN: return '\r';
        case VK_BACK:   return '\b';
        case VK_TAB:    return '\t';
        case VK_ESCAPE: return 0x1B;
        default:        return 0;
    }
}

NTSTATUS APIENTRY UserTranslateMessage(PW32K_MSG Msg, ULONG Flags)
{
    UCHAR ch;
    ULONG CharMsg;

    if (!Msg) return STATUS_INVALID_PARAMETER;

    /* WM_SYSKEYDOWN is always translatable. WM_KEYDOWN only when the caller
     * asks for it via TM_KEYDOWN (matches TranslateMessage's default of
     * synthesising chars for system keys; apps that want plain key-down
     * translation request it explicitly). */
    if (Msg->message == WM_KEYDOWN && !(Flags & TM_KEYDOWN))
        return STATUS_NO_TRANSLATION;
    if (Msg->message != WM_KEYDOWN && Msg->message != WM_SYSKEYDOWN)
        return STATUS_NO_TRANSLATION;

    ch = Win32kVkToChar((ULONG)Msg->wParam);
    if (!ch)
        return STATUS_NO_TRANSLATION;

    CharMsg = (Msg->message == WM_SYSKEYDOWN) ? WM_SYSCHAR : WM_CHAR;

    DbgPrint("WIN32K: UserTranslateMessage: VK 0x%02X -> '%c' (0x%04X) flags=0x%X\n",
             (ULONG)Msg->wParam, ch, CharMsg, Flags);

    /* Only post the synthesized character message when TM_POSTCHARCHARS is
     * set; otherwise the translation is computed but not queued. */
    if (!(Flags & TM_POSTCHARCHARS))
        return STATUS_SUCCESS;

    return Win32kQueuePostMessage(Msg->hwnd, CharMsg, (ULONG_PTR)ch, Msg->lParam);
}

/* ---- Window registration hook for Phase 3 -------------------------------
 * Not a syscall. UserCreateWindowEx will forward-declare and call this once
 * it exists, exactly the way win32k.c forward-declares functions that live
 * in other .c files. */

NTSTATUS Win32kRegisterWindowProc(HWND hwnd, W32K_WNDPROC wndproc)
{
    ULONG i;

    if (!hwnd || !wndproc) return STATUS_INVALID_PARAMETER;

    for (i = 0; i < W32K_MAX_WINDOWS; i++) {
        if (!g_WndTable[i].InUse) {
            g_WndTable[i].hwnd    = hwnd;
            g_WndTable[i].wndproc = wndproc;
            g_WndTable[i].InUse   = TRUE;
            DbgPrint("WIN32K: Win32kRegisterWindowProc(%p, %p) -> slot %d\n",
                     (PVOID)hwnd, (PVOID)(ULONG_PTR)wndproc, i);
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}

/* ---- UserDispatchMessage (0x1035) ----------------------------------------
 * Real hwnd -> wndproc lookup and call. The table is genuinely empty right
 * now because nothing creates windows yet — that's Phase 3, not a stub. */

NTSTATUS APIENTRY UserDispatchMessage(PW32K_MSG Msg)
{
    ULONG i;

    if (!Msg) return STATUS_INVALID_PARAMETER;

    if (!Msg->hwnd || Msg->message == WM_NULL)
        return STATUS_SUCCESS; /* thread message / WM_NULL: real DispatchMessage returns 0 here too */

    for (i = 0; i < W32K_MAX_WINDOWS; i++) {
        if (g_WndTable[i].InUse && g_WndTable[i].hwnd == Msg->hwnd) {
            DbgPrint("WIN32K: UserDispatchMessage: hwnd=%p msg=0x%04X -> wndproc %p\n",
                     (PVOID)Msg->hwnd, Msg->message, (PVOID)(ULONG_PTR)g_WndTable[i].wndproc);
            g_WndTable[i].wndproc(Msg->hwnd, Msg->message, Msg->wParam, Msg->lParam);
            return STATUS_SUCCESS;
        }
    }

    DbgPrint("WIN32K: UserDispatchMessage: hwnd=%p not registered (Phase 3 not wired yet)\n",
             (PVOID)Msg->hwnd);
    return STATUS_INVALID_HANDLE;
}

/* ---- UserSendMessage (0x1009) ------------------------------------------
 * Synchronous: looks up wndproc and calls it directly, no queue involved.
 * Returns the LRESULT from the window procedure. */

LONG_PTR APIENTRY
UserSendMessage(ULONG_PTR hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    ULONG i;
    HWND hwnd = (HWND)hWnd;

    if (!hwnd || Msg == WM_NULL)
        return 0;

    for (i = 0; i < W32K_MAX_WINDOWS; i++) {
        if (g_WndTable[i].InUse && g_WndTable[i].hwnd == hwnd) {
            DbgPrint("WIN32K: UserSendMessage hwnd=%p msg=0x%04X -> wndproc %p\n",
                     (PVOID)hwnd, Msg, (PVOID)(ULONG_PTR)g_WndTable[i].wndproc);
            return g_WndTable[i].wndproc(hwnd, Msg, wParam, lParam);
        }
    }

    DbgPrint("WIN32K: UserSendMessage hwnd=%p msg=0x%04X not found\n", (PVOID)hwnd, Msg);
    return 0;
}
