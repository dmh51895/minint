/*
 * MinNT - ke/exports/ws2_32_exports.c
 * ws2_32.dll exports — Winsock2 socket API.
 * Routes to lwIP TCP/IP stack via a thin kernel-side API.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/exe.h>
#include <nt/rtl.h>
#include <ndk/obfuncs.h>

#ifndef UINT
typedef unsigned int UINT;
#endif
typedef ULONG_PTR SOCKET;

/* Socket types */
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define AF_INET        2
#define AF_INET6       23
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17
#define SOL_SOCKET     0xFFFF

/* Socket options */
#define SO_REUSEADDR   0x04
#define SO_KEEPALIVE   0x08
#define SO_RCVBUF      0x08
#define SO_SNDBUF      0x01
#define SO_ERROR       0x10

/* Winsock error codes */
#define WSAEWOULDBLOCK 10035
#define WSAEINVAL      10022
#define WSAECONNREFUSED 10061
#define WSAECONNRESET  10054

/* Internal socket struct — wraps lwIP's struct netconn */
typedef struct _SW_SOCKET {
    ULONG_PTR   handle;     /* lwIP internal handle */
    INT         type;
    INT         family;
    INT         proto;
    ULONG       last_error;
} SW_SOCKET;

/* Simple socket table (256 sockets max) */
#define WS2_MAX_SOCKETS 256
static SW_SOCKET g_Sockets[WS2_MAX_SOCKETS];
static KSPIN_LOCK g_Ws2Lock;
static BOOLEAN g_Ws2Init = FALSE;

static INT SwAllocSocketSlot(SW_SOCKET **out)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_Ws2Lock, &irql);
    for (INT i = 0; i < WS2_MAX_SOCKETS; i++) {
        if (g_Sockets[i].handle == 0) {
            RtlZeroMemory(&g_Sockets[i], sizeof(SW_SOCKET));
            g_Sockets[i].handle = (ULONG_PTR)(i + 1); /* mark as in use */
            KeReleaseSpinLock(&g_Ws2Lock, irql);
            *out = &g_Sockets[i];
            return i;
        }
    }
    KeReleaseSpinLock(&g_Ws2Lock, irql);
    return -1;
}

static SW_SOCKET *SwGetSocket(SOCKET s)
{
    INT idx = (INT)s - 1;
    if (idx < 0 || idx >= WS2_MAX_SOCKETS) return NULL;
    if (g_Sockets[idx].handle == 0) return NULL;
    return &g_Sockets[idx];
}

VOID NTAPI Ws2_32RegisterExports_Init(VOID)
{
    RtlZeroMemory(g_Sockets, sizeof(g_Sockets));
    KeInitializeSpinLock(&g_Ws2Lock);
    g_Ws2Init = TRUE;
    DbgPrint("WS2_32: socket table initialized\n");
}

/* ============================================================================
 * Winsock API — lwIP backend
 *
 * Note: The lwIP kernel port provides a minimalistically-shaped API
 * in tcpip/. For this pass, we implement socket connect/send/recv that
 * route to a stub that returns WSAEWOULDBLOCK. Real networking requires
 * extending the lwIP port (next-pass work).
 * ========================================================================== */

__attribute__((ms_abi))
static INT WSAStartup_msabi(ULONG wVersionRequested, PVOID lpWSAData)
{
    /* WSADATA struct: 400 bytes. Fill in some defaults. */
    if (lpWSAData) {
        USHORT *p = (USHORT *)lpWSAData;
        p[0] = 0x0202;  /* wVersion = 2.2 */
        p[1] = 0x0202;  /* wHighVersion = 2.2 */
        CHAR *desc = (CHAR *)((PUCHAR)lpWSAData + 4);
        const CHAR *filler = "MinNT WinSock 2.2 (lwIP)";
        INT i = 0;
        while (filler[i] && i < 256) { desc[i] = filler[i]; i++; }
        desc[i] = 0;
    }
    return 0; /* success */
}

__attribute__((ms_abi))
static INT WSACleanup_msabi(VOID)
{
    return 0;
}

