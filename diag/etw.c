/*
 * MinNT - diag/etw.c
 * Event Tracing for Windows (ETW) infrastructure.
 *
 * Provides in-kernel event tracing - a high-performance logging
 * mechanism for diagnostic events. Subscribers register callbacks
 * and receive all events from providers they're interested in.
 *
 * Concepts:
 *   Provider - source of events (identified by GUID)
 *   Session   - tracing session that collects events from providers
 *   Consumer  - reads events from a session (kernel/user mode)
 *   Event     - a single trace record with header + payload
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>

#define MAX_ETW_PROVIDERS 32
#define MAX_ETW_SESSIONS 8
#define MAX_ETW_CONSUMERS 16
#define ETW_BUFFER_SIZE 4096

/* Event header - matches NT's EVENT_HEADER structure */
typedef struct _ETW_EVENT_HEADER {
    USHORT Size;
    USHORT HeaderType;
    USHORT Flags;
    USHORT EventProperty;
    ULONG  ThreadId;
    ULONG  ProcessId;
    ULONG64 TimeStamp;
    GUID   ProviderId;
    GUID   EventId;
    ULONG  KernelTime;
    ULONG  UserTime;
} ETW_EVENT_HEADER, *PETW_EVENT_HEADER;

/* Event data follows the header in the buffer */

/* Provider state */
typedef struct _ETW_PROVIDER {
    GUID     ProviderId;
    CHAR     Name[64];
    BOOLEAN  Enabled;
    ULONG    EnableMask;       /* bitmask of enabled event levels */
    ULONG    RefCount;
    BOOLEAN  InUse;
} ETW_PROVIDER, *PETW_PROVIDER;

/* Session state */
typedef struct _ETW_SESSION {
    ULONG   SessionId;
    GUID    SessionGuid;
    CHAR    Name[64];
    ULONG   Flags;             /* ETW_SESSION_FLAG_* */
    ULONG   BufferSize;
    PUCHAR  Buffer;
    ULONG   BytesWritten;
    PUCHAR  Consumers[MAX_ETW_CONSUMERS];
    ULONG   ConsumerCount;
    BOOLEAN Active;
    BOOLEAN InUse;
} ETW_SESSION, *PETW_SESSION;

/* Consumer callback type */
typedef VOID (*PETW_EVENT_CALLBACK)(PETW_EVENT_HEADER Event, PVOID Payload,
                                     ULONG PayloadSize, PVOID Context);

static ETW_PROVIDER g_Providers[MAX_ETW_PROVIDERS];
static ETW_SESSION g_Sessions[MAX_ETW_SESSIONS];
static KSPIN_LOCK g_EtwLock;
static ULONG64 g_EtwSequence = 0;

NTSTATUS NTAPI EtwInit(VOID)
{
    RtlZeroMemory(g_Providers, sizeof(g_Providers));
    RtlZeroMemory(g_Sessions, sizeof(g_Sessions));
    KeInitializeSpinLock(&g_EtwLock);

    /* Register built-in kernel providers */
    {
        GUID kernelProv = { 0x9E814AAD, 0x3204, 0x11DF, { 0xBA, 0x48, 0xE0, 0x04, 0x02, 0xC7, 0x7C, 0x8B }};
        EtwRegisterProvider(&kernelProv, "Kernel");
        g_Providers[0].Enabled = TRUE;
        g_Providers[0].EnableMask = 0xFFFFFFFF;
    }
    {
        GUID win32kProv = { 0x8E33188B, 0x6D4E, 0x4A8B, { 0x8C, 0xA4, 0xE8, 0xCF, 0x47, 0x6E, 0x2B, 0xC8 }};
        EtwRegisterProvider(&win32kProv, "Win32k");
        g_Providers[1].Enabled = TRUE;
    }

    DbgPrint("ETW: tracing subsystem initialized (%d providers, %d sessions)\n",
             MAX_ETW_PROVIDERS, MAX_ETW_SESSIONS);
    return STATUS_SUCCESS;
}

