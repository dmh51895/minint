/*
 * MinNT - ws2_32.c
 * WinSock 2.0 Implementation for MinNT Kernel
 * 
 * This module provides Windows-compatible socket API that internally
 * uses lwIP's BSD-style socket implementation. It includes:
 * - Socket table management (max 64 sockets)
 * - Type translation between Windows SOCKADDR and BSD sockaddr
 * - Complete WinSock 2.0 API implementation
 * - Error code translation from lwIP to Windows WSA errors
 * - Synchronization via spinlocks
 *
 * Copyright (c) 2024 MinNT Project
 */

#include <nt/ntdef.h>
#include <string.h>
#include <errno.h>

/* lwIP headers - use system paths */
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/inet.h"

/* Windows type definitions missing from ntdef.h */
#ifndef UINT
typedef unsigned int UINT;
#endif

#ifndef UINT_PTR
typedef ULONG_PTR UINT_PTR;
#endif

#ifndef WORD
typedef unsigned short WORD;
#endif

#ifndef CONST
#define CONST const
#endif

#ifndef PCSTR
typedef const char *PCSTR;
#endif

#ifndef PSTR
typedef char *PSTR;
#endif

/* Forward declarations for lwIP functions */
extern int lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen);
extern int lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen);
extern const char *lwip_inet_ntop(int af, const void *src, char *dst, socklen_t size);
extern int lwip_inet_pton(int af, const char *src, void *dst);

/* lwIP type compatibility */
#ifndef u8_t
typedef uint8_t u8_t;
#endif

/* lwIP message flags compatibility */
#ifndef MSG_DONTROUTE
#define MSG_DONTROUTE   0x04    /* Send without using routing tables */
#endif

/* ============================================================================
 * WINDOWS WINSOCK TYPE DEFINITIONS
 * ============================================================================
 *
 * Windows uses different structure layouts than BSD/lwIP.
 * We define Windows-compatible types here for the API.
 */

/* Windows socket types */
#define WS2_32_SOCKET_TYPE_STREAM    1
#define WS2_32_SOCKET_TYPE_DGRAM     2
#define WS2_32_SOCKET_TYPE_RAW       3
#define WS2_32_SOCKET_TYPE_RDM       4
#define WS2_32_SOCKET_TYPE_SEQPACKET 5

/* Windows address families */
#define WS2_32_AF_UNSPEC     0
#define WS2_32_AF_UNIX       1
#define WS2_32_AF_INET       2
#define WS2_32_AF_IMPLINK    3
#define WS2_32_AF_PUP        4
#define WS2_32_AF_CHAOS      5
#define WS2_32_AF_NS         6
#define WS2_32_AF_ISO        7
#define WS2_32_AF_OSI        WS2_32_AF_ISO
#define WS2_32_AF_ECMA       8
#define WS2_32_AF_DATAKIT    9
#define WS2_32_AF_CCITT      10
#define WS2_32_AF_SNA        11
#define WS2_32_AF_DECnet     12
#define WS2_32_AF_DLI        13
#define WS2_32_AF_LAT        14
#define WS2_32_AF_HYLINK     15
#define WS2_32_AF_APPLETALK  16
#define WS2_32_AF_NETBIOS    17
#define WS2_32_AF_VOICEVIEW  18
#define WS2_32_AF_FIREFOX    19
#define WS2_32_AF_UNKNOWN1   20
#define WS2_32_AF_BAN        21
#define WS2_32_AF_ATM        22
#define WS2_32_AF_INET6      23
#define WS2_32_AF_CLUSTER    24
#define WS2_32_AF_12844      25
#define WS2_32_AF_IRDA       26
#define WS2_32_AF_NETDES     28

#define WS2_32_PF_INET       WS2_32_AF_INET
#define WS2_32_PF_INET6      WS2_32_AF_INET6

/* Windows socket option levels */
#define WS2_32_SOL_SOCKET    0xffff

/* Windows socket options */
#define WS2_32_SO_DEBUG      0x0001
#define WS2_32_SO_ACCEPTCONN 0x0002
#define WS2_32_SO_REUSEADDR  0x0004
#define WS2_32_SO_KEEPALIVE  0x0008
#define WS2_32_SO_DONTROUTE  0x0010
#define WS2_32_SO_BROADCAST  0x0020
#define WS2_32_SO_USELOOPBACK 0x0040
#define WS2_32_SO_LINGER     0x0080
#define WS2_32_SO_OOBINLINE  0x0100
#define WS2_32_SO_DONTLINGER (int)(~WS2_32_SO_LINGER)
#define WS2_32_SO_EXCLUSIVEADDRUSE ((int)(~WS2_32_SO_REUSEADDR))

#define WS2_32_SO_SNDBUF     0x1001
#define WS2_32_SO_RCVBUF     0x1002
#define WS2_32_SO_SNDLOWAT   0x1003
#define WS2_32_SO_RCVLOWAT   0x1004
#define WS2_32_SO_SNDTIMEO   0x1005
#define WS2_32_SO_RCVTIMEO   0x1006
#define WS2_32_SO_ERROR      0x1007
#define WS2_32_SO_TYPE      0x1008

/* Windows shutdown types */
#define WS2_32_SD_RECEIVE    0
#define WS2_32_SD_SEND       1
#define WS2_32_SD_BOTH       2

/* Windows socket flags */
#define WS2_32_MSG_OOB       0x01
#define WS2_32_MSG_PEEK      0x02
#define WS2_32_MSG_DONTROUTE 0x04
#define WS2_32_MSG_WAITALL   0x08
#define WS2_32_MSG_PARTIAL   0x8000

/* Windows ioctls */
#define WS2_32_FIONREAD      0x4004667f
#define WS2_32_FIONBIO       0x8004667e
#define WS2_32_FIOASYNC      0x8004667d
#define WS2_32_SIOCATMARK    0x40047307

/* Windows WSA error codes */
#define WS2_32_WSAEINTR           10004
#define WS2_32_WSAEBADF           10009
#define WS2_32_WSAEACCES          10013
#define WS2_32_WSAEFAULT          10014
#define WS2_32_WSAEINVAL          10022
#define WS2_32_WSAEMFILE          10024
#define WS2_32_WSAEWOULDBLOCK     10035
#define WS2_32_WSAEINPROGRESS     10036
#define WS2_32_WSAEALREADY        10037
#define WS2_32_WSAENOTSOCK        10038
#define WS2_32_WSAEDESTADDRREQ    10039
#define WS2_32_WSAEMSGSIZE        10040
#define WS2_32_WSAEPROTOTYPE      10041
#define WS2_32_WSAENOPROTOOPT     10042
#define WS2_32_WSAEPROTONOSUPPORT 10043
#define WS2_32_WSAESOCKTNOSUPPORT 10044
#define WS2_32_WSAEOPNOTSUPP      10045
#define WS2_32_WSAEPFNOSUPPORT    10046
#define WS2_32_WSAEAFNOSUPPORT    10047
#define WS2_32_WSAEADDRINUSE      10048
#define WS2_32_WSAEADDRNOTAVAIL   10049
#define WS2_32_WSAENETDOWN        10050
#define WS2_32_WSAENETUNREACH     10051
#define WS2_32_WSAENETRESET       10052
#define WS2_32_WSAECONNABORTED    10053
#define WS2_32_WSAECONNRESET      10054
#define WS2_32_WSAENOBUFS         10055
#define WS2_32_WSAEISCONN         10056
#define WS2_32_WSAENOTCONN        10057
#define WS2_32_WSAESHUTDOWN       10058
#define WS2_32_WSAETOOMANYREFS    10059
#define WS2_32_WSAETIMEDOUT       10060
#define WS2_32_WSAECONNREFUSED    10061
#define WS2_32_WSAELOOP           10062
#define WS2_32_WSAENAMETOOLONG    10063
#define WS2_32_WSAEHOSTDOWN       10064
#define WS2_32_WSAEHOSTUNREACH    10065
#define WS2_32_WSAENOTEMPTY       10066
#define WS2_32_WSAEPROCLIM        10067
#define WS2_32_WSAEUSERS          10068
#define WS2_32_WSAEDQUOT          10069
#define WS2_32_WSAESTALE          10070
#define WS2_32_WSAEREMOTE         10071
#define WS2_32_WSAHOST_NOT_FOUND  11001
#define WS2_32_WSATRY_AGAIN       11002
#define WS2_32_WSANO_RECOVERY     11003
#define WS2_32_WSANO_DATA         11004

