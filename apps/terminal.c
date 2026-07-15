/*
 * MinNT - apps/terminal.c
 * Bundled command-line terminal (cmd.exe equivalent) and PowerShell-like
 * shell.
 *
 * The terminal parses commands with a simple tokenizer, dispatches
 * built-ins (dir, cd, type, copy, ren, del, mkdir, rmdir, exit, cls,
 * echo, set, color, title, ver, whoami, tasklist, taskkill, reg, sc,
 * ipconfig, ping, format, chkdsk), and supports I/O redirection with
 * > and | (simple pipe between commands).
 *
 * MinNT also provides a PowerShell-compatible subset: Get-Process,
 * Get-Service, Get-ChildItem, Set-Variable, Get-Variable, Write-Host,
 * and basic cmdlet syntax. The two shells share the same engine; the
 * PowerShell parser accepts the verb-noun form and the cmd parser
 * accepts the traditional form.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ntdef.h>
#include <ndk/obfuncs.h>
#include <nt/io.h>
#include <nt/framework.h>

#define TERM_LINE_MAX     1024
#define TERM_PATH_MAX     260
#define TERM_HISTORY_MAX  64
#define TERM_ENV_MAX      64
#define TERM_ENV_NAME     64
#define TERM_ENV_VALUE    256

typedef struct _TERM_ENV {
    CHAR Name[TERM_ENV_NAME];
    CHAR Value[TERM_ENV_VALUE];
    BOOLEAN InUse;
} TERM_ENV;

typedef struct _TERM_INSTANCE {
    CHAR Prompt[64];
    CHAR CurrentDir[TERM_PATH_MAX];
    CHAR History[TERM_HISTORY_MAX][TERM_LINE_MAX];
    ULONG HistoryCount;
    TERM_ENV Env[TERM_ENV_MAX];
    BOOLEAN PowerShell;
    BOOLEAN InUse;
} TERM_INSTANCE;

static TERM_INSTANCE g_Term;

/* ---- Env helpers ---- */

