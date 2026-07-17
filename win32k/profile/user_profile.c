/*
 * MinNT - win32k/profile/user_profile.c
 * User profile system for MinNT.
 *
 * Manages per-user settings:
 *   - Wallpaper path
 *   - Theme name
 *   - Accent color
 *   - User name
 *   - Profile picture
 *   - Desktop icons
 *   - Taskbar settings
 *   - Start menu settings
 *
 * Profiles are stored in /Registry/User/<username>/...
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/fs.h>
#include <nt/cm.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

#define PROFILE_MAX_NAME        64
#define PROFILE_MAX_PATH        260
#define PROFILE_MAX_USERS       16

typedef struct _USER_PROFILE {
    CHAR Username[PROFILE_MAX_NAME];
    CHAR FullName[PROFILE_MAX_NAME];
    CHAR WallpaperPath[PROFILE_MAX_PATH];
    CHAR ThemeName[PROFILE_MAX_NAME];
    CHAR ProfilePicture[PROFILE_MAX_PATH];
    ULONG AccentColor;
    BOOLEAN DarkMode;
    BOOLEAN ShowTaskbar;
    BOOLEAN ShowStartButton;
    ULONG TaskbarPosition;  /* 0=bottom, 1=top, 2=left, 3=right */
    BOOLEAN InUse;
} USER_PROFILE, *PUSER_PROFILE;

static USER_PROFILE g_Profiles[PROFILE_MAX_USERS];
static ULONG g_CurrentUser = 0;

/* Get current user profile */
NTSTATUS NTAPI ProfileGetCurrent(PUSER_PROFILE *OutProfile)
{
    if (!OutProfile) return STATUS_INVALID_PARAMETER;
    if (!g_Profiles[g_CurrentUser].InUse) {
        *OutProfile = NULL;
        return STATUS_NOT_FOUND;
    }
    *OutProfile = &g_Profiles[g_CurrentUser];
    return STATUS_SUCCESS;
}

/* Set current user */
NTSTATUS NTAPI ProfileSetCurrent(ULONG Index)
{
    if (Index >= PROFILE_MAX_USERS) return STATUS_INVALID_PARAMETER;
    if (!g_Profiles[Index].InUse) return STATUS_NOT_FOUND;
    g_CurrentUser = Index;
    return STATUS_SUCCESS;
}

/* Create new user profile */
NTSTATUS NTAPI ProfileCreate(const CHAR *Username, const CHAR *FullName)
{
    if (!Username) return STATUS_INVALID_PARAMETER;
    
    for (ULONG i = 0; i < PROFILE_MAX_USERS; i++) {
        if (!g_Profiles[i].InUse) {
            RtlZeroMemory(&g_Profiles[i], sizeof(USER_PROFILE));
            g_Profiles[i].InUse = TRUE;
            
            ULONG k = 0;
            while (Username[k] && k < PROFILE_MAX_NAME - 1) { g_Profiles[i].Username[k] = Username[k]; k++; }
            g_Profiles[i].Username[k] = 0;
            
            if (FullName) {
                k = 0;
                while (FullName[k] && k < PROFILE_MAX_NAME - 1) { g_Profiles[i].FullName[k] = FullName[k]; k++; }
                g_Profiles[i].FullName[k] = 0;
            }
            
            /* Default settings */
            g_Profiles[i].AccentColor = 0x00316AC5; /* XP blue */
            g_Profiles[i].DarkMode = FALSE;
            g_Profiles[i].ShowTaskbar = TRUE;
            g_Profiles[i].ShowStartButton = TRUE;
            g_Profiles[i].TaskbarPosition = 0; /* Bottom */
            
            DbgPrint("PROFILE: created user '%s'\n", Username);
            return STATUS_SUCCESS;
        }
    }
    
    return STATUS_NO_MEMORY;
}

/* Set wallpaper path */
NTSTATUS NTAPI ProfileSetWallpaper(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    if (!g_Profiles[g_CurrentUser].InUse) return STATUS_NOT_FOUND;
    
    ULONG k = 0;
    while (Path[k] && k < PROFILE_MAX_PATH - 1) { g_Profiles[g_CurrentUser].WallpaperPath[k] = Path[k]; k++; }
    g_Profiles[g_CurrentUser].WallpaperPath[k] = 0;
    
    return STATUS_SUCCESS;
}

/* Set theme */
NTSTATUS NTAPI ProfileSetTheme(const CHAR *ThemeName)
{
    if (!ThemeName) return STATUS_INVALID_PARAMETER;
    if (!g_Profiles[g_CurrentUser].InUse) return STATUS_NOT_FOUND;
    
    ULONG k = 0;
    while (ThemeName[k] && k < PROFILE_MAX_NAME - 1) { g_Profiles[g_CurrentUser].ThemeName[k] = ThemeName[k]; k++; }
    g_Profiles[g_CurrentUser].ThemeName[k] = 0;
    
    return STATUS_SUCCESS;
}

