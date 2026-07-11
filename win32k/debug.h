/*
 * MinNT - win32k/debug.h
 * Debug macros for win32k subsystem
 * Generated from ReactOS win32ss debug.h
 */

#ifndef _WIN32K_DEBUG_H_
#define _WIN32K_DEBUG_H_

#include <nt/ke.h>

enum
{
    CHM_FATAL = 0,
    CHM_INTERN = 1,
    CHM_APICALLS = 2,
    CHM_GDI = 3,
    CHM_USER = 4,
    CHM_CONSOLE = 5,
    CHM_HOOKS = 6,
    CHM_PNP = 7,
    CHM_SYNCH = 8,
    CHM_DDE = 9,
    CHM_IMM = 10,
    CHM_INIT = 11,
    CHM_HANDLE = 12,
    CHM_OBJCACHE = 13
};

#define ASSERT(x) \
    do { if (!(x)) KeBugCheckEx(0xDEAD0001, (ULONG_PTR)__FILE__, __LINE__, (ULONG_PTR)#x, 0); } while(0)

#define DPRINT(fmt, ...) \
    DbgPrint("WIN32K[%d]: " fmt, Channel, ##__VA_ARGS__)

#define DPRINT1(fmt, ...) \
    DbgPrint("WIN32K[FATAL]: " fmt, ##__VA_ARGS__)

#define WIN32K_DBG_CH_ENABLE(ch) ((DbgPrint("WIN32K: Enabling channel %d\n", ch)), 1)

#define STATIC_ASSERT(expr) typedef int static_assert_[(expr) ? 1 : -1]

#define ASSERTMSG(msg, expr) \
    do { if (!(expr)) KeBugCheckEx(0xDEAD0002, (ULONG_PTR)__FILE__, __LINE__, (ULONG_PTR)#msg, 0); } while(0)

#endif /* _WIN32K_DEBUG_H_ */