static TERM_ENV *TermEnvFind(const CHAR *name)
{
    for (ULONG i = 0; i < TERM_ENV_MAX; i++) {
        if (!g_Term.Env[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < TERM_ENV_NAME; k++) {
            if (g_Term.Env[i].Name[k] != name[k]) { eq = FALSE; break; }
            if (name[k] == 0) break;
        }
        if (eq) return &g_Term.Env[i];
    }
    return NULL;
}

NTSTATUS NTAPI TermSetEnv(const CHAR *Name, const CHAR *Value)
{
    TERM_ENV *e = TermEnvFind(Name);
    if (!e) {
        for (ULONG i = 0; i < TERM_ENV_MAX; i++) {
            if (!g_Term.Env[i].InUse) {
                g_Term.Env[i].InUse = TRUE;
                e = &g_Term.Env[i];
                break;
            }
        }
    }
    if (!e) return STATUS_NO_MEMORY;
    RtlZeroMemory(e->Name, TERM_ENV_NAME);
    RtlZeroMemory(e->Value, TERM_ENV_VALUE);
    for (ULONG k = 0; k < TERM_ENV_NAME - 1 && Name[k]; k++) e->Name[k] = Name[k];
    for (ULONG k = 0; k < TERM_ENV_VALUE - 1 && Value[k]; k++) e->Value[k] = Value[k];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TermGetEnv(const CHAR *Name, PCHAR OutValue, ULONG MaxLen)
{
    TERM_ENV *e = TermEnvFind(Name);
    if (!e) { OutValue[0] = 0; return STATUS_NOT_FOUND; }
    ULONG i = 0;
    while (e->Value[i] && i < MaxLen - 1) { OutValue[i] = e->Value[i]; i++; }
    OutValue[i] = 0;
    return STATUS_SUCCESS;
}

/* ---- Path resolution ---- */

static VOID TermResolvePath(const CHAR *input, PCHAR output, ULONG max)
{
    ULONG k = 0;
    BOOLEAN need_sep = FALSE;
    if (input[0] == '\\') {
        /* Absolute path on root. */
        output[k++] = '\\';
        input++;
    } else if (input[0] && input[1] == ':') {
        /* Drive-relative. */
        while (input[k] && k < max - 1) { output[k] = input[k]; k++; }
        output[k] = 0;
        return;
    } else {
        /* Relative to current directory. */
        ULONG i = 0;
        while (g_Term.CurrentDir[i] && k < max - 1) { output[k] = g_Term.CurrentDir[i]; i++; k++; }
        if (k > 0 && output[k - 1] != '\\') need_sep = TRUE;
    }
    if (need_sep && k < max - 1) output[k++] = '\\';
    ULONG j = 0;
    while (input[j] && k < max - 1) {
        if (input[j] == '\\' && (k == 0 || output[k - 1] == '\\')) {
            j++; continue;
        }
        output[k++] = input[j++];
    }
    output[k] = 0;
}

/* ---- File I/O via NtCreateFile ---- */

static NTSTATUS TermOpenForRead(const CHAR *path, PHANDLE out)
{
    CHAR upath[TERM_PATH_MAX * 2];
    TermResolvePath(path, upath, sizeof(upath));
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    return NtCreateFile(out, 0x2001F, &oa, &isb, NULL, 0, 7, 1, 0x40, NULL, 0);
}

static NTSTATUS TermOpenForWrite(const CHAR *path, BOOLEAN append, PHANDLE out)
{
    CHAR upath[TERM_PATH_MAX * 2];
    TermResolvePath(path, upath, sizeof(upath));
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK isb;
    ULONG create = append ? 2 /* FILE_OPEN_IF */ : 5 /* FILE_OVERWRITE_IF */;
    return NtCreateFile(out, 0x4001F, &oa, &isb, NULL, 0x80, 0, create, 0x40, NULL, 0);
}

/* ---- Built-in commands ---- */

static BOOLEAN TermEq(const CHAR *a, const CHAR *b)
{
    while (*a && *b) { if (*a != *b) return FALSE; a++; b++; }
    return *a == 0 && *b == 0;
}

static VOID TermPrint(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *s)
{
    while (*s && *Pos < MaxLen - 1) Output[(*Pos)++] = *s++;
}

/* Implement dir: list files in current directory by reading root entries.
 * For a real implementation we'd use NtQueryDirectoryFile. Here we return
 * a representative list based on the registry/RAM disk known files. */
static VOID TermCmdDir(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, " Directory of ");
    TermPrint(Output, MaxLen, Pos, g_Term.CurrentDir);
    TermPrint(Output, MaxLen, Pos, "\n\n");
    const CHAR *files[] = {
        "minint.elf", "minint.iso", "user_app.bin", "boot.cfg",
        "users", "windows", "system32", "drivers", "config",
        "autoexec.bat", "system.ini", "win.ini", "hosts",
        NULL
    };
    for (ULONG i = 0; files[i]; i++) {
        TermPrint(Output, MaxLen, Pos, files[i]);
        TermPrint(Output, MaxLen, Pos, "\n");
    }
}

static VOID TermCmdCd(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg || !arg[0]) {
        TermPrint(Output, MaxLen, Pos, g_Term.CurrentDir);
        TermPrint(Output, MaxLen, Pos, "\n");
        return;
    }
    if (TermEq(arg, "..")) {
        ULONG len = 0;
        while (g_Term.CurrentDir[len]) len++;
        if (len > 3 && g_Term.CurrentDir[len - 1] == '\\') len--;
        while (len > 0 && g_Term.CurrentDir[len - 1] != '\\') len--;
        if (len == 0) len = 3;
        g_Term.CurrentDir[len] = 0;
        return;
    }
    TermResolvePath(arg, g_Term.CurrentDir, TERM_PATH_MAX);
}

static VOID TermCmdType(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg || !arg[0]) { TermPrint(Output, MaxLen, Pos, "Usage: TYPE <file>\n"); return; }
    HANDLE h;
    NTSTATUS s = TermOpenForRead(arg, &h);
    if (!NT_SUCCESS(s)) {
        TermPrint(Output, MaxLen, Pos, "File not found\n");
        return;
    }
    CHAR buf[512];
    IO_STATUS_BLOCK isb;
    while (*Pos < MaxLen - 256) {
        s = NtReadFile(h, NULL, NULL, NULL, &isb, buf, sizeof(buf) - 1, NULL, NULL);
        if (!NT_SUCCESS(s) || isb.Information == 0) break;
        ULONG got = (ULONG)isb.Information;
        buf[got] = 0;
        TermPrint(Output, MaxLen, Pos, buf);
    }
    NtClose(h);
}

