/*
 * MinNT - win32k/precomp.h
 * Precompiled header for Win32k subsystem.
 * Every .c file in this directory includes this first.
 */

#ifndef _WIN32K_PRECOMP_H_
#define _WIN32K_PRECOMP_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ps.h>
#include <nt/hal.h>
#include <nt/ob.h>
#include <nt/rtl.h>
#include <nt/dispatcher.h>

#include "win32k.h"

/* Common macros */
#define W32K_VALIDATE_HWND(hwnd) \
    do { \
        if (!(hwnd) || (ULONG_PTR)(hwnd) < 0x1000) \
            return STATUS_INVALID_HANDLE; \
    } while(0)

#define W32K_VALIDATE_PTR(ptr) \
    do { \
        if (!(ptr)) \
            return STATUS_INVALID_PARAMETER; \
    } while(0)

#define W32K_LOCK_SPIN(lock, irql) \
    KeAcquireSpinLock(&(lock), &(irql))

#define W32K_UNLOCK_SPIN(lock, irql) \
    KeReleaseSpinLock(&(lock), (irql))

/* Global spin lock declarations (defined in their respective .c files) */
extern KSPIN_LOCK g_AtomLock;
extern KSPIN_LOCK g_ClipboardLock;
extern KSPIN_LOCK g_DesktopLock;
extern KSPIN_LOCK g_TimerLock;
extern KSPIN_LOCK g_QueueLock;
extern KSPIN_LOCK g_UpdateLock;

#endif /* _WIN32K_PRECOMP_H_ */