/* Windows socket handle type */
typedef UINT_PTR SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#define SOCKET_ERROR   (-1)

/* Windows SOCKADDR structure - matches Windows layout exactly */
typedef struct _WS2_32_SOCKADDR {
    USHORT  sa_family;
    CHAR    sa_data[14];
} WS2_32_SOCKADDR, *PWS2_32_SOCKADDR, *LPWS2_32_SOCKADDR;

/* Windows SOCKADDR_IN structure */
typedef struct _WS2_32_SOCKADDR_IN {
    USHORT  sin_family;
    USHORT  sin_port;
    struct {
        UCHAR s_b1, s_b2, s_b3, s_b4;
    } sin_addr;
    CHAR    sin_zero[8];
} WS2_32_SOCKADDR_IN, *PWS2_32_SOCKADDR_IN;

/* Windows fd_set structure */
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif

typedef struct _WS2_32_FD_SET {
    UINT    fd_count;
    SOCKET  fd_array[FD_SETSIZE];
} WS2_32_FD_SET, *PWS2_32_FD_SET, *LPWS2_32_FD_SET;

/* Windows timeval structure */
typedef struct _WS2_32_TIMEVAL {
    LONG tv_sec;
    LONG tv_usec;
} WS2_32_TIMEVAL, *PWS2_32_TIMEVAL, *LPWS2_32_TIMEVAL;

/* Windows linger structure */
typedef struct _WS2_32_LINGER {
    USHORT l_onoff;
    USHORT l_linger;
} WS2_32_LINGER, *PWS2_32_LINGER, *LPWS2_32_LINGER;

/* ============================================================================
 * SOCKET TABLE MANAGEMENT
 * ============================================================================
 */

#define MAX_WINSOCK_SOCKETS 64
#define SOCKET_TABLE_LOCK_SPIN_COUNT 1000

typedef enum _SOCKET_STATE {
    SOCKET_STATE_FREE = 0,
    SOCKET_STATE_ALLOCATED,
    SOCKET_STATE_BOUND,
    SOCKET_STATE_LISTENING,
    SOCKET_STATE_CONNECTED,
    SOCKET_STATE_CONNECTING,
    SOCKET_STATE_CLOSING
} SOCKET_STATE;

typedef struct _SOCKET_ENTRY {
    SOCKET_STATE    State;
    INT             lwip_socket;
    INT             Domain;
    INT             Type;
    INT             Protocol;
    ULONG           NonBlocking;
    ULONG           SelectEvents;
    INT             LastError;
    KSPIN_LOCK      EntryLock;
} SOCKET_ENTRY, *PSOCKET_ENTRY;

typedef struct _SOCKET_TABLE {
    SOCKET_ENTRY    Entries[MAX_WINSOCK_SOCKETS];
    KSPIN_LOCK      TableLock;
    ULONG           SocketCount;
    BOOLEAN         Initialized;
} SOCKET_TABLE, *PSOCKET_TABLE;

/* Global socket table */
static SOCKET_TABLE g_SocketTable = {0};
static INT g_LastWSAError = 0;

/* ============================================================================
 * SPINLOCK IMPLEMENTATION
 * ============================================================================
 */

FORCEINLINE VOID KeAcquireSpinLock(PKSPIN_LOCK SpinLock, PKIRQL OldIrql)
{
    *OldIrql = DISPATCH_LEVEL;
    /* Simple atomic exchange spinlock */
    while (__sync_lock_test_and_set((LONG*)SpinLock, 1)) {
        /* Spin with brief pause */
        for (volatile INT i = 0; i < 100; i++);
    }
}

FORCEINLINE VOID KeReleaseSpinLock(PKSPIN_LOCK SpinLock, KIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(OldIrql);
    __sync_lock_release((LONG*)SpinLock);
}

FORCEINLINE VOID KeAcquireSpinLockAtDpcLevel(PKSPIN_LOCK SpinLock)
{
    while (__sync_lock_test_and_set((LONG*)SpinLock, 1)) {
        for (volatile INT i = 0; i < 100; i++);
    }
}

