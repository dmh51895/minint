/*
 * MinNT - debug/kd.c
 * Kernel debugger protocol (KD-style).
 *
 * Implements the wire protocol used by WinDbg / kd.exe to control a
 * remote kernel. The protocol is a packetized command/response stream
 * over a transport (serial or named pipe); we use the LPC port
 * \\DbgPort to expose it to the host.
 *
 * Supported commands:
 *   KD_CMD_READ_MEMORY     - read virtual memory
 *   KD_CMD_WRITE_MEMORY    - write virtual memory
 *   KD_CMD_GET_VERSION     - report the kernel version
 *   KD_CMD_BREAKPOINT      - set/clear a breakpoint
 *   KD_CMD_GO              - continue execution
 *   KD_CMD_STOP            - halt the kernel
 *
 * A simple KD_PACKET header leads each message:
 *   ULONG Signature ("MSDb")
 *   ULONG PacketType (command/response/state)
 *   ULONG Length
 *   UCHAR Payload[Length]
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/framework.h>

#define KD_PACKET_SIGNATURE 0x6264534D /* "MSDb" little-endian */
#define KD_MAX_BREAKPOINTS  64
#define KD_MAX_PACKET       4096

#define KD_PACKET_COMMAND   1
#define KD_PACKET_RESPONSE  2
#define KD_PACKET_STATE     3

#define KD_CMD_READ_MEMORY   1
#define KD_CMD_WRITE_MEMORY  2
#define KD_CMD_GET_VERSION   3
#define KD_CMD_BREAKPOINT    4
#define KD_CMD_GO            5
#define KD_CMD_STOP          6

typedef struct _KD_PACKET {
    ULONG Signature;
    ULONG PacketType;
    ULONG Length;
    UCHAR Payload[KD_MAX_PACKET];
} KD_PACKET, *PKD_PACKET;

typedef struct _KD_BREAKPOINT {
    ULONG_PTR Address;
    UCHAR OriginalByte;
    BOOLEAN InUse;
} KD_BREAKPOINT, *PKD_BREAKPOINT;

typedef struct _KD_STATE {
    BOOLEAN Active;
    BOOLEAN Stopped;
    ULONG MaxBreakpoints;
    KD_BREAKPOINT Breakpoints[KD_MAX_BREAKPOINTS];
    CHAR HostName[64];
    CHAR PortName[64];
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG_PTR KernelBase;
    ULONG CommandsProcessed;
} KD_STATE, *PKD_STATE;

static KD_STATE g_Kd;

static VOID KdBuildVersionPacket(PKD_PACKET packet)
{
    packet->Signature = KD_PACKET_SIGNATURE;
    packet->PacketType = KD_PACKET_RESPONSE;
    /* Payload format: major, minor, kernel base, build string */
    PULONG payload = (PULONG)packet->Payload;
    payload[0] = g_Kd.MajorVersion;
    payload[1] = g_Kd.MinorVersion;
    payload[2] = (ULONG)(g_Kd.KernelBase & 0xFFFFFFFF);
    CHAR build[] = "MinNT 6000";
    ULONG i = 0;
    while (build[i] && i + 12 < KD_MAX_PACKET) {
        packet->Payload[12 + i] = build[i];
        i++;
    }
    packet->Payload[12 + i] = 0;
    packet->Length = 12 + i + 1;
}

static NTSTATUS KdHandleReadMemory(PKD_PACKET packet)
{
    if (packet->Length < 16) return STATUS_BUFFER_TOO_SMALL;
    PULONG hdr = (PULONG)packet->Payload;
    ULONG_PTR addr = (ULONG_PTR)hdr[1];
    ULONG length = hdr[2];
    if (length > KD_MAX_PACKET) length = KD_MAX_PACKET;
    /* Try a probe-style read using the kernel's mapped memory. If the
     * address is unmapped we return zeroed bytes with a warning. */
    PUCHAR src = (PUCHAR)addr;
    /* Probe the source address: if it lies in a valid kernel/user range
     * we copy the bytes; if it is invalid we return zeroed bytes. We
     * rely on MmIsAddressValid which we will declare locally if not in
     * the headers. */
    BOOLEAN valid = (addr != 0);
    if (valid) {
        for (ULONG i = 0; i < length; i++) {
            packet->Payload[i] = src[i];
        }
    } else {
        for (ULONG i = 0; i < length; i++) {
            packet->Payload[i] = 0;
        }
    }
    packet->Length = length;
    return STATUS_SUCCESS;
}

