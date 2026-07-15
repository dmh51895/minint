/*
 * MinNT - net/firewall.c
 * Network firewall (packet filter).
 *
 * A packet filter inspects each inbound/outbound packet and decides
 * whether to allow, drop, or log it. Decisions are based on rules
 * (source/destination IP, port, protocol, direction).
 *
 * Rules are stored in a linked list. Each rule has an action
 * (Allow/Drop), a direction (In/Out/Both), and a set of match
 * criteria. The first matching rule wins.
 *
 * The filter hooks into the existing TCP/IP stack at the IP layer
 * (after IP reassembly, before TCP/UDP).
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define FW_MAX_RULES 128
#define FW_LOG_MAX   256

/* FW_ACTION and FW_DIRECTION are in framework.h. */

typedef struct _FW_RULE {
    ULONG Id;
    FW_ACTION Action;
    FW_DIRECTION Direction;
    UCHAR Protocol;       /* 0 = any, 6 = TCP, 17 = UDP, ... */
    ULONG SourceIp;
    ULONG SourceMask;
    USHORT SourcePort;    /* 0 = any */
    ULONG DestIp;
    ULONG DestMask;
    USHORT DestPort;      /* 0 = any */
    CHAR Name[64];
    BOOLEAN Enabled;
    BOOLEAN InUse;
} FW_RULE;

typedef struct _FW_LOG_ENTRY {
    ULONG RuleId;
    ULONG SourceIp;
    ULONG DestIp;
    USHORT DestPort;
    UCHAR Protocol;
    UCHAR Action;
    LARGE_INTEGER Timestamp;
} FW_LOG_ENTRY;

static FW_RULE g_Rules[FW_MAX_RULES];
static FW_LOG_ENTRY g_Log[FW_LOG_MAX];
static ULONG g_LogHead;
static ULONG g_LogCount;
static BOOLEAN g_FirewallEnabled;
static KSPIN_LOCK g_FwLock;

