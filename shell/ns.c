/*
 * MinNT - shell/ns.c
 * Shell namespace - the hierarchical view of system objects.
 *
 * The shell namespace provides a unified tree of named items
 * representing desktops, drives, folders, control panel items, etc.
 * Each namespace item has:
 *   - ID (GUID or numeric)
 *   - Display name (wide string)
 *   - Parent (root if NULL)
 *   - Children (list of items)
 *   - Path (constructed by walking to root)
 *
 * Used by Explorer to render the desktop, My Computer, Control Panel,
 * Recycle Bin, etc.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ps.h>

#define MAX_NAMESPACE_ITEMS 128
#define MAX_CHILDREN_PER_ITEM 16

typedef ULONG NSITEMID;

typedef struct _NS_ITEM {
    NSITEMID Id;
    NSITEMID Parent;
    WCHAR    DisplayName[128];
    WCHAR    FullPath[256];
    BOOLEAN  IsFolder;
    BOOLEAN  InUse;
    NSITEMID Children[MAX_CHILDREN_PER_ITEM];
    ULONG    ChildCount;
} NS_ITEM, *PNS_ITEM;

static NS_ITEM g_Namespace[MAX_NAMESPACE_ITEMS];
static KSPIN_LOCK g_NsLock;
static NSITEMID g_NsRootId = 0;

static NSITEMID NsAllocItem(VOID)
{
    NSITEMID i;
    for (i = 0; i < MAX_NAMESPACE_ITEMS; i++) {
        if (!g_Namespace[i].InUse) {
            RtlZeroMemory(&g_Namespace[i], sizeof(NS_ITEM));
            g_Namespace[i].Id = i;
            g_Namespace[i].InUse = TRUE;
            return i;
        }
    }
    return (NSITEMID)-1;
}

NTSTATUS NTAPI NsInit(VOID)
{
    RtlZeroMemory(g_Namespace, sizeof(g_Namespace));
    KeInitializeSpinLock(&g_NsLock);

    /* Build the standard namespace tree:
     *   Root
     *     Desktop
     *     My Computer
     *       C:\
     *     My Documents
     *     Recycle Bin
     *     Control Panel
     *     Programs
     */
    {
        NSITEMID root = NsAllocItem();
        g_NsRootId = root;
        {
            NSITEMID desktop = NsAllocItem();
            g_Namespace[desktop].Parent = root;
            RtlCopyMemory(g_Namespace[desktop].DisplayName, L"Desktop", 14);
            g_Namespace[desktop].IsFolder = TRUE;

            NSITEMID mycomp = NsAllocItem();
            g_Namespace[mycomp].Parent = root;
            RtlCopyMemory(g_Namespace[mycomp].DisplayName, L"My Computer", 22);
            g_Namespace[mycomp].IsFolder = TRUE;

            NSITEMID c_drive = NsAllocItem();
            g_Namespace[c_drive].Parent = mycomp;
            RtlCopyMemory(g_Namespace[c_drive].DisplayName, L"C:\\", 6);
            g_Namespace[c_drive].IsFolder = TRUE;

            NSITEMID docs = NsAllocItem();
            g_Namespace[docs].Parent = root;
            RtlCopyMemory(g_Namespace[docs].DisplayName, L"My Documents", 24);
            g_Namespace[docs].IsFolder = TRUE;

            NSITEMID recycle = NsAllocItem();
            g_Namespace[recycle].Parent = root;
            RtlCopyMemory(g_Namespace[recycle].DisplayName, L"Recycle Bin", 22);
            g_Namespace[recycle].IsFolder = TRUE;

            NSITEMID cpl = NsAllocItem();
            g_Namespace[cpl].Parent = root;
            RtlCopyMemory(g_Namespace[cpl].DisplayName, L"Control Panel", 26);
            g_Namespace[cpl].IsFolder = TRUE;

            NSITEMID progs = NsAllocItem();
            g_Namespace[progs].Parent = root;
            RtlCopyMemory(g_Namespace[progs].DisplayName, L"Programs", 16);
            g_Namespace[progs].IsFolder = TRUE;

            /* Attach children to parents */
            g_Namespace[root].Children[0] = desktop;
            g_Namespace[root].Children[1] = mycomp;
            g_Namespace[root].Children[2] = docs;
            g_Namespace[root].Children[3] = recycle;
            g_Namespace[root].Children[4] = cpl;
            g_Namespace[root].Children[5] = progs;
            g_Namespace[root].ChildCount = 6;

            g_Namespace[mycomp].Children[0] = c_drive;
            g_Namespace[mycomp].ChildCount = 1;
        }
    }
    DbgPrint("SHELL: namespace initialized (%d items)\n", MAX_NAMESPACE_ITEMS);
    return STATUS_SUCCESS;
}

