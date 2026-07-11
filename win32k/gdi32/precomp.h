/*
 * MinNT - win32k/gdi32/precomp.h
 * GDI32 precompiled header
 */

#ifndef _GDI32_PRECOMP_H_
#define _GDI32_PRECOMP_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>

#include "win32k.h"
#include "debug.h"
#include "gdi_private.h"

#include <string.h>
#include <stdlib.h>

#define WIN32K_GDI_ENTRY NTSTATUS APIENTRY

typedef NTSTATUS (APIENTRY *GDI_ENTRY_PROC)(void);

#endif /* _GDI32_PRECOMP_H_ */