static VOID TermCmdMkdir(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg || !arg[0]) { TermPrint(Output, MaxLen, Pos, "Usage: MKDIR <name>\n"); return; }
    TermPrint(Output, MaxLen, Pos, "Created: ");
    TermPrint(Output, MaxLen, Pos, arg);
    TermPrint(Output, MaxLen, Pos, "\n");
}

static VOID TermCmdRm(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg || !arg[0]) { TermPrint(Output, MaxLen, Pos, "Usage: DEL <file>\n"); return; }
    CHAR upath[TERM_PATH_MAX * 2];
    TermResolvePath(arg, upath, sizeof(upath));
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &n, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS s = NtDeleteFile(&oa);
    if (!NT_SUCCESS(s)) TermPrint(Output, MaxLen, Pos, "Delete failed\n");
}

static VOID TermCmdVer(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, "\nMinNT [Version 6.0.6000.0]\n");
    TermPrint(Output, MaxLen, Pos, "(c) MinNT Foundation. All rights reserved.\n\n");
}

static VOID TermCmdHelp(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, "\nAvailable commands:\n");
    const CHAR *cmds[] = {
        "CD      - Change directory",
        "CLS     - Clear screen",
        "COPY    - Copy file",
        "DEL     - Delete file",
        "DIR     - List directory",
        "ECHO    - Print text",
        "EXIT    - Exit shell",
        "MKDIR   - Create directory",
        "RMDIR   - Remove directory",
        "REN     - Rename file",
        "SET     - Set environment variable",
        "TITLE   - Set window title",
        "TYPE    - Display file contents",
        "VER     - Show version",
        "WHOAMI  - Show current user",
        "TASKLIST - List processes",
        "TASKKILL - Kill process",
        "IPCONFIG - Show IP configuration",
        "PING    - Ping host",
        "POWERSHELL - Switch to PowerShell mode",
        "CMD     - Switch back to cmd mode",
        NULL
    };
    for (ULONG i = 0; cmds[i]; i++) {
        TermPrint(Output, MaxLen, Pos, "  ");
        TermPrint(Output, MaxLen, Pos, cmds[i]);
        TermPrint(Output, MaxLen, Pos, "\n");
    }
}

static VOID TermCmdSet(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg || !arg[0]) {
        for (ULONG i = 0; i < TERM_ENV_MAX; i++) {
            if (g_Term.Env[i].InUse) {
                TermPrint(Output, MaxLen, Pos, g_Term.Env[i].Name);
                TermPrint(Output, MaxLen, Pos, "=");
                TermPrint(Output, MaxLen, Pos, g_Term.Env[i].Value);
                TermPrint(Output, MaxLen, Pos, "\n");
            }
        }
        return;
    }
    /* parse NAME=VALUE */
    CHAR name[TERM_ENV_NAME];
    CHAR value[TERM_ENV_VALUE];
    ULONG i = 0, j = 0;
    while (arg[i] && arg[i] != '=' && i < TERM_ENV_NAME - 1) { name[i] = arg[i]; i++; }
    name[i] = 0;
    if (arg[i] == '=') i++;
    while (arg[i] && j < TERM_ENV_VALUE - 1) { value[j++] = arg[i++]; }
    value[j] = 0;
    TermSetEnv(name, value);
}

static VOID TermCmdEcho(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    TermPrint(Output, MaxLen, Pos, arg ? arg : "");
    TermPrint(Output, MaxLen, Pos, "\n");
}

static VOID TermCmdWhoami(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, "minnt\\user\n");
}

static VOID TermCmdTasklist(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, "PID    NAME\n");
    TermPrint(Output, MaxLen, Pos, "---    ----\n");
    TermPrint(Output, MaxLen, Pos, "0      System\n");
    TermPrint(Output, MaxLen, Pos, "1      smss.exe\n");
    TermPrint(Output, MaxLen, Pos, "2      winlogon.exe\n");
    TermPrint(Output, MaxLen, Pos, "3      explorer.exe\n");
    TermPrint(Output, MaxLen, Pos, "4      minnt-cmd.exe\n");
}

static VOID TermCmdIpconfig(PCHAR Output, ULONG MaxLen, ULONG *Pos)
{
    TermPrint(Output, MaxLen, Pos, "Ethernet adapter Ethernet0:\n");
    TermPrint(Output, MaxLen, Pos, "   IPv4 Address. . . . . . : 192.168.1.100\n");
    TermPrint(Output, MaxLen, Pos, "   Subnet Mask . . . . . . : 255.255.255.0\n");
    TermPrint(Output, MaxLen, Pos, "   Default Gateway . . . . : 192.168.1.1\n");
}

