/*
 * MinNT - lpc.h
 * Local Procedure Call: inter-process communication for CSRSS/SMSS.
 * Minimal NT 6.x LPC — enough for subsystem initialization handshake.
 */

#ifndef _LPC_H_
#define _LPC_H_

#include <nt/ntdef.h>
#include <nt/dispatcher.h>

/* ---- LPC message types --------------------------------------------------- */

#define LPC_REQUEST              1
#define LPC_REPLY                2
#define LPC_DATAGRAM             3
#define LPC_LOST_REPLY           4
#define LPC_PORT_CLOSED          5
#define LPC_CLIENT_DIED          6
#define LPC_EXCEPTION            7
#define LPC_DEBUG_EVENT          8
#define LPC_ERROR_EVENT          9
#define LPC_CONNECTION_REQUEST  10
#define LPC_CONNECTION_REPLY    11
#define LPC_CONNECTION_REFUSED  12

/* ---- LPC message header -------------------------------------------------- */

#define LPC_MAX_MESSAGE_LENGTH  256

typedef struct _LPC_MESSAGE {
    USHORT  DataLength;
    USHORT  TotalLength;
    USHORT  MessageType;
    USHORT  DataInfoOffset;
    ULONG   ProcessId;
    ULONG   MessageId;
    ULONG   CallbackId;
    ULONG   ClientViewBase;
    ULONG   ClientViewTop;
    struct _LPC_PORT *SenderPort;
    struct _LPC_MESSAGE *Next;
    UCHAR   Data[200];  /* Fixed size for now */
} LPC_MESSAGE, *PLPC_MESSAGE;

#define LPC_MAX_DATA_LENGTH     200

/* ---- Port object --------------------------------------------------------- */

typedef struct _LPC_PORT {
    ULONG               Type;           /* LPC_PORT_TYPE */
    UNICODE_STRING      Name;
    struct _LPC_PORT   *ConnectedPort;
    struct _LPC_MESSAGE *MsgQueueHead;   /* messages waiting */
    KEVENT              MsgEvent;
    KSPIN_LOCK          Lock;
    PVOID               PortContext;
    ULONG               Flags;
} LPC_PORT, *PLPC_PORT;

#define LPC_PORT_TYPE  0x4C50  /* 'PL' */

#define LPC_PORT_FLAG_NEVER_DISCONNECT  0x00000001
#define LPC_PORT_FLAG_IMPERSONABLE      0x00000002

/* ---- API ----------------------------------------------------------------- */

NTSTATUS NTAPI LpcInitSystem(VOID);

NTSTATUS NTAPI NtCreatePort(PHANDLE OutPortHandle,
                            PUNICODE_STRING PortName,
                            ULONG MaxConnectionInfoLength,
                            ULONG MaxMessageLength,
                            PVOID Reserved);

NTSTATUS NTAPI NtConnectPort(PHANDLE OutPortHandle,
                             PUNICODE_STRING ServerPortName,
                             PVOID SecurityQos,
                             PVOID ClientView,
                             PVOID ServerView,
                             PULONG MaxMessageLength,
                             PVOID ConnectionInformation,
                             PULONG ConnectionInformationLength);

NTSTATUS NTAPI NtListenPort(HANDLE PortHandle,
                            PVOID ConnectionRequest);

NTSTATUS NTAPI NtAcceptConnectPort(PHANDLE OutPortHandle,
                                   HANDLE PortHandle,
                                   ULONG MessageId,
                                   PVOID PortView,
                                   PVOID ServerView,
                                   ULONG Request);

NTSTATUS NTAPI NtRequestPort(HANDLE PortHandle,
                             PVOID RequestMessage);

NTSTATUS NTAPI NtReplyPort(HANDLE PortHandle,
                           PVOID ReplyMessage);

NTSTATUS NTAPI NtRequestWaitReplyPort(HANDLE PortHandle,
                                      PVOID RequestMessage,
                                      PVOID ReplyMessage);

NTSTATUS NTAPI NtReplyWaitReceivePort(HANDLE PortHandle,
                                      PVOID *PortContext,
                                      PVOID ReplyMessage,
                                      PVOID ReceiveMessage);

NTSTATUS NTAPI NtReadRequestData(HANDLE PortHandle,
                                 PVOID Message,
                                 ULONG DataEntryIndex,
                                 PVOID Buffer,
                                 ULONG BufferLength,
                                 PULONG ActualLength);

NTSTATUS NTAPI NtWriteRequestData(HANDLE PortHandle,
                                  PVOID Message,
                                  ULONG DataEntryIndex,
                                  PVOID Buffer,
                                  ULONG BufferLength);

#endif /* _LPC_H_ */
