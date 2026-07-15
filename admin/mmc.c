/*
 * MinNT - admin/mmc.c
 * Microsoft Management Console (MMC) framework.
 *
 * MMC is a "snap-in host" that provides a unified UI for management
 * tools (Device Manager, Event Viewer, Services, etc.). Each snap-in
 * registers itself with MMC, declaring:
 *   - Snap-in CLSID
 *   - Display name
 *   - Root node type
 *   - Extendable (can host child snap-ins)
 *
 * The MMC host provides:
 *   - Snap-in registration database
 *   - Tree view of registered snap-ins
 *   - Scope pane + result pane layout
 *   - Action menu for selected nodes
 *
 * For MinNT, the MMC framework provides:
 *   - Snap-in registry (similar to COM registration)
 *   - A unified "Computer Management" snap-in that hosts child tools
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/cm.h>

#define MAX_SNAPINS 16
#define MAX_CHILDREN_PER_SNAPIN 8

typedef ULONG MMCNODEID;

typedef struct _MMC_SNAPIN {
    MMCNODEID Id;
    MMCNODEID Parent;          /* 0 = root */
    GUID     Clsid;            /* snap-in's COM CLSID */
    WCHAR    DisplayName[64];
    WCHAR    Description[128];
    ULONG    NodeType;          /* 0=root, 1=scope, 2=leaf */
    BOOLEAN  Extendable;       /* can have child snap-ins */
    BOOLEAN  InUse;
    MMCNODEID Children[MAX_CHILDREN_PER_SNAPIN];
    ULONG    ChildCount;
} MMC_SNAPIN, *PMMC_SNAPIN;

static MMC_SNAPIN g_Snapins[MAX_SNAPINS];
static KSPIN_LOCK g_MmcLock;
static MMCNODEID g_MmcRootId = 0;

static MMCNODEID MmcAllocItem(VOID)
{
    MMCNODEID i;
    for (i = 0; i < MAX_SNAPINS; i++) {
        if (!g_Snapins[i].InUse) {
            RtlZeroMemory(&g_Snapins[i], sizeof(MMC_SNAPIN));
            g_Snapins[i].Id = i;
            g_Snapins[i].InUse = TRUE;
            return i;
        }
    }
    return (MMCNODEID)-1;
}

