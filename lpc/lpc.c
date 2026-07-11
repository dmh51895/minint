/*
 * MinNT - lpc/lpc.c
 * Local Procedure Call: inter-process communication for CSRSS/SMSS.
 * Minimal message-passing LPC with port objects and message queues.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/lpc.h>
#include <nt/rtl.h>
#include <nt/hal.h>

/* ---- Init --------------------------------------------------------------- */

NTSTATUS NTAPI LpcInitSystem(VOID)
{
    DbgPrint("LPC: local procedure call initialized\n");
    return STATUS_SUCCESS;
}

/* ---- Create port --------------------------------------------------------- */

NTSTATUS NTAPI NtCreatePort(PHANDLE OutPortHandle,
                            PUNICODE_STRING PortName,
                            ULONG MaxConnectionInfoLength,
                            ULONG MaxMessageLength,
                            PVOID Reserved)
{
    PLPC_PORT port;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(MaxConnectionInfoLength);
    UNREFERENCED_PARAMETER(MaxMessageLength);
    UNREFERENCED_PARAMETER(Reserved);

    if (!OutPortHandle || !PortName) return STATUS_INVALID_PARAMETER;

    status = ObCreateObject(NULL, sizeof(LPC_PORT), PortName, (PVOID *)&port);
    if (!NT_SUCCESS(status)) return status;

    port->Type = LPC_PORT_TYPE;
    port->Name = *PortName;
    port->ConnectedPort = NULL;
    port->MsgQueueHead = NULL;
    KeInitializeEvent(&port->MsgEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&port->Lock);
    port->PortContext = NULL;
    port->Flags = 0;

    status = ObInsertHandle(port, OutPortHandle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(port);
        return status;
    }

    DbgPrint("LPC: created port '%wZ'\n", PortName);
    return STATUS_SUCCESS;
}

/* ---- Connect port -------------------------------------------------------- */

NTSTATUS NTAPI NtConnectPort(PHANDLE OutPortHandle,
                             PUNICODE_STRING ServerPortName,
                             PVOID SecurityQos,
                             PVOID ClientView,
                             PVOID ServerView,
                             PULONG MaxMessageLength,
                             PVOID ConnectionInformation,
                             PULONG ConnectionInformationLength)
{
    PVOID body;
    NTSTATUS status;
    PLPC_PORT serverPort;

    UNREFERENCED_PARAMETER(SecurityQos);
    UNREFERENCED_PARAMETER(ClientView);
    UNREFERENCED_PARAMETER(ServerView);
    UNREFERENCED_PARAMETER(MaxMessageLength);
    UNREFERENCED_PARAMETER(ConnectionInformation);
    UNREFERENCED_PARAMETER(ConnectionInformationLength);

    status = ObLookupObjectByName(ServerPortName, &body);
    if (!NT_SUCCESS(status)) return status;

    serverPort = (PLPC_PORT)body;

    /* Create client-side port object */
    PLPC_PORT clientPort;
    status = ObCreateObject(NULL, sizeof(LPC_PORT), NULL, (PVOID *)&clientPort);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(body);
        return status;
    }

    clientPort->Type = LPC_PORT_TYPE;
    clientPort->ConnectedPort = serverPort;
    clientPort->Flags = LPC_PORT_FLAG_NEVER_DISCONNECT;
    KeInitializeEvent(&clientPort->MsgEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&clientPort->Lock);

    status = ObInsertHandle(clientPort, OutPortHandle);
    ObDereferenceObject(clientPort);
    ObDereferenceObject(body);

    return status;
}

/* ---- Listen port --------------------------------------------------------- */

NTSTATUS NTAPI NtListenPort(HANDLE PortHandle,
                            PVOID ConnectionRequest)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(ConnectionRequest);

    /* Stub: no-op for now */
    return STATUS_SUCCESS;
}

/* ---- Accept connect ------------------------------------------------------ */

NTSTATUS NTAPI NtAcceptConnectPort(PHANDLE OutPortHandle,
                                   HANDLE PortHandle,
                                   ULONG MessageId,
                                   PVOID PortView,
                                   PVOID ServerView,
                                   ULONG Request)
{
    UNREFERENCED_PARAMETER(PortView);
    UNREFERENCED_PARAMETER(ServerView);
    UNREFERENCED_PARAMETER(MessageId);

    /* Stub: return the same port handle */
    if (OutPortHandle)
        *OutPortHandle = PortHandle;

    return STATUS_SUCCESS;
}

/* ---- Request port -------------------------------------------------------- */

NTSTATUS NTAPI NtRequestPort(HANDLE PortHandle,
                             PVOID RequestMessage)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(RequestMessage);

    /* Stub: no-op */
    return STATUS_SUCCESS;
}

/* ---- Reply port ---------------------------------------------------------- */

NTSTATUS NTAPI NtReplyPort(HANDLE PortHandle,
                           PVOID ReplyMessage)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(ReplyMessage);

    /* Stub: no-op */
    return STATUS_SUCCESS;
}

/* ---- Request/Wait Reply -------------------------------------------------- */

NTSTATUS NTAPI NtRequestWaitReplyPort(HANDLE PortHandle,
                                      PVOID RequestMessage,
                                      PVOID ReplyMessage)
{
    PLPC_MESSAGE msg = (PLPC_MESSAGE)RequestMessage;
    PLPC_MESSAGE reply = (PLPC_MESSAGE)ReplyMessage;

    UNREFERENCED_PARAMETER(PortHandle);

    if (!msg) return STATUS_INVALID_PARAMETER;

    /* Stub: echo the message back */
    if (reply) {
        RtlCopyMemory(reply, msg, sizeof(LPC_MESSAGE));
        reply->MessageType = LPC_REPLY;
    }

    return STATUS_SUCCESS;
}

/* ---- Read/Write request data --------------------------------------------- */

NTSTATUS NTAPI NtReadRequestData(HANDLE PortHandle,
                                 PVOID Message,
                                 ULONG DataEntryIndex,
                                 PVOID Buffer,
                                 ULONG BufferLength,
                                 PULONG ActualLength)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Message);
    UNREFERENCED_PARAMETER(DataEntryIndex);

    /* Stub: zero the buffer */
    RtlZeroMemory(Buffer, BufferLength);
    if (ActualLength) *ActualLength = 0;

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtWriteRequestData(HANDLE PortHandle,
                                  PVOID Message,
                                  ULONG DataEntryIndex,
                                  PVOID Buffer,
                                  ULONG BufferLength)
{
    UNREFERENCED_PARAMETER(PortHandle);
    UNREFERENCED_PARAMETER(Message);
    UNREFERENCED_PARAMETER(DataEntryIndex);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferLength);

    /* Stub: no-op */
    return STATUS_SUCCESS;
}
