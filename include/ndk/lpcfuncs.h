/*
 * MinNT - ndk/lpcfuncs.h
 * Local Procedure Call functions.
 */

#ifndef _NDK_LPCFUNCS_H_
#define _NDK_LPCFUNCS_H_

#include <nt/ntdef.h>
#include <ndk/setypes.h>
#include <ndk/psfuncs.h>

/* ---- CLIENT_ID ---------------------------------------------------------- */

#ifndef _CLIENT_ID_DEFINED
#define _CLIENT_ID_DEFINED
typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;
#endif

/* ---- PORT_MESSAGE ------------------------------------------------------- */

typedef struct _PORT_MESSAGE {
    union {
        struct {
            CSHORT DataLength;
            CSHORT TotalLength;
        } s1;
        ULONG Length;
    } u1;
    CLIENT_ID ClientId;
    union {
        struct {
            CSHORT ZeroInit;
            CSHORT Type;
        };
        struct {
            CSHORT DataInfoOffset;
        };
    } u2;
    union { UCHAR Data[1]; };
} PORT_MESSAGE, *PPORT_MESSAGE;

/* ---- SmConnectToSm (SM API) --------------------------------------------- */

NTSTATUS NTAPI SmConnectToSm(
    PUNICODE_STRING PortName,
    HANDLE SmApiPort,
    ULONG SubSystemType,
    PHANDLE ConnectionPort
);

NTSTATUS NTAPI SmExecPgm(
    HANDLE SmApiPort,
    PRTL_USER_PROCESS_INFORMATION ProcessInformation,
    BOOLEAN DebugSection
);


typedef struct _REMOTE_PORT_VIEW {
    ULONG Length;
    HANDLE ViewBase;
    HANDLE SectionHandle;
} REMOTE_PORT_VIEW, *PREMOTE_PORT_VIEW;

typedef struct _SECURITY_QUALITY_OF_SERVICE {
    ULONG Length;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
    BOOLEAN ContextTrackingMode;
    BOOLEAN EffectiveOnly;
} SECURITY_QUALITY_OF_SERVICE, *PSECURITY_QUALITY_OF_SERVICE;

#define SecurityIdentification 1
#define SECURITY_DYNAMIC_TRACKING 1
#define SecurityImpersonation 2
#define SecurityDelegation 3

#define STATUS_PENDING ((NTSTATUS)0x00000103L)
#endif /* _NDK_LPCFUNCS_H_ */
