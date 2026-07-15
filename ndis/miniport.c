/*
 * MinNT - ndis/miniport.c
 * NDIS Miniport driver for RTL8821CU WiFi.
 *
 * Architecture:
 *   TCP/IP stack (lwip_port.c)
 *         ↓
 *   NDIS miniport (this file)
 *         ↓
 *   WiFi USB transport (rtw/rtw_usb.c)
 *         ↓
 *   USB device enumeration (usb/usbenum.c)
 *         ↓
 *   UHCI host controller (usb/uhci.c)
 *
 * Boot integration:
 *   kiinit.c → RtwInitSystem() → RtwUsbInit() → Rtw8821cuInit()
 *                                   ↓
 *   RtwNdisInit() ← called after USB chip init
 *       ↓
 *   NdisMInitializeWrapper()
 *       ↓
 *   NdisMSetAttributes()
 *       ↓
 *   Adapter ready for sends/receives
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/io.h>
#include <nt/rtl.h>
#include <ndis.h>
#include <rtw_ndis.h>
#include <rtw/rtw_usb.h>

#define TAG_NDIS_MP 0x4D504E44  /* 'MPND' */

/* ---- NDIS miniport adapter ---------------------------------------- */

static RTW_NDIS_ADAPTER RtwMiniportAdapter;

/* ---- Forward declarations for miniport handlers -------------------- */

static VOID NTAPI MiniportHalt(NDIS_HANDLE MiniportAdapterContext);
static NDIS_STATUS NTAPI MiniportInitialize(NDIS_HANDLE MiniportAdapterContext);
static VOID NTAPI MiniportSend(NDIS_HANDLE MiniportAdapterContext,
                               PNDIS_PACKET Packet);
static VOID NTAPI MiniportSendPackets(NDIS_HANDLE MiniportAdapterContext,
                                      PNDIS_PACKET * Packets,
                                      ULONG PacketCount);
static NDIS_STATUS NTAPI MiniportQueryInformation(NDIS_HANDLE MiniportAdapterContext,
                                                  NDIS_OID Oid,
                                                  PVOID InformationBuffer,
                                                  ULONG InformationBufferLength,
                                                  PULONG BytesWritten,
                                                  PULONG BytesNeeded);
static NDIS_STATUS NTAPI MiniportSetInformation(NDIS_HANDLE MiniportAdapterContext,
                                                NDIS_OID Oid,
                                                PVOID InformationBuffer,
                                                ULONG InformationBufferLength,
                                                PULONG BytesRead,
                                                ULONG BytesNeeded);
static NDIS_STATUS NTAPI MiniportReset(NDIS_HANDLE MiniportAdapterContext);

/* ---- NDIS_MINIPORT_CHARACTERISTICS -------------------------------- */

/*
 * The characteristics table describes all our miniport handler functions.
 * Passed to NdisMRegisterMiniport during DriverEntry.
 */
static NDIS_MINIPORT_CHARACTERISTICS MiniportChars = {
    NDIS_MINIPORT_MAJOR_VERSION,
    NDIS_MINIPORT_MINOR_VERSION,
    0, /* Filler */
    { 0 }, /* Reserved */
    NULL, /*XlatNpToLiu*/
    NULL, /*SendPacketsHandler*/
    MiniportHalt,
    MiniportInitialize,
    NULL, /*PauseHandler*/
    MiniportQueryInformation,
    MiniportReset,
    MiniportSend,
    MiniportSetInformation,
    NULL, /*CheckForHangHandler*/
    NULL, /*ISRHandler*/
    NULL, /*ReturnPacketHandler*/
    NULL, /*SendCompleteHandler*/
    NULL, /*ReceiveHandler*/
    NULL, /*ReceiveCompleteHandler*/
    NULL, /*StatusHandler*/
    NULL, /*StatusCompleteHandler*/
    NULL, /*AddressChangeHandler*/
    NULL, /*PnPEventNotifyHandler*/
    NULL, /*AdapterShutdownHandler*/
};

/* ---- Miniport handler implementations ------------------------------ */

/*
 * MiniportHalt - Shut down the miniport.
 *
 * Called during system shutdown or driver unload.
 * Frees resources and de-initializes the hardware.
 */
