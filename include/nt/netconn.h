/*
 * MinNT - include/nt/netconn.h
 * Network Connections subsystem.
 */

#ifndef _NETCONN_H_
#define _NETCONN_H_

NTSTATUS NTAPI NetConnectionsInit(VOID);
NTSTATUS NTAPI NetConnectionsRefresh(VOID);
ULONG    NTAPI NetConnectionsEnum(ULONG MaxCount, PCHAR *pNames,
                                    PULONG pStates, PULONG pLinkSpeeds);
NTSTATUS NTAPI NetConnectionsGetAdapter(ULONG Index, PCHAR pName, ULONG NameLen,
                                          PULONG pState, PULONG pLinkSpeed,
                                          PULONG pDhcp, PULONG pIp, PULONG pMask,
                                          PULONG pGateway, PULONG pDns);
NTSTATUS NTAPI NetConnectionsSetAdapter(ULONG Index, ULONG DhcpEnabled,
                                          ULONG StaticIp, ULONG StaticMask,
                                          ULONG StaticGateway, ULONG StaticDns);
NTSTATUS NTAPI NetConnectionsSetAdapterEnabled(ULONG Index, BOOLEAN Enabled);
NTSTATUS NTAPI NetConnectionsGetCounters(ULONG Index, PULONG64 pBytesSent,
                                          PULONG64 pBytesReceived);
VOID NTAPI NetConnectionsUpdateCounters(ULONG Index, ULONG64 BytesSent,
                                          ULONG64 BytesReceived);

#endif /* _NETCONN_H_ */