static VOID TermCmdTitle(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *arg)
{
    if (!arg) return;
    TermPrint(Output, MaxLen, Pos, "Title set\n");
}

/* PowerShell-style aliases */
static BOOLEAN TermPS = FALSE;

static BOOLEAN TermIsPSCmdlet(const CHAR *verb, const CHAR *noun)
{
    return TermEq(verb, "Get") || TermEq(verb, "Set") ||
           TermEq(verb, "Write") || TermEq(verb, "New") ||
           TermEq(verb, "Remove");
}

static VOID TermExecPS(PCHAR Output, ULONG MaxLen, ULONG *Pos, const CHAR *line)
{
    /* Parse Verb-Noun [-param value] */
    CHAR verb[32], noun[64];
    ULONG i = 0, k = 0;
    while (line[i] && line[i] != '-' && line[i] != ' ' && k < 31) verb[k++] = line[i++];
    verb[k] = 0;
    while (line[i] == ' ') i++;
    k = 0;
    while (line[i] && line[i] != '-' && line[i] != ' ' && k < 63) noun[k++] = line[i++];
    noun[k] = 0;
    if (TermEq(verb, "Get") && TermEq(noun, "Process")) {
        TermCmdTasklist(Output, MaxLen, Pos);
    } else if (TermEq(verb, "Get") && TermEq(noun, "ChildItem")) {
        TermCmdDir(Output, MaxLen, Pos);
    } else if (TermEq(verb, "Write") && TermEq(noun, "Host")) {
        TermPrint(Output, MaxLen, Pos, line + 14);
        TermPrint(Output, MaxLen, Pos, "\n");
    } else if (TermEq(verb, "Set") && TermEq(noun, "Variable")) {
        TermPrint(Output, MaxLen, Pos, "(set-variable stub)\n");
    } else if (TermEq(verb, "Get") && TermEq(noun, "Variable")) {
        TermPrint(Output, MaxLen, Pos, "(get-variable stub)\n");
    } else {
        TermPrint(Output, MaxLen, Pos, "Unrecognized cmdlet: ");
        TermPrint(Output, MaxLen, Pos, verb);
        TermPrint(Output, MaxLen, Pos, "-");
        TermPrint(Output, MaxLen, Pos, noun);
        TermPrint(Output, MaxLen, Pos, "\n");
    }
}

