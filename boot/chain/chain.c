/*
 * MinNT - boot/chain/chain.c
 * Boot chain initialization.
 * Launches SMSS → CSRSS → Winlogon → Explorer as kernel threads.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include "chain.h"

/* ---- Boot chain globals ------------------------------------------------- */

static KEVENT ChainInitCompleteEvent;
static PETHREAD ChainSmssThread;

/* ---- Boot chain init ---------------------------------------------------- */

NTSTATUS NTAPI BootChainInit(VOID)
{
    NTSTATUS status;

    DbgPrint("\n");
    DbgPrint("╔══════════════════════════════════════════╗\n");
    DbgPrint("║  MinNT Boot Chain Initialization         ║\n");
    DbgPrint("║  SMSS → CSRSS → Winlogon → Explorer      ║\n");
    DbgPrint("╚══════════════════════════════════════════╝\n\n");

    /* Initialize the completion event */
    KeInitializeEvent(&ChainInitCompleteEvent, NotificationEvent, FALSE);

    /* Launch SMSS — it will chain the rest */
    DbgPrint("CHAIN: launching SMSS (Session Manager)...\n");
    status = PsCreateSystemThread(PsInitialSystemProcess,
                                  SmssThread, NULL, &ChainSmssThread);
    if (!NT_SUCCESS(status)) {
        DbgPrint("CHAIN: failed to launch SMSS: 0x%08lx\n", (ULONG)status);
        return status;
    }

    DbgPrint("CHAIN: boot chain initiated\n");
    return STATUS_SUCCESS;
}

/* ---- Boot chain status -------------------------------------------------- */

BOOLEAN NTAPI BootChainIsComplete(VOID)
{
    return KeReadStateEvent(&ChainInitCompleteEvent);
}

VOID NTAPI BootChainSignalComplete(VOID)
{
    KeSetEvent(&ChainInitCompleteEvent, 0, FALSE);
}
