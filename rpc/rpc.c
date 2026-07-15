/*
 * MinNT - rpc/rpc.c
 * Remote Procedure Call (RPC) runtime.
 *
 * RPC in NT is built on top of LPC for local calls and adds a network
 * transport (named pipes, TCP) for remote calls. This file implements
 * the binding/runtime layer: endpoint registration, well-known endpoint
 * UUIDs, a port-pool of RPC listeners, marshalling of in/out parameters,
 * and a packet format compatible with the DCE/RPC NDR/CNF stub layout
 * (simplified, sufficient for local IPC and stub-based stubs).
 *
 * MinNT also exports RPC interfaces through the LPC ports we already have
 * (e.g. the se/scm.c service control manager listens on \RPC\svcctl).
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ob.h>
#include <nt/lpc.h>
#include <nt/ex.h>
#include <nt/ntdef.h>
#include <ndk/obfuncs.h>
#include <nt/framework.h>

#define RPC_MAX_ENDPOINTS       32
#define RPC_MAX_BINDINGS        64
#define RPC_MAX_CALL_DATA       4096
#define RPC_IF_UUID_SIZE        16

/* DCE/RPC packet types */
#define RPC_PKT_REQUEST         0
#define RPC_PKT_RESPONSE        2
#define RPC_PKT_BIND            11
#define RPC_PKT_BIND_ACK        12
#define RPC_PKT_FAULT           3

/* RPC interface UUID + version. The first 16 bytes are the UUID; the
 * next two ULONGs are the version major and minor numbers (in LE). */
typedef struct _RPC_UUID {
    ULONG Data1;
    USHORT Data2;
    USHORT Data3;
    UCHAR Data4[8];
} RPC_UUID, *PRPC_UUID;

typedef struct _RPC_ENDPOINT {
    CHAR Name[64];
    RPC_UUID Uuid;
    ULONG VersionMajor;
    ULONG VersionMinor;
    HANDLE LpcPort;          /* underlying LPC port */
    PVOID ServerRoutine;     /* function pointer invoked per request */
    ULONG Active;
    struct _RPC_ENDPOINT *Next;
} RPC_ENDPOINT, *PRPC_ENDPOINT;

typedef struct _RPC_BINDING {
    PRPC_ENDPOINT Endpoint;
    ULONG CallId;
    BOOLEAN InUse;
} RPC_BINDING, *PRPC_BINDING;

typedef struct _RPC_PACKET {
    UCHAR PacketType;
    UCHAR PacketFlags;
    UCHAR DataRepresentation;
    UCHAR Reserved;
    ULONG FragLength;
    ULONG AuthLength;
    ULONG CallId;
    UCHAR Payload[1];
} RPC_PACKET, *PRPC_PACKET;

static RPC_ENDPOINT *g_Endpoints = NULL;
static RPC_BINDING g_Bindings[RPC_MAX_BINDINGS];
static ULONG g_BindingCount = 0;

static ULONG RpcHashUuid(PRPC_UUID u)
{
    return u->Data1 ^ ((ULONG)u->Data2 << 16) ^ ((ULONG)u->Data3);
}

/* Find an endpoint by its fully-qualified name (e.g. \RPC\svcctl). */
static PRPC_ENDPOINT RpcFindEndpointByName(const CHAR *name)
{
    PRPC_ENDPOINT e = g_Endpoints;
    while (e) {
        BOOLEAN eq = TRUE;
        for (ULONG i = 0; i < 64; i++) {
            if (e->Name[i] != name[i]) { eq = FALSE; break; }
            if (name[i] == 0) break;
        }
        if (eq) return e;
        e = e->Next;
    }
    return NULL;
}

static PRPC_ENDPOINT RpcFindEndpointByUuid(PRPC_UUID uuid, ULONG vmajor, ULONG vminor)
{
    PRPC_ENDPOINT e = g_Endpoints;
    while (e) {
        if (RpcHashUuid(&e->Uuid) == RpcHashUuid(uuid) &&
            e->VersionMajor == vmajor && e->VersionMinor == vminor) {
            return e;
        }
        e = e->Next;
    }
    return NULL;
}