FORCEINLINE VOID KeReleaseSpinLockFromDpcLevel(PKSPIN_LOCK SpinLock)
{
    __sync_lock_release((LONG*)SpinLock);
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================
 */

FORCEINLINE BOOLEAN IsValidSocket(SOCKET s)
{
    return (s != INVALID_SOCKET && s < MAX_WINSOCK_SOCKETS);
}

FORCEINLINE PSOCKET_ENTRY GetSocketEntry(SOCKET s)
{
    if (!IsValidSocket(s)) {
        return NULL;
    }
    return &g_SocketTable.Entries[s];
}

static VOID SetLastWSAError(INT error)
{
    g_LastWSAError = error;
}

INT GetLastWSAError(VOID)
{
    return g_LastWSAError;
}

/* ============================================================================
 * ERROR CODE TRANSLATION
 * ============================================================================
 *
 * Maps lwIP error codes (err_t) to Windows WSA error codes.
 */

static INT lwip_err_to_wsa_error(err_t err)
{
    switch (err) {
        case ERR_OK:
            return 0;
        case ERR_MEM:
            return WS2_32_WSAENOBUFS;
        case ERR_BUF:
            return WS2_32_WSAENOBUFS;
        case ERR_TIMEOUT:
            return WS2_32_WSAETIMEDOUT;
        case ERR_RTE:
            return WS2_32_WSAENETUNREACH;
        case ERR_INPROGRESS:
            return WS2_32_WSAEINPROGRESS;
        case ERR_VAL:
            return WS2_32_WSAEINVAL;
        case ERR_WOULDBLOCK:
            return WS2_32_WSAEWOULDBLOCK;
        case ERR_USE:
            return WS2_32_WSAEADDRINUSE;
        case ERR_ALREADY:
            return WS2_32_WSAEALREADY;
        case ERR_ISCONN:
            return WS2_32_WSAEISCONN;
        case ERR_CONN:
            return WS2_32_WSAENOTCONN;
        case ERR_IF:
            return WS2_32_WSAENETDOWN;
        case ERR_ABRT:
            return WS2_32_WSAECONNABORTED;
        case ERR_RST:
            return WS2_32_WSAECONNRESET;
        case ERR_CLSD:
            return WS2_32_WSAESHUTDOWN;
        case ERR_ARG:
            return WS2_32_WSAEFAULT;
        default:
            return WS2_32_WSAEOPNOTSUPP;
    }
}

static INT errno_to_wsa_error(INT err)
{
    switch (err) {
        case 0:
            return 0;
        case EBADF:
            return WS2_32_WSAEBADF;
        case EINVAL:
            return WS2_32_WSAEINVAL;
        case EACCES:
            return WS2_32_WSAEACCES;
        case EFAULT:
            return WS2_32_WSAEFAULT;
        case EMFILE:
            return WS2_32_WSAEMFILE;
        case EWOULDBLOCK:
            return WS2_32_WSAEWOULDBLOCK;
        case EINPROGRESS:
            return WS2_32_WSAEINPROGRESS;
        case EALREADY:
            return WS2_32_WSAEALREADY;
        case ENOTSOCK:
            return WS2_32_WSAENOTSOCK;
        case EDESTADDRREQ:
            return WS2_32_WSAEDESTADDRREQ;
        case EMSGSIZE:
            return WS2_32_WSAEMSGSIZE;
        case EPROTOTYPE:
            return WS2_32_WSAEPROTOTYPE;
        case ENOPROTOOPT:
            return WS2_32_WSAENOPROTOOPT;
        case EPROTONOSUPPORT:
            return WS2_32_WSAEPROTONOSUPPORT;
        case ESOCKTNOSUPPORT:
            return WS2_32_WSAESOCKTNOSUPPORT;
        case EOPNOTSUPP:
            return WS2_32_WSAEOPNOTSUPP;
        case EPFNOSUPPORT:
            return WS2_32_WSAEPFNOSUPPORT;
        case EAFNOSUPPORT:
            return WS2_32_WSAEAFNOSUPPORT;
        case EADDRINUSE:
            return WS2_32_WSAEADDRINUSE;
        case EADDRNOTAVAIL:
            return WS2_32_WSAEADDRNOTAVAIL;
        case ENETDOWN:
            return WS2_32_WSAENETDOWN;
        case ENETUNREACH:
            return WS2_32_WSAENETUNREACH;
        case ENETRESET:
            return WS2_32_WSAENETRESET;
        case ECONNABORTED:
            return WS2_32_WSAECONNABORTED;
        case ECONNRESET:
            return WS2_32_WSAECONNRESET;
        case ENOBUFS:
            return WS2_32_WSAENOBUFS;
        case EISCONN:
            return WS2_32_WSAEISCONN;
        case ENOTCONN:
            return WS2_32_WSAENOTCONN;
        case ESHUTDOWN:
            return WS2_32_WSAESHUTDOWN;
        case ETIMEDOUT:
            return WS2_32_WSAETIMEDOUT;
        case ECONNREFUSED:
            return WS2_32_WSAECONNREFUSED;
        case EHOSTDOWN:
            return WS2_32_WSAEHOSTDOWN;
        case EHOSTUNREACH:
            return WS2_32_WSAEHOSTUNREACH;
        default:
            return WS2_32_WSAEOPNOTSUPP;
    }
}

/* ============================================================================
 * ADDRESS FAMILY TRANSLATION
 * ============================================================================
 */

static INT ws2_af_to_lwip_af(INT af)
{
    switch (af) {
        case WS2_32_AF_INET:
            return AF_INET;
#if LWIP_IPV6
        case WS2_32_AF_INET6:
            return AF_INET6;
#endif
        case WS2_32_AF_UNSPEC:
        default:
            return AF_UNSPEC;
    }
    return AF_UNSPEC;  /* Never reached, silences compiler warning */
}

static INT lwip_af_to_ws2_af(INT af)
{
    switch (af) {
        case AF_INET:
            return WS2_32_AF_INET;
#if LWIP_IPV6
        case AF_INET6:
            return WS2_32_AF_INET6;
#endif
        case AF_UNSPEC:
        default:
            return WS2_32_AF_UNSPEC;
    }
    return WS2_32_AF_UNSPEC;  /* Never reached, silences compiler warning */
}

/* ============================================================================
 * SOCKET TYPE TRANSLATION
 * ============================================================================
 */

static INT ws2_socktype_to_lwip_socktype(INT type)
{
    switch (type) {
        case WS2_32_SOCKET_TYPE_STREAM:
            return SOCK_STREAM;
        case WS2_32_SOCKET_TYPE_DGRAM:
            return SOCK_DGRAM;
        case WS2_32_SOCKET_TYPE_RAW:
            return SOCK_RAW;
        default:
            return type; /* Pass through if already lwIP type */
    }
}

/* ============================================================================
 * TYPE TRANSLATION: Windows SOCKADDR to BSD sockaddr
 * ============================================================================
 */

VOID WinSock_SOCKADDR_to_lwip_sockaddr(
    IN CONST WS2_32_SOCKADDR *wsaddr,
    IN INT wsaddrlen,
    OUT struct sockaddr *lwipaddr,
    OUT socklen_t *lwipaddrlen
)
{
    if (!wsaddr || !lwipaddr || !lwipaddrlen) {
        if (lwipaddrlen) {
            *lwipaddrlen = 0;
        }
        return;
    }

    /* Translate address family */
    lwipaddr->sa_family = (sa_family_t)ws2_af_to_lwip_af((INT)wsaddr->sa_family);
    lwipaddr->sa_len = sizeof(struct sockaddr);

    /* Copy address data */
    if (lwipaddr->sa_family == AF_INET && wsaddrlen >= sizeof(struct sockaddr_in)) {
        /* IPv4 address conversion */
        struct sockaddr_in *sin = (struct sockaddr_in *)lwipaddr;
        CONST WS2_32_SOCKADDR_IN *wsin = (CONST WS2_32_SOCKADDR_IN *)wsaddr;
        
        sin->sin_family = AF_INET;
        sin->sin_port = wsin->sin_port;  /* Already in network byte order */
        sin->sin_addr.s_addr = 
            ((ULONG)wsin->sin_addr.s_b1) |
            (((ULONG)wsin->sin_addr.s_b2) << 8) |
            (((ULONG)wsin->sin_addr.s_b3) << 16) |
            (((ULONG)wsin->sin_addr.s_b4) << 24);
        memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
        *lwipaddrlen = sizeof(struct sockaddr_in);
    } else {
        /* Generic address handling */
        size_t copylen = (wsaddrlen < 14) ? wsaddrlen : 14;
        memcpy(lwipaddr->sa_data, wsaddr->sa_data, copylen);
        *lwipaddrlen = sizeof(struct sockaddr);
    }
}

/* ============================================================================
 * TYPE TRANSLATION: BSD sockaddr to Windows SOCKADDR
 * ============================================================================
 */

VOID lwip_sockaddr_to_WinSock_SOCKADDR(
    IN CONST struct sockaddr *lwipaddr,
    IN socklen_t lwipaddrlen,
    OUT WS2_32_SOCKADDR *wsaddr,
    IN OUT INT *wsaddrlen
)
{
    if (!wsaddr || !lwipaddr || !wsaddrlen) {
        return;
    }

    /* Clear output structure */
    memset(wsaddr, 0, sizeof(WS2_32_SOCKADDR));

    /* Translate address family */
    wsaddr->sa_family = (USHORT)lwip_af_to_ws2_af((INT)lwipaddr->sa_family);

    /* Handle specific address families */
    if (lwipaddr->sa_family == AF_INET && lwipaddrlen >= sizeof(struct sockaddr_in)) {
        /* IPv4 address conversion */
        CONST struct sockaddr_in *sin = (CONST struct sockaddr_in *)lwipaddr;
        WS2_32_SOCKADDR_IN *wsin = (WS2_32_SOCKADDR_IN *)wsaddr;
        
        wsin->sin_family = WS2_32_AF_INET;
        wsin->sin_port = sin->sin_port;  /* Already in network byte order */
        wsin->sin_addr.s_b1 = (UCHAR)(sin->sin_addr.s_addr & 0xFF);
        wsin->sin_addr.s_b2 = (UCHAR)((sin->sin_addr.s_addr >> 8) & 0xFF);
        wsin->sin_addr.s_b3 = (UCHAR)((sin->sin_addr.s_addr >> 16) & 0xFF);
        wsin->sin_addr.s_b4 = (UCHAR)((sin->sin_addr.s_addr >> 24) & 0xFF);
        memset(wsin->sin_zero, 0, sizeof(wsin->sin_zero));
        
        if (*wsaddrlen >= (INT)sizeof(WS2_32_SOCKADDR_IN)) {
            *wsaddrlen = sizeof(WS2_32_SOCKADDR_IN);
        } else {
            *wsaddrlen = sizeof(WS2_32_SOCKADDR);
        }
    } else {
        /* Generic address handling */
        size_t copylen = (lwipaddrlen < 14) ? lwipaddrlen : 14;
        memcpy(wsaddr->sa_data, lwipaddr->sa_data, copylen);
        *wsaddrlen = sizeof(WS2_32_SOCKADDR);
    }
}

/* ============================================================================
 * SOCKET TABLE INITIALIZATION
 * ============================================================================
 */

static NTSTATUS SocketTableInitialize(VOID)
{
    KIRQL OldIrql;
    
    if (g_SocketTable.Initialized) {
        return STATUS_SUCCESS;
    }

    KeAcquireSpinLock(&g_SocketTable.TableLock, &OldIrql);

    if (g_SocketTable.Initialized) {
        KeReleaseSpinLock(&g_SocketTable.TableLock, OldIrql);
        return STATUS_SUCCESS;
    }

    memset(g_SocketTable.Entries, 0, sizeof(g_SocketTable.Entries));
    g_SocketTable.SocketCount = 0;

    for (INT i = 0; i < MAX_WINSOCK_SOCKETS; i++) {
        g_SocketTable.Entries[i].State = SOCKET_STATE_FREE;
        g_SocketTable.Entries[i].lwip_socket = -1;
        g_SocketTable.Entries[i].Domain = 0;
        g_SocketTable.Entries[i].Type = 0;
        g_SocketTable.Entries[i].Protocol = 0;
        g_SocketTable.Entries[i].NonBlocking = FALSE;
        g_SocketTable.Entries[i].SelectEvents = 0;
        g_SocketTable.Entries[i].LastError = 0;
        /* Initialize entry spinlock */
        g_SocketTable.Entries[i].EntryLock = 0;
    }

    g_SocketTable.Initialized = TRUE;

    KeReleaseSpinLock(&g_SocketTable.TableLock, OldIrql);

    return STATUS_SUCCESS;
}

static SOCKET AllocateSocketHandle(VOID)
{
    KIRQL OldIrql;
    SOCKET s = INVALID_SOCKET;

    KeAcquireSpinLock(&g_SocketTable.TableLock, &OldIrql);

    for (INT i = 0; i < MAX_WINSOCK_SOCKETS; i++) {
        if (g_SocketTable.Entries[i].State == SOCKET_STATE_FREE) {
            g_SocketTable.Entries[i].State = SOCKET_STATE_ALLOCATED;
            s = (SOCKET)i;
            g_SocketTable.SocketCount++;
            break;
        }
    }

    KeReleaseSpinLock(&g_SocketTable.TableLock, OldIrql);

    return s;
}

static VOID FreeSocketHandle(SOCKET s)
{
    KIRQL OldIrql;
    PSOCKET_ENTRY entry;

    if (!IsValidSocket(s)) {
        return;
    }

    entry = GetSocketEntry(s);

    KeAcquireSpinLock(&g_SocketTable.TableLock, &OldIrql);

    if (entry->State != SOCKET_STATE_FREE) {
        entry->State = SOCKET_STATE_FREE;
        entry->lwip_socket = -1;
        entry->Domain = 0;
        entry->Type = 0;
        entry->Protocol = 0;
        entry->NonBlocking = FALSE;
        entry->SelectEvents = 0;
        entry->LastError = 0;
        if (g_SocketTable.SocketCount > 0) {
            g_SocketTable.SocketCount--;
        }
    }

    KeReleaseSpinLock(&g_SocketTable.TableLock, OldIrql);
}

/* ============================================================================
 * WINSOCK API IMPLEMENTATION
 * ============================================================================
 */

/*
 * WS2_32_WSAStartup - Initialize WinSock library
 */
INT WS2_32_WSAStartup(WORD wVersionRequested, PVOID lpWSAData)
{
    UNREFERENCED_PARAMETER(wVersionRequested);
    UNREFERENCED_PARAMETER(lpWSAData);

    NTSTATUS status = SocketTableInitialize();
    if (!NT_SUCCESS(status)) {
        SetLastWSAError(WS2_32_WSAEPROCLIM);
        return WS2_32_WSAEPROCLIM;
    }

    return 0;
}

/*
 * WS2_32_WSACleanup - Terminate WinSock library
 */
INT WS2_32_WSACleanup(VOID)
{
    /* Nothing to clean up at this level */
    return 0;
}

/*
 * WS2_32_socket - Create a new socket
 */
SOCKET WS2_32_socket(INT af, INT type, INT protocol)
{
    SOCKET s;
    PSOCKET_ENTRY entry;
    INT lwip_af, lwip_type, lwip_sock;

    /* Initialize socket table if needed */
    SocketTableInitialize();

    /* Allocate WinSock socket handle */
    s = AllocateSocketHandle();
    if (s == INVALID_SOCKET) {
        SetLastWSAError(WS2_32_WSAEMFILE);
        return INVALID_SOCKET;
    }

    entry = GetSocketEntry(s);

    /* Convert Windows parameters to lwIP parameters */
    lwip_af = ws2_af_to_lwip_af(af);
    lwip_type = ws2_socktype_to_lwip_socktype(type);

    /* Create lwIP socket */
    lwip_sock = lwip_socket(lwip_af, lwip_type, protocol);
    if (lwip_sock < 0) {
        FreeSocketHandle(s);
        SetLastWSAError(errno_to_wsa_error(errno));
        return INVALID_SOCKET;
    }

    /* Store socket information */
    entry->lwip_socket = lwip_sock;
    entry->Domain = af;
    entry->Type = type;
    entry->Protocol = protocol;
    entry->State = SOCKET_STATE_ALLOCATED;

    return s;
}

/*
 * WS2_32_closesocket - Close a socket
 */
INT WS2_32_closesocket(SOCKET s)
{
    PSOCKET_ENTRY entry;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Close lwIP socket */
    if (entry->lwip_socket >= 0) {
        result = lwip_close(entry->lwip_socket);
        if (result < 0) {
            SetLastWSAError(errno_to_wsa_error(errno));
            return SOCKET_ERROR;
        }
    }

    /* Free WinSock socket handle */
    FreeSocketHandle(s);

    return 0;
}

/*
 * WS2_32_shutdown - Shut down socket send/receive operations
 */
INT WS2_32_shutdown(SOCKET s, INT how)
{
    PSOCKET_ENTRY entry;
    INT lwip_how;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Convert Windows shutdown constant to lwIP */
    switch (how) {
        case WS2_32_SD_RECEIVE:
            lwip_how = SHUT_RD;
            break;
        case WS2_32_SD_SEND:
            lwip_how = SHUT_WR;
            break;
        case WS2_32_SD_BOTH:
            lwip_how = SHUT_RDWR;
            break;
        default:
            SetLastWSAError(WS2_32_WSAEINVAL);
            return SOCKET_ERROR;
    }

    result = lwip_shutdown(entry->lwip_socket, lwip_how);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    if (how == WS2_32_SD_BOTH) {
        entry->State = SOCKET_STATE_CLOSING;
    }

    return 0;
}

/*
 * WS2_32_bind - Bind a socket to a local address
 */
INT WS2_32_bind(SOCKET s, CONST WS2_32_SOCKADDR *name, INT namelen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!name) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate Windows address to lwIP address */
    memset(&lwip_addr, 0, sizeof(lwip_addr));
    WinSock_SOCKADDR_to_lwip_sockaddr(name, namelen, 
                                       (struct sockaddr *)&lwip_addr, 
                                       &lwip_addrlen);

    result = lwip_bind(entry->lwip_socket, (struct sockaddr *)&lwip_addr, lwip_addrlen);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    entry->State = SOCKET_STATE_BOUND;

    return 0;
}

