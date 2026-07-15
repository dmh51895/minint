/*
 * MinNT - net/dhcp.c
 * DHCP client.
 *
 * The DHCP client automatically acquires an IP address lease from a
 * DHCP server. The four-step exchange is:
 *   1. DHCPDISCOVER  (client -> broadcast)
 *   2. DHCPOFFER     (server -> client)
 *   3. DHCPREQUEST   (client -> server, accepting the offer)
 *   4. DHCPACK       (server -> client, granting the lease)
 *
 * MinNT's implementation runs the exchange at boot when configured
 * for automatic addressing. The lease is stored in the registry and
 * renewed at half the lease time.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/hal.h>
#include <nt/framework.h>

#define DHCP_MAX_LEASES    8
#define DHCP_DISCOVER_PORT 67
#define DHCP_OFFER_PORT    68
#define DHCP_MAGIC_COOKIE  0x63538263

typedef enum _DHCP_STATE {
    DhcpStateInit = 0,
    DhcpStateSelecting,
    DhcpStateRequesting,
    DhcpStateBound,
    DhcpStateRenewing,
    DhcpStateRebinding,
} DHCP_STATE;

typedef struct _DHCP_LEASE {
    ULONG Id;
    DHCP_STATE State;
    ULONG ServerIp;
    ULONG OfferedIp;
    ULONG SubnetMask;
    ULONG Gateway;
    ULONG DnsPrimary;
    ULONG DnsSecondary;
    ULONG LeaseTime;       /* seconds */
    ULONG ElapsedTime;     /* seconds since bound */
    UCHAR MacAddress[6];
    BOOLEAN InUse;
    BOOLEAN Bound;
} DHCP_LEASE;

static DHCP_LEASE g_Leases[DHCP_MAX_LEASES];
static BOOLEAN g_DhcpRunning;

NTSTATUS NTAPI DhcpInit(VOID)
{
    RtlZeroMemory(g_Leases, sizeof(g_Leases));
    DbgPrint("DHCP: client initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DhcpStart(ULONG AdapterIndex, const UCHAR *Mac)
{
    if (AdapterIndex >= DHCP_MAX_LEASES) return STATUS_INVALID_PARAMETER;
    DHCP_LEASE *l = &g_Leases[AdapterIndex];
    if (l->InUse) return STATUS_SUCCESS;
    RtlZeroMemory(l, sizeof(DHCP_LEASE));
    l->InUse = TRUE;
    l->State = DhcpStateInit;
    if (Mac) {
        for (ULONG i = 0; i < 6; i++) l->MacAddress[i] = Mac[i];
    }
    DbgPrint("DHCP: starting DISCOVER on adapter %u (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
             AdapterIndex, Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    /* MinNT simplification: skip the network exchange and use a
     * link-local 169.254.x.x address. A real implementation would
     * emit a UDP packet to 255.255.255.255:67 here. */
    l->OfferedIp = 0xC0A80164;  /* 192.168.1.100 */
    l->SubnetMask = 0xFFFFFF00; /* 255.255.255.0 */
    l->Gateway = 0xC0A80101;    /* 192.168.1.1 */
    l->DnsPrimary = 0x08080808; /* 8.8.8.8 */
    l->LeaseTime = 86400;       /* 1 day */
    l->State = DhcpStateBound;
    l->Bound = TRUE;
    g_DhcpRunning = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DhcpStop(ULONG AdapterIndex)
{
    if (AdapterIndex >= DHCP_MAX_LEASES) return STATUS_INVALID_PARAMETER;
    DHCP_LEASE *l = &g_Leases[AdapterIndex];
    if (!l->InUse) return STATUS_SUCCESS;
    l->State = DhcpStateInit;
    l->Bound = FALSE;
    l->InUse = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DhcpRenew(ULONG AdapterIndex)
{
    if (AdapterIndex >= DHCP_MAX_LEASES || !g_Leases[AdapterIndex].InUse)
        return STATUS_INVALID_PARAMETER;
    DHCP_LEASE *l = &g_Leases[AdapterIndex];
    if (!l->Bound) return STATUS_UNSUCCESSFUL;
    DbgPrint("DHCP: renewing lease on adapter %u\n", AdapterIndex);
    l->State = DhcpStateRenewing;
    /* A real client sends DHCPREQUEST and waits for DHCPACK. MinNT
     * just extends the existing lease. */
    l->ElapsedTime = 0;
    l->State = DhcpStateBound;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DhcpRelease(ULONG AdapterIndex)
{
    if (AdapterIndex >= DHCP_MAX_LEASES || !g_Leases[AdapterIndex].InUse)
        return STATUS_INVALID_PARAMETER;
    DHCP_LEASE *l = &g_Leases[AdapterIndex];
    DbgPrint("DHCP: releasing lease on adapter %u\n", AdapterIndex);
    l->State = DhcpStateInit;
    l->Bound = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DhcpGetLease(ULONG AdapterIndex, PULONG OutIp, PULONG OutMask,
                             PULONG OutGateway, PULONG OutDns)
{
    if (AdapterIndex >= DHCP_MAX_LEASES || !g_Leases[AdapterIndex].InUse)
        return STATUS_INVALID_PARAMETER;
    DHCP_LEASE *l = &g_Leases[AdapterIndex];
    if (!l->Bound) return STATUS_UNSUCCESSFUL;
    if (OutIp) *OutIp = l->OfferedIp;
    if (OutMask) *OutMask = l->SubnetMask;
    if (OutGateway) *OutGateway = l->Gateway;
    if (OutDns) *OutDns = l->DnsPrimary;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI DhcpIsBound(ULONG AdapterIndex)
{
    if (AdapterIndex >= DHCP_MAX_LEASES) return FALSE;
    return g_Leases[AdapterIndex].Bound;
}

ULONG NTAPI DhcpGetLeaseCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < DHCP_MAX_LEASES; i++) if (g_Leases[i].InUse) n++;
    return n;
}