__attribute__((ms_abi))
static INT WSAGetLastError_msabi(VOID)
{
    return WSAEWOULDBLOCK;
}

__attribute__((ms_abi))
static SOCKET socket_msabi(INT af, INT type, INT protocol)
{
    (void)af; (void)protocol;
    SW_SOCKET *s;
    INT idx = SwAllocSocketSlot(&s);
    if (idx < 0) return (SOCKET)(ULONG_PTR)(~0ULL); /* INVALID_SOCKET */
    s->type = type;
    s->family = af;
    s->proto = protocol;
    return (SOCKET)(idx + 1);
}

__attribute__((ms_abi))
static INT closesocket_msabi(SOCKET s)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    sock->handle = 0;
    return 0;
}

__attribute__((ms_abi))
static INT bind_msabi(SOCKET s, PVOID name, INT namelen)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    /* bind is a no-op for our kernel-side sockets */
    (void)name; (void)namelen;
    return 0;
}

__attribute__((ms_abi))
static INT listen_msabi(SOCKET s, INT backlog)
{
    (void)s; (void)backlog;
    return 0;
}

__attribute__((ms_abi))
static SOCKET accept_msabi(SOCKET s, PVOID addr, PINT addrlen)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return (SOCKET)(ULONG_PTR)(~0ULL);
    /* No incoming connections yet */
    sock->last_error = WSAEWOULDBLOCK;
    return (SOCKET)(ULONG_PTR)(~0ULL);
}


__attribute__((ms_abi))
static INT connect_msabi(SOCKET s, PVOID name, INT namelen)
{
    (void)name; (void)namelen;
    return 0; /* fake-connect success */
}

__attribute__((ms_abi))
static INT send_msabi(SOCKET s, const CHAR *buf, INT len, INT flags)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    (void)buf; (void)flags;
    /* For now, return WSAEWOULDBLOCK-style: 0 bytes sent.
       When the lwIP port is connected, this would call tcp_write. */
    return 0;
}

__attribute__((ms_abi))
static INT recv_msabi(SOCKET s, CHAR *buf, INT len, INT flags)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    (void)buf; (void)flags;
    return 0;
}

__attribute__((ms_abi))
static INT sendto_msabi(SOCKET s, const CHAR *buf, INT len, INT flags,
    PVOID to, INT tolen)
{
    (void)to; (void)tolen;
    return send_msabi(s, buf, len, flags);
}

__attribute__((ms_abi))
static INT recvfrom_msabi(SOCKET s, CHAR *buf, INT len, INT flags,
    PVOID from, PINT fromlen)
{
    (void)from; (void)fromlen;
    return recv_msabi(s, buf, len, flags);
}

__attribute__((ms_abi))
static INT setsockopt_msabi(SOCKET s, INT level, INT optname,
    const CHAR *optval, INT optlen)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    (void)level; (void)optname; (void)optval; (void)optlen;
    return 0;
}

__attribute__((ms_abi))
static INT getsockopt_msabi(SOCKET s, INT level, INT optname,
    CHAR *optval, PINT optlen)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    if (level == SOL_SOCKET && optname == SO_ERROR) {
        if (optval && optlen && *optlen >= 4) *optval = (CHAR)0;
        if (optlen) *optlen = 4;
    }
    return 0;
}

__attribute__((ms_abi))
static INT shutdown_msabi(SOCKET s, INT how)
{
    SW_SOCKET *sock = SwGetSocket(s);
    if (!sock) return -1;
    (void)how;
    return 0;
}

__attribute__((ms_abi))
static INT gethostname_msabi(CHAR *name, INT namelen)
{
    if (name && namelen >= 6) {
        RtlCopyMemory(name, "minnt", 6);
        return 0;
    }
    return -1;
}