/*
 * WS2_32_connect - Connect a socket to a remote address
 */
INT WS2_32_connect(SOCKET s, CONST WS2_32_SOCKADDR *name, INT namelen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!name) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate Windows address to lwIP address */
    memset(&lwip_addr, 0, sizeof(lwip_addr));
    WinSock_SOCKADDR_to_lwip_sockaddr(name, namelen, 
                                       (struct sockaddr *)&lwip_addr, 
                                       &lwip_addrlen);

    result = lwip_connect(entry->lwip_socket, (struct sockaddr *)&lwip_addr, lwip_addrlen);
    if (result < 0) {
        INT err = errno;
        if (err == EINPROGRESS) {
            entry->State = SOCKET_STATE_CONNECTING;
        }
        SetLastWSAError(errno_to_wsa_error(err));
        return SOCKET_ERROR;
    }

    entry->State = SOCKET_STATE_CONNECTED;

    return 0;
}

/*
 * WS2_32_listen - Listen for incoming connections
 */
INT WS2_32_listen(SOCKET s, INT backlog)
{
    PSOCKET_ENTRY entry;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    result = lwip_listen(entry->lwip_socket, backlog);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    entry->State = SOCKET_STATE_LISTENING;

    return 0;
}

/*
 * WS2_32_accept - Accept an incoming connection
 */
