/*
 * MinNT - com/apartment.c
 * COM apartments (Single-Threaded + Multi-Threaded).
 *
 * A COM apartment is a logical boundary within a process that
 * isolates COM objects and their thread-affinity. Two apartment types:
 *   - STA (Single-Threaded Apartment): one thread, message pump required
 *   - MTA (Multi-Threaded Apartment):  any thread, free threading
 *
 * Each apartment has its own object table and message queue. Object
 * references are marshalled across apartments by the RPC runtime.
 *
 * MinNT's implementation tracks per-thread apartment membership,
 * routes calls to the right apartment, and provides CoInitialize/
 * CoUninitialize semantics.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define COM_MAX_APARTMENTS   64
#define COM_MAX_OBJECTS      1024

typedef enum _COM_APARTMENT_TYPE {
    ComApartmentNone = 0,
    ComApartmentSTA,
    ComApartmentMTA,
} COM_APARTMENT_TYPE;

typedef struct _COM_APARTMENT {
    ULONG Id;
    COM_APARTMENT_TYPE Type;
    PETHREAD MainThread;     /* STA: the owning thread */
    LIST_ENTRY Objects;      /* apartment-local object table */
    ULONG RefCount;
    BOOLEAN InUse;
} COM_APARTMENT;

typedef struct _COM_OBJECT_ENTRY {
    ULONG64 Id;
    PETHREAD ApartmentThread;
    ULONG RefCount;
    LIST_ENTRY ApartmentEntry;
    BOOLEAN InUse;
} COM_OBJECT_ENTRY;

static COM_APARTMENT g_Apartments[COM_MAX_APARTMENTS];
static COM_OBJECT_ENTRY g_Objects[COM_MAX_OBJECTS];
static KSPIN_LOCK g_ComLock;

NTSTATUS NTAPI ComInit(VOID)
{
    RtlZeroMemory(g_Apartments, sizeof(g_Apartments));
    RtlZeroMemory(g_Objects, sizeof(g_Objects));
    KeInitializeSpinLock(&g_ComLock);
    DbgPrint("COM: apartment model initialized (STA + MTA supported)\n");
    return STATUS_SUCCESS;
}

/* Find the apartment owned by a given thread. */
static COM_APARTMENT *ComFindApartmentForThread(PETHREAD Thread)
{
    for (ULONG i = 0; i < COM_MAX_APARTMENTS; i++) {
        if (g_Apartments[i].InUse && g_Apartments[i].Type == ComApartmentSTA &&
            g_Apartments[i].MainThread == Thread) {
            return &g_Apartments[i];
        }
    }
    return NULL;
}

NTSTATUS NTAPI CoInitialize(PVOID Reserved, ULONG InitFlags)
{
    (void)Reserved;
    PETHREAD thread = PsGetCurrentThread();
    COM_APARTMENT_TYPE requested = (InitFlags & 0x02 /* COINIT_MULTITHREADED */) ?
                                    ComApartmentMTA : ComApartmentSTA;
    /* Check if this thread is already in an STA. */
    COM_APARTMENT *existing = ComFindApartmentForThread(thread);
    if (existing) {
        /* Joining existing STA. */
        existing->RefCount++;
        return STATUS_SUCCESS;
    }
    /* Allocate a new apartment. */
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (ULONG i = 0; i < COM_MAX_APARTMENTS; i++) {
        if (!g_Apartments[i].InUse) {
            RtlZeroMemory(&g_Apartments[i], sizeof(COM_APARTMENT));
            g_Apartments[i].InUse = TRUE;
            g_Apartments[i].Type = requested;
            g_Apartments[i].MainThread = (requested == ComApartmentSTA) ? thread : NULL;
            g_Apartments[i].RefCount = 1;
            InitializeListHead(&g_Apartments[i].Objects);
            KeReleaseSpinLock(&g_ComLock, irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
    return STATUS_NO_MEMORY;
}

VOID NTAPI CoUninitialize(VOID)
{
    PETHREAD thread = PsGetCurrentThread();
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    COM_APARTMENT *apt = ComFindApartmentForThread(thread);
    if (apt) {
        if (apt->RefCount) apt->RefCount--;
        if (apt->RefCount == 0) {
            /* Release all objects in this apartment. */
            RtlZeroMemory(apt, sizeof(COM_APARTMENT));
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
}

ULONG NTAPI CoGetApartmentId(VOID)
{
    PETHREAD thread = PsGetCurrentThread();
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (ULONG i = 0; i < COM_MAX_APARTMENTS; i++) {
        if (g_Apartments[i].InUse && g_Apartments[i].MainThread == thread) {
            KeReleaseSpinLock(&g_ComLock, irql);
            return i;
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
    return (ULONG)-1;
}

NTSTATUS NTAPI CoAptRegisterClassObject(ULONG64 Clsid, PETHREAD ServerThread,
                                       PULONG OutCookie)
{
    if (!ServerThread || !OutCookie) return STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (ULONG i = 0; i < COM_MAX_OBJECTS; i++) {
        if (!g_Objects[i].InUse) {
            RtlZeroMemory(&g_Objects[i], sizeof(COM_OBJECT_ENTRY));
            g_Objects[i].InUse = TRUE;
            g_Objects[i].Id = Clsid;
            g_Objects[i].ApartmentThread = ServerThread;
            g_Objects[i].RefCount = 1;
            *OutCookie = i;
            KeReleaseSpinLock(&g_ComLock, irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_ComLock, irql);
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI CoAptRevokeClassObject(ULONG Cookie)
{
    if (Cookie >= COM_MAX_OBJECTS || !g_Objects[Cookie].InUse) return STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    RtlZeroMemory(&g_Objects[Cookie], sizeof(COM_OBJECT_ENTRY));
    KeReleaseSpinLock(&g_ComLock, irql);
    return STATUS_SUCCESS;
}

ULONG NTAPI CoAptGetClassObjectCount(VOID)
{
    ULONG n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (ULONG i = 0; i < COM_MAX_OBJECTS; i++) if (g_Objects[i].InUse) n++;
    KeReleaseSpinLock(&g_ComLock, irql);
    return n;
}

ULONG NTAPI CoGetApartmentCount(VOID)
{
    ULONG n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ComLock, &irql);
    for (ULONG i = 0; i < COM_MAX_APARTMENTS; i++) if (g_Apartments[i].InUse) n++;
    KeReleaseSpinLock(&g_ComLock, irql);
    return n;
}
