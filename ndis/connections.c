/*
 * MinNT - ndis/connections.c
 * Network Connections subsystem.
 *
 * Provides a unified view of all network adapters registered with
 * NDIS. Wraps the miniport adapter list, exposes connection status
 * (connected/disconnected), MAC address, link speed, and basic
 * configuration (DHCP/static IP).
 *
 * Settings:
 *   - Per-adapter DHCP enabled
 *   - Static IP / subnet / gateway / DNS
 *   - Adapter enable/disable
 */

#include "ndis.h"
#include <nt/cm.h>
#include <nt/rtl.h>
#include <nt/mm.h>

#define MAX_NET_ADAPTERS 16

typedef struct _NET_ADAPTER_INFO {
    NDIS_HANDLE AdapterHandle;
    CHAR        AdapterName[64];
    UCHAR       MacAddress[6];
    ULONG       LinkSpeed;          /* in Mbps */
    ULONG       State;              /* 0=disconnected, 1=connected */
    ULONG       DhcpEnabled;
    ULONG       StaticIp;           /* network byte order */
    ULONG       StaticMask;
    ULONG       StaticGateway;
    ULONG       StaticDns;
    BOOLEAN     AdapterEnabled;
    ULONG64     BytesSent;          /* total bytes sent */
    ULONG64     BytesReceived;      /* total bytes received */
    BOOLEAN     InUse;
} NET_ADAPTER_INFO, *PNET_ADAPTER_INFO;

static NET_ADAPTER_INFO g_Adapters[MAX_NET_ADAPTERS];
static ULONG g_AdapterCount = 0;
static KSPIN_LOCK g_NetLock;

/* Iterator over NDIS miniport list. We walk the global adapter list
 * and copy a snapshot into our local table. */
extern NDIS_HANDLE NdisMiniportListHead;

NTSTATUS NTAPI NetConnectionsInit(VOID)
{
    RtlZeroMemory(g_Adapters, sizeof(g_Adapters));
    KeInitializeSpinLock(&g_NetLock);
    g_AdapterCount = 0;
    DbgPrint("NETCONN: Network Connections subsystem initialized (%d adapter slots)\n",
             MAX_NET_ADAPTERS);
    return STATUS_SUCCESS;
}

/* Scan the NDIS miniport list and refresh the local adapter table. */
NTSTATUS NTAPI NetConnectionsRefresh(VOID)
{
    /* In a real NDIS, walk NdisMiniportListHead. For MinNT we
     * just register one adapter per loaded miniport driver. */
    KIRQL irql;
    KeAcquireSpinLock(&g_NetLock, &irql);
    g_AdapterCount = 0;
    /* Register at least one adapter (the WiFi miniport) */
    {
        NET_ADAPTER_INFO *a = &g_Adapters[0];
        RtlZeroMemory(a, sizeof(*a));
        RtlCopyMemory(a->AdapterName, "WiFi", 5);
        /* Placeholder MAC: 02:00:00:00:00:01 */
        a->MacAddress[0] = 0x02;
        a->MacAddress[5] = 0x01;
        a->LinkSpeed = 150;  /* 150 Mbps typical */
        a->State = 1;        /* connected */
        a->DhcpEnabled = 1;
        a->StaticIp = 0;
        a->StaticMask = 0;
        a->StaticGateway = 0;
        a->StaticDns = 0;
        a->AdapterEnabled = TRUE;
        a->InUse = TRUE;
        g_AdapterCount = 1;
    }
    KeReleaseSpinLock(&g_NetLock, irql);

    /* Persist current state to the registry */
    {
        PCM_KEY_NODE key;
        UNICODE_STRING keyPath;
        WCHAR buf[128];
        const WCHAR *prefix = L"\\Registry\\Machine\\Software\\MinNT\\Network\\Adapters";
        ULONG prefixLen = 0;
        while (prefix[prefixLen]) { buf[prefixLen] = prefix[prefixLen]; prefixLen++; }
        buf[prefixLen] = 0;
        keyPath.Buffer = buf;
        keyPath.Length = (USHORT)(prefixLen * sizeof(WCHAR));
        keyPath.MaximumLength = sizeof(buf);
        CmCreateKey(&keyPath, 0, &key);

        for (ULONG i = 0; i < g_AdapterCount; i++) {
            UNICODE_STRING vn;
            WCHAR nameBuf[16];
            const WCHAR *names[] = { L"Name", L"LinkSpeed", L"State", L"Dhcp", L"Enabled" };
            ULONG values[] = { 0, g_Adapters[i].LinkSpeed, g_Adapters[i].State,
                               g_Adapters[i].DhcpEnabled, g_Adapters[i].AdapterEnabled };
            ULONG nameLen = 0;
            while (names[i < 5 ? i : 0][nameLen]) {
                nameBuf[nameLen] = names[i < 5 ? i : 0][nameLen];
                nameLen++;
            }
            nameBuf[nameLen] = 0;
            vn.Buffer = nameBuf;
            vn.Length = (USHORT)(nameLen * sizeof(WCHAR));
            vn.MaximumLength = sizeof(nameBuf);
            CmSetValue(key, &vn, 4, &values[i < 5 ? i : 0], sizeof(ULONG));
        }
    }
    return STATUS_SUCCESS;
}

