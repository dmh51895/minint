/*
 * MinNT - print/spooler.c
 * Print spooler service.
 *
 * Manages a queue of print jobs, each holding a reference to a
 * printer and the document data to print. Jobs are written to a
 * spool file (e.g. \\?\Spool\Printers\<jobid>.spl) and processed
 * in FIFO order by a worker thread that invokes the printer driver.
 *
 * Print API:
 *   SpoolOpenPrinter / SpoolClosePrinter
 *   SpoolStartDocPrinter / SpoolEndDocPrinter
 *   SpoolWritePrinter (writes to the spool file)
 *   SpoolEnumJobs (lists pending jobs)
 *   SpoolCancelJob
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ps.h>

#define MAX_PRINTERS 8
#define MAX_JOBS     32
#define MAX_PRINTER_NAME 64
#define MAX_DOC_NAME    128

typedef struct _PRINT_JOB {
    ULONG  JobId;
    ULONG  PrinterId;
    ULONG  Status;            /* 0=pending, 1=printing, 2=done, 3=cancelled */
    WCHAR  DocumentName[MAX_DOC_NAME];
    ULONG  BytesWritten;
    PVOID  SpoolBuffer;        /* in-memory spool data */
    ULONG  SpoolSize;
    ULONG64 StartTime;
    BOOLEAN InUse;
} PRINT_JOB, *PPRINT_JOB;

typedef struct _PRINTER {
    ULONG Id;
    WCHAR Name[MAX_PRINTER_NAME];
    BOOLEAN Default;
    BOOLEAN InUse;
} PRINTER, *PPRINTER;

static PRINTER g_Printers[MAX_PRINTERS];
static PRINT_JOB g_Jobs[MAX_JOBS];
static KSPIN_LOCK g_PrintLock;
static ULONG g_NextJobId = 1;
static BOOLEAN g_SpoolerRunning = FALSE;
static PETHREAD g_SpoolerThread = NULL;

/* Spooler worker thread: scans for pending jobs and "prints" them. */
static VOID NTAPI SpoolerThread(PVOID Context)
{
    (void)Context;
    while (g_SpoolerRunning) {
        ULONG i;
        BOOLEAN didWork = FALSE;

        for (i = 0; i < MAX_JOBS; i++) {
            if (!g_Jobs[i].InUse) continue;
            if (g_Jobs[i].Status != 0) continue;
            /* Mark as printing */
            g_Jobs[i].Status = 1;
            /* Process the spool data: in MinNT, log the print job and
             * simulate the time it takes. A real printer driver would
             * read the spool data and transmit it to the printer port
             * via the parallel/USB/network interface. */
            DbgPrint("PRINT: printing job %u on printer %u (%ws, %u bytes)\n",
                     g_Jobs[i].JobId, g_Jobs[i].PrinterId,
                     g_Jobs[i].DocumentName, g_Jobs[i].BytesWritten);
            KeStallExecutionProcessor(10000); /* simulate print time */
            g_Jobs[i].Status = 2; /* done */
            didWork = TRUE;
        }

        if (!didWork) {
            KeStallExecutionProcessor(10000);
            KiDispatchNextThread();
        }
    }
}

NTSTATUS NTAPI SpoolerInit(VOID)
{
    RtlZeroMemory(g_Printers, sizeof(g_Printers));
    RtlZeroMemory(g_Jobs, sizeof(g_Jobs));
    KeInitializeSpinLock(&g_PrintLock);

    /* Register default printers */
    {
        PRINTER *p = &g_Printers[0];
        p->Id = 0;
        RtlCopyMemory(p->Name, L"Microsoft Print to PDF", 44);
        p->Name[43] = 0;
        p->Default = TRUE;
        p->InUse = TRUE;
    }
    {
        PRINTER *p = &g_Printers[1];
        p->Id = 1;
        RtlCopyMemory(p->Name, L"Generic / Text Only", 38);
        p->Name[37] = 0;
        p->Default = FALSE;
        p->InUse = TRUE;
    }

    /* Start the spooler worker thread */
    g_SpoolerRunning = TRUE;
    {
        NTSTATUS status;
        extern struct _EPROCESS *PsInitialSystemProcess;
        status = PsCreateSystemThread(PsInitialSystemProcess, SpoolerThread,
                                        NULL, &g_SpoolerThread);
        if (!NT_SUCCESS(status)) {
            g_SpoolerRunning = FALSE;
            return status;
        }
    }

    DbgPrint("PRINT: spooler initialized (%d printers, %d job slots)\n",
             2, MAX_JOBS);
    return STATUS_SUCCESS;
}