SOCKET WS2_32_accept(SOCKET s, WS2_32_SOCKADDR *addr, INT *addrlen)
{
    PSOCKET_ENTRY entry, new_entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen = sizeof(lwip_addr);
    INT new_lwip_sock;
    SOCKET new_s;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return INVALID_SOCKET;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return INVALID_SOCKET;
    }

    if (entry->State != SOCKET_STATE_LISTENING) {
        SetLastWSAError(WS2_32_WSAEINVAL);
        return INVALID_SOCKET;
    }

    /* Allocate new WinSock socket for the accepted connection */
    new_s = AllocateSocketHandle();
    if (new_s == INVALID_SOCKET) {
        SetLastWSAError(WS2_32_WSAEMFILE);
        return INVALID_SOCKET;
    }

    new_entry = GetSocketEntry(new_s);

    /* Accept connection via lwIP */
    new_lwip_sock = lwip_accept(entry->lwip_socket, 
                                 (struct sockaddr *)&lwip_addr, 
                                 &lwip_addrlen);
    if (new_lwip_sock < 0) {
        FreeSocketHandle(new_s);
        SetLastWSAError(errno_to_wsa_error(errno));
        return INVALID_SOCKET;
    }

    /* Translate lwIP address back to Windows format if requested */
    if (addr && addrlen && *addrlen > 0) {
        lwip_sockaddr_to_WinSock_SOCKADDR((struct sockaddr *)&lwip_addr, lwip_addrlen,
                                           addr, addrlen);
    }

    /* Initialize new socket entry */
    new_entry->lwip_socket = new_lwip_sock;
    new_entry->Domain = entry->Domain;
    new_entry->Type = entry->Type;
    new_entry->Protocol = entry->Protocol;
    new_entry->State = SOCKET_STATE_CONNECTED;

    return new_s;
}

/*
 * WS2_32_send - Send data on a connected socket
 */
INT WS2_32_send(SOCKET s, CONST CHAR *buf, INT len, INT flags)
{
    PSOCKET_ENTRY entry;
    INT result;
    INT lwip_flags = 0;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!buf) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate flags */
    if (flags & WS2_32_MSG_PEEK) {
        lwip_flags |= MSG_PEEK;
    }
    if (flags & WS2_32_MSG_DONTROUTE) {
        lwip_flags |= MSG_DONTROUTE;
    }
    if (flags & WS2_32_MSG_OOB) {
        lwip_flags |= MSG_OOB;
    }

    result = (INT)lwip_send(entry->lwip_socket, buf, (size_t)len, lwip_flags);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return result;
}

/*
 * WS2_32_sendto - Send data to a specific address
 */
INT WS2_32_sendto(SOCKET s, CONST CHAR *buf, INT len, INT flags,
                   CONST WS2_32_SOCKADDR *to, INT tolen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen;
    INT result;
    INT lwip_flags = 0;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!buf) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate flags */
    if (flags & WS2_32_MSG_PEEK) {
        lwip_flags |= MSG_PEEK;
    }
    if (flags & WS2_32_MSG_DONTROUTE) {
        lwip_flags |= MSG_DONTROUTE;
    }
    if (flags & WS2_32_MSG_OOB) {
        lwip_flags |= MSG_OOB;
    }

    /* Translate destination address if provided */
    if (to) {
        memset(&lwip_addr, 0, sizeof(lwip_addr));
        WinSock_SOCKADDR_to_lwip_sockaddr(to, tolen, 
                                           (struct sockaddr *)&lwip_addr, 
                                           &lwip_addrlen);
        result = (INT)lwip_sendto(entry->lwip_socket, buf, (size_t)len, lwip_flags,
                                   (struct sockaddr *)&lwip_addr, lwip_addrlen);
    } else {
        result = (INT)lwip_send(entry->lwip_socket, buf, (size_t)len, lwip_flags);
    }

    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return result;
}

/*
 * WS2_32_recv - Receive data from a connected socket
 */
