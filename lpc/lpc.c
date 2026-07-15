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

/* External object type declaration */
extern POBJECT_TYPE LpcPortObjectType;

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
    PVOID serverBody;
    NTSTATUS status;
    PLPC_PORT serverPort;
    PLPC_PORT clientPort;

    UNREFERENCED_PARAMETER(SecurityQos);
    UNREFERENCED_PARAMETER(ClientView);
    UNREFERENCED_PARAMETER(ServerView);
    UNREFERENCED_PARAMETER(MaxMessageLength);
    UNREFERENCED_PARAMETER(ConnectionInformation);
    UNREFERENCED_PARAMETER(ConnectionInformationLength);

    if (!OutPortHandle || !ServerPortName) 
        return STATUS_INVALID_PARAMETER;

    /* Look up the server port */
    status = ObLookupObjectByName(ServerPortName, &serverBody);
    if (!NT_SUCCESS(status)) 
        return status;

    serverPort = (PLPC_PORT)serverBody;

    /* Create client-side port object */
    status = ObCreateObject(LpcPortObjectType, sizeof(LPC_PORT), NULL, (PVOID *)&clientPort);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(serverBody);
        return status;
    }

    clientPort->Type = LPC_PORT_TYPE;
    clientPort->ConnectedPort = serverPort;
    clientPort->MsgQueueHead = NULL;
    KeInitializeEvent(&clientPort->MsgEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&clientPort->Lock);
    clientPort->PortContext = NULL;
    clientPort->Flags = LPC_PORT_FLAG_NEVER_DISCONNECT;

    /* Set up the server's connected port */
    KIRQL irql;
    KeAcquireSpinLock(&serverPort->Lock, &irql);
    if (serverPort->ConnectedPort == NULL) {
        serverPort->ConnectedPort = clientPort;
    }
    KeReleaseSpinLock(&serverPort->Lock, irql);

    /* Insert handle for client port */
    status = ObInsertHandle(clientPort, OutPortHandle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(clientPort);
        ObDereferenceObject(serverBody);
        return status;
    }

    ObDereferenceObject(clientPort);
    ObDereferenceObject(serverBody);

    DbgPrint("LPC: connected to port '%wZ'\n", ServerPortName);
    return STATUS_SUCCESS;
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
    PLPC_PORT clientPort, serverPort;
    NTSTATUS status;
    PLPC_MESSAGE queuedMsg;

    if (!msg || !ReplyMessage) 
        return STATUS_INVALID_PARAMETER;

    /* Reference the client port */
    status = ObReferenceObjectByHandle(PortHandle, LpcPortObjectType, (PVOID *)&clientPort);
    if (!NT_SUCCESS(status)) 
        return status;

    if (clientPort->Type != LPC_PORT_TYPE) {
        ObDereferenceObject(clientPort);
        return STATUS_INVALID_HANDLE;
    }

    serverPort = clientPort->ConnectedPort;
    if (!serverPort) {
        ObDereferenceObject(clientPort);
        return STATUS_PORT_DISCONNECTED;
    }

    /* Allocate message buffer */
    queuedMsg = ExAllocatePoolWithTag(NonPagedPool, sizeof(LPC_MESSAGE), 'PCML');
    if (!queuedMsg) {
        ObDereferenceObject(clientPort);
        return STATUS_NO_MEMORY;
    }

    /* Copy the request message */
    RtlCopyMemory(queuedMsg, msg, msg->TotalLength);
    queuedMsg->SenderPort = clientPort;
    queuedMsg->Next = NULL;
    queuedMsg->ProcessId = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    queuedMsg->MessageId = (ULONG)(ULONG_PTR)KeGetCurrentThread(); /* Unique message ID */

    /* Queue message to server */
    KIRQL irql;
    KeAcquireSpinLock(&serverPort->Lock, &irql);
    if (serverPort->MsgQueueHead == NULL) {
        serverPort->MsgQueueHead = queuedMsg;
    } else {
        PLPC_MESSAGE last = serverPort->MsgQueueHead;
        while (last->Next) 
            last = last->Next;
        last->Next = queuedMsg;
    }
    KeSetEvent(&serverPort->MsgEvent, 0, FALSE);
    KeReleaseSpinLock(&serverPort->Lock, irql);

    DbgPrint("LPC: sent message type %u to server\n", msg->MessageType);

    /* Wait for reply */
    PLPC_MESSAGE replyMsg = NULL;
    ULONG timeout = 0;
    while (replyMsg == NULL) {
        KeAcquireSpinLock(&clientPort->Lock, &irql);
        if (clientPort->MsgQueueHead) {
            replyMsg = clientPort->MsgQueueHead;
            clientPort->MsgQueueHead = replyMsg->Next;
            replyMsg->Next = NULL;
        }
        KeReleaseSpinLock(&clientPort->Lock, irql);
        
        if (replyMsg == NULL) {
            /* Timeout after 10 seconds to prevent infinite hang */
            if (timeout++ > 10000) {
                ObDereferenceObject(clientPort);
                ExFreePoolWithTag(queuedMsg, 'PCML');
                return STATUS_TIMEOUT;
            }
            KeStallExecutionProcessor(1000); /* 1ms delay */
        }
    }

    /* Copy reply to caller */
    RtlCopyMemory(reply, replyMsg, replyMsg->TotalLength);
    ExFreePoolWithTag(replyMsg, 'PCML');
    ObDereferenceObject(clientPort);

    DbgPrint("LPC: received reply type %u\n", reply->MessageType);
    return STATUS_SUCCESS;
}

