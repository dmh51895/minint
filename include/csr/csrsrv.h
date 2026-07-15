/* MinNT - CSR types */
#ifndef _CSR_CSRSRV_H_
#define _CSR_CSRSRV_H_

typedef struct _CSR_PROCESS {
    LIST_ENTRY Links;
    CLIENT_ID ClientId;
    HANDLE ProcessHandle;
    struct _CSR_THREAD *ThreadList;
    ULONG ReferenceCount;
    ULONG Flags;
    ULONG DebugFlags;
    PVOID ServerData[4];
} CSR_PROCESS, *PCSR_PROCESS;

typedef struct _CSR_THREAD {
    LIST_ENTRY Links;
    CLIENT_ID ClientId;
    HANDLE ThreadHandle;
    CSR_PROCESS *Process;
    struct _CSR_THREAD *Next;
    ULONG ReferenceCount;
} CSR_THREAD, *PCSR_THREAD;

typedef struct _CSR_API_MESSAGE {
    PORT_MESSAGE Header;
    ULONG ApiNumber;
    NTSTATUS Status;
    ULONG Data[1];
} CSR_API_MESSAGE, *PCSR_API_MESSAGE;

typedef NTSTATUS (*PCSR_API_ROUTINE)(PCSR_API_MESSAGE, PCSR_THREAD, PCSR_PROCESS);
typedef NTSTATUS (*PCSR_SERVER_DLL_INIT_CALLBACK)(ULONG, PCHAR, PCHAR, PVOID, ULONG_PTR, PULONG, PULONG);

#define CsrpMaxApiNumber 32
#define CSRSRV_FIRST_API_NUMBER 0x100

typedef struct _CSR_SERVER_DLL {
    ULONG Size;
    ULONG Length;
    HANDLE SharedSection;
    UNICODE_STRING Name;
    PVOID ServerDllBase;
    ULONG EntryCount;
    PULONG ApiOffsets;
    PCSR_API_ROUTINE ApiDispatchTable;
    PCSR_SERVER_DLL_INIT_CALLBACK InitCallback;
    ULONG SizeOfProcessData;
    NTSTATUS (*NewProcessCallback)(PVOID, PCSR_PROCESS);
} CSR_SERVER_DLL, *PCSR_SERVER_DLL;

VOID NTAPI CsrSrvClientConnect(PCSR_API_MESSAGE ApiMessage);
VOID NTAPI CsrSrvUnusedFunction(VOID);


typedef struct _CSRSS_API_CONNECT_INFO {
    ULONG Size;
    ULONG Unknown[4];
} CSRSS_API_CONNECT_INFO, *PCRSS_API_CONNECT_INFO;


typedef struct _CSR_WAIT_BLOCK {
    LIST_ENTRY WaitList;
    CSR_PROCESS *Process;
    CSR_THREAD *Thread;
    PVOID WaitBlock;
} CSR_WAIT_BLOCK, *PCSR_WAIT_BLOCK;

typedef struct _CSR_NT_SESSION {
    ULONG SessionId;
    CSR_PROCESS *Process;
    ULONG ReferenceCount;
} CSR_NT_SESSION, *PCSR_NT_SESSION;

typedef CSRSS_API_CONNECT_INFO CSR_API_CONNECTINFO, *PCSR_API_CONNECTINFO;

#define DAACL_SECURITY_INFORMATION  0x04
#define DACL_SECURITY_INFORMATION   0x04
#define CSRSRV_SERVERDLL_INDEX  0

#endif

typedef struct _STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING, ANSI_STRING, *PANSI_STRING;

#define STATUS_TOO_MANY_NAMES ((NTSTATUS)0xC00000AEL)