INT WS2_32_recv(SOCKET s, CHAR *buf, INT len, INT flags)
{
    PSOCKET_ENTRY entry;
    INT result;
    INT lwip_flags = 0;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!buf && len > 0) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate flags */
    if (flags & WS2_32_MSG_PEEK) {
        lwip_flags |= MSG_PEEK;
    }
    if (flags & WS2_32_MSG_OOB) {
        lwip_flags |= MSG_OOB;
    }
    if (flags & WS2_32_MSG_WAITALL) {
        lwip_flags |= MSG_WAITALL;
    }
    if (entry->NonBlocking || (flags & WS2_32_MSG_DONTROUTE)) {
        lwip_flags |= MSG_DONTWAIT;
    }

    result = (INT)lwip_recv(entry->lwip_socket, buf, (size_t)len, lwip_flags);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return result;
}

/*
 * WS2_32_recvfrom - Receive data and source address
 */
INT WS2_32_recvfrom(SOCKET s, CHAR *buf, INT len, INT flags,
                     WS2_32_SOCKADDR *from, INT *fromlen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen = sizeof(lwip_addr);
    INT result;
    INT lwip_flags = 0;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!buf && len > 0) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate flags */
    if (flags & WS2_32_MSG_PEEK) {
        lwip_flags |= MSG_PEEK;
    }
    if (flags & WS2_32_MSG_OOB) {
        lwip_flags |= MSG_OOB;
    }
    if (flags & WS2_32_MSG_WAITALL) {
        lwip_flags |= MSG_WAITALL;
    }
    if (entry->NonBlocking) {
        lwip_flags |= MSG_DONTWAIT;
    }

    result = (INT)lwip_recvfrom(entry->lwip_socket, buf, (size_t)len, lwip_flags,
                                 (struct sockaddr *)&lwip_addr, &lwip_addrlen);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    /* Translate source address to Windows format if requested */
    if (from && fromlen && *fromlen > 0) {
        lwip_sockaddr_to_WinSock_SOCKADDR((struct sockaddr *)&lwip_addr, lwip_addrlen,
                                           from, fromlen);
    }

    return result;
}

/*
 * WS2_32_setsockopt - Set socket options
 */
INT WS2_32_setsockopt(SOCKET s, INT level, INT optname, 
                       CONST CHAR *optval, INT optlen)
{
    PSOCKET_ENTRY entry;
    INT lwip_level;
    INT lwip_optname;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!optval && optlen > 0) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    /* Translate option level */
    if (level == WS2_32_SOL_SOCKET) {
        lwip_level = SOL_SOCKET;
        
        /* Translate Windows socket options to lwIP options */
        switch (optname) {
            case WS2_32_SO_DEBUG:
                lwip_optname = SO_DEBUG;
                break;
            case WS2_32_SO_REUSEADDR:
                lwip_optname = SO_REUSEADDR;
                break;
            case WS2_32_SO_KEEPALIVE:
                lwip_optname = SO_KEEPALIVE;
                break;
            case WS2_32_SO_DONTROUTE:
                lwip_optname = SO_DONTROUTE;
                break;
            case WS2_32_SO_BROADCAST:
                lwip_optname = SO_BROADCAST;
                break;
            case WS2_32_SO_LINGER:
                lwip_optname = SO_LINGER;
                break;
            case WS2_32_SO_OOBINLINE:
                lwip_optname = SO_OOBINLINE;
                break;
            case WS2_32_SO_SNDBUF:
                lwip_optname = SO_SNDBUF;
                break;
            case WS2_32_SO_RCVBUF:
                lwip_optname = SO_RCVBUF;
                break;
            case WS2_32_SO_SNDTIMEO:
                lwip_optname = SO_SNDTIMEO;
                break;
            case WS2_32_SO_RCVTIMEO:
                lwip_optname = SO_RCVTIMEO;
                break;
            case WS2_32_SO_ERROR:
                /* SO_ERROR is read-only, can't be set */
                SetLastWSAError(WS2_32_WSAENOPROTOOPT);
                return SOCKET_ERROR;
            case WS2_32_SO_TYPE:
                /* SO_TYPE is read-only, can't be set */
                SetLastWSAError(WS2_32_WSAENOPROTOOPT);
                return SOCKET_ERROR;
            default:
                SetLastWSAError(WS2_32_WSAENOPROTOOPT);
                return SOCKET_ERROR;
        }
    } else if (level == WS2_32_AF_INET) {
        lwip_level = IPPROTO_IP;
        lwip_optname = optname;  /* Pass through for IP options */
    } else {
        /* Other levels - pass through as-is */
        lwip_level = level;
        lwip_optname = optname;
    }

    result = lwip_setsockopt(entry->lwip_socket, lwip_level, lwip_optname, optval, optlen);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return 0;
}

/*
 * WS2_32_getsockopt - Get socket options
 */
INT WS2_32_getsockopt(SOCKET s, INT level, INT optname,
                       CHAR *optval, INT *optlen)
{
    PSOCKET_ENTRY entry;
    INT lwip_level;
    INT lwip_optname;
    socklen_t lwip_optlen;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!optval || !optlen) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    lwip_optlen = (socklen_t)*optlen;

    /* Translate option level */
    if (level == WS2_32_SOL_SOCKET) {
        lwip_level = SOL_SOCKET;
        
        switch (optname) {
            case WS2_32_SO_DEBUG:
                lwip_optname = SO_DEBUG;
                break;
            case WS2_32_SO_REUSEADDR:
                lwip_optname = SO_REUSEADDR;
                break;
            case WS2_32_SO_KEEPALIVE:
                lwip_optname = SO_KEEPALIVE;
                break;
            case WS2_32_SO_DONTROUTE:
                lwip_optname = SO_DONTROUTE;
                break;
            case WS2_32_SO_BROADCAST:
                lwip_optname = SO_BROADCAST;
                break;
            case WS2_32_SO_LINGER:
                lwip_optname = SO_LINGER;
                break;
            case WS2_32_SO_OOBINLINE:
                lwip_optname = SO_OOBINLINE;
                break;
            case WS2_32_SO_SNDBUF:
                lwip_optname = SO_SNDBUF;
                break;
            case WS2_32_SO_RCVBUF:
                lwip_optname = SO_RCVBUF;
                break;
            case WS2_32_SO_SNDTIMEO:
                lwip_optname = SO_SNDTIMEO;
                break;
            case WS2_32_SO_RCVTIMEO:
                lwip_optname = SO_RCVTIMEO;
                break;
            case WS2_32_SO_ERROR:
                lwip_optname = SO_ERROR;
                break;
            case WS2_32_SO_TYPE:
                lwip_optname = SO_TYPE;
                break;
            default:
                SetLastWSAError(WS2_32_WSAENOPROTOOPT);
                return SOCKET_ERROR;
        }
    } else {
        lwip_level = level;
        lwip_optname = optname;
    }

    result = lwip_getsockopt(entry->lwip_socket, lwip_level, lwip_optname, optval, &lwip_optlen);
    *optlen = (INT)lwip_optlen;
    
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return 0;
}

/*
 * WS2_32_ioctlsocket - Control socket I/O mode
 */
