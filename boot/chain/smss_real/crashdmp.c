/*
 * PROJECT:         ReactOS Windows-Compatible Session Manager
 * LICENSE:         BSD 2-Clause License
 * FILE:            base/system/smss/crashdmp.c
 * PURPOSE:         Main SMSS Code
 * PROGRAMMERS:     Alex Ionescu
 */

/* INCLUDES *******************************************************************/


/* MinNT includes */
#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/lpc.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <ndk/obfuncs.h>
#include <ndk/cmfuncs.h>
#include <ndk/lpcfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/setypes.h>
#include <ndk/rtlfuncs.h>

#include "smss.h"

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

BOOLEAN
NTAPI
SmpCheckForCrashDump(IN PUNICODE_STRING FileName)
{
    UNREFERENCED_PARAMETER(FileName);
    return FALSE;
}