NTSTATUS NTAPI MmcInit(VOID)
{
    RtlZeroMemory(g_Snapins, sizeof(g_Snapins));
    KeInitializeSpinLock(&g_MmcLock);

    /* Build the standard "Computer Management" tree */
    g_MmcRootId = MmcAllocItem();
    {
        MMCNODEID root = g_MmcRootId;
        MMCNODEID compMgmt = MmcAllocItem();
        GUID cmGuid = { 0xB708B3A8, 0x6D62, 0x4D94, { 0xBE, 0x92, 0x1F, 0x55, 0xA4, 0x1B, 0xC7, 0x95 }};

        g_Snapins[root].NodeType = 0;
        RtlCopyMemory(g_Snapins[root].DisplayName, L"Console Root", 24);
        g_Snapins[root].DisplayName[23] = 0;

        g_Snapins[compMgmt].Parent = root;
        g_Snapins[compMgmt].Clsid = cmGuid;
        g_Snapins[compMgmt].NodeType = 0;
        g_Snapins[compMgmt].Extendable = TRUE;
        RtlCopyMemory(g_Snapins[compMgmt].DisplayName, L"Computer Management", 36);
        g_Snapins[compMgmt].DisplayName[35] = 0;
        RtlCopyMemory(g_Snapins[compMgmt].Description,
                       L"Manage devices, events, services, and storage", 92);
        g_Snapins[compMgmt].Description[91] = 0;

        g_Snapins[root].Children[0] = compMgmt;
        g_Snapins[root].ChildCount = 1;

        /* Add child snap-ins under Computer Management */
        {
            MMCNODEID dm = MmcAllocItem();
            GUID dmGuid = { 0x74246BFC, 0x095C, 0x11D0, { 0xB8, 0xCD, 0x00, 0xAA, 0x00, 0xC0, 0x4F, 0xD8 }};
            g_Snapins[dm].Parent = compMgmt;
            g_Snapins[dm].Clsid = dmGuid;
            g_Snapins[dm].NodeType = 1;
            RtlCopyMemory(g_Snapins[dm].DisplayName, L"Device Manager", 28);
            g_Snapins[dm].DisplayName[27] = 0;
            RtlCopyMemory(g_Snapins[dm].Description, L"View and configure hardware", 56);
            g_Snapins[dm].Description[55] = 0;

            MMCNODEID ev = MmcAllocItem();
            GUID evGuid = { 0xB06C6F62, 0xCB53, 0x11D0, { 0xB8, 0xCD, 0x00, 0xAA, 0x00, 0xC0, 0x4F, 0xD8 }};
            g_Snapins[ev].Parent = compMgmt;
            g_Snapins[ev].Clsid = evGuid;
            g_Snapins[ev].NodeType = 1;
            RtlCopyMemory(g_Snapins[ev].DisplayName, L"Event Viewer", 24);
            g_Snapins[ev].DisplayName[23] = 0;
            RtlCopyMemory(g_Snapins[ev].Description, L"View system and application logs", 60);
            g_Snapins[ev].Description[59] = 0;

            MMCNODEID svc = MmcAllocItem();
            GUID svcGuid = { 0x2D8D6F4D, 0xC2C1, 0x4D9A, { 0xB8, 0xC1, 0x4F, 0x3F, 0xC1, 0x8E, 0x5D, 0x77 }};
            g_Snapins[svc].Parent = compMgmt;
            g_Snapins[svc].Clsid = svcGuid;
            g_Snapins[svc].NodeType = 1;
            RtlCopyMemory(g_Snapins[svc].DisplayName, L"Services", 16);
            g_Snapins[svc].DisplayName[15] = 0;
            RtlCopyMemory(g_Snapins[svc].Description, L"Manage background services", 52);
            g_Snapins[svc].Description[51] = 0;

            MMCNODEID storage = MmcAllocItem();
            GUID stGuid = { 0x4C8A6F8C, 0xC2C1, 0x4D9A, { 0xB8, 0xC1, 0x4F, 0x3F, 0xC1, 0x8E, 0x5D, 0x88 }};
            g_Snapins[storage].Parent = compMgmt;
            g_Snapins[storage].Clsid = stGuid;
            g_Snapins[storage].NodeType = 1;
            RtlCopyMemory(g_Snapins[storage].DisplayName, L"Storage", 14);
            g_Snapins[storage].DisplayName[13] = 0;
            RtlCopyMemory(g_Snapins[storage].Description, L"Manage disks and volumes", 44);
            g_Snapins[storage].Description[43] = 0;

            MMCNODEID net = MmcAllocItem();
            GUID netGuid = { 0x4C8A6F8C, 0xC2C1, 0x4D9A, { 0xB8, 0xC1, 0x4F, 0x3F, 0xC1, 0x8E, 0x5D, 0x99 }};
            g_Snapins[net].Parent = compMgmt;
            g_Snapins[net].Clsid = netGuid;
            g_Snapins[net].NodeType = 1;
            RtlCopyMemory(g_Snapins[net].DisplayName, L"Network", 14);
            g_Snapins[net].DisplayName[13] = 0;
            RtlCopyMemory(g_Snapins[net].Description, L"Network adapters and connections", 60);
            g_Snapins[net].Description[59] = 0;

            MMCNODEID users = MmcAllocItem();
            GUID usGuid = { 0x4C8A6F8C, 0xC2C1, 0x4D9A, { 0xB8, 0xC1, 0x4F, 0x3F, 0xC1, 0x8E, 0x5D, 0xAA }};
            g_Snapins[users].Parent = compMgmt;
            g_Snapins[users].Clsid = usGuid;
            g_Snapins[users].NodeType = 1;
            RtlCopyMemory(g_Snapins[users].DisplayName, L"Local Users and Groups", 42);
            g_Snapins[users].DisplayName[41] = 0;
            RtlCopyMemory(g_Snapins[users].Description, L"Manage user accounts", 36);
            g_Snapins[users].Description[35] = 0;

            g_Snapins[compMgmt].Children[0] = dm;
            g_Snapins[compMgmt].Children[1] = ev;
            g_Snapins[compMgmt].Children[2] = svc;
            g_Snapins[compMgmt].Children[3] = storage;
            g_Snapins[compMgmt].Children[4] = net;
            g_Snapins[compMgmt].Children[5] = users;
            g_Snapins[compMgmt].ChildCount = 6;
        }
    }

    DbgPrint("MMC: framework initialized with %d snap-ins\n", 7);
    return STATUS_SUCCESS;
}

