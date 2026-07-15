/*
 * MinNT - setupapi/installer.c
 * Windows Installer / transactional package manager.
 *
 * MinNT's installer tracks per-package state (installed version,
 * rollback manifest, dependency graph) and exposes add/remove
 * operations. A real MSI engine maintains a transactional script;
 * we model the script as a list of (action, key, value) tuples that
 * can be executed, rolled forward, or rolled back.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define INST_MAX_PACKAGES    64
#define INST_MAX_NAME        64
#define INST_MAX_VER         32
#define INST_MAX_SCRIPT      128
#define INST_MAX_PAYLOAD     4096

typedef enum _INST_ACTION {
    INST_ACT_CREATE_FILE = 0,
    INST_ACT_WRITE_FILE,
    INST_ACT_DELETE_FILE,
    INST_ACT_CREATE_REG,
    INST_ACT_WRITE_REG,
    INST_ACT_DELETE_REG,
    INST_ACT_CREATE_DIR,
    INST_ACT_INSTALL_SERVICE,
} INST_ACTION;

typedef struct _INST_SCRIPT_ENTRY {
    INST_ACTION Action;
    CHAR Key[260];
    UCHAR Value[256];
    ULONG ValueLength;
    BOOLEAN Done;
    BOOLEAN InUse;
} INST_SCRIPT_ENTRY, *PINST_SCRIPT_ENTRY;

typedef struct _INST_PACKAGE {
    CHAR Name[INST_MAX_NAME];
    CHAR Version[INST_MAX_VER];
    CHAR Provider[64];
    ULONG ScriptStart;
    ULONG ScriptCount;
    BOOLEAN Installed;
    BOOLEAN InUse;
} INST_PACKAGE, *PINST_PACKAGE;

static INST_PACKAGE g_Packages[INST_MAX_PACKAGES];
static INST_SCRIPT_ENTRY g_Script[INST_MAX_SCRIPT];

static INST_PACKAGE *InstFindPackage(const CHAR *name)
{
    for (ULONG i = 0; i < INST_MAX_PACKAGES; i++) {
        if (!g_Packages[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < INST_MAX_NAME; k++) {
            if (g_Packages[i].Name[k] != name[k]) { eq = FALSE; break; }
            if (name[k] == 0) break;
        }
        if (eq) return &g_Packages[i];
    }
    return NULL;
}

static NTSTATUS InstExec(PINST_SCRIPT_ENTRY e)
{
    switch (e->Action) {
    case INST_ACT_CREATE_FILE:
    case INST_ACT_CREATE_DIR:
    case INST_ACT_CREATE_REG:
    case INST_ACT_WRITE_REG:
    case INST_ACT_DELETE_REG:
    case INST_ACT_WRITE_FILE:
    case INST_ACT_DELETE_FILE:
        /* Each of these maps to a real I/O or registry operation; we
         * log the action so the user can verify the script ran. */
        DbgPrint("INSTALL: action %u key=%s len=%u\n",
                 e->Action, e->Key, e->ValueLength);
        break;
    case INST_ACT_INSTALL_SERVICE:
        DbgPrint("INSTALL: install service %s\n", e->Key);
        break;
    }
    e->Done = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI InstRegisterPackage(const CHAR *Name, const CHAR *Version,
                                   const CHAR *Provider)
{
    if (InstFindPackage(Name)) return STATUS_OBJECT_NAME_COLLISION;
    for (ULONG i = 0; i < INST_MAX_PACKAGES; i++) {
        if (!g_Packages[i].InUse) {
            RtlZeroMemory(&g_Packages[i], sizeof(INST_PACKAGE));
            g_Packages[i].InUse = TRUE;
            for (ULONG k = 0; k < INST_MAX_NAME; k++) {
                g_Packages[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            for (ULONG k = 0; k < INST_MAX_VER; k++) {
                g_Packages[i].Version[k] = Version[k];
                if (Version[k] == 0) break;
            }
            for (ULONG k = 0; k < 64; k++) {
                g_Packages[i].Provider[k] = Provider[k];
                if (Provider[k] == 0) break;
            }
            /* Find a free script range. */
            for (ULONG j = 0; j < INST_MAX_SCRIPT; j++) {
                if (!g_Script[j].InUse) {
                    g_Packages[i].ScriptStart = j;
                    g_Packages[i].ScriptCount = 0;
                    break;
                }
            }
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI InstAddAction(const CHAR *Package, INST_ACTION Action,
                             const CHAR *Key, PVOID Value, ULONG ValueLength)
{
    INST_PACKAGE *p = InstFindPackage(Package);
    if (!p) return STATUS_NOT_FOUND;
    ULONG slot = p->ScriptStart + p->ScriptCount;
    if (slot >= INST_MAX_SCRIPT) return STATUS_NO_MEMORY;
    RtlZeroMemory(&g_Script[slot], sizeof(INST_SCRIPT_ENTRY));
    g_Script[slot].InUse = TRUE;
    g_Script[slot].Action = Action;
    for (ULONG k = 0; k < 260; k++) {
        g_Script[slot].Key[k] = Key[k];
        if (Key[k] == 0) break;
    }
    if (Value && ValueLength) {
        if (ValueLength > sizeof(g_Script[slot].Value)) ValueLength = sizeof(g_Script[slot].Value);
        RtlCopyMemory(g_Script[slot].Value, Value, ValueLength);
        g_Script[slot].ValueLength = ValueLength;
    }
    p->ScriptCount++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI InstInstall(const CHAR *Package)
{
    INST_PACKAGE *p = InstFindPackage(Package);
    if (!p) return STATUS_NOT_FOUND;
    if (p->Installed) return STATUS_SUCCESS;
    for (ULONG k = 0; k < p->ScriptCount; k++) {
        ULONG slot = p->ScriptStart + k;
        if (slot >= INST_MAX_SCRIPT) break;
        if (!g_Script[slot].InUse) continue;
        NTSTATUS s = InstExec(&g_Script[slot]);
        if (!NT_SUCCESS(s)) return s;
    }
    p->Installed = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI InstUninstall(const CHAR *Package)
{
    INST_PACKAGE *p = InstFindPackage(Package);
    if (!p) return STATUS_NOT_FOUND;
    if (!p->Installed) return STATUS_SUCCESS;
    /* Walk the script in reverse and undo each action. */
    for (LONG k = (LONG)p->ScriptCount - 1; k >= 0; k--) {
        ULONG slot = p->ScriptStart + (ULONG)k;
        if (slot >= INST_MAX_SCRIPT) continue;
        if (!g_Script[slot].InUse) continue;
        /* A real rollback would reverse the action; we just clear the
         * Done flag here. */
        g_Script[slot].Done = FALSE;
    }
    p->Installed = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI InstEnum(PULONG OutArray, PULONG InOutCount)
{
    if (!OutArray || !InOutCount) return STATUS_INVALID_PARAMETER;
    ULONG max = *InOutCount;
    ULONG count = 0;
    for (ULONG i = 0; i < INST_MAX_PACKAGES && count < max; i++) {
        if (g_Packages[i].InUse) OutArray[count++] = i + 1;
    }
    *InOutCount = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI InstInit(VOID)
{
    RtlZeroMemory(g_Packages, sizeof(g_Packages));
    RtlZeroMemory(g_Script, sizeof(g_Script));
    /* Seed a few well-known packages. */
    InstRegisterPackage("MinNT-Core", "6.0.6000.0", "MinNT Foundation");
    InstRegisterPackage("MinNT-Cabinet", "6.0.6000.0", "MinNT Foundation");
    InstRegisterPackage("MinNT-Shell", "6.0.6000.0", "MinNT Foundation");
    DbgPrint("INSTALL: transactional installer initialized\n");
    return STATUS_SUCCESS;
}
