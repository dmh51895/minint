/*
 * MinNT - boot/profile.c
 * Boot profile system.
 *
 * A boot profile declares which subsystems the kernel should
 * initialize. Instead of sprinkling `if (install)` checks everywhere,
 * each subsystem registers itself by name and the profile decides
 * which subsystems to enable.
 *
 * Profiles:
 *   - BootProfileNormal:   everything (regular desktop boot)
 *   - BootProfileLive:     everything except the OS installer
 *   - BootProfileInstall:  HAL + Memory + Disk + FS + Keyboard + FB
 *                          (no Win32k, no Network, no Shell, no WMI)
 *   - BootProfileSafe:     Normal minus USB/Audio/GPU/Gaming drivers
 *   - BootProfileRecovery: Normal minus shell/Explorer/Apps
 *   - BootProfileTerminal: Normal with text mode, no Explorer
 *   - BootProfileDebug:    Normal with verbose logging
 *
 * Adding a new subsystem: just call BootRegisterSubsystem() in the
 * subsystem's Init() and add its name to the relevant profiles.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/framework.h>

#define BOOT_MAX_PROFILES    8
#define BOOT_MAX_SUBSYSTEMS 96  /* matches boot/registry.c */

typedef struct _BOOT_PROFILE_DEF {
    BOOT_PROFILE Id;
    CHAR Name[32];
    const CHAR *Required[BOOT_MAX_SUBSYSTEMS];
    ULONG RequiredCount;
    const CHAR *Excluded[BOOT_MAX_SUBSYSTEMS];
    ULONG ExcludedCount;
    BOOLEAN InUse;
} BOOT_PROFILE_DEF;

static BOOT_PROFILE_DEF g_Profiles[BOOT_MAX_PROFILES];
static BOOT_PROFILE g_ActiveProfile = BootProfileNormal;

/* ---- Subsystem registration ----
 *
 * Old-style registration is now handled by boot/registry.c which
 * manages the full dependency-ordered registry. This file only
 * manages the boot profile definitions and per-subsystem gating.
 */

/* ---- Profile definitions ---- */

static NTSTATUS AddProfileSubsys(BOOT_PROFILE_DEF *p, const CHAR *Name, BOOLEAN Required)
{
    const CHAR **list = Required ? p->Required : p->Excluded;
    ULONG *count = Required ? &p->RequiredCount : &p->ExcludedCount;
    if (*count >= BOOT_MAX_SUBSYSTEMS) return STATUS_NO_MEMORY;
    list[(*count)++] = Name;
    return STATUS_SUCCESS;
}