/* ---- Reply/Wait Receive -------------------------------------------------- */

NTSTATUS NTAPI NtReplyWaitReceivePort(HANDLE PortHandle,
                                      PVOID *PortContext,
                                      PVOID ReplyMessage,
                                      PVOID ReceiveMessage)
{
    PLPC_PORT serverPort;
    NTSTATUS status;
    PLPC_MESSAGE replyMsg = (PLPC_MESSAGE)ReplyMessage;
    PLPC_MESSAGE receiveMsg = (PLPC_MESSAGE)ReceiveMessage;

    UNREFERENCED_PARAMETER(PortContext);

    /* Reference the server port */
    status = ObReferenceObjectByHandle(PortHandle, LpcPortObjectType, (PVOID *)&serverPort);
    if (!NT_SUCCESS(status)) 
        return status;

    if (serverPort->Type != LPC_PORT_TYPE) {
        ObDereferenceObject(serverPort);
        return STATUS_INVALID_HANDLE;
    }

    /* Send reply if provided */
    if (replyMsg) {
        PLPC_PORT clientPort = replyMsg->SenderPort;
        if (clientPort) {
            KIRQL irql;
            KeAcquireSpinLock(&clientPort->Lock, &irql);
            /* Queue reply to client */
            if (clientPort->MsgQueueHead == NULL) {
                clientPort->MsgQueueHead = replyMsg;
            } else {
                PLPC_MESSAGE last = clientPort->MsgQueueHead;
                while (last->Next) 
                    last = last->Next;
                last->Next = replyMsg;
            }
            KeSetEvent(&clientPort->MsgEvent, 0, FALSE);
            KeReleaseSpinLock(&clientPort->Lock, irql);
        }
    }

    /* Wait for incoming message */
    PLPC_MESSAGE incomingMsg = NULL;
    ULONG timeout = 0;
    while (incomingMsg == NULL) {
        KIRQL irql;
        KeAcquireSpinLock(&serverPort->Lock, &irql);
        if (serverPort->MsgQueueHead) {
            incomingMsg = serverPort->MsgQueueHead;
            serverPort->MsgQueueHead = incomingMsg->Next;
            incomingMsg->Next = NULL;
        }
        KeReleaseSpinLock(&serverPort->Lock, irql);
        
        if (incomingMsg == NULL) {
            /* Timeout after 30 seconds */
            if (timeout++ > 30000) {
                ObDereferenceObject(serverPort);
                return STATUS_TIMEOUT;
            }
            KeStallExecutionProcessor(1000); /* 1ms delay */
        }
    }

    /* Copy incoming message to caller */
    RtlCopyMemory(receiveMsg, incomingMsg, incomingMsg->TotalLength);
    ExFreePoolWithTag(incomingMsg, 'PCML');
    ObDereferenceObject(serverPort);

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