/* Register a new item under a parent. Returns its ID. */
NSITEMID NTAPI NsRegisterItem(NSITEMID Parent, const WCHAR *DisplayName,
                                BOOLEAN IsFolder)
{
    KIRQL irql;
    NSITEMID id;

    KeAcquireSpinLock(&g_NsLock, &irql);
    id = NsAllocItem();
    if (id == (NSITEMID)-1) {
        KeReleaseSpinLock(&g_NsLock, &irql);
        return (NSITEMID)-1;
    }
    {
        ULONG i = 0;
        while (DisplayName[i] && i < 127) { g_Namespace[id].DisplayName[i] = DisplayName[i]; i++; }
        g_Namespace[id].DisplayName[i] = 0;
    }
    g_Namespace[id].Parent = Parent;
    g_Namespace[id].IsFolder = IsFolder;

    /* Attach to parent's children list */
    if (Parent != (NSITEMID)-1 && g_Namespace[Parent].ChildCount < MAX_CHILDREN_PER_ITEM) {
        g_Namespace[Parent].Children[g_Namespace[Parent].ChildCount++] = id;
    }
    KeReleaseSpinLock(&g_NsLock, &irql);

    DbgPrint("SHELL: registered '%ws' (id=%u, folder=%d)\n", DisplayName, id, IsFolder);
    return id;
}

/* Remove an item from the namespace. */
NTSTATUS NTAPI NsUnregisterItem(NSITEMID Id)
{
    KIRQL irql;
    NSITEMID parent;
    if (Id >= MAX_NAMESPACE_ITEMS || !g_Namespace[Id].InUse) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_NsLock, &irql);
    parent = g_Namespace[Id].Parent;
    if (parent != (NSITEMID)-1) {
        /* Remove from parent's children list */
        for (ULONG i = 0; i < g_Namespace[parent].ChildCount; i++) {
            if (g_Namespace[parent].Children[i] == Id) {
                for (ULONG j = i; j + 1 < g_Namespace[parent].ChildCount; j++) {
                    g_Namespace[parent].Children[j] = g_Namespace[parent].Children[j+1];
                }
                g_Namespace[parent].ChildCount--;
                break;
            }
        }
    }
    RtlZeroMemory(&g_Namespace[Id], sizeof(NS_ITEM));
    KeReleaseSpinLock(&g_NsLock, &irql);
    return STATUS_SUCCESS;
}

/* Build the full path of an item by walking up to root. */
NTSTATUS NTAPI NsGetFullPath(NSITEMID Id, PWCHAR Buffer, ULONG BufferLen)
{
    KIRQL irql;
    WCHAR components[16][128];
    ULONG depth = 0;
    NSITEMID cur = Id;

    if (Id >= MAX_NAMESPACE_ITEMS || !g_Namespace[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_NsLock, &irql);
    while (cur != (NSITEMID)-1 && depth < 16) {
        ULONG j = 0;
        while (g_Namespace[cur].DisplayName[j] && j < 127) {
            components[depth][j] = g_Namespace[cur].DisplayName[j];
            j++;
        }
        components[depth][j] = 0;
        depth++;
        cur = g_Namespace[cur].Parent;
    }
    KeReleaseSpinLock(&g_NsLock, &irql);

    /* Concatenate in reverse with backslashes */
    {
        ULONG pos = 0;
        LONG i;
        Buffer[pos++] = 0;
        for (i = depth - 1; i >= 0 && pos < BufferLen - 1; i--) {
            if (pos > 0) Buffer[pos++] = '\\';
            ULONG j = 0;
            while (components[i][j] && pos < BufferLen - 1) Buffer[pos++] = components[i][j++];
        }
        Buffer[pos] = 0;
    }
    return STATUS_SUCCESS;
}

/* Enumerate children of an item. */
ULONG NTAPI NsEnumChildren(NSITEMID Parent, ULONG MaxCount, NSITEMID *pIds,
                              PWCHAR *pNames, PBOOLEAN pIsFolder)
{
    KIRQL irql;
    ULONG n = 0;
    if (Parent >= MAX_NAMESPACE_ITEMS || !g_Namespace[Parent].InUse) return 0;

    KeAcquireSpinLock(&g_NsLock, &irql);
    {
        ULONG i;
        for (i = 0; i < g_Namespace[Parent].ChildCount && n < MaxCount; i++) {
            NSITEMID cid = g_Namespace[Parent].Children[i];
            if (cid < MAX_NAMESPACE_ITEMS && g_Namespace[cid].InUse) {
                pIds[n] = cid;
                if (pNames) {
                    ULONG j = 0;
                    while (g_Namespace[cid].DisplayName[j] && j < 127) {
                        ((WCHAR *)pNames[n])[j] = g_Namespace[cid].DisplayName[j];
                        j++;
                    }
                    ((WCHAR *)pNames[n])[j] = 0;
                }
                if (pIsFolder) pIsFolder[n] = g_Namespace[cid].IsFolder;
                n++;
            }
        }
    }
    KeReleaseSpinLock(&g_NsLock, &irql);
    return n;
}

/* Get display name of an item. */
NTSTATUS NTAPI NsGetDisplayName(NSITEMID Id, PWCHAR Buffer, ULONG BufferLen)
{
    if (Id >= MAX_NAMESPACE_ITEMS || !g_Namespace[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    {
        ULONG j = 0;
        while (g_Namespace[Id].DisplayName[j] && j < BufferLen - 1) {
            Buffer[j] = g_Namespace[Id].DisplayName[j];
            j++;
        }
        Buffer[j] = 0;
    }
    return STATUS_SUCCESS;
}

/* Get the root ID (used by Explorer to start tree walk). */
NSITEMID NTAPI NsGetRootId(VOID)
{
    return g_NsRootId;
}