static VOID NTAPI MiniportHalt(NDIS_HANDLE MiniportAdapterContext)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;

    DbgPrint("RTWNDIS: MiniportHalt\n");

    if (!Adapter)
        return;

    /* Deinit the USB layer with the real adapter */
    RtwUsbDeinit(Adapter->UsbContext);

    Adapter->Initialized = FALSE;
}

/*
 * MiniportInitialize - Initialize the WiFi adapter.
 *
 * Called by NDIS during adapter detection.
 * Sets adapter attributes and starts the hardware.
 */
static NDIS_STATUS NTAPI MiniportInitialize(NDIS_HANDLE MiniportAdapterContext)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;
    NTSTATUS Status;

    DbgPrint("RTWNDIS: MiniportInitialize\n");

    if (!Adapter)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Tell NDIS about our adapter capabilities */
    Adapter->MediaConnected = FALSE;

    /* Use the real USB adapter, not NULL */
    Status = RtwUsbInit(Adapter->UsbContext);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTWNDIS: USB init failed in MiniportInitialize\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    Adapter->Initialized = TRUE;
    return NDIS_STATUS_SUCCESS;
}

/*
 * MiniportSend - Send a single packet.
 *
 * Called by NDIS when a protocol wants to send a frame.
 * The miniport owns the packet until MiniportSendComplete is called.
 *
 * Forwards the packet to RtwNdisSendPacket for USB transmission.
 */
static VOID NTAPI MiniportSend(NDIS_HANDLE MiniportAdapterContext,
                               PNDIS_PACKET Packet)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;
    NTSTATUS Status;
    ULONG frame_len;

    if (!Adapter || !Adapter->Initialized)
    {
        NdisMSendComplete(Adapter->MiniportHandle,
                          Packet,
                          NDIS_STATUS_FAILURE);
        return;
    }

    if (!Packet)
    {
        NdisMSendComplete(Adapter->MiniportHandle,
                          Packet,
                          NDIS_STATUS_FAILURE);
        return;
    }

    /* In this minimal implementation NDIS_PACKET is a raw buffer pointer.
     * Use the adapter's tracked frame length, defaulting to max MTU. */
    frame_len = Adapter->TxFrameLength;
    if (frame_len == 0 || frame_len > RTW_USB_MAX_BULK_SIZE)
        frame_len = RTW_USB_MAX_BULK_SIZE;

    extern RTW_ADAPTER RtwAdapter;
    Status = RtwUsbBulkOut(&RtwAdapter, Packet, frame_len);
    if (!NT_SUCCESS(Status))
    {
        Adapter->TxErrors++;
        DbgPrint("RTWNDIS: MiniportSend bulk_out failed 0x%08lx\n", (unsigned long)Status);
        NdisMSendComplete(Adapter->MiniportHandle,
                          Packet,
                          NDIS_STATUS_FAILURE);
        return;
    }

    Adapter->TxPackets++;
    NdisMSendComplete(Adapter->MiniportHandle,
                      Packet,
                      NDIS_STATUS_SUCCESS);
}

/*
 * MiniportSendPackets - Send multiple packets.
 *
 * Efficient multi-packet send handler.
 */
static VOID NTAPI MiniportSendPackets(NDIS_HANDLE MiniportAdapterContext,
                                       PNDIS_PACKET * Packets,
                                       ULONG PacketCount)
{
    ULONG i;

    for (i = 0; i < PacketCount; i++)
    {
        MiniportSend(MiniportAdapterContext, Packets[i]);
    }
}

/*
 * MiniportQueryInformation - Query an OID.
 *
 * Handles queries for adapter properties including:
 * - OID_GEN_HARDWARE_STATUS
 * - OID_GEN_MEDIA_CONNECT_STATUS
 * - OID_GEN_CURRENT_PACKET_FILTER
 * - OID_GEN_CURRENT_LOOKAHEAD
 * - OID_GEN_NETWORK_LINK_SPEED
 * - OID_GEN_TRANSMIT_BUFFER_SPACE
 * - OID_GEN_RECEIVE_BUFFER_SPACE
 * - OID_GEN_TRANSMIT_BLOCK_SIZE
 * - OID_GEN_RECEIVE_BLOCK_SIZE
 * - OID_802_11_MAC_ADDRESS
 * - OID_802_11_BSSID
 * - OID_802_11_SSID
 * - OID_802_11_RSSI
 */
