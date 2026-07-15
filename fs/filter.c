/*
 * MinNT - fs/filter.c
 * File system filter manager (FltMgr).
 *
 * The filter manager sits between the I/O manager and the filesystem
 * drivers. It allows third-party filter drivers to:
 *   - Inspect every I/O request before/after it reaches the FSD
 *   - Modify or reject the request
 *   - Register for specific operations (open, read, write, etc.)
 *
 * Filters are stacked. Each filter has an "altitude" that determines
 * its position in the stack (higher altitude = sees the request
 * earlier). FltSendMessage delivers messages between filters and
 * user-mode filter manager service.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/io.h>
#include <nt/framework.h>

#define FLT_MAX_FILTERS    32
#define FLT_MAX_NAME        64

/* FLT_OPERATION is in framework.h. */

typedef struct _FLT_CALLBACK {
    PVOID Callback;
    PVOID Context;
    BOOLEAN InUse;
} FLT_CALLBACK;

typedef struct _FLT_FILTER {
    ULONG Id;
    CHAR Name[FLT_MAX_NAME];
    ULONG Altitude;            /* 0..100000, lower = further from FS */
    FLT_CALLBACK Callbacks[FltOpMax];
    BOOLEAN Attached;
    BOOLEAN InUse;
} FLT_FILTER;

static FLT_FILTER g_Filters[FLT_MAX_FILTERS];
static ULONG g_NextFilterId = 1;

NTSTATUS NTAPI FltMgrInit(VOID)
{
    RtlZeroMemory(g_Filters, sizeof(g_Filters));
    DbgPrint("FLTMGR: filter manager initialized (max %u filters)\n", FLT_MAX_FILTERS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltRegisterFilter(const CHAR *Name, ULONG Altitude,
                                  PFLT_FILTER *OutFilter)
{
    if (!Name || !OutFilter) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FLT_MAX_FILTERS; i++) {
        if (!g_Filters[i].InUse) {
            RtlZeroMemory(&g_Filters[i], sizeof(FLT_FILTER));
            g_Filters[i].InUse = TRUE;
            g_Filters[i].Id = g_NextFilterId++;
            g_Filters[i].Altitude = Altitude;
            for (ULONG k = 0; k < FLT_MAX_NAME - 1 && Name[k]; k++) g_Filters[i].Name[k] = Name[k];
            *OutFilter = &g_Filters[i];
            DbgPrint("FLTMGR: registered filter '%s' altitude %u id %u\n",
                     Name, Altitude, g_Filters[i].Id);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI FltAttachFilter(PFLT_FILTER Filter)
{
    if (!Filter) return STATUS_INVALID_PARAMETER;
    Filter->Attached = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltDetachFilter(PFLT_FILTER Filter)
{
    if (!Filter) return STATUS_INVALID_PARAMETER;
    Filter->Attached = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltUnregisterFilter(PFLT_FILTER Filter)
{
    if (!Filter) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Filter, sizeof(FLT_FILTER));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FltSetCallback(PFLT_FILTER Filter, FLT_OPERATION Operation,
                               PVOID Callback, PVOID Context)
{
    if (!Filter || Operation >= FltOpMax) return STATUS_INVALID_PARAMETER;
    Filter->Callbacks[Operation].Callback = Callback;
    Filter->Callbacks[Operation].Context = Context;
    Filter->Callbacks[Operation].InUse = TRUE;
    return STATUS_SUCCESS;
}

/* Walk the filter stack in altitude order and invoke matching
 * callbacks. Returns the first non-STATUS_SUCCESS if any filter
 * rejects the request. */
NTSTATUS NTAPI FltInvokeCallbacks(FLT_OPERATION Operation, PVOID Request)
{
    for (ULONG pass = 0; pass < 2; pass++) {
        /* Two passes: highest altitude first (pre), then lowest first (post). */
        ULONG start, end, step;
        if (pass == 0) { start = FLT_MAX_FILTERS; step = (ULONG)-1; end = (ULONG)-1; }
        else { start = 0; step = 1; end = FLT_MAX_FILTERS; }
        for (ULONG i = start; i != end; i += step) {
            if (!g_Filters[i].InUse || !g_Filters[i].Attached) continue;
            if (!g_Filters[i].Callbacks[Operation].InUse) continue;
            if (!g_Filters[i].Callbacks[Operation].Callback) continue;
            NTSTATUS s = ((NTSTATUS (NTAPI *)(PVOID, PVOID))g_Filters[i].Callbacks[Operation].Callback)(
                g_Filters[i].Callbacks[Operation].Context, Request);
            if (!NT_SUCCESS(s)) return s;
        }
    }
    return STATUS_SUCCESS;
}

ULONG NTAPI FltGetAttachedCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < FLT_MAX_FILTERS; i++) {
        if (g_Filters[i].InUse && g_Filters[i].Attached) n++;
    }
    return n;
}
