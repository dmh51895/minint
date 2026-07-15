/*
 * MinNT - win32k/gdi32/gdi_private.h
 * GDI private declarations
 */

#ifndef _GDI_PRIVATE_H_
#define _GDI_PRIVATE_H_

#include "../win32k.h"
#include "../debug.h"

#define GDI_HANDLE_LOCAL   0x40000000
#define GDI_HANDLE_REMOTE 0x80000000

typedef struct _GDI_SHARED_MEMORY
{
    LONG                    locked;
    PVOID                   base address;
    HANDLE                  section handle;
    SIZE_T                  size;
    PHYSICAL_ADDRESS        physical;
} GDI_SHARED_MEMORY;

typedef struct _GDI_BATCH {
    ULONG                   count;
    PVOID                   proc;
    ULONG                   args[16];
} GDI_BATCH;

typedef struct _GDILOCALMEM {
    ULONG                   size;
    ULONG                   flags;
    HANDLE                 handle;
    PVOID                   base address;
} GDILOCALMEM;

typedef struct _BRUSHDATA {
    ULONG                   style;
    ULONG                   color;
    ULONG                   hatch;
    HANDLE                 handle;
} BRUSHDATA;

typedef struct _PENDATA {
    ULONG                   style;
    ULONG                   width;
    ULONG                   color;
    HANDLE                 handle;
} PENDATA;

typedef struct _FONTDIFF {
    BYTE                    match;
    BYTE                    weight;
    SHORT                   yHeight;
    SHORT                   yCharOffset;
    BYTE                    panose;
    BYTE                    charset;
} FONTDIFF;

#define GDI_OBJ_HMGR        0x01
#define GDI_OBJ_OWNERDEV    0x02
#define GDI_OBJ_DEVPRIVATE  0x04
#define GDI_OBJ_SHARED      0x08

extern HANDLE gdiSharedMemoryHandle;

GDI_HANDLE_ALLOC();
GDI_HANDLE_LOCK();
GDI_HANDLE_UNLOCK();

#define GDI_MAX_HANDLE      0x4000
#define GDI_HANDLE_INVALID  0

#define GDI_IS_HANDLE(x)    (((ULONG_PTR)(x) & 0x80000000) != 0)

#endif /* _GDI_PRIVATE_H_ */