/* Register a provider. */
NTSTATUS NTAPI EtwRegisterProvider(GUID *ProviderId, const CHAR *Name)
{
    ULONG i;
    KIRQL irql;
    if (!ProviderId || !Name) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_PROVIDERS; i++) {
        if (!g_Providers[i].InUse) break;
    }
    if (i == MAX_ETW_PROVIDERS) {
        KeReleaseSpinLock(&g_EtwLock, &irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(&g_Providers[i], sizeof(ETW_PROVIDER));
    __builtin_memcpy(&g_Providers[i].ProviderId, ProviderId, sizeof(GUID));
    {
        ULONG j = 0;
        while (Name[j] && j < 63) g_Providers[i].Name[j] = Name[j], j++;
        g_Providers[i].Name[j] = 0;
    }
    g_Providers[i].InUse = TRUE;
    g_Providers[i].RefCount = 1;
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_SUCCESS;
}

/* Enable a provider (mark it as tracing). */
NTSTATUS NTAPI EtwEnableProvider(GUID *ProviderId, ULONG EnableMask)
{
    ULONG i;
    KIRQL irql;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_PROVIDERS; i++) {
        if (g_Providers[i].InUse &&
            RtlCompareMemory(&g_Providers[i].ProviderId, ProviderId, sizeof(GUID)) == 0) {
            g_Providers[i].Enabled = TRUE;
            g_Providers[i].EnableMask = EnableMask;
            KeReleaseSpinLock(&g_EtwLock, &irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_NOT_FOUND;
}

/* Disable a provider. */
NTSTATUS NTAPI EtwDisableProvider(GUID *ProviderId)
{
    ULONG i;
    KIRQL irql;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_PROVIDERS; i++) {
        if (g_Providers[i].InUse &&
            RtlCompareMemory(&g_Providers[i].ProviderId, ProviderId, sizeof(GUID)) == 0) {
            g_Providers[i].Enabled = FALSE;
            KeReleaseSpinLock(&g_EtwLock, &irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_NOT_FOUND;
}

/* Start a new trace session. */
NTSTATUS NTAPI EtwStartSession(const CHAR *Name, GUID *SessionGuid, ULONG Flags,
                                 ULONG BufferSize)
{
    ULONG i;
    KIRQL irql;
    if (!Name || !SessionGuid) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_SESSIONS; i++) {
        if (!g_Sessions[i].InUse) break;
    }
    if (i == MAX_ETW_SESSIONS) {
        KeReleaseSpinLock(&g_EtwLock, &irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(&g_Sessions[i], sizeof(ETW_SESSION));
    g_Sessions[i].SessionId = i;
    __builtin_memcpy(&g_Sessions[i].SessionGuid, SessionGuid, sizeof(GUID));
    {
        ULONG j = 0;
        while (Name[j] && j < 63) g_Sessions[i].Name[j] = Name[j], j++;
        g_Sessions[i].Name[j] = 0;
    }
    g_Sessions[i].Flags = Flags;
    g_Sessions[i].BufferSize = (BufferSize > 0 && BufferSize <= 65536) ? BufferSize : ETW_BUFFER_SIZE;
    g_Sessions[i].Buffer = ExAllocatePool(NonPagedPool, g_Sessions[i].BufferSize);
    if (!g_Sessions[i].Buffer) {
        KeReleaseSpinLock(&g_EtwLock, &irql);
        return STATUS_NO_MEMORY;
    }
    g_Sessions[i].Active = TRUE;
    g_Sessions[i].InUse = TRUE;
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_SUCCESS;
}

/* Stop a trace session and flush its buffer. */
NTSTATUS NTAPI EtwStopSession(ULONG SessionId)
{
    KIRQL irql;
    if (SessionId >= MAX_ETW_SESSIONS || !g_Sessions[SessionId].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    g_Sessions[SessionId].Active = FALSE;
    if (g_Sessions[SessionId].Buffer) {
        ExFreePool(g_Sessions[SessionId].Buffer);
        g_Sessions[SessionId].Buffer = NULL;
    }
    g_Sessions[SessionId].InUse = FALSE;
    g_Sessions[SessionId].BytesWritten = 0;
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_SUCCESS;
}

/* Write an event to all active sessions. The event payload follows the
 * header in memory. */
NTSTATUS NTAPI EtwWrite(GUID *ProviderId, GUID *EventId, ULONG EventLevel,
                         PVOID Payload, ULONG PayloadSize)
{
    ULONG i, j;
    KIRQL irql;
    ULONG offset;
    PETW_EVENT_HEADER hdr;
    BOOLEAN providerFound = FALSE;

    if (!ProviderId || !EventId) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_EtwLock, &irql);
    /* Check provider is enabled */
    for (i = 0; i < MAX_ETW_PROVIDERS; i++) {
        if (g_Providers[i].InUse &&
            RtlCompareMemory(&g_Providers[i].ProviderId, ProviderId, sizeof(GUID)) == 0) {
            if (!g_Providers[i].Enabled) {
                KeReleaseSpinLock(&g_EtwLock, &irql);
                return STATUS_SUCCESS;
            }
            if (!(g_Providers[i].EnableMask & (1 << (EventLevel & 0x1F)))) {
                KeReleaseSpinLock(&g_EtwLock, &irql);
                return STATUS_SUCCESS;
            }
            providerFound = TRUE;
            break;
        }
    }
    if (!providerFound) {
        KeReleaseSpinLock(&g_EtwLock, &irql);
        return STATUS_NOT_FOUND;
    }

    /* Write to all active sessions */
    for (j = 0; j < MAX_ETW_SESSIONS; j++) {
        if (!g_Sessions[j].InUse || !g_Sessions[j].Active) continue;
        if (!g_Sessions[j].Buffer) continue;

        /* Wrap buffer if needed (simple ring buffer) */
        if (g_Sessions[j].BytesWritten + sizeof(ETW_EVENT_HEADER) + PayloadSize > g_Sessions[j].BufferSize) {
            g_Sessions[j].BytesWritten = 0;
        }
        offset = g_Sessions[j].BytesWritten;
        hdr = (PETW_EVENT_HEADER)(g_Sessions[j].Buffer + offset);
        hdr->Size = (USHORT)(sizeof(ETW_EVENT_HEADER) + PayloadSize);
        hdr->HeaderType = 1; /* EVENT_HEADER */
        hdr->Flags = 0;
        hdr->EventProperty = 0;
        hdr->ThreadId = 0;
        hdr->ProcessId = 0;
        hdr->TimeStamp = (ULONG64)KeTickCount;
        __builtin_memcpy(&hdr->ProviderId, ProviderId, sizeof(GUID));
        __builtin_memcpy(&hdr->EventId, EventId, sizeof(GUID));
        hdr->KernelTime = 0;
        hdr->UserTime = 0;
        if (Payload && PayloadSize > 0) {
            __builtin_memcpy(g_Sessions[j].Buffer + offset + sizeof(ETW_EVENT_HEADER),
                             Payload, PayloadSize);
        }
        g_Sessions[j].BytesWritten += hdr->Size;
        g_EtwSequence++;
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return STATUS_SUCCESS;
}

/* Read events from a session's buffer. Copies up to MaxBytes into
 * the caller's buffer. */
ULONG NTAPI EtwReadSession(ULONG SessionId, PVOID OutBuffer, ULONG MaxBytes)
{
    ULONG i;
    ULONG copied = 0;
    KIRQL irql;
    if (SessionId >= MAX_ETW_SESSIONS || !g_Sessions[SessionId].InUse) return 0;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    {
        ETW_SESSION *s = &g_Sessions[SessionId];
        if (!s->Buffer || !s->Active || s->BytesWritten == 0) {
            KeReleaseSpinLock(&g_EtwLock, &irql);
            return 0;
        }
        i = s->BytesWritten;
        if (i > MaxBytes) i = MaxBytes;
        __builtin_memcpy(OutBuffer, s->Buffer, i);
        copied = i;
        s->BytesWritten = 0; /* consume */
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return copied;
}

/* Enumerate registered providers. */
ULONG NTAPI EtwEnumProviders(ULONG MaxCount, PCHAR *pNames, GUID *pIds)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_PROVIDERS && n < MaxCount; i++) {
        if (g_Providers[i].InUse) {
            ULONG j = 0;
            while (g_Providers[i].Name[j] && j < 63) pNames[n][j] = g_Providers[i].Name[j], j++;
            pNames[n][j] = 0;
            __builtin_memcpy(&pIds[n], &g_Providers[i].ProviderId, sizeof(GUID));
            n++;
        }
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return n;
}

/* Enumerate sessions. */
ULONG NTAPI EtwEnumSessions(ULONG MaxCount, PCHAR *pNames, PULONG pFlags)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    for (i = 0; i < MAX_ETW_SESSIONS && n < MaxCount; i++) {
        if (g_Sessions[i].InUse) {
            ULONG j = 0;
            while (g_Sessions[i].Name[j] && j < 63) pNames[n][j] = g_Sessions[i].Name[j], j++;
            pNames[n][j] = 0;
            pFlags[n] = g_Sessions[i].Flags;
            n++;
        }
    }
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return n;
}

/* Get total event count. */
ULONG64 NTAPI EtwGetEventCount(VOID)
{
    ULONG64 seq;
    KIRQL irql;
    KeAcquireSpinLock(&g_EtwLock, &irql);
    seq = g_EtwSequence;
    KeReleaseSpinLock(&g_EtwLock, &irql);
    return seq;
}
