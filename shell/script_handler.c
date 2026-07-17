/*
 * MinNT - shell/script_handler.c
 * Script execution handler (.bat, .cmd, .ps1)
 *
 * Handles script files by routing to appropriate interpreter.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/fs.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

/* Execute batch script (.bat or .cmd) */
NTSTATUS NTAPI ScriptExecuteBat(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    
    DbgPrint("SCRIPT: executing batch file %s\n", Path);
    
    /* Read and execute batch file line by line */
    UNICODE_STRING upath;
    RtlInitUnicodeString(&upath, (PCWSTR)Path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &upath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x2001F, &oa, &isb, NULL, 0, 7, 1, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    
    /* Read file in chunks */
    UCHAR buf[4096];
    ULONG offset = 0;
    
    while (TRUE) {
        s = NtReadFile(h, NULL, NULL, NULL, &isb, buf, sizeof(buf), NULL, NULL);
        if (!NT_SUCCESS(s) || isb.Information == 0) break;
        
        ULONG bytesRead = (ULONG)isb.Information;
        
        /* Process each line */
        ULONG lineStart = 0;
        for (ULONG i = 0; i < bytesRead; i++) {
            if (buf[i] == '\n' || buf[i] == '\r') {
                /* Process line */
                if (i > lineStart) {
                    /* Echo the command (simplified) */
                    DbgPrint("BATCH> %.*s\n", i - lineStart, &buf[lineStart]);
                }
                lineStart = i + 1;
            }
        }
        
        offset += bytesRead;
    }
    
    NtClose(h);
    return STATUS_SUCCESS;
}

/* Execute PowerShell script (.ps1) */
NTSTATUS NTAPI ScriptExecutePs1(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    
    DbgPrint("SCRIPT: executing PowerShell script %s\n", Path);
    
    /* Simplified: just log that we would execute it.
     * A real implementation would need a PowerShell interpreter.
     * For now, we can try to execute it via WINE if available.
     */
    extern NTSTATUS NTAPI WineRunExecutable(const CHAR *Path, const CHAR *Args);
    return WineRunExecutable(Path, NULL);
}

/* Main script execution router */
NTSTATUS NTAPI ScriptExecute(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    
    /* Determine script type by extension */
    const CHAR *ext = Path;
    while (*ext) ext++;
    while (ext > Path && *ext != '.' && *ext != '/' && *ext != '\\') ext--;
    
    if (*ext != '.') return STATUS_INVALID_PARAMETER;
    ext++;
    
    if (ext[0] == 'b' || ext[0] == 'B') {
        return ScriptExecuteBat(Path);
    } else if (ext[0] == 'c' && (ext[1] == 'm' || ext[1] == 'M')) {
        return ScriptExecuteBat(Path);
    } else if (ext[0] == 'p' && ext[1] == 's' && ext[2] == '1') {
        return ScriptExecutePs1(Path);
    } else if (ext[0] == 's' && ext[1] == 'h') {
        /* Shell script - not natively supported but try */
        return ScriptExecuteBat(Path);
    }
    
    return STATUS_NOT_SUPPORTED;
}