INT WS2_32_ioctlsocket(SOCKET s, LONG cmd, ULONG *argp)
{
    PSOCKET_ENTRY entry;
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!argp) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    switch (cmd) {
        case WS2_32_FIONBIO:
            /* Set non-blocking mode */
            entry->NonBlocking = (*argp != 0);
            result = lwip_ioctl(entry->lwip_socket, FIONBIO, argp);
            break;
            
        case WS2_32_FIONREAD:
            /* Get number of bytes available to read */
            result = lwip_ioctl(entry->lwip_socket, FIONREAD, argp);
            break;
            
        case WS2_32_SIOCATMARK:
            /* Check if at OOB mark (not fully supported by lwIP) */
            *argp = 0;  /* Not at OOB mark */
            result = 0;
            break;
            
        default:
            /* Try lwIP ioctl directly for other commands */
            result = lwip_ioctl(entry->lwip_socket, cmd, argp);
            break;
    }

    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    return 0;
}

/*
 * WS2_32_select - Synchronous I/O multiplexing
 *
 * This is a complex function that needs to translate Windows fd_set
 * structures to lwIP fd_set structures.
 */
INT WS2_32_select(INT nfds, WS2_32_FD_SET *readfds, WS2_32_FD_SET *writefds,
                  WS2_32_FD_SET *exceptfds, CONST WS2_32_TIMEVAL *timeout)
{
    fd_set lwip_readfds, lwip_writefds, lwip_exceptfds;
    struct timeval lwip_timeout;
    INT result;
    INT max_lwip_fd = -1;
    INT i, j;
    WS2_32_FD_SET out_readfds, out_writefds, out_exceptfds;
    BOOLEAN found;

    /* Initialize output fd_sets */
    out_readfds.fd_count = 0;
    out_writefds.fd_count = 0;
    out_exceptfds.fd_count = 0;

    /* Initialize lwIP fd_sets */
    FD_ZERO(&lwip_readfds);
    FD_ZERO(&lwip_writefds);
    FD_ZERO(&lwip_exceptfds);

    /* Translate Windows fd_sets to lwIP fd_sets */
    if (readfds) {
        for (i = 0; i < (INT)readfds->fd_count; i++) {
            SOCKET ws_socket = readfds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->State != SOCKET_STATE_FREE && entry->lwip_socket >= 0) {
                FD_SET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_readfds);
                if (entry->lwip_socket > max_lwip_fd) {
                    max_lwip_fd = entry->lwip_socket;
                }
            }
        }
    }

    if (writefds) {
        for (i = 0; i < (INT)writefds->fd_count; i++) {
            SOCKET ws_socket = writefds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->State != SOCKET_STATE_FREE && entry->lwip_socket >= 0) {
                FD_SET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_writefds);
                if (entry->lwip_socket > max_lwip_fd) {
                    max_lwip_fd = entry->lwip_socket;
                }
            }
        }
    }

    if (exceptfds) {
        for (i = 0; i < (INT)exceptfds->fd_count; i++) {
            SOCKET ws_socket = exceptfds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->State != SOCKET_STATE_FREE && entry->lwip_socket >= 0) {
                FD_SET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_exceptfds);
                if (entry->lwip_socket > max_lwip_fd) {
                    max_lwip_fd = entry->lwip_socket;
                }
            }
        }
    }

    /* Convert timeout structure */
    if (timeout) {
        lwip_timeout.tv_sec = timeout->tv_sec;
        lwip_timeout.tv_usec = timeout->tv_usec;
    }

    /* Call lwIP select */
    result = lwip_select(max_lwip_fd + LWIP_SOCKET_OFFSET + 1,
                          readfds ? &lwip_readfds : NULL,
                          writefds ? &lwip_writefds : NULL,
                          exceptfds ? &lwip_exceptfds : NULL,
                          timeout ? &lwip_timeout : NULL);

    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    /* Translate lwIP fd_sets back to Windows fd_sets */
    if (readfds) {
        for (i = 0; i < (INT)readfds->fd_count; i++) {
            SOCKET ws_socket = readfds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->lwip_socket >= 0 &&
                FD_ISSET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_readfds)) {
                out_readfds.fd_array[out_readfds.fd_count++] = ws_socket;
            }
        }
        memcpy(readfds, &out_readfds, sizeof(WS2_32_FD_SET));
    }

    if (writefds) {
        for (i = 0; i < (INT)writefds->fd_count; i++) {
            SOCKET ws_socket = writefds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->lwip_socket >= 0 &&
                FD_ISSET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_writefds)) {
                out_writefds.fd_array[out_writefds.fd_count++] = ws_socket;
            }
        }
        memcpy(writefds, &out_writefds, sizeof(WS2_32_FD_SET));
    }

    if (exceptfds) {
        for (i = 0; i < (INT)exceptfds->fd_count; i++) {
            SOCKET ws_socket = exceptfds->fd_array[i];
            PSOCKET_ENTRY entry = GetSocketEntry(ws_socket);
            
            if (entry && entry->lwip_socket >= 0 &&
                FD_ISSET(entry->lwip_socket + LWIP_SOCKET_OFFSET, &lwip_exceptfds)) {
                out_exceptfds.fd_array[out_exceptfds.fd_count++] = ws_socket;
            }
        }
        memcpy(exceptfds, &out_exceptfds, sizeof(WS2_32_FD_SET));
    }

    return result;
}

/*
 * WS2_32_getpeername - Get address of connected peer
 */
INT WS2_32_getpeername(SOCKET s, WS2_32_SOCKADDR *name, INT *namelen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen = sizeof(lwip_addr);
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!name || !namelen) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    result = lwip_getpeername(entry->lwip_socket, 
                               (struct sockaddr *)&lwip_addr, 
                               &lwip_addrlen);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    /* Translate to Windows format */
    lwip_sockaddr_to_WinSock_SOCKADDR((struct sockaddr *)&lwip_addr, lwip_addrlen,
                                       name, namelen);

    return 0;
}

/*
 * WS2_32_getsockname - Get local address of socket
 */
INT WS2_32_getsockname(SOCKET s, WS2_32_SOCKADDR *name, INT *namelen)
{
    PSOCKET_ENTRY entry;
    struct sockaddr_storage lwip_addr;
    socklen_t lwip_addrlen = sizeof(lwip_addr);
    INT result;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    if (!name || !namelen) {
        SetLastWSAError(WS2_32_WSAEFAULT);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    result = lwip_getsockname(entry->lwip_socket,
                               (struct sockaddr *)&lwip_addr,
                               &lwip_addrlen);
    if (result < 0) {
        SetLastWSAError(errno_to_wsa_error(errno));
        return SOCKET_ERROR;
    }

    /* Translate to Windows format */
    lwip_sockaddr_to_WinSock_SOCKADDR((struct sockaddr *)&lwip_addr, lwip_addrlen,
                                       name, namelen);

    return 0;
}

/*
 * WS2_32_WSAGetLastError - Get last error code
 */
INT WS2_32_WSAGetLastError(VOID)
{
    return GetLastWSAError();
}

/*
 * WS2_32_WSASetLastError - Set last error code
 */
VOID WS2_32_WSASetLastError(INT iError)
{
    SetLastWSAError(iError);
}

/*
 * WS2_32_WSAIsBlocking - Check if socket is blocking
 */
INT WS2_32_WSAIsBlocking(SOCKET s)
{
    PSOCKET_ENTRY entry;

    if (!IsValidSocket(s)) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    entry = GetSocketEntry(s);

    if (entry->State == SOCKET_STATE_FREE) {
        SetLastWSAError(WS2_32_WSAENOTSOCK);
        return SOCKET_ERROR;
    }

    return entry->NonBlocking ? 0 : 1;
}

/*
 * WS2_32_WSAUnhookBlockingHook - Unhook blocking hook (stub)
 */
INT WS2_32_WSAUnhookBlockingHook(VOID)
{
    /* Not implemented - return success */
    return 0;
}

/*
 * FD_SET macros for Windows fd_set
 */
VOID WS2_32_FD_SET_Internal(SOCKET fd, WS2_32_FD_SET *set)
{
    UINT i;
    
    if (!set || fd == INVALID_SOCKET) {
        return;
    }
    
    /* Check if already in set */
    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd) {
            return;
        }
    }
    
    /* Add to set if not full */
    if (set->fd_count < FD_SETSIZE) {
        set->fd_array[set->fd_count++] = fd;
    }
}