static NDIS_STATUS NTAPI MiniportQueryInformation(
    NDIS_HANDLE MiniportAdapterContext,
    NDIS_OID Oid,
    PVOID InformationBuffer,
    ULONG InformationBufferLength,
    PULONG BytesWritten,
    PULONG BytesNeeded)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;
    ULONG InfoSize = 0;

    if (!BytesWritten || !BytesNeeded)
        return NDIS_STATUS_INVALID_PARAMETER;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    if (!Adapter)
        return NDIS_STATUS_INVALID_PARAMETER;

    switch (Oid)
    {
        /* ---- OID_GEN_HARDWARE_STATUS ---- */
        case OID_GEN_HARDWARE_STATUS:
        {
            if (InformationBufferLength < sizeof(NDIS_HARDWARE_STATUS))
            {
                *BytesNeeded = sizeof(NDIS_HARDWARE_STATUS);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_HARDWARE_STATUS *hw = (NDIS_HARDWARE_STATUS *)InformationBuffer;
            hw->HardwareStatus = Adapter->Initialized
                ? NDIS_HARDWARE_STATUS_READY
                : NDIS_HARDWARE_STATUS_IS_DISABLED;
            hw->HardwareStatusEx = 0;
            *BytesWritten = sizeof(NDIS_HARDWARE_STATUS);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_MEDIA_CONNECT_STATUS ---- */
        case OID_GEN_MEDIA_CONNECT_STATUS:
        {
            if (InformationBufferLength < sizeof(NDIS_MEDIA_CONNECT_STATUS))
            {
                *BytesNeeded = sizeof(NDIS_MEDIA_CONNECT_STATUS);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_MEDIA_CONNECT_STATUS *cs = (NDIS_MEDIA_CONNECT_STATUS *)InformationBuffer;
            cs->ConnectionState = Adapter->MediaConnected
                ? NDIS_MEDIA_STATE_CONNECTED
                : NDIS_MEDIA_STATE_DISCONNECTED;
            cs->Flags = 0;
            *BytesWritten = sizeof(NDIS_MEDIA_CONNECT_STATUS);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_CURRENT_PACKET_FILTER ---- */
        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *filter = (ULONG *)InformationBuffer;
            *filter = NDIS_PACKET_TYPE_DIRECTED
                    | NDIS_PACKET_TYPE_MULTICAST
                    | NDIS_PACKET_TYPE_BROADCAST;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_CURRENT_LOOKAHEAD ---- */
        case OID_GEN_CURRENT_LOOKAHEAD:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *lookahead = (ULONG *)InformationBuffer;
            *lookahead = RTW_USB_MAX_BULK_SIZE;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_NETWORK_LINK_SPEED ---- */
        case OID_GEN_LINK_SPEED:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *speed = (ULONG *)InformationBuffer;
            *speed = Adapter->Initialized ? 54000000 : 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_TRANSMIT_BUFFER_SPACE ---- */
        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *buf = (ULONG *)InformationBuffer;
            *buf = RTW_USB_MAX_BULK_SIZE * 4;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_RECEIVE_BUFFER_SPACE ---- */
        case OID_GEN_RECEIVE_BUFFER_SPACE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *buf = (ULONG *)InformationBuffer;
            *buf = RTW_USB_MAX_BULK_SIZE * 4;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_TRANSMIT_BLOCK_SIZE ---- */
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *blk = (ULONG *)InformationBuffer;
            *blk = RTW_USB_MAX_BULK_SIZE;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_RECEIVE_BLOCK_SIZE ---- */
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *blk = (ULONG *)InformationBuffer;
            *blk = RTW_USB_MAX_BULK_SIZE;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_DRIVER_VERSION ---- */
        case OID_GEN_DRIVER_VERSION:
        {
            if (InformationBufferLength < sizeof(USHORT))
            {
                *BytesNeeded = sizeof(USHORT);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            USHORT *ver = (USHORT *)InformationBuffer;
            *ver = (USHORT)((1 << 8) | 0);
            *BytesWritten = sizeof(USHORT);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_MAC_OPTIONS ---- */
        case OID_GEN_MAC_OPTIONS:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *opts = (ULONG *)InformationBuffer;
            *opts = NDIS_MAC_OPTIONS_CONNECTABLE | NDIS_MAC_OPTIONS_FULL_DUPLEX
                  | NDIS_MAC_OPTIONS_EMAC_OPTIONS;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_PHYSICAL_MEDIA_TYPE ---- */
        case OID_GEN_PHYSICAL_MEDIA_TYPE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *pmt = (ULONG *)InformationBuffer;
            *pmt = NDIS_PHYSICAL_MEDIUM_IEEE80211;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_MEDIA_TYPE ---- */
        case OID_GEN_MEDIA_TYPE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *mt = (ULONG *)InformationBuffer;
            *mt = NDIS_MEDIUM_IEEE80211;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_VENDOR_ID ---- */
        case OID_GEN_VENDOR_ID:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *vid = (ULONG *)InformationBuffer;
            *vid = 0x0000C000;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_VENDOR_DESCRIPTION ---- */
        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const char desc[] = "Realtek RTL8821CU WiFi";
            if (InformationBufferLength < sizeof(desc))
            {
                *BytesNeeded = sizeof(desc);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            RtlCopyMemory(InformationBuffer, desc, sizeof(desc));
            *BytesWritten = sizeof(desc);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_VENDOR_DRIVER_VERSION ---- */
        case OID_GEN_VENDOR_DRIVER_VERSION:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *ver = (ULONG *)InformationBuffer;
            *ver = (1 << 8) | 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_MAXIMUM_TOTAL_SIZE ---- */
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *max = (ULONG *)InformationBuffer;
            *max = RTW_USB_MAX_BULK_SIZE;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_MAXIMUM_SEND_PACKETS ---- */
        case OID_GEN_MAXIMUM_SEND_PACKETS:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *cnt = (ULONG *)InformationBuffer;
            *cnt = 8;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_MAC_ADDRESS ---- */
        case OID_802_11_MAC_ADDRESS:
        {
            if (InformationBufferLength < 6)
            {
                *BytesNeeded = 6;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            RtlCopyMemory(InformationBuffer, Adapter->CurrentMacAddress, 6);
            *BytesWritten = 6;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_BSSID ---- */
        case OID_802_11_BSSID:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_BSSID))
            {
                *BytesNeeded = sizeof(NDIS_802_11_BSSID);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_802_11_BSSID *bssid = (NDIS_802_11_BSSID *)InformationBuffer;
            if (Adapter->MediaConnected)
            {
                bssid->BssidLength = 6;
                RtlCopyMemory(bssid->BSSID, Adapter->CurrentMacAddress, 6);
            }
            else
            {
                bssid->BssidLength = 0;
                RtlZeroMemory(bssid->BSSID, 32);
            }
            *BytesWritten = sizeof(NDIS_802_11_BSSID);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_SSID ---- */
        case OID_802_11_SSID:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_SSID))
            {
                *BytesNeeded = sizeof(NDIS_802_11_SSID);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_802_11_SSID *ssid = (NDIS_802_11_SSID *)InformationBuffer;
            if (Adapter->MediaConnected)
            {
                ssid->SsidLength = 0;
                RtlZeroMemory(ssid->SSID, 32);
            }
            else
            {
                ssid->SsidLength = 0;
                RtlZeroMemory(ssid->SSID, 32);
            }
            *BytesWritten = sizeof(NDIS_802_11_SSID);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_RSSI ---- */
        case OID_802_11_RSSI:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_RSSI))
            {
                *BytesNeeded = sizeof(NDIS_802_11_RSSI);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_802_11_RSSI *rssi = (NDIS_802_11_RSSI *)InformationBuffer;
            rssi->Rssi = Adapter->Initialized ? Adapter->Rssi : 0;
            *BytesWritten = sizeof(NDIS_802_11_RSSI);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_STATISTICS ---- */
        case OID_GEN_STATISTICS:
        {
            if (InformationBufferLength < sizeof(NDIS_STATISTICS_INFO))
            {
                *BytesNeeded = sizeof(NDIS_STATISTICS_INFO);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_STATISTICS_INFO *stats = (NDIS_STATISTICS_INFO *)InformationBuffer;
            RtlZeroMemory(stats, sizeof(NDIS_STATISTICS_INFO));
            stats->OutboundKBytes = (ULONG)(Adapter->TxPackets / 1024);
            stats->InboundKBytes = (ULONG)(Adapter->RxPackets / 1024);
            stats->OutboundunicastPackets = (ULONG)Adapter->TxPackets;
            stats->InboundunicastPackets = (ULONG)Adapter->RxPackets;
            stats->OutboundErrors = Adapter->TxErrors;
            stats->InboundErrors = Adapter->RxErrors;
            *BytesWritten = sizeof(NDIS_STATISTICS_INFO);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_AUTH_ALGORITHM ---- */
        case OID_802_11_CURRENT_AUTH_ALG:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *alg = (ULONG *)InformationBuffer;
            *alg = Adapter->MediaConnected ? 1 : 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_ENCAP_SUPPORTED ---- */
        case OID_GEN_ENCAP_SUPPORTED:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *val = (ULONG *)InformationBuffer;
            *val = 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_XMIT_ERROR ---- */
        case OID_GEN_XMIT_ERROR:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *cnt = (ULONG *)InformationBuffer;
            *cnt = Adapter->TxErrors;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_RCV_ERROR ---- */
        case OID_GEN_RCV_ERROR:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *cnt = (ULONG *)InformationBuffer;
            *cnt = Adapter->RxErrors;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_RCV_NO_BUFFER ---- */
        case OID_GEN_RCV_NO_BUFFER:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *cnt = (ULONG *)InformationBuffer;
            *cnt = 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_TRANSMIT_QUEUE_LENGTH ---- */
        case OID_GEN_TRANSMIT_QUEUE_LENGTH:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *len = (ULONG *)InformationBuffer;
            *len = 0;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_LINK_STATE ---- */
        case OID_802_11_LINK_STATE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG *state = (ULONG *)InformationBuffer;
            *state = Adapter->MediaConnected;
            *BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        default:
            DbgPrint("RTWNDIS: QueryInformation OID=0x%08lx (not supported)\n",
                     (unsigned long)Oid);
            return NDIS_STATUS_NOT_RECOGNIZED;
    }
}

/*
 * MiniportSetInformation - Set an OID.
 *
 * Handles OID sets:
 * - OID_GEN_CURRENT_PACKET_FILTER
 * - OID_802_11_MAC_ADDRESS
 * - OID_802_11_DISASSOCIATE
 * - OID_802_11_BSSID_LIST_SCAN
 * - OID_802_11_ADD_KEY
 * - OID_802_11_REMOVE_KEY
 */
static NDIS_STATUS NTAPI MiniportSetInformation(
    NDIS_HANDLE MiniportAdapterContext,
    NDIS_OID Oid,
    PVOID InformationBuffer,
    ULONG InformationBufferLength,
    PULONG BytesRead,
    ULONG BytesNeeded)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;

    if (!BytesRead)
        return NDIS_STATUS_INVALID_PARAMETER;

    *BytesRead = 0;

    if (!Adapter)
        return NDIS_STATUS_INVALID_PARAMETER;

    switch (Oid)
    {
        /* ---- OID_GEN_CURRENT_PACKET_FILTER ---- */
        case OID_GEN_CURRENT_PACKET_FILTER:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            ULONG filter = *(ULONG *)InformationBuffer;
            if (filter & ~(NDIS_PACKET_TYPE_DIRECTED
                         | NDIS_PACKET_TYPE_MULTICAST
                         | NDIS_PACKET_TYPE_ALL_MULTICAST
                         | NDIS_PACKET_TYPE_BROADCAST
                         | NDIS_PACKET_TYPE_PROMISCUOUS
                         | NDIS_PACKET_TYPE_SOURCE_ROUTING
                         | NDIS_PACKET_TYPE_ALL_LOCAL
                         | NDIS_PACKET_TYPE_ALL_FUNCTIONAL
                         | NDIS_PACKET_TYPE_FUNCTIONAL
                         | NDIS_PACKET_TYPE_MAC_FRAME))
            {
                return NDIS_STATUS_INVALID_DATA;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_MAC_ADDRESS ---- */
        case OID_802_11_MAC_ADDRESS:
        {
            if (InformationBufferLength < 6)
            {
                *BytesRead = 6;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            RtlCopyMemory(Adapter->CurrentMacAddress, InformationBuffer, 6);
            RtlCopyMemory(Adapter->PermanentMacAddress, InformationBuffer, 6);
            *BytesRead = 6;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_DISASSOCIATE ---- */
        case OID_802_11_DISASSOCIATE:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_CONFIGURATION))
            {
                *BytesRead = sizeof(NDIS_802_11_CONFIGURATION);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            Adapter->MediaConnected = FALSE;
            *BytesRead = sizeof(NDIS_802_11_CONFIGURATION);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_BSSID_LIST_SCAN ---- */
        case OID_802_11_BSSID_LIST_SCAN:
        {
            *BytesRead = 0;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_ADD_KEY ---- */
        case OID_802_11_ADD_KEY:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_KEY))
            {
                *BytesRead = sizeof(NDIS_802_11_KEY);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            NDIS_802_11_KEY *key = (NDIS_802_11_KEY *)InformationBuffer;
            if (key->Length > InformationBufferLength - offsetof(NDIS_802_11_KEY, KeyMaterial))
            {
                return NDIS_STATUS_INVALID_DATA;
            }
            *BytesRead = sizeof(NDIS_802_11_KEY) + key->KeyMaterialLength;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_REMOVE_KEY ---- */
        case OID_802_11_REMOVE_KEY:
        {
            if (InformationBufferLength < sizeof(NDIS_802_11_KEY))
            {
                *BytesRead = sizeof(NDIS_802_11_KEY);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(NDIS_802_11_KEY);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_CURRENT_LOOKAHEAD ---- */
        case OID_GEN_CURRENT_LOOKAHEAD:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_LINK_PARAMETERS ---- */
        case OID_GEN_LINK_PARAMETERS:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_NETWORK_TYPE ---- */
        case OID_802_11_NETWORK_TYPE:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_PRIVACY_STATUS ---- */
        case OID_802_11_PRIVACY_STATUS:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_ENCRYPTION_STATUS ---- */
        case OID_802_11_ENCRYPTION_STATUS:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_CTSPROTECTION ---- */
        case OID_802_11_CTSPROTECTION:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_802_11_TX_RETRIES ---- */
        case OID_802_11_TX_RETRIES:
        {
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesRead = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_XMIT_ERROR ---- */
        case OID_GEN_XMIT_ERROR:
        {
            *BytesRead = 0;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_RCV_ERROR ---- */
        case OID_GEN_RCV_ERROR:
        {
            *BytesRead = 0;
            return NDIS_STATUS_SUCCESS;
        }

        /* ---- OID_GEN_STATISTICS (reset) ---- */
        case OID_GEN_RESET_COUNTS:
        {
            Adapter->TxPackets = 0;
            Adapter->RxPackets = 0;
            Adapter->TxErrors = 0;
            Adapter->RxErrors = 0;
            *BytesRead = 0;
            return NDIS_STATUS_SUCCESS;
        }

        default:
            DbgPrint("RTWNDIS: SetInformation OID=0x%08lx (not supported)\n",
                     (unsigned long)Oid);
            return NDIS_STATUS_NOT_RECOGNIZED;
    }
}

/*
 * MiniportReset - Reset the adapter.
 *
 * Cancels pending TX, resets hardware to clean state.
 * Reinitializes the USB hardware.
 */
static NDIS_STATUS NTAPI MiniportReset(NDIS_HANDLE MiniportAdapterContext)
{
    PRTW_NDIS_ADAPTER Adapter = (PVOID)MiniportAdapterContext;
    NTSTATUS Status;

    if (!Adapter)
        return NDIS_STATUS_INVALID_PARAMETER;

    DbgPrint("RTWNDIS: MiniportReset reinitializing hardware\n");

    /* Deinit the USB layer */
    RtwUsbDeinit(Adapter->UsbContext);

    /* Reinitialize the USB layer */
    Status = RtwUsbInit(Adapter->UsbContext);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTWNDIS: MiniportReset USB reinit failed 0x%08lx\n", (unsigned long)Status);
        return NDIS_STATUS_FAILURE;
    }

    DbgPrint("RTWNDIS: MiniportReset success\n");
    return NDIS_STATUS_SUCCESS;
}

/* ---- Public API (called from rtw_usb.c boot flow) ------------------ */

/*
 * RtwNdisInit - Initialize the NDIS miniport layer.
 *
 * Called from RtwInitSystem() after USB chip init succeeds.
 * Sets up the NDIS wrapper and registers the miniport.
 *
 * @UsbAdapter: The USB adapter context from RtwUsbInit
 * @return: STATUS_SUCCESS if NDIS layer initialized
 */
NTSTATUS NTAPI RtwNdisInit(PRTW_NDIS_ADAPTER UsbAdapter)
{
    NDIS_HANDLE NdisWrapperHandle;
    NDIS_HANDLE MiniportHandle;
    NDIS_STATUS Status;
    PRTW_NDIS_ADAPTER Adapter = &RtwMiniportAdapter;

    DbgPrint("RTWNDIS: initializing NDIS miniport...\n");

    RtlZeroMemory(Adapter, sizeof(RtwMiniportAdapter));

    /* Initialize the NDIS wrapper */
    NdisMInitializeWrapper(&NdisWrapperHandle,
                             NULL, NULL, NULL);
    if (!NdisWrapperHandle)
    {
        DbgPrint("RTWNDIS: NdisMInitializeWrapper failed\n");
        return STATUS_UNSUCCESSFUL;
    }

    Adapter->NdisWrapperHandle = NdisWrapperHandle;

    /* Register the miniport with NDIS */
    Status = NdisMRegisterMiniport(NdisWrapperHandle,
                                     &MiniportChars,
                                     sizeof(MiniportChars));
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("RTWNDIS: NdisMRegisterMiniport failed: 0x%08lx\n",
                 (unsigned long)Status);
        return STATUS_UNSUCCESSFUL;
    }

    /* Set adapter attributes so we get a valid MiniportAdapterHandle */
    NdisMSetAttributes(NdisWrapperHandle,
                       Adapter,
                       FALSE, /* Not bus master (USB) */
                       NDIS_INTERFACE_PCI);

    if (UsbAdapter && UsbAdapter->Initialized)
    {
        RtlCopyMemory(Adapter->PermanentMacAddress,
                       UsbAdapter->PermanentMacAddress, 6);
        RtlCopyMemory(Adapter->CurrentMacAddress,
                       UsbAdapter->CurrentMacAddress, 6);
    }

    Adapter->MiniportHandle = MiniportHandle;
    Adapter->Initialized = TRUE;

    DbgPrint("RTWNDIS: NDIS miniport ready, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
             Adapter->CurrentMacAddress[0],
             Adapter->CurrentMacAddress[1],
             Adapter->CurrentMacAddress[2],
             Adapter->CurrentMacAddress[3],
             Adapter->CurrentMacAddress[4],
             Adapter->CurrentMacAddress[5]);

    return STATUS_SUCCESS;
}

/*
 * RtwNdisIsConnected - Check if WiFi is associated.
 */
BOOLEAN NTAPI RtwNdisIsConnected(PRTW_NDIS_ADAPTER Adapter)
{
    if (!Adapter)
        return FALSE;
    return Adapter->MediaConnected;
}

/*
 * RtwNdisGetMacAddress - Copy current MAC address.
 */
VOID NTAPI RtwNdisGetMacAddress(PRTW_NDIS_ADAPTER Adapter,
                               PUCHAR MacAddress)
{
    if (!Adapter || !MacAddress)
        return;

    RtlCopyMemory(MacAddress, Adapter->CurrentMacAddress, 6);
}

/*
 * RtwNdisSendPacket - Send a raw Ethernet frame via the WiFi USB adapter.
 *
 * Called from lwIP's netif->output callback (rtw_netif_output in lwip_port.c).
 * The packet data is an Ethernet frame starting with destination MAC.
 */
VOID NTAPI RtwNdisSendPacket(PVOID Packet, ULONG Length)
{
    extern RTW_ADAPTER RtwAdapter;
    extern NTSTATUS NTAPI RtwUsbBulkOut(PRTW_ADAPTER Adapter, PVOID Buffer, ULONG Length);
    NTSTATUS Status;

    if (!Packet || Length == 0 || Length > RTW_USB_MAX_BULK_SIZE)
    {
        DbgPrint("RTWNDIS: SendPacket invalid args len=%lu\n", (unsigned long)Length);
        return;
    }

    Status = RtwUsbBulkOut(&RtwAdapter, Packet, Length);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("RTWNDIS: SendPacket bulk_out failed 0x%08lx\n", (unsigned long)Status);
    }
}