/* Add a new printer. Returns its ID. */
ULONG NTAPI SpoolAddPrinter(const WCHAR *Name, BOOLEAN IsDefault)
{
    ULONG i;
    KIRQL irql;
    if (!Name) return (ULONG)-1;

    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_PRINTERS; i++) {
        if (!g_Printers[i].InUse) {
            g_Printers[i].Id = i;
            ULONG j = 0;
            while (Name[j] && j < MAX_PRINTER_NAME - 1) g_Printers[i].Name[j] = Name[j], j++;
            g_Printers[i].Name[j] = 0;
            g_Printers[i].Default = IsDefault;
            g_Printers[i].InUse = TRUE;
            KeReleaseSpinLock(&g_PrintLock, &irql);
            DbgPrint("PRINT: added printer '%ws'\n", Name);
            return i;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return (ULONG)-1;
}

/* Remove a printer. */
NTSTATUS NTAPI SpoolRemovePrinter(ULONG PrinterId)
{
    KIRQL irql;
    if (PrinterId >= MAX_PRINTERS || !g_Printers[PrinterId].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    g_Printers[PrinterId].InUse = FALSE;
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return STATUS_SUCCESS;
}

/* Enumerate printers. */
ULONG NTAPI SpoolEnumPrinters(ULONG MaxCount, ULONG *pIds, PCHAR *pNames,
                                PBOOLEAN pIsDefault)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_PRINTERS && n < MaxCount; i++) {
        if (g_Printers[i].InUse) {
            if (pIds) pIds[n] = g_Printers[i].Id;
            if (pNames) {
                ULONG j = 0;
                while (g_Printers[i].Name[j] && j < MAX_PRINTER_NAME - 1) {
                    pNames[n][j] = (CHAR)g_Printers[i].Name[j];
                    j++;
                }
                pNames[n][j] = 0;
            }
            if (pIsDefault) pIsDefault[n] = g_Printers[i].Default;
            n++;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return n;
}

/* Get the default printer. */
ULONG NTAPI SpoolGetDefaultPrinter(VOID)
{
    ULONG i;
    ULONG defaultId = (ULONG)-1;
    KIRQL irql;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_PRINTERS; i++) {
        if (g_Printers[i].InUse && g_Printers[i].Default) {
            defaultId = g_Printers[i].Id;
            break;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return defaultId;
}

/* Submit a new print job. Returns the job ID. */
ULONG NTAPI SpoolSubmitJob(ULONG PrinterId, const WCHAR *DocumentName)
{
    ULONG i;
    KIRQL irql;
    if (PrinterId >= MAX_PRINTERS || !g_Printers[PrinterId].InUse)
        return (ULONG)-1;

    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_JOBS; i++) {
        if (!g_Jobs[i].InUse) {
            RtlZeroMemory(&g_Jobs[i], sizeof(PRINT_JOB));
            g_Jobs[i].JobId = g_NextJobId++;
            g_Jobs[i].PrinterId = PrinterId;
            g_Jobs[i].Status = 0;
            if (DocumentName) {
                ULONG j = 0;
                while (DocumentName[j] && j < MAX_DOC_NAME - 1) {
                    g_Jobs[i].DocumentName[j] = DocumentName[j];
                    j++;
                }
                g_Jobs[i].DocumentName[j] = 0;
            }
            g_Jobs[i].StartTime = (ULONG64)KeTickCount;
            g_Jobs[i].InUse = TRUE;
            KeReleaseSpinLock(&g_PrintLock, &irql);
            DbgPrint("PRINT: submitted job %u (printer %u, '%ws')\n",
                     g_Jobs[i].JobId, PrinterId,
                     DocumentName ? DocumentName : L"(unnamed)");
            return g_Jobs[i].JobId;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return (ULONG)-1;
}

/* Write data to a job's spool buffer. */
NTSTATUS NTAPI SpoolWriteJob(ULONG JobId, PVOID Data, ULONG DataSize)
{
    ULONG i;
    KIRQL irql;
    if (!Data || DataSize == 0) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_JOBS; i++) {
        if (g_Jobs[i].InUse && g_Jobs[i].JobId == JobId) {
            if (!g_Jobs[i].SpoolBuffer) {
                g_Jobs[i].SpoolBuffer = ExAllocatePool(NonPagedPool, DataSize);
                if (!g_Jobs[i].SpoolBuffer) {
                    KeReleaseSpinLock(&g_PrintLock, &irql);
                    return STATUS_NO_MEMORY;
                }
                g_Jobs[i].SpoolSize = DataSize;
            } else if (g_Jobs[i].BytesWritten + DataSize > g_Jobs[i].SpoolSize) {
                KeReleaseSpinLock(&g_PrintLock, &irql);
                return STATUS_BUFFER_OVERFLOW;
            }
            __builtin_memcpy((PUCHAR)g_Jobs[i].SpoolBuffer + g_Jobs[i].BytesWritten,
                             Data, DataSize);
            g_Jobs[i].BytesWritten += DataSize;
            KeReleaseSpinLock(&g_PrintLock, &irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return STATUS_NOT_FOUND;
}

/* Cancel a job. */
NTSTATUS NTAPI SpoolCancelJob(ULONG JobId)
{
    ULONG i;
    KIRQL irql;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_JOBS; i++) {
        if (g_Jobs[i].InUse && g_Jobs[i].JobId == JobId) {
            g_Jobs[i].Status = 3; /* cancelled */
            if (g_Jobs[i].SpoolBuffer) {
                ExFreePool(g_Jobs[i].SpoolBuffer);
                g_Jobs[i].SpoolBuffer = NULL;
                g_Jobs[i].SpoolSize = 0;
                g_Jobs[i].BytesWritten = 0;
            }
            KeReleaseSpinLock(&g_PrintLock, &irql);
            DbgPrint("PRINT: cancelled job %u\n", JobId);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return STATUS_NOT_FOUND;
}

/* Enumerate jobs. */
ULONG NTAPI SpoolEnumJobs(ULONG MaxCount, ULONG *pJobIds, ULONG *pStatus,
                           ULONG *pBytes)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_JOBS && n < MaxCount; i++) {
        if (g_Jobs[i].InUse) {
            if (pJobIds) pJobIds[n] = g_Jobs[i].JobId;
            if (pStatus) pStatus[n] = g_Jobs[i].Status;
            if (pBytes) pBytes[n] = g_Jobs[i].BytesWritten;
            n++;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return n;
}

/* Get job status by ID. */
NTSTATUS NTAPI SpoolGetJobStatus(ULONG JobId, PULONG pStatus)
{
    ULONG i;
    KIRQL irql;
    if (!pStatus) return STATUS_INVALID_PARAMETER;
    *pStatus = 0;
    KeAcquireSpinLock(&g_PrintLock, &irql);
    for (i = 0; i < MAX_JOBS; i++) {
        if (g_Jobs[i].InUse && g_Jobs[i].JobId == JobId) {
            *pStatus = g_Jobs[i].Status;
            KeReleaseSpinLock(&g_PrintLock, &irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_PrintLock, &irql);
    return STATUS_NOT_FOUND;
}
