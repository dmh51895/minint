/*
 * MinNT - ndk/iofuncs.h
 * I/O functions.
 */

#ifndef _NDK_IOFUNCS_H_
#define _NDK_IOFUNCS_H_

#include <nt/ntdef.h>

/* ---- IO_STATUS_BLOCK ---------------------------------------------------- */

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

#endif /* _NDK_IOFUNCS_H_ */