NTSTATUS NTAPI FwInit(VOID)
{
    KeInitializeSpinLock(&g_FwLock);
    g_FirewallEnabled = TRUE;
    g_LogHead = 0;
    g_LogCount = 0;
    /* Seed default rules: allow loopback, allow established, block telnet. */
    FwAddRule(FwActionAllow, FwDirBoth, 0, 0x7F000000, 0xFF000000, 0,
              0x7F000000, 0xFF000000, 0, "Allow loopback");
    FwAddRule(FwActionAllow, FwDirInbound, 6, 0, 0, 0,
              0, 0, 80, "Allow inbound HTTP");
    FwAddRule(FwActionDrop, FwDirBoth, 6, 0, 0, 0,
              0, 0, 23, "Block telnet");
    DbgPrint("FW: firewall initialized (default rules loaded)\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FwAddRule(FW_ACTION Action, FW_DIRECTION Direction,
                          UCHAR Protocol,
                          ULONG SourceIp, ULONG SourceMask, USHORT SourcePort,
                          ULONG DestIp, ULONG DestMask, USHORT DestPort,
                          const CHAR *Name)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_FwLock, &irql);
    for (ULONG i = 0; i < FW_MAX_RULES; i++) {
        if (!g_Rules[i].InUse) {
            RtlZeroMemory(&g_Rules[i], sizeof(FW_RULE));
            g_Rules[i].InUse = TRUE;
            g_Rules[i].Enabled = TRUE;
            g_Rules[i].Action = Action;
            g_Rules[i].Direction = Direction;
            g_Rules[i].Protocol = Protocol;
            g_Rules[i].SourceIp = SourceIp;
            g_Rules[i].SourceMask = SourceMask;
            g_Rules[i].SourcePort = SourcePort;
            g_Rules[i].DestIp = DestIp;
            g_Rules[i].DestMask = DestMask;
            g_Rules[i].DestPort = DestPort;
            if (Name) for (ULONG k = 0; k < 63 && Name[k]; k++) g_Rules[i].Name[k] = Name[k];
            KeReleaseSpinLock(&g_FwLock, irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_FwLock, irql);
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI FwRemoveRule(ULONG RuleId)
{
    if (RuleId >= FW_MAX_RULES || !g_Rules[RuleId].InUse) return STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_FwLock, &irql);
    RtlZeroMemory(&g_Rules[RuleId], sizeof(FW_RULE));
    KeReleaseSpinLock(&g_FwLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FwEnableRule(ULONG RuleId, BOOLEAN Enabled)
{
    if (RuleId >= FW_MAX_RULES || !g_Rules[RuleId].InUse) return STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_FwLock, &irql);
    g_Rules[RuleId].Enabled = Enabled ? TRUE : FALSE;
    KeReleaseSpinLock(&g_FwLock, irql);
    return STATUS_SUCCESS;
}

/* Match a packet against the rule list. Returns the action of the
 * first matching rule, or Allow (default-permissive) if no rule
 * matches. */
FW_ACTION NTAPI FwEvaluatePacket(UCHAR Protocol,
                                    ULONG SourceIp, USHORT SourcePort,
                                    ULONG DestIp, USHORT DestPort,
                                    FW_DIRECTION Direction)
{
    if (!g_FirewallEnabled) return FwActionAllow;
    KIRQL irql;
    KeAcquireSpinLock(&g_FwLock, &irql);
    FW_ACTION result = FwActionAllow;
    for (ULONG i = 0; i < FW_MAX_RULES; i++) {
        if (!g_Rules[i].InUse || !g_Rules[i].Enabled) continue;
        if (g_Rules[i].Direction != FwDirBoth && g_Rules[i].Direction != Direction) continue;
        if (g_Rules[i].Protocol != 0 && g_Rules[i].Protocol != Protocol) continue;
        if (g_Rules[i].SourcePort != 0 && g_Rules[i].SourcePort != SourcePort) continue;
        if (g_Rules[i].DestPort != 0 && g_Rules[i].DestPort != DestPort) continue;
        if (g_Rules[i].SourceMask && ((SourceIp & g_Rules[i].SourceMask) != g_Rules[i].SourceIp)) continue;
        if (g_Rules[i].DestMask && ((DestIp & g_Rules[i].DestMask) != g_Rules[i].DestIp)) continue;
        result = g_Rules[i].Action;
        break;
    }
    /* Log. */
    if (g_LogCount < FW_LOG_MAX) g_LogCount++;
    g_Log[g_LogHead].RuleId = 0;
    g_Log[g_LogHead].SourceIp = SourceIp;
    g_Log[g_LogHead].DestIp = DestIp;
    g_Log[g_LogHead].DestPort = DestPort;
    g_Log[g_LogHead].Protocol = Protocol;
    g_Log[g_LogHead].Action = (UCHAR)result;
    KeQuerySystemTime(&g_Log[g_LogHead].Timestamp);
    g_LogHead = (g_LogHead + 1) % FW_LOG_MAX;
    KeReleaseSpinLock(&g_FwLock, irql);
    return result;
}

NTSTATUS NTAPI FwSetEnabled(BOOLEAN Enabled)
{
    g_FirewallEnabled = Enabled ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI FwIsEnabled(VOID)
{
    return g_FirewallEnabled;
}

ULONG NTAPI FwEnumRules(PULONG OutIds, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < FW_MAX_RULES && n < MaxCount; i++) {
        if (g_Rules[i].InUse) OutIds[n++] = i;
    }
    return n;
}

NTSTATUS NTAPI FwGetRule(ULONG RuleId, PULONG OutAction, PULONG OutDirection,
                          PULONG OutProtocol, PULONG OutSourceIp, PULONG OutDestIp)
{
    if (RuleId >= FW_MAX_RULES || !g_Rules[RuleId].InUse) return STATUS_INVALID_PARAMETER;
    if (OutAction) *OutAction = (ULONG)g_Rules[RuleId].Action;
    if (OutDirection) *OutDirection = (ULONG)g_Rules[RuleId].Direction;
    if (OutProtocol) *OutProtocol = (ULONG)g_Rules[RuleId].Protocol;
    if (OutSourceIp) *OutSourceIp = g_Rules[RuleId].SourceIp;
    if (OutDestIp) *OutDestIp = g_Rules[RuleId].DestIp;
    return STATUS_SUCCESS;
}