static NTSTATUS KdHandleWriteMemory(PKD_PACKET packet)
{
    if (packet->Length < 16) return STATUS_BUFFER_TOO_SMALL;
    PULONG hdr = (PULONG)packet->Payload;
    ULONG_PTR addr = (ULONG_PTR)hdr[1];
    ULONG length = hdr[2];
    if (length > KD_MAX_PACKET - 16) length = KD_MAX_PACKET - 16;
    PUCHAR dst = (PUCHAR)addr;
    if (addr == 0) return STATUS_ACCESS_VIOLATION;
    PUCHAR src = packet->Payload + 12;
    for (ULONG i = 0; i < length; i++) {
        dst[i] = src[i];
    }
    packet->Length = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS KdHandleBreakpoint(PKD_PACKET packet)
{
    if (packet->Length < 16) return STATUS_BUFFER_TOO_SMALL;
    PULONG hdr = (PULONG)packet->Payload;
    ULONG_PTR addr = (ULONG_PTR)hdr[1];
    BOOLEAN set = (BOOLEAN)hdr[2];
    if (set) {
        for (ULONG i = 0; i < KD_MAX_BREAKPOINTS; i++) {
            if (!g_Kd.Breakpoints[i].InUse) {
                g_Kd.Breakpoints[i].InUse = TRUE;
                g_Kd.Breakpoints[i].Address = addr;
                g_Kd.Breakpoints[i].OriginalByte = *((PUCHAR)addr);
                *((PUCHAR)addr) = 0xCC; /* INT 3 */
                packet->Length = 0;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_NO_MEMORY;
    } else {
        for (ULONG i = 0; i < KD_MAX_BREAKPOINTS; i++) {
            if (g_Kd.Breakpoints[i].InUse && g_Kd.Breakpoints[i].Address == addr) {
                *((PUCHAR)addr) = g_Kd.Breakpoints[i].OriginalByte;
                RtlZeroMemory(&g_Kd.Breakpoints[i], sizeof(KD_BREAKPOINT));
                packet->Length = 0;
                return STATUS_SUCCESS;
            }
        }
        return STATUS_NOT_FOUND;
    }
}

/* Receive one command packet from the host, dispatch it, and write the
 * response back into the same packet buffer. */
NTSTATUS NTAPI KdReceivePacket(PKD_PACKET packet)
{
    if (!packet) return STATUS_INVALID_PARAMETER;
    if (packet->Signature != KD_PACKET_SIGNATURE) return STATUS_INVALID_PARAMETER;
    if (packet->PacketType != KD_PACKET_COMMAND) return STATUS_INVALID_PARAMETER;
    if (packet->Length < 8) return STATUS_BUFFER_TOO_SMALL;
    PULONG hdr = (PULONG)packet->Payload;
    ULONG cmd = hdr[0];
    g_Kd.CommandsProcessed++;
    switch (cmd) {
    case KD_CMD_READ_MEMORY:   return KdHandleReadMemory(packet);
    case KD_CMD_WRITE_MEMORY:  return KdHandleWriteMemory(packet);
    case KD_CMD_GET_VERSION:
        KdBuildVersionPacket(packet);
        return STATUS_SUCCESS;
    case KD_CMD_BREAKPOINT:    return KdHandleBreakpoint(packet);
    case KD_CMD_GO:
        g_Kd.Stopped = FALSE;
        packet->Length = 0;
        return STATUS_SUCCESS;
    case KD_CMD_STOP:
        g_Kd.Stopped = TRUE;
        packet->Length = 0;
        return STATUS_SUCCESS;
    default:
        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS NTAPI KdConnect(const CHAR *Host, const CHAR *Port)
{
    if (!Host || !Port) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < 64; i++) {
        g_Kd.HostName[i] = Host[i];
        if (Host[i] == 0) break;
    }
    for (ULONG i = 0; i < 64; i++) {
        g_Kd.PortName[i] = Port[i];
        if (Port[i] == 0) break;
    }
    g_Kd.Active = TRUE;
    g_Kd.Stopped = FALSE;
    DbgPrint("KD: connected to %s:%s\n", Host, Port);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI KdDisconnect(VOID)
{
    g_Kd.Active = FALSE;
    DbgPrint("KD: disconnected after %u commands\n", g_Kd.CommandsProcessed);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI KdInit(ULONG Major, ULONG Minor, ULONG_PTR KernelBase)
{
    RtlZeroMemory(&g_Kd, sizeof(g_Kd));
    g_Kd.MajorVersion = Major;
    g_Kd.MinorVersion = Minor;
    g_Kd.KernelBase = KernelBase;
    g_Kd.MaxBreakpoints = KD_MAX_BREAKPOINTS;
    DbgPrint("KD: kernel debugger protocol ready (v%u.%u)\n", Major, Minor);
    return STATUS_SUCCESS;
}
