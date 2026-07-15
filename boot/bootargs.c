/*
 * MinNT - boot/bootargs.c
 * Multiboot2 command-line argument parser.
 *
 * GRUB passes the kernel command line as a multiboot2 tag of type 1.
 * We extract it and provide accessor functions for the rest of the
 * kernel to check for flags like /install, /safemode, /terminal,
 * /recovery, /debug.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/hal.h>

#define MB2_TAG_TYPE_CMDLINE 1

static CHAR g_BootCmdLine[512] = {0};
static BOOLEAN g_Parsed = FALSE;

NTSTATUS NTAPI BootArgsParse(PVOID Mb2Info)
{
    if (!Mb2Info) return STATUS_INVALID_PARAMETER;
    if (g_Parsed) return STATUS_SUCCESS;

    ULONG totalSize = *(ULONG *)Mb2Info;
    UCHAR *ptr = (UCHAR *)Mb2Info + 8;
    UCHAR *end = (UCHAR *)Mb2Info + totalSize;

    while (ptr < end) {
        ULONG tagType = *(ULONG *)ptr;
        ULONG tagSize = *(ULONG *)(ptr + 4);
        if (tagType == 0 && tagSize == 0) break;
        if (tagType == MB2_TAG_TYPE_CMDLINE) {
            /* The cmdline string starts at ptr + 8. */
            CHAR *cmdline = (CHAR *)(ptr + 8);
            ULONG i = 0;
            while (cmdline[i] && i < sizeof(g_BootCmdLine) - 1) {
                g_BootCmdLine[i] = cmdline[i];
                i++;
            }
            g_BootCmdLine[i] = 0;
            DbgPrint("BOOTARGS: command line = '%s'\n", g_BootCmdLine);
            g_Parsed = TRUE;
            return STATUS_SUCCESS;
        }
        ULONG aligned = (tagSize + 7) & ~7;
        ptr += aligned;
    }

    g_Parsed = TRUE;
    DbgPrint("BOOTARGS: no command line tag found\n");
    return STATUS_NOT_FOUND;
}

const CHAR *NTAPI BootArgsGetCmdLine(VOID)
{
    return g_BootCmdLine;
}

BOOLEAN NTAPI BootArgsHas(const CHAR *Flag)
{
    if (!Flag) return FALSE;
    /* Search for Flag as a substring in the command line. */
    ULONG flagLen = 0;
    while (Flag[flagLen]) flagLen++;
    ULONG cmdLen = 0;
    while (g_BootCmdLine[cmdLen]) cmdLen++;
    if (flagLen > cmdLen) return FALSE;
    for (ULONG i = 0; i + flagLen <= cmdLen; i++) {
        BOOLEAN match = TRUE;
        for (ULONG j = 0; j < flagLen; j++) {
            if (g_BootCmdLine[i + j] != Flag[j]) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

BOOLEAN NTAPI BootArgsIsInstall(VOID)     { return BootArgsHas("/install"); }
BOOLEAN NTAPI BootArgsIsSafeMode(VOID)    { return BootArgsHas("/safemode"); }
BOOLEAN NTAPI BootArgsIsTerminal(VOID)    { return BootArgsHas("/terminal"); }
BOOLEAN NTAPI BootArgsIsRecovery(VOID)    { return BootArgsHas("/recovery"); }
BOOLEAN NTAPI BootArgsIsDebug(VOID)       { return BootArgsHas("/debug"); }
BOOLEAN NTAPI BootArgsIsNetwork(VOID)     { return BootArgsHas("/network"); }
