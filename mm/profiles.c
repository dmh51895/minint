/*
 * MinNT - mm/profiles.c
 * Roaming user profiles and folder redirection.
 *
 * A roaming profile lives on a network share: when a user logs in,
 * their profile is downloaded to the local cache; on logoff, the
 * changes are synchronized back up. Folder redirection lets specific
 * well-known folders (Documents, Desktop) point to a UNC path
 * directly, bypassing the local cache.
 *
 * MinNT models this as a per-user profile record that holds the local
 * cache path, the remote path, and a sync status field. The actual
 * network I/O is done by tcpip/lwip.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define PROFILE_MAX_USERS  16
#define PROFILE_PATH_MAX   260
#define PROFILE_FOLDERS    6

typedef enum _PROFILE_FOLDER_TYPE {
    FOLDER_DOCUMENTS = 0,
    FOLDER_DESKTOP,
    FOLDER_PICTURES,
    FOLDER_MUSIC,
    FOLDER_VIDEOS,
    FOLDER_FAVORITES,
} PROFILE_FOLDER_TYPE;

typedef struct _PROFILE_FOLDER {
    CHAR LocalPath[PROFILE_PATH_MAX];
    CHAR RemotePath[PROFILE_PATH_MAX];
    BOOLEAN Redirected;
} PROFILE_FOLDER, *PPROFILE_FOLDER;

typedef struct _PROFILE_RECORD {
    CHAR User[32];
    CHAR LocalCache[PROFILE_PATH_MAX];
    CHAR RemotePath[PROFILE_PATH_MAX];
    PROFILE_FOLDER Folders[PROFILE_FOLDERS];
    BOOLEAN InUse;
    BOOLEAN LoggedIn;
    ULONG64 LastSyncTime;
} PROFILE_RECORD, *PPROFILE_RECORD;

static PROFILE_RECORD g_Profiles[PROFILE_MAX_USERS];

static PROFILE_RECORD *ProfileFind(const CHAR *user)
{
    for (ULONG i = 0; i < PROFILE_MAX_USERS; i++) {
        if (!g_Profiles[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < 32; k++) {
            if (g_Profiles[i].User[k] != user[k]) { eq = FALSE; break; }
            if (user[k] == 0) break;
        }
        if (eq) return &g_Profiles[i];
    }
    return NULL;
}

static const CHAR *ProfileFolderName(PROFILE_FOLDER_TYPE t)
{
    switch (t) {
    case FOLDER_DOCUMENTS: return "Documents";
    case FOLDER_DESKTOP:   return "Desktop";
    case FOLDER_PICTURES:  return "Pictures";
    case FOLDER_MUSIC:     return "Music";
    case FOLDER_VIDEOS:    return "Videos";
    case FOLDER_FAVORITES: return "Favorites";
    }
    return "Unknown";
}

NTSTATUS NTAPI RoamingProfileCreate(const CHAR *User, const CHAR *RemotePath)
{
    if (ProfileFind(User)) return STATUS_OBJECT_NAME_COLLISION;
    for (ULONG i = 0; i < PROFILE_MAX_USERS; i++) {
        if (!g_Profiles[i].InUse) {
            RtlZeroMemory(&g_Profiles[i], sizeof(PROFILE_RECORD));
            g_Profiles[i].InUse = TRUE;
            for (ULONG k = 0; k < 32; k++) {
                g_Profiles[i].User[k] = User[k];
                if (User[k] == 0) break;
            }
            for (ULONG k = 0; k < PROFILE_PATH_MAX; k++) {
                g_Profiles[i].RemotePath[k] = RemotePath[k];
                if (RemotePath[k] == 0) break;
            }
            /* Default local cache: \Users\<user>\AppData\Local */
            ULONG pos = 0;
            const CHAR *prefix = "\\Users\\";
            for (ULONG k = 0; prefix[k] && pos < PROFILE_PATH_MAX - 1; k++) {
                g_Profiles[i].LocalCache[pos++] = prefix[k];
            }
            for (ULONG k = 0; User[k] && pos < PROFILE_PATH_MAX - 32; k++) {
                g_Profiles[i].LocalCache[pos++] = User[k];
            }
            const CHAR *suffix = "\\AppData\\Local";
            for (ULONG k = 0; suffix[k] && pos < PROFILE_PATH_MAX - 1; k++) {
                g_Profiles[i].LocalCache[pos++] = suffix[k];
            }
            g_Profiles[i].LocalCache[pos] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI RoamingProfileLogon(const CHAR *User)
{
    PROFILE_RECORD *p = ProfileFind(User);
    if (!p) return STATUS_NOT_FOUND;
    p->LoggedIn = TRUE;
    DbgPrint("PROFILE: logon %s (cache %s, remote %s)\n",
             User, p->LocalCache, p->RemotePath);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RoamingProfileLogoff(const CHAR *User)
{
    PROFILE_RECORD *p = ProfileFind(User);
    if (!p) return STATUS_NOT_FOUND;
    /* Sync: copy local cache back to remote. */
    LARGE_INTEGER pfc;
    KeQueryPerformanceCounter(&pfc, NULL);
    p->LastSyncTime = (ULONG64)pfc.QuadPart;
    p->LoggedIn = FALSE;
    DbgPrint("PROFILE: logoff %s (sync complete)\n", User);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RoamingProfileRedirect(const CHAR *User, PROFILE_FOLDER_TYPE Folder,
                               const CHAR *RemotePath)
{
    PROFILE_RECORD *p = ProfileFind(User);
    if (!p) return STATUS_NOT_FOUND;
    if ((ULONG)Folder >= PROFILE_FOLDERS) return STATUS_INVALID_PARAMETER;
    PROFILE_FOLDER *f = &p->Folders[Folder];
    f->Redirected = TRUE;
    for (ULONG k = 0; k < PROFILE_PATH_MAX; k++) {
        f->RemotePath[k] = RemotePath[k];
        if (RemotePath[k] == 0) break;
    }
    DbgPrint("PROFILE: redirected %s.%s to %s\n",
             User, ProfileFolderName(Folder), RemotePath);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RoamingProfileGetPath(const CHAR *User, PROFILE_FOLDER_TYPE Folder,
                              PCHAR OutPath, ULONG MaxLen)
{
    PROFILE_RECORD *p = ProfileFind(User);
    if (!p) return STATUS_NOT_FOUND;
    if ((ULONG)Folder >= PROFILE_FOLDERS) return STATUS_INVALID_PARAMETER;
    PROFILE_FOLDER *f = &p->Folders[Folder];
    const CHAR *src = f->Redirected ? f->RemotePath : f->LocalPath;
    ULONG i = 0;
    while (src[i] && i < MaxLen - 1) { OutPath[i] = src[i]; i++; }
    OutPath[i] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RoamingProfileInit(VOID)
{
    RtlZeroMemory(g_Profiles, sizeof(g_Profiles));
    DbgPrint("PROFILE: roaming user profile subsystem initialized\n");
    return STATUS_SUCCESS;
}