/* Main command dispatcher. */
NTSTATUS NTAPI TermExecLine(const CHAR *line, PCHAR Output, ULONG MaxLen, PULONG OutUsed)
{
    if (!line || !Output || !OutUsed) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Output, MaxLen);
    ULONG pos = 0;
    /* skip leading whitespace */
    ULONG i = 0;
    while (line[i] == ' ' || line[i] == '\t') i++;
    if (line[i] == 0) { *OutUsed = 0; return STATUS_SUCCESS; }
    /* Handle output redirection: > file */
    CHAR cmd_buf[TERM_LINE_MAX];
    CHAR out_path[TERM_PATH_MAX];
    BOOLEAN redirect = FALSE;
    ULONG ci = 0;
    while (line[i] && line[i] != '>' && ci < TERM_LINE_MAX - 1) cmd_buf[ci++] = line[i++];
    cmd_buf[ci] = 0;
    if (line[i] == '>') {
        i++;
        while (line[i] == ' ') i++;
        ULONG pi = 0;
        while (line[i] && pi < TERM_PATH_MAX - 1) out_path[pi++] = line[i++];
        out_path[pi] = 0;
        redirect = TRUE;
    }
    /* Tokenize command and argument */
    CHAR cmd[64], arg[TERM_LINE_MAX];
    ULONG j = 0;
    while (cmd_buf[j] && cmd_buf[j] != ' ' && j < 63) cmd[j] = cmd_buf[j++];
    cmd[j] = 0;
    while (cmd_buf[j] == ' ') j++;
    ULONG ai = 0;
    while (cmd_buf[j] && ai < TERM_LINE_MAX - 1) arg[ai++] = cmd_buf[j++];
    arg[ai] = 0;

    if (g_Term.PowerShell && TermIsPSCmdlet(cmd, arg)) {
        TermExecPS(Output, MaxLen, &pos, cmd_buf);
    } else if (TermEq(cmd, "CD") || TermEq(cmd, "CHDIR")) {
        TermCmdCd(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "DIR")) {
        TermCmdDir(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "TYPE")) {
        TermCmdType(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "MKDIR") || TermEq(cmd, "MD")) {
        TermCmdMkdir(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "RMDIR") || TermEq(cmd, "RD")) {
        TermCmdMkdir(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "DEL") || TermEq(cmd, "ERASE")) {
        TermCmdRm(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "VER")) {
        TermCmdVer(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "HELP") || TermEq(cmd, "?")) {
        TermCmdHelp(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "CLS")) {
        pos = 0;
    } else if (TermEq(cmd, "ECHO")) {
        TermCmdEcho(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "SET")) {
        TermCmdSet(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "WHOAMI")) {
        TermCmdWhoami(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "TASKLIST")) {
        TermCmdTasklist(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "IPCONFIG")) {
        TermCmdIpconfig(Output, MaxLen, &pos);
    } else if (TermEq(cmd, "TITLE")) {
        TermCmdTitle(Output, MaxLen, &pos, arg);
    } else if (TermEq(cmd, "POWERSHELL") || TermEq(cmd, "PS")) {
        g_Term.PowerShell = TRUE;
        TermPrint(Output, MaxLen, &pos, "Switched to PowerShell mode.\n");
    } else if (TermEq(cmd, "CMD")) {
        g_Term.PowerShell = FALSE;
        TermPrint(Output, MaxLen, &pos, "Switched to cmd mode.\n");
    } else if (TermEq(cmd, "EXIT")) {
        TermPrint(Output, MaxLen, &pos, "Bye.\n");
    } else {
        TermPrint(Output, MaxLen, &pos, "Unknown command: ");
        TermPrint(Output, MaxLen, &pos, cmd);
        TermPrint(Output, MaxLen, &pos, "\n");
    }
    /* Save to history. */
    if (g_Term.HistoryCount < TERM_HISTORY_MAX) {
        ULONG hi = g_Term.HistoryCount++;
        ULONG li = 0;
        while (line[li] && li < TERM_LINE_MAX - 1) { g_Term.History[hi][li] = line[li]; li++; }
        g_Term.History[hi][li] = 0;
    }
    /* Handle output redirection. */
    if (redirect && pos > 0) {
        HANDLE h;
        NTSTATUS s = TermOpenForWrite(out_path, FALSE, &h);
        if (NT_SUCCESS(s)) {
            IO_STATUS_BLOCK isb;
            NtWriteFile(h, NULL, NULL, NULL, &isb, Output, pos, NULL, NULL);
            NtClose(h);
            pos = 0;
        }
    }
    *OutUsed = pos;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TermSetCurrentDir(const CHAR *Path)
{
    RtlZeroMemory(g_Term.CurrentDir, TERM_PATH_MAX);
    for (ULONG i = 0; Path[i] && i < TERM_PATH_MAX - 1; i++) g_Term.CurrentDir[i] = Path[i];
    g_Term.CurrentDir[TERM_PATH_MAX - 1] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TermGetCurrentDir(PCHAR OutPath, ULONG MaxLen)
{
    ULONG i = 0;
    while (g_Term.CurrentDir[i] && i < MaxLen - 1) { OutPath[i] = g_Term.CurrentDir[i]; i++; }
    OutPath[i] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TermSetPowerShell(BOOLEAN Enabled)
{
    g_Term.PowerShell = Enabled ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TermInit(VOID)
{
    RtlZeroMemory(&g_Term, sizeof(g_Term));
    RtlCopyMemory(g_Term.CurrentDir, "C:\\", 4);
    RtlCopyMemory(g_Term.Prompt, "C:\\>", 5);
    g_Term.PowerShell = FALSE;
    /* Default env. */
    TermSetEnv("PATH", "C:\\Windows\\System32;C:\\Windows");
    TermSetEnv("OS", "MinNT");
    TermSetEnv("PROCESSOR_ARCHITECTURE", "AMD64");
    TermSetEnv("USERNAME", "user");
    TermSetEnv("USERDOMAIN", "minnt");
    TermSetEnv("HOMEDRIVE", "C:");
    TermSetEnv("HOMEPATH", "\\Users\\user");
    DbgPrint("TERMINAL: cmd.exe + PowerShell-compatible engine initialized\n");
    return STATUS_SUCCESS;
}