VOID WS2_32_FD_CLR_Internal(SOCKET fd, WS2_32_FD_SET *set)
{
    UINT i;
    
    if (!set || fd == INVALID_SOCKET) {
        return;
    }
    
    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd) {
            /* Shift remaining elements */
            while (i < set->fd_count - 1) {
                set->fd_array[i] = set->fd_array[i + 1];
                i++;
            }
            set->fd_count--;
            return;
        }
    }
}

INT WS2_32_FD_ISSET_Internal(SOCKET fd, WS2_32_FD_SET *set)
{
    UINT i;
    
    if (!set || fd == INVALID_SOCKET) {
        return 0;
    }
    
    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd) {
            return 1;
        }
    }
    
    return 0;
}

VOID WS2_32_FD_ZERO_Internal(WS2_32_FD_SET *set)
{
    if (set) {
        set->fd_count = 0;
    }
}

/*
 * htons - Convert short from host to network byte order
 */
USHORT WS2_32_htons(USHORT hostshort)
{
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

/*
 * ntohs - Convert short from network to host byte order
 */
USHORT WS2_32_ntohs(USHORT netshort)
{
    return WS2_32_htons(netshort);  /* Same operation */
}

/*
 * htonl - Convert long from host to network byte order
 */
ULONG WS2_32_htonl(ULONG hostlong)
{
    return ((hostlong & 0x000000FF) << 24) |
           ((hostlong & 0x0000FF00) << 8) |
           ((hostlong & 0x00FF0000) >> 8) |
           ((hostlong & 0xFF000000) >> 24);
}

/*
 * ntohl - Convert long from network to host byte order
 */
ULONG WS2_32_ntohl(ULONG netlong)
{
    return WS2_32_htonl(netlong);  /* Same operation */
}

/*
 * inet_addr - Convert dotted decimal string to network address
 */
ULONG WS2_32_inet_addr(CONST CHAR *cp)
{
    UINT b1, b2, b3, b4;
    
    if (!cp) {
        return (ULONG)-1;  /* INADDR_NONE */
    }
    
    if (sscanf(cp, "%u.%u.%u.%u", &b1, &b2, &b3, &b4) == 4) {
        if (b1 <= 255 && b2 <= 255 && b3 <= 255 && b4 <= 255) {
            return ((b1) | (b2 << 8) | (b3 << 16) | (b4 << 24));
        }
    }
    
    return (ULONG)-1;  /* INADDR_NONE */
}

/*
 * inet_ntoa - Convert network address to dotted decimal string
 * Note: This uses a static buffer - not thread safe!
 */
CHAR *WS2_32_inet_ntoa(struct in_addr in)
{
    static CHAR buffer[16];
    UCHAR *p = (UCHAR *)&in.s_addr;
    
    snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", 
             p[0], p[1], p[2], p[3]);
    
    return buffer;
}

/*
 * WS2_32_inet_ntop - Convert network address to presentation format
 */
PCSTR WS2_32_inet_ntop(INT family, PVOID pAddr, PSTR pStringBuf, SIZE_T StringBufSize)
{
    INT af;
    
    af = ws2_af_to_lwip_af(family);
    return lwip_inet_ntop(af, pAddr, pStringBuf, (socklen_t)StringBufSize);
}

/*
 * WS2_32_inet_pton - Convert presentation format to network address
 */
INT WS2_32_inet_pton(INT family, PCSTR pszAddrString, PVOID pAddrBuf)
{
    INT af;
    INT result;
    
    af = ws2_af_to_lwip_af(family);
    result = lwip_inet_pton(af, pszAddrString, pAddrBuf);
    
    if (result < 0) {
        SetLastWSAError(WS2_32_WSAEAFNOSUPPORT);
        return -1;
    }
    
    return result;
}

/*
 * WS2_32___WSAFDIsSet - Check if socket is in fd_set (internal function)
 */
INT WS2_32___WSAFDIsSet(SOCKET s, WS2_32_FD_SET *set)
{
    return WS2_32_FD_ISSET_Internal(s, set);
}

/* ============================================================================
 * INITIALIZATION AND CLEANUP
 * ============================================================================
 */

/*
 * Ws2_32Init - Initialize the WinSock subsystem
 */
NTSTATUS Ws2_32Init(VOID)
{
    return SocketTableInitialize();
}

/*
 * Ws2_32Cleanup - Cleanup the WinSock subsystem
 */
VOID Ws2_32Cleanup(VOID)
{
    INT i;
    KIRQL OldIrql;
    
    KeAcquireSpinLock(&g_SocketTable.TableLock, &OldIrql);
    
    /* Close all remaining sockets */
    for (i = 0; i < MAX_WINSOCK_SOCKETS; i++) {
        if (g_SocketTable.Entries[i].State != SOCKET_STATE_FREE &&
            g_SocketTable.Entries[i].lwip_socket >= 0) {
            lwip_close(g_SocketTable.Entries[i].lwip_socket);
            g_SocketTable.Entries[i].State = SOCKET_STATE_FREE;
        }
    }
    
    g_SocketTable.Initialized = FALSE;
    
    KeReleaseSpinLock(&g_SocketTable.TableLock, OldIrql);
}

/* ============================================================================
 * EXPORT TABLE
 * ============================================================================
 *
 * These are the functions that would be exported from ws2_32.dll on Windows.
 * For the MinNT kernel, they're available as direct function calls.
 */

/*
 * Standard BSD function names exported with WSA prefix compatibility:
 * - accept
 * - bind
 * - closesocket
 * - connect
 * - getpeername
 * - getsockname
 * - getsockopt
 * - htonl
 * - htons
 * - inet_addr
 * - inet_ntoa
 * - ioctlsocket
 * - listen
 * - ntohl
 * - ntohs
 * - recv
 * - recvfrom
 * - select
 * - send
 * - sendto
 * - setsockopt
 * - shutdown
 * - socket
 * 
 * Windows-specific extensions:
 * - WSAStartup
 * - WSACleanup
 * - WSAGetLastError
 * - WSASetLastError
 * - WSAIsBlocking
 * - WSAUnhookBlockingHook
 * - inet_ntop
 * - inet_pton
 */

/* End of ws2_32.c */