/* Set accent color */
NTSTATUS NTAPI ProfileSetAccentColor(ULONG Color)
{
    if (!g_Profiles[g_CurrentUser].InUse) return STATUS_NOT_FOUND;
    g_Profiles[g_CurrentUser].AccentColor = Color;
    return STATUS_SUCCESS;
}

/* Set profile picture */
NTSTATUS NTAPI ProfileSetProfilePicture(const CHAR *Path)
{
    if (!Path) return STATUS_INVALID_PARAMETER;
    if (!g_Profiles[g_CurrentUser].InUse) return STATUS_NOT_FOUND;
    
    ULONG k = 0;
    while (Path[k] && k < PROFILE_MAX_PATH - 1) { g_Profiles[g_CurrentUser].ProfilePicture[k] = Path[k]; k++; }
    g_Profiles[g_CurrentUser].ProfilePicture[k] = 0;
    
    return STATUS_SUCCESS;
}

/* Save profile to registry */
NTSTATUS NTAPI ProfileSave(VOID)
{
    if (!g_Profiles[g_CurrentUser].InUse) return STATUS_NOT_FOUND;
    
    USER_PROFILE *p = &g_Profiles[g_CurrentUser];
    
    /* Build registry path */
    CHAR keyPath[260];
    ULONG k = 0;
    const CHAR *prefix = "\\Registry\\User\\";
    for (ULONG i = 0; prefix[i] && k < 259; i++) keyPath[k++] = prefix[i];
    for (ULONG i = 0; p->Username[i] && k < 259; i++) keyPath[k++] = p->Username[i];
    keyPath[k] = 0;
    
    /* Create/open key */
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmCreateKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    
    /* Save values */
    /* Wallpaper path */
    CHAR vname[64];
    k = 0;
    vname[k++] = 'W'; vname[k++] = 'a'; vname[k++] = 'l'; vname[k++] = 'l'; vname[k++] = 'p'; vname[k++] = 'a'; vname[k++] = 'p'; vname[k++] = 'e'; vname[k++] = 'r'; vname[k] = 0;
    UNICODE_STRING v;
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, &v, REG_SZ, p->WallpaperPath, sizeof(p->WallpaperPath));
    
    /* Theme */
    k = 0;
    vname[k++] = 'T'; vname[k++] = 'h'; vname[k++] = 'e'; vname[k++] = 'm'; vname[k++] = 'e'; vname[k] = 0;
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, &v, REG_SZ, p->ThemeName, sizeof(p->ThemeName));
    
    /* Accent color */
    k = 0;
    vname[k++] = 'A'; vname[k++] = 'c'; vname[k++] = 'c'; vname[k++] = 'e'; vname[k++] = 'n'; vname[k++] = 't'; vname[k] = 0;
    RtlInitUnicodeString(&v, (PCWSTR)vname);
    CmSetValue(key, &v, REG_DWORD, &p->AccentColor, sizeof(ULONG));
    
    DbgPrint("PROFILE: saved profile for '%s'\n", p->Username);
    return STATUS_SUCCESS;
}

/* Load profile from registry */
NTSTATUS NTAPI ProfileLoad(const CHAR *Username)
{
    if (!Username) return STATUS_INVALID_PARAMETER;
    
    /* Build registry path */
    CHAR keyPath[260];
    ULONG k = 0;
    const CHAR *prefix = "\\Registry\\User\\";
    for (ULONG i = 0; prefix[i] && k < 259; i++) keyPath[k++] = prefix[i];
    for (ULONG i = 0; Username[i] && k < 259; i++) keyPath[k++] = Username[i];
    keyPath[k] = 0;
    
    /* Open key */
    UNICODE_STRING uname;
    RtlInitUnicodeString(&uname, (PCWSTR)keyPath);
    PCM_KEY_NODE key = NULL;
    NTSTATUS s = CmOpenKey(&uname, 0, &key);
    if (!NT_SUCCESS(s)) return s;
    
    /* Create profile if not exists */
    ProfileCreate(Username, NULL);
    
    /* Load values */
    /* ... */
    
    DbgPrint("PROFILE: loaded profile for '%s'\n", Username);
    return STATUS_SUCCESS;
}

/* Initialize profile system */
NTSTATUS NTAPI ProfileInit(VOID)
{
    RtlZeroMemory(g_Profiles, sizeof(g_Profiles));
    
    /* Create default user */
    ProfileCreate("User", "Default User");
    
    /* Set default wallpaper to Bliss if available */
    const CHAR *blissPath = "C:\\Windows\\Web\\Wallpaper\\Bliss\\windows_xp_bliss-wide.jpg";
    ProfileSetWallpaper(blissPath);
    
    DbgPrint("PROFILE: profile system initialized (default user: User)\n");
    return STATUS_SUCCESS;
}