static NTSTATUS BootDefineProfile(BOOT_PROFILE Id, const CHAR *Name)
{
    for (ULONG i = 0; i < BOOT_MAX_PROFILES; i++) {
        if (!g_Profiles[i].InUse) {
            RtlZeroMemory(&g_Profiles[i], sizeof(BOOT_PROFILE_DEF));
            g_Profiles[i].InUse = TRUE;
            g_Profiles[i].Id = Id;
            for (ULONG k = 0; k < 31 && Name[k]; k++) g_Profiles[i].Name[k] = Name[k];
            g_Profiles[i].Name[31] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

static BOOT_PROFILE_DEF *BootFindProfile(BOOT_PROFILE Id)
{
    for (ULONG i = 0; i < BOOT_MAX_PROFILES; i++) {
        if (g_Profiles[i].InUse && g_Profiles[i].Id == Id) return &g_Profiles[i];
    }
    return NULL;
}

NTSTATUS NTAPI BootProfileAddRequired(BOOT_PROFILE Id, const CHAR *Name)
{
    BOOT_PROFILE_DEF *p = BootFindProfile(Id);
    if (!p) return STATUS_NOT_FOUND;
    return AddProfileSubsys(p, Name, TRUE);
}

NTSTATUS NTAPI BootProfileAddExcluded(BOOT_PROFILE Id, const CHAR *Name)
{
    BOOT_PROFILE_DEF *p = BootFindProfile(Id);
    if (!p) return STATUS_NOT_FOUND;
    return AddProfileSubsys(p, Name, FALSE);
}

/* ---- Activation + lookup ---- */

NTSTATUS NTAPI BootProfileActivate(BOOT_PROFILE Id)
{
    g_ActiveProfile = Id;
    BOOT_PROFILE_DEF *p = BootFindProfile(Id);
    if (p) DbgPrint("BOOTPROFILE: active = %s (req=%u, exc=%u)\n",
                    p->Name, p->RequiredCount, p->ExcludedCount);
    return STATUS_SUCCESS;
}

BOOT_PROFILE NTAPI BootProfileGetActive(VOID)
{
    return g_ActiveProfile;
}

/* Returns TRUE if the named subsystem should be initialized under the
 * currently active profile. */
BOOLEAN NTAPI BootProfileAllowsSubsystem(const CHAR *Name)
{
    if (!Name) return FALSE;
    BOOT_PROFILE_DEF *p = BootFindProfile(g_ActiveProfile);
    /* Normal profile includes everything unless explicitly excluded. */
    for (ULONG i = 0; i < p->ExcludedCount; i++) {
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 32; k++) {
            if (p->Excluded[i][k] != Name[k]) { eq = FALSE; break; }
            if (Name[k] == 0) break;
        }
        if (eq) return FALSE;
    }
    /* If the profile has required entries, only those count. */
    if (p->RequiredCount > 0) {
        for (ULONG i = 0; i < p->RequiredCount; i++) {
            BOOLEAN eq = TRUE;
            for (ULONG k = 0; k < 32; k++) {
                if (p->Required[i][k] != Name[k]) { eq = FALSE; break; }
                if (Name[k] == 0) break;
            }
            if (eq) return TRUE;
        }
        return FALSE;
    }
    return TRUE;
}

/* Returns TRUE if the subsystem is required (vs optional). */
BOOLEAN NTAPI BootProfileIsSubsystemRequired(const CHAR *Name)
{
    if (!Name) return FALSE;
    BOOT_PROFILE_DEF *p = BootFindProfile(g_ActiveProfile);
    for (ULONG i = 0; i < p->RequiredCount; i++) {
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 32; k++) {
            if (p->Required[i][k] != Name[k]) { eq = FALSE; break; }
            if (Name[k] == 0) break;
        }
        if (eq) return TRUE;
    }
    return FALSE;
}

/* ---- Initialization ---- */

/* Define all profiles. Called once during Phase 0. */
NTSTATUS NTAPI BootProfileInit(VOID)
{
    RtlZeroMemory(g_Profiles, sizeof(g_Profiles));

    /* Define profiles. */
    BootDefineProfile(BootProfileNormal,   "Normal");
    BootDefineProfile(BootProfileLive,     "Live");
    BootDefineProfile(BootProfileInstall,  "Install");
    BootDefineProfile(BootProfileSafe,     "Safe");
    BootDefineProfile(BootProfileRecovery, "Recovery");
    BootDefineProfile(BootProfileTerminal, "Terminal");
    BootDefineProfile(BootProfileDebug,    "Debug");

    /* INSTALL profile: minimal set for OS installation. */
    BootProfileAddRequired(BootProfileInstall, "HAL");
    BootProfileAddRequired(BootProfileInstall, "Ke");
    BootProfileAddRequired(BootProfileInstall, "Mm");
    BootProfileAddRequired(BootProfileInstall, "Io");
    BootProfileAddRequired(BootProfileInstall, "Ahci");
    BootProfileAddRequired(BootProfileInstall, "Fs");
    BootProfileAddRequired(BootProfileInstall, "Framebuffer");
    BootProfileAddRequired(BootProfileInstall, "Keyboard");
    BootProfileAddRequired(BootProfileInstall, "Installer");
    BootProfileAddRequired(BootProfileInstall, "OsInstall");

    /* SAFE profile: Normal minus optional drivers. */
    BootProfileAddExcluded(BootProfileSafe, "Touch");
    BootProfileAddExcluded(BootProfileSafe, "Gamepad");
    BootProfileAddExcluded(BootProfileSafe, "Tpm");
    BootProfileAddExcluded(BootProfileSafe, "Win32k");
    BootProfileAddExcluded(BootProfileSafe, "Explorer");
    BootProfileAddExcluded(BootProfileSafe, "Audio");

    /* RECOVERY profile: minimal GUI. */
    BootProfileAddExcluded(BootProfileRecovery, "Explorer");
    BootProfileAddExcluded(BootProfileRecovery, "Apps");
    BootProfileAddExcluded(BootProfileRecovery, "Network");

    /* TERMINAL profile: no GUI shell. */
    BootProfileAddExcluded(BootProfileTerminal, "Explorer");
    BootProfileAddExcluded(BootProfileTerminal, "BootChain");

    /* DEBUG profile: same as Normal but enables extra logging. */
    BootProfileAddRequired(BootProfileDebug, "KDBG");

    /* LIVE profile: Normal minus Installer. */
    BootProfileAddExcluded(BootProfileLive, "Installer");

    DbgPrint("BOOTPROFILE: 7 profiles registered\n");
    return STATUS_SUCCESS;
}

/* Resolve the boot profile from the boot flags. */
NTSTATUS NTAPI BootProfileResolve(VOID)
{
    if (BootArgsIsInstall()) {
        BootProfileActivate(BootProfileInstall);
    } else if (BootArgsIsSafeMode()) {
        BootProfileActivate(BootProfileSafe);
    } else if (BootArgsIsRecovery()) {
        BootProfileActivate(BootProfileRecovery);
    } else if (BootArgsIsTerminal()) {
        BootProfileActivate(BootProfileTerminal);
    } else if (BootArgsIsDebug()) {
        BootProfileActivate(BootProfileDebug);
    } else {
        BootProfileActivate(BootProfileNormal);
    }
    return STATUS_SUCCESS;
}

/* Helper macro: run an init only if the active profile allows it. */
NTSTATUS NTAPI BootRunSubsystem(const CHAR *Name, NTSTATUS (*Init)(VOID))
{
    if (!BootProfileAllowsSubsystem(Name)) {
        DbgPrint("BOOTPROFILE: skip %s (not in profile)\n", Name);
        return STATUS_SUCCESS;
    }
    NTSTATUS s = Init();
    if (NT_SUCCESS(s)) BootMarkSubsystemInitialized(Name);
    return s;
}

/* Enumeration/initialization queries are now handled by boot/registry.c
 * which has the authoritative subsystem table. The profile module
 * only deals with per-profile Required/Excluded lists. */