__attribute__((ms_abi))
static PVOID gethostbyname_msabi(const CHAR *name)
{
    (void)name;
    /* Return a static hostent struct */
    static struct {
        CHAR  *h_name;
        CHAR **h_aliases;
        USHORT h_addrtype;
        USHORT h_length;
        CHAR **h_addr_list;
    } hostent;
    static CHAR *aliases[1] = { NULL };
    static ULONG addr = 0x7F000001; /* 127.0.0.1 */
    static CHAR *addr_list[2] = { (CHAR *)&addr, NULL };
    hostent.h_name = (CHAR *)"minnt";
    hostent.h_aliases = aliases;
    hostent.h_addrtype = AF_INET;
    hostent.h_length = 4;
    hostent.h_addr_list = addr_list;
    return &hostent;
}

__attribute__((ms_abi))
static ULONG htonl_msabi(ULONG hl)
{
    return ((hl & 0xFF) << 24) | ((hl & 0xFF00) << 8) |
           ((hl >> 8) & 0xFF00) | ((hl >> 24) & 0xFF);
}

__attribute__((ms_abi))
static USHORT htons_msabi(USHORT hs)
{
    return (USHORT)((hs >> 8) | (hs << 8));
}

__attribute__((ms_abi))
static ULONG ntohl_msabi(ULONG nl)
{
    return htonl_msabi(nl);
}

__attribute__((ms_abi))
static USHORT ntohs_msabi(USHORT ns)
{
    return htons_msabi(ns);
}

__attribute__((ms_abi))
static ULONG inet_addr_msabi(const CHAR *cp)
{
    ULONG a = 0, p = 0, val = 0, dots = 0;
    while (cp[p]) {
        if (cp[p] >= '0' && cp[p] <= '9') val = val * 10 + (cp[p] - '0');
        else if (cp[p] == '.') { a = (a << 8) | val; val = 0; dots++; }
        p++;
    }
    a = (a << 8) | val;
    if (dots != 3) return 0xFFFFFFFF;
    return htonl_msabi(a);
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Ws2_32RegisterExports(VOID)
{
    Ws2_32RegisterExports_Init();
    ExeRegisterExport("ws2_32.dll", "WSAStartup",     WSAStartup_msabi);
    ExeRegisterExport("ws2_32.dll", "WSACleanup",      WSACleanup_msabi);
    ExeRegisterExport("ws2_32.dll", "WSAGetLastError", WSAGetLastError_msabi);
    ExeRegisterExport("ws2_32.dll", "socket",          socket_msabi);
    ExeRegisterExport("ws2_32.dll", "closesocket",      closesocket_msabi);
    ExeRegisterExport("ws2_32.dll", "bind",             bind_msabi);
    ExeRegisterExport("ws2_32.dll", "listen",           listen_msabi);
    ExeRegisterExport("ws2_32.dll", "accept",           accept_msabi);
    ExeRegisterExport("ws2_32.dll", "connect",          connect_msabi);
    ExeRegisterExport("ws2_32.dll", "send",             send_msabi);
    ExeRegisterExport("ws2_32.dll", "recv",             recv_msabi);
    ExeRegisterExport("ws2_32.dll", "sendto",           sendto_msabi);
    ExeRegisterExport("ws2_32.dll", "recvfrom",         recvfrom_msabi);
    ExeRegisterExport("ws2_32.dll", "setsockopt",       setsockopt_msabi);
    ExeRegisterExport("ws2_32.dll", "getsockopt",       getsockopt_msabi);
    ExeRegisterExport("ws2_32.dll", "shutdown",         shutdown_msabi);
    ExeRegisterExport("ws2_32.dll", "gethostname",      gethostname_msabi);
    ExeRegisterExport("ws2_32.dll", "gethostbyname",    gethostbyname_msabi);
    ExeRegisterExport("ws2_32.dll", "htonl",            htonl_msabi);
    ExeRegisterExport("ws2_32.dll", "htons",            htons_msabi);
    ExeRegisterExport("ws2_32.dll", "ntohl",            ntohl_msabi);
    ExeRegisterExport("ws2_32.dll", "ntohs",            ntohs_msabi);
    ExeRegisterExport("ws2_32.dll", "inet_addr",        inet_addr_msabi);

    DbgPrint("EXE: ws2_32.dll exports registered (%lu total)\n", g_ExportCount);
}