NTSTATUS NTAPI RpcServerRegisterInterface(const CHAR *EndpointName,
                                          PRPC_UUID Uuid,
                                          ULONG VersionMajor,
                                          ULONG VersionMinor,
                                          PVOID ServerRoutine)
{
    if (RpcFindEndpointByName(EndpointName)) return STATUS_OBJECT_NAME_COLLISION;
    if (RpcFindEndpointByUuid(Uuid, VersionMajor, VersionMinor)) return STATUS_OBJECT_NAME_COLLISION;

    PRPC_ENDPOINT e = (PRPC_ENDPOINT)ExAllocatePool(0, sizeof(RPC_ENDPOINT));
    if (!e) return STATUS_NO_MEMORY;
    RtlZeroMemory(e, sizeof(*e));
    for (ULONG i = 0; i < 64; i++) {
        e->Name[i] = EndpointName[i];
        if (EndpointName[i] == 0) break;
    }
    RtlCopyMemory(&e->Uuid, Uuid, sizeof(RPC_UUID));
    e->VersionMajor = VersionMajor;
    e->VersionMinor = VersionMinor;
    e->ServerRoutine = ServerRoutine;
    e->Active = 1;
    e->Next = g_Endpoints;
    g_Endpoints = e;

    /* Bind the underlying LPC port to the endpoint name. */
    UNICODE_STRING name;
    CHAR full[128];
    ULONG j = 0;
    while (EndpointName[j] && j < sizeof(full) - 1) full[j] = EndpointName[j], j++;
    full[j] = 0;
    RtlInitUnicodeString(&name, (PCWSTR)full);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    e->LpcPort = 0;
    NTSTATUS s = NtCreatePort(&e->LpcPort, &oa, 0, 256, 256);
    if (!NT_SUCCESS(s)) {
        DbgPrint("RPC: registered endpoint %s (LPC bind failed 0x%08x)\n",
                 EndpointName, s);
    } else {
        DbgPrint("RPC: registered endpoint %s with LPC port %u\n",
                 EndpointName, e->LpcPort);
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RpcBindingBind(PRPC_UUID Uuid, ULONG VersionMajor, ULONG VersionMinor,
                              PULONG OutBindingId)
{
    PRPC_ENDPOINT e = RpcFindEndpointByUuid(Uuid, VersionMajor, VersionMinor);
    if (!e) return STATUS_NOT_FOUND;
    for (ULONG i = 0; i < RPC_MAX_BINDINGS; i++) {
        if (!g_Bindings[i].InUse) {
            g_Bindings[i].InUse = TRUE;
            g_Bindings[i].Endpoint = e;
            g_Bindings[i].CallId = i + 1;
            if (OutBindingId) *OutBindingId = i;
            g_BindingCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI RpcBindingUnbind(ULONG BindingId)
{
    if (BindingId >= RPC_MAX_BINDINGS) return STATUS_INVALID_PARAMETER;
    if (!g_Bindings[BindingId].InUse) return STATUS_INVALID_HANDLE;
    g_Bindings[BindingId].InUse = FALSE;
    g_Bindings[BindingId].Endpoint = NULL;
    if (g_BindingCount) g_BindingCount--;
    return STATUS_SUCCESS;
}

/* Marshal a stub call's input buffer into a DCE/RPC request packet. */
static NTSTATUS RpcBuildRequest(UCHAR *OutBuf, ULONG *OutLen,
                                ULONG BindingId, ULONG ProcNum,
                                PVOID InData, ULONG InLen)
{
    if (*OutLen < sizeof(RPC_PACKET) + InLen) return STATUS_BUFFER_TOO_SMALL;
    PRPC_PACKET p = (PRPC_PACKET)OutBuf;
    p->PacketType = RPC_PKT_REQUEST;
    p->PacketFlags = 0;
    p->DataRepresentation = 0x10; /* little-endian, ASCII */
    p->Reserved = 0;
    p->FragLength = sizeof(RPC_PACKET) + InLen;
    p->AuthLength = 0;
    p->CallId = g_Bindings[BindingId].CallId;
    /* Inside the request payload we encode the procedure number followed
     * by the input buffer. */
    PCHAR payload = (PCHAR)p->Payload;
    PULONG proc = (PULONG)payload;
    *proc = ProcNum;
    if (InLen) RtlCopyMemory(payload + sizeof(ULONG), InData, InLen);
    *OutLen = p->FragLength;
    return STATUS_SUCCESS;
}

/* Send an RPC call through the LPC port of the bound endpoint. */
NTSTATUS NTAPI RpcCall(ULONG BindingId, ULONG ProcNum,
                       PVOID InData, ULONG InLen,
                       PVOID OutData, ULONG *OutLen,
                       ULONG *OutReturn)
{
    if (BindingId >= RPC_MAX_BINDINGS) return STATUS_INVALID_PARAMETER;
    if (!g_Bindings[BindingId].InUse) return STATUS_INVALID_HANDLE;
    PRPC_ENDPOINT e = g_Bindings[BindingId].Endpoint;
    if (!e) return STATUS_INVALID_HANDLE;

    UCHAR packet[RPC_MAX_CALL_DATA];
    ULONG packetLen = sizeof(packet);
    NTSTATUS s = RpcBuildRequest(packet, &packetLen, BindingId, ProcNum, InData, InLen);
    if (!NT_SUCCESS(s)) return s;

    /* Synchronously send through the LPC port and wait for the reply. */
    LPC_MESSAGE msg;
    RtlZeroMemory(&msg, sizeof(msg));
    ULONG copy = packetLen;
    if (copy > sizeof(msg.Data)) copy = sizeof(msg.Data);
    RtlCopyMemory(msg.Data, packet, copy);
    msg.DataLength = (USHORT)copy;
    msg.TotalLength = (USHORT)(sizeof(LPC_MESSAGE) - sizeof(msg.Data) + copy);
    msg.MessageType = LPC_REQUEST;
    s = NtRequestWaitReplyPort(e->LpcPort, &msg, &msg);
    if (!NT_SUCCESS(s)) return s;

    PRPC_PACKET reply = (PRPC_PACKET)msg.Data;
    if (reply->PacketType == RPC_PKT_FAULT) {
        PULONG code = (PULONG)reply->Payload;
        if (OutReturn) *OutReturn = *code;
        return STATUS_REMOTE_NOT_LISTENING;
    }
    if (OutLen) {
        ULONG got = msg.DataLength - sizeof(RPC_PACKET);
        if (got > *OutLen) got = *OutLen;
        RtlCopyMemory(OutData, reply->Payload, got);
        *OutLen = got;
    }
    if (OutReturn) {
        PULONG code = (PULONG)((PUCHAR)reply->Payload + (msg.DataLength - sizeof(RPC_PACKET) - sizeof(ULONG)));
        *OutReturn = *code;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RpcInit(VOID)
{
    RtlZeroMemory(g_Bindings, sizeof(g_Bindings));
    g_BindingCount = 0;

    /* Register the SCM interface as a built-in RPC endpoint. */
    RPC_UUID svcctl = { 0x367ABB81, 0x9844, 0x35F1,
                        { 0xAD, 0x32, 0x98, 0xF0, 0x02, 0x29, 0x3A, 0xC6 } };
    RpcServerRegisterInterface("\\RPC\\svcctl", &svcctl, 1, 0, NULL);

    RPC_UUID eventlog = { 0x82273FDC, 0xE32A, 0x18C3,
                          { 0x3F, 0x78, 0x82, 0x79, 0x92, 0xA1, 0xC6, 0x17 } };
    RpcServerRegisterInterface("\\RPC\\eventlog", &eventlog, 1, 0, NULL);

    DbgPrint("RPC: runtime initialized with %u endpoints\n",
             g_Endpoints ? 2 : 0);
    return STATUS_SUCCESS;
}