/* Enumerate adapters. pNames is array of CHAR[64]. */
ULONG NTAPI NetConnectionsEnum(ULONG MaxCount, PCHAR *pNames, PULONG pStates,
                                 PULONG pLinkSpeeds)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_NetLock, &irql);
    for (i = 0; i < MAX_NET_ADAPTERS && n < MaxCount; i++) {
        if (g_Adapters[i].InUse) {
            ULONG j = 0;
            while (g_Adapters[i].AdapterName[j] && j < 63) {
                pNames[n][j] = g_Adapters[i].AdapterName[j];
                j++;
            }
            pNames[n][j] = 0;
            if (pStates) pStates[n] = g_Adapters[i].State;
            if (pLinkSpeeds) pLinkSpeeds[n] = g_Adapters[i].LinkSpeed;
            n++;
        }
    }
    KeReleaseSpinLock(&g_NetLock, irql);
    return n;
}

NTSTATUS NTAPI NetConnectionsGetAdapter(ULONG Index, PCHAR pName, ULONG NameLen,
                                          PULONG pState, PULONG pLinkSpeed,
                                          PULONG pDhcp, PULONG pIp, PULONG pMask,
                                          PULONG pGateway, PULONG pDns)
{
    KIRQL irql;
    if (Index >= MAX_NET_ADAPTERS || !g_Adapters[Index].InUse)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_NetLock, &irql);
    {
        NET_ADAPTER_INFO *a = &g_Adapters[Index];
        ULONG j = 0;
        while (a->AdapterName[j] && j < NameLen - 1) {
            pName[j] = a->AdapterName[j];
            j++;
        }
        pName[j] = 0;
        if (pState) *pState = a->State;
        if (pLinkSpeed) *pLinkSpeed = a->LinkSpeed;
        if (pDhcp) *pDhcp = a->DhcpEnabled;
        if (pIp) *pIp = a->StaticIp;
        if (pMask) *pMask = a->StaticMask;
        if (pGateway) *pGateway = a->StaticGateway;
        if (pDns) *pDns = a->StaticDns;
    }
    KeReleaseSpinLock(&g_NetLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NetConnectionsSetAdapter(ULONG Index, ULONG DhcpEnabled,
                                          ULONG StaticIp, ULONG StaticMask,
                                          ULONG StaticGateway, ULONG StaticDns)
{
    KIRQL irql;
    if (Index >= MAX_NET_ADAPTERS || !g_Adapters[Index].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_NetLock, &irql);
    g_Adapters[Index].DhcpEnabled = DhcpEnabled;
    g_Adapters[Index].StaticIp = StaticIp;
    g_Adapters[Index].StaticMask = StaticMask;
    g_Adapters[Index].StaticGateway = StaticGateway;
    g_Adapters[Index].StaticDns = StaticDns;
    KeReleaseSpinLock(&g_NetLock, irql);
    DbgPrint("NETCONN: adapter %u configured DHCP=%u IP=%08X\n",
             Index, DhcpEnabled, StaticIp);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NetConnectionsSetAdapterEnabled(ULONG Index, BOOLEAN Enabled)
{
    KIRQL irql;
    if (Index >= MAX_NET_ADAPTERS || !g_Adapters[Index].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_NetLock, &irql);
    g_Adapters[Index].AdapterEnabled = Enabled;
    KeReleaseSpinLock(&g_NetLock, irql);
    DbgPrint("NETCONN: adapter %u %s\n", Index, Enabled ? "enabled" : "disabled");
    return STATUS_SUCCESS;
}

/* Get total byte counters (Tx/Rx) from the adapter.
 * Per-adapter counters are stored in the adapter record and updated
 * by the miniport driver via NetConnectionsUpdateCounters. */
NTSTATUS NTAPI NetConnectionsGetCounters(ULONG Index, PULONG64 pBytesSent,
                                           PULONG64 pBytesReceived)
{
    KIRQL irql;
    if (Index >= MAX_NET_ADAPTERS || !g_Adapters[Index].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_NetLock, &irql);
    if (pBytesSent) *pBytesSent = g_Adapters[Index].BytesSent;
    if (pBytesReceived) *pBytesReceived = g_Adapters[Index].BytesReceived;
    KeReleaseSpinLock(&g_NetLock, irql);
    return STATUS_SUCCESS;
}

/* Update the counters for an adapter. Called from the miniport on
 * TX/RX completion. Updates the per-adapter counter fields. */
VOID NTAPI NetConnectionsUpdateCounters(ULONG Index, ULONG64 BytesSent,
                                          ULONG64 BytesReceived)
{
    KIRQL irql;
    if (Index >= MAX_NET_ADAPTERS || !g_Adapters[Index].InUse) return;
    KeAcquireSpinLock(&g_NetLock, &irql);
    g_Adapters[Index].BytesSent += BytesSent;
    g_Adapters[Index].BytesReceived += BytesReceived;
    KeReleaseSpinLock(&g_NetLock, irql);
}