/* Register a new snap-in under a parent. */
MMCNODEID NTAPI MmcRegisterSnapIn(MMCNODEID Parent, GUID *Clsid,
                                    const WCHAR *DisplayName,
                                    const WCHAR *Description,
                                    ULONG NodeType, BOOLEAN Extendable)
{
    KIRQL irql;
    MMCNODEID id;

    KeAcquireSpinLock(&g_MmcLock, &irql);
    id = MmcAllocItem();
    if (id == (MMCNODEID)-1) {
        KeReleaseSpinLock(&g_MmcLock, &irql);
        return (MMCNODEID)-1;
    }
    {
        ULONG i = 0;
        if (Clsid) for (i = 0; i < 16; i++) ((UCHAR *)&g_Snapins[id].Clsid)[i] = ((UCHAR *)Clsid)[i];
    }
    if (DisplayName) {
        ULONG j = 0;
        while (DisplayName[j] && j < 63) g_Snapins[id].DisplayName[j] = DisplayName[j], j++;
        g_Snapins[id].DisplayName[j] = 0;
    }
    if (Description) {
        ULONG j = 0;
        while (Description[j] && j < 127) g_Snapins[id].Description[j] = Description[j], j++;
        g_Snapins[id].Description[j] = 0;
    }
    g_Snapins[id].NodeType = NodeType;
    g_Snapins[id].Extendable = Extendable;
    g_Snapins[id].Parent = Parent;

    if (Parent != (MMCNODEID)-1 && g_Snapins[Parent].ChildCount < MAX_CHILDREN_PER_SNAPIN) {
        g_Snapins[Parent].Children[g_Snapins[Parent].ChildCount++] = id;
    }
    KeReleaseSpinLock(&g_MmcLock, &irql);

    DbgPrint("MMC: registered '%ws'\n", DisplayName ? DisplayName : L"(unnamed)");
    return id;
}

/* Enumerate children of a snap-in. */
ULONG NTAPI MmcEnumChildren(MMCNODEID Parent, ULONG MaxCount, MMCNODEID *pIds)
{
    ULONG i, n = 0;
    KIRQL irql;
    if (Parent >= MAX_SNAPINS || !g_Snapins[Parent].InUse) return 0;

    KeAcquireSpinLock(&g_MmcLock, &irql);
    for (i = 0; i < g_Snapins[Parent].ChildCount && n < MaxCount; i++) {
        MMCNODEID cid = g_Snapins[Parent].Children[i];
        if (cid < MAX_SNAPINS && g_Snapins[cid].InUse) {
            pIds[n++] = cid;
        }
    }
    KeReleaseSpinLock(&g_MmcLock, &irql);
    return n;
}

/* Get a snap-in's display name. */
NTSTATUS NTAPI MmcGetDisplayName(MMCNODEID Id, PWCHAR Buffer, ULONG BufferLen)
{
    if (Id >= MAX_SNAPINS || !g_Snapins[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    {
        ULONG j = 0;
        while (g_Snapins[Id].DisplayName[j] && j < BufferLen - 1) {
            Buffer[j] = g_Snapins[Id].DisplayName[j];
            j++;
        }
        Buffer[j] = 0;
    }
    return STATUS_SUCCESS;
}

/* Get the root ID. */
MMCNODEID NTAPI MmcGetRootId(VOID)
{
    return g_MmcRootId;
}

/* Check if a node has children. */
BOOLEAN NTAPI MmcHasChildren(MMCNODEID Id)
{
    BOOLEAN result = FALSE;
    KIRQL irql;
    if (Id >= MAX_SNAPINS || !g_Snapins[Id].InUse) return FALSE;
    KeAcquireSpinLock(&g_MmcLock, &irql);
    result = (g_Snapins[Id].ChildCount > 0);
    KeReleaseSpinLock(&g_MmcLock, &irql);
    return result;
}

/* Get a snap-in's CLSID. */
NTSTATUS NTAPI MmcGetClsid(MMCNODEID Id, GUID *OutClsid)
{
    KIRQL irql;
    if (Id >= MAX_SNAPINS || !g_Snapins[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutClsid) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_MmcLock, &irql);
    {
        ULONG i;
        for (i = 0; i < 16; i++) ((UCHAR *)OutClsid)[i] = ((UCHAR *)&g_Snapins[Id].Clsid)[i];
    }
    KeReleaseSpinLock(&g_MmcLock, &irql);
    return STATUS_SUCCESS;
}
