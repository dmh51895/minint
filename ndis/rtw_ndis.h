/*
 * MinNT - ndis/rtw_ndis.h
 * WiFi NDIS miniport — public interface.
 *
 * This is the interface between the WiFi USB transport (rtw_usb.c)
 * and the NDIS miniport layer (ndis/miniport.c).
 */

#ifndef _RTW_NDIS_H_
#define _RTW_NDIS_H_

#include <nt/ntdef.h>
#include <ndis.h>

/* ---- RTW adapter that wraps USB + NDIS state ---------------------- */

typedef struct _RTW_NDIS_ADAPTER {
    BOOLEAN             Initialized;
    BOOLEAN             MediaConnected;
    NDIS_HANDLE         MiniportHandle;
    NDIS_HANDLE         NdisWrapperHandle;
    UCHAR               PermanentMacAddress[6];
    UCHAR               CurrentMacAddress[6];
    ULONG               Channel;
    ULONG               Rssi;
    ULONG64             TxPackets;
    ULONG64             RxPackets;
    ULONG               TxErrors;
    ULONG               RxErrors;
    ULONG               TxFrameLength;

    /* Link to USB layer */
    NDIS_HANDLE         UsbContext;
} RTW_NDIS_ADAPTER, *PRTW_NDIS_ADAPTER;

/* ---- NDIS miniport functions --------------------------------------- */

NTSTATUS NTAPI RtwNdisInit(PRTW_NDIS_ADAPTER UsbAdapter);
BOOLEAN NTAPI RtwNdisIsConnected(PRTW_NDIS_ADAPTER Adapter);
VOID NTAPI RtwNdisGetMacAddress(PRTW_NDIS_ADAPTER Adapter, PUCHAR MacAddress);
VOID NTAPI RtwNdisSendPacket(PVOID Packet, ULONG Length);

#endif /* _RTW_NDIS_H_ */