# Next Session Note

## STATUS UPDATE: 2026-07-09 (Phase 2 complete)

## ACHIEVED THIS SESSION

### 1. Win32k USER Message Pump — Phase 2 ✅
**win32k/usermsg.c** (new file) contains REAL (not stub) USER message-pump functions:

| Function | Syscall | Status |
|---------|---------|--------|
| UserPeekMessage | 0x1001 | REAL - non-blocking filtered queue lookup |
| UserGetMessage | 0x1006 | REAL - blocks on a real KEVENT via KeWaitForSingleObject |
| UserTranslateMessage | 0x100D | REAL - VK_* -> WM_CHAR/WM_SYSCHAR, posts the char message |
| UserPostMessage | 0x100E | REAL - enqueues into the circular buffer, signals the event |
| UserDispatchMessage | 0x1035 | REAL - hwnd -> WNDPROC lookup + call (table starts empty, see below) |

Simplification, stated up front the way Phase 1 states its own: **one systemwide
message queue**, not per-thread/per-window — same simplification UserGetDC/
UserReleaseDC already made by ignoring hWnd. What's real: a genuine 256-slot
circular buffer with filtered scan-and-remove (a filtered Get/Peek can pull a
message out of the middle of the queue, not just the head — verified with a
standalone host-side test, see below), a genuine KEVENT (SynchronizationEvent,
auto-reset, wakes exactly one waiter) that GetMessage actually blocks on
instead of busy-polling, and a genuine VK -> char table for TranslateMessage
(no shift/capslock state tracked yet since there's no GetKeyboardState array —
letters come out lowercase, documented not silent).

UserDispatchMessage's hwnd -> WNDPROC table is real infrastructure but starts
empty, because nothing creates windows yet. `Win32kRegisterWindowProc(HWND,
W32K_WNDPROC)` is the hook — Phase 3's UserCreateWindowEx forward-declares and
calls it exactly the way win32k.c forward-declares gdikernel.c's functions.

### 2. New types in win32k.h
- `HWND` (was missing entirely — GDI's HDC/HRGN/HBITMAP existed, HWND didn't)
- `W32K_MSG` / `PW32K_MSG` (hwnd, message, wParam, lParam, time, pt)
- `W32K_WNDPROC` function pointer typedef
- `WM_NULL/WM_QUIT/WM_KEYDOWN/WM_KEYUP/WM_CHAR/WM_SYSKEYDOWN/WM_SYSKEYUP/WM_SYSCHAR`
- `PM_NOREMOVE/PM_REMOVE/PM_NOYIELD`
- `VK_BACK/VK_TAB/VK_RETURN/VK_ESCAPE/VK_SPACE/VK_0/VK_9/VK_A/VK_Z`

Deliberately did NOT touch `win32k/ntuser.h` — it already declares matching
`NtUser*` prototypes (HWND, PMSG, etc.) but has never been compiled into the
build (no .c file includes it), and it redefines `HANDLE` as `ULONG_PTR`
which collides with `nt/ntdef.h`'s real `typedef PVOID HANDLE`. Wiring it in
is a separate cleanup, not part of this phase.

### 3. include/nt/ntdef.h
- Added `STATUS_NO_TRANSLATION` (0xC0000101, the real NT code) for
  TranslateMessage's "nothing to translate" path.

### 4. Verification done this session
- Full `make clean && make`: exit 0, zero errors. Total warning count actually
  **dropped** (942 -> 876) — removing 5 DECLARE_STUB expansions removes ~75
  generic "unused parameter" warnings; the only warnings usermsg.c itself
  produces are the same `implicit declaration of DbgPrint/RtlZeroMemory`
  pattern every other file in this tree already has (confirmed via
  `git stash` diff against a clean baseline build — not a regression).
- `nm minint.elf | grep User` confirms all 5 syscall functions plus
  `Win32kInitMessageQueue`/`Win32kRegisterWindowProc` are real defined
  symbols in the final linked kernel, not dead code.
- Standalone host-side test of the filtered scan-and-remove circular-buffer
  logic (the one genuinely tricky part): FIFO order, filtered removal from
  the middle preserving order of what's left, peek-doesn't-consume, full
  queue rejects post, no-match leaves queue untouched, index wraparound.
  All 6 cases pass.

## Build/Test

```bash
cd /home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint
make clean && make -j4 && make iso
timeout 40 qemu-system-x86_64 -cdrom minint.iso -serial stdio -m 256M -display none
```

## Next Targets

### Phase 3: Window Management
```
UserCreateWindowEx (0x1077, 15 args)  <- the big one, also populates the WNDPROC table
UserShowWindow      (0x1057)
UserSetWindowPos    (0x1023)
UserDestroyWindow   (0x109E)
```
This is the phase that makes UserDispatchMessage's table stop being empty:
UserCreateWindowEx should call `Win32kRegisterWindowProc(hwnd, wndproc)`
(declared wherever it's implemented — usermsg.c — the same way every other
cross-file call in this codebase forward-declares its target) once it has a
real HWND and a WNDCLASS's `lpfnWndProc` to hand it.

### Also still stubbed (unchanged this session)
```
UserGetThreadState, UserDefWindowProc, UserBeginPaint, UserEndPaint,
UserGetAsyncKeyState, UserSetFocus, UserInvalidateRect,
UserGetForegroundWindow, UserSetForegroundWindow, UserSetCapture,
UserReleaseCapture, UserGetWindowRect, UserGetClientRect,
UserRegisterClassEx, GdiSelectFont, GdiMaskBlt, GdiAlphaBlend,
GdiTransformPoints
```

## Reference Files
- `win32k/usermsg.c` — Phase 2 message pump (new file, ~230 lines)
- `win32k-stripped/ntuser/msgqueue.c`, `message.c` — ReactOS reference (not
  ported — MinNT's queue is a deliberately simpler single systemwide one)
- `win32k-stripped/WIN32K_SYSCALL_TABLE.md` — exact syscall spec

## Philosophy Reminder
**"WORKS OR IT DOESN'T. NOT WORKING IS IN FACT AN ACCEPTABLE OPTION."**
- UserDispatchMessage's empty table is an honest "not wired yet", not a
  fake success — it returns STATUS_INVALID_HANDLE when it can't find a
  window, not STATUS_SUCCESS
- GetMessage really blocks (KeWaitForSingleObject), not a spin loop
- TranslateMessage really posts a new message into the real queue
