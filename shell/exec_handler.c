/*
 * MinNT - shell/exec_handler.c
 * Shell executable handler - handles double-click on .exe files
 *
 * When user double-clicks an EXE in Explorer, this handler:
 *   1. Detects PE format
 *   2. Decides whether to run natively or via WINE
 *   3. Spawns the executable with proper arguments
 *   4. Tracks the running process
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/ps.h>
#include <nt/pe.h>
#include <nt/framework.h>

/* Executable types */
#define EXEC_TYPE_UNKNOWN  0
#define EXEC_TYPE_NATIVE   1  /* AMD64 or ARM64 - run natively */
#define EXEC_TYPE_WINE     2  /* x86 or ARM - run via WINE */
#define EXEC_TYPE_SCRIPT   3  /* .bat, .cmd, .ps1 */
#define EXEC_TYPE_MSI      4  /* Windows Installer */

typedef struct _EXEC_REQUEST {
    CHAR Path[260];
    CHAR Args[512];
    CHAR WorkDir[260];
    ULONG Type;
    BOOLEAN UseWine;
    BOOLEAN RunAsAdmin;
    ULONG WindowMode;  /* 0=normal, 1=minimized, 2=maximized */
    PEPROCESS Process;
    HANDLE Thread;
} EXEC_REQUEST, *PEXEC_REQUEST;

static EXEC_REQUEST g_ExecHistory[32];
static ULONG g_ExecHistoryCount = 0;

/* Detect executable type from file extension and PE header */
NTSTATUS NTAPI ExecDetectType(const CHAR *Path, PULONG OutType)
{
    if (!Path || !OutType) return STATUS_INVALID_PARAMETER;
    
    /* Check extension first */
    const CHAR *ext = Path;
    ULONG len = 0;
    while (Path[len]) len++;
    
    /* Find last dot */
    const CHAR *dot = NULL;
    for (ULONG i = len; i > 0; i--) {
        if (Path[i-1] == '.') { dot = &Path[i-1]; break; }
        if (Path[i-1] == '/' || Path[i-1] == '\\') break;
    }
    
    if (dot) {
        dot++; /* Skip the dot */
        if (RtlCompareMemory(dot, "exe", 3) == 3 ||
            RtlCompareMemory(dot, "EXE", 3) == 3) {
            /* Check PE format */
            ULONG format = 0;
            extern NTSTATUS NTAPI WineDetectBinaryFormat(const CHAR *, PULONG);
            WineDetectBinaryFormat(Path, &format);
            if (format == 1 || format == 3) *OutType = EXEC_TYPE_NATIVE;
            else if (format == 2 || format == 4) *OutType = EXEC_TYPE_WINE;
            else *OutType = EXEC_TYPE_UNKNOWN;
            return STATUS_SUCCESS;
        }
        if (RtlCompareMemory(dot, "msi", 3) == 3 ||
            RtlCompareMemory(dot, "MSI", 3) == 3) {
            *OutType = EXEC_TYPE_MSI;
            return STATUS_SUCCESS;
        }
        if (RtlCompareMemory(dot, "bat", 3) == 3 ||
            RtlCompareMemory(dot, "cmd", 3) == 3 ||
            RtlCompareMemory(dot, "ps1", 3) == 3) {
            *OutType = EXEC_TYPE_SCRIPT;
            return STATUS_SUCCESS;
        }
    }
    
    *OutType = EXEC_TYPE_UNKNOWN;
    return STATUS_SUCCESS;
}

/* Execute an executable - main entry point for shell */
NTSTATUS NTAPI ExecRun(const CHAR *Path, const CHAR *Args, const CHAR *WorkDir)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    
    DbgPrint("EXEC: running %s (args=%s, workdir=%s)\n", Path, Args, WorkDir);
    
    /* Detect type */
    ULONG type = EXEC_TYPE_UNKNOWN;
    ExecDetectType(Path, &type);
    
    /* Check user preferences */
    BOOLEAN useWine = FALSE;
    extern NTSTATUS NTAPI PropsGetUseWine(const CHAR *, PBOOLEAN);
    PropsGetUseWine(Path, &useWine);
    
    NTSTATUS s;
    switch (type) {
    case EXEC_TYPE_NATIVE:
        if (useWine) {
            extern NTSTATUS NTAPI WineRunExecutable(const CHAR *, const CHAR *);
            s = WineRunExecutable(Path, Args);
        } else {
            extern NTSTATUS NTAPI PeRunExecutable(const CHAR *, const CHAR *);
            s = PeRunExecutable(Path, Args);
        }
        break;
        
    case EXEC_TYPE_WINE:
        {
            extern NTSTATUS NTAPI WineRunExecutable(const CHAR *, const CHAR *);
            s = WineRunExecutable(Path, Args);
        }
        break;
        
    case EXEC_TYPE_MSI:
        DbgPrint("EXEC: MSI package - launching installer\n");
        {
            extern NTSTATUS NTAPI MsiExecute(const CHAR *Path);
            s = MsiExecute(Path);
        }
        break;
        
    case EXEC_TYPE_SCRIPT:
        DbgPrint("EXEC: Script - launching interpreter\n");
        {
            extern NTSTATUS NTAPI ScriptExecute(const CHAR *Path);
            s = ScriptExecute(Path);
        }
        break;
        
    default:
        DbgPrint("EXEC: Unknown type for %s\n", Path);
        s = STATUS_INVALID_IMAGE_FORMAT;
        break;
    }
    
    /* Record in history */
    if (g_ExecHistoryCount < 32) {
        EXEC_REQUEST *req = &g_ExecHistory[g_ExecHistoryCount];
        RtlZeroMemory(req, sizeof(EXEC_REQUEST));
        ULONG k = 0;
        while (Path[k] && k < 259) { req->Path[k] = Path[k]; k++; }
        req->Path[k] = 0;
        if (Args) {
            k = 0;
            while (Args[k] && k < 511) { req->Args[k] = Args[k]; k++; }
            req->Args[k] = 0;
        }
        req->Type = type;
        req->UseWine = useWine;
        g_ExecHistoryCount++;
    }
    
    return s;
}

/* Get execution history for recent files menu */
ULONG NTAPI ExecGetHistory(PEXEC_REQUEST OutBuffer, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < g_ExecHistoryCount && n < MaxCount; i++) {
        RtlCopyMemory(&OutBuffer[n], &g_ExecHistory[i], sizeof(EXEC_REQUEST));
        n++;
    }
    return n;
}

/* Clear execution history */
NTSTATUS NTAPI ExecClearHistory(VOID)
{
    RtlZeroMemory(g_ExecHistory, sizeof(g_ExecHistory));
    g_ExecHistoryCount = 0;
    return STATUS_SUCCESS;
}