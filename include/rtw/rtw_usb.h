/*
 * MinNT - rtw_usb.h
 * Realtek 8821CU USB WiFi driver — USB transport layer.
 * Ported from ReactOS rtw88 driver, stripped of NDIS/WDM dependencies.
 */

#ifndef _RTW_USB_H_
#define _RTW_USB_H_

#include <nt/ntdef.h>
#include <nt/usb.h>

/* ---- USB endpoint addresses ------------------------------------------- */

#define RTW_USB_BULK_IN_PIPE     0x81
#define RTW_USB_BULK_OUT_PIPE    0x02
#define RTW_USB_INTERRUPT_PIPE   0x83

/* ---- USB control request types ---------------------------------------- */

#define RTW_USB_VENDOR_REQUEST   0x40
#define RTW_USB_REG_READ         0x83
#define RTW_USB_REG_WRITE        0x03

/* ---- Transfer limits -------------------------------------------------- */

#define RTW_USB_MAX_BULK_SIZE    32768
#define RTW_USB_MAX_CTRL_SIZE    512
#define RTW_USB_TIMEOUT          1000   /* ms */

/* ---- Adapter structure ------------------------------------------------- */

typedef struct _RTW_ADAPTER {
    BOOLEAN         Initialized;
    BOOLEAN         MediaConnected;

    /* USB device handle */
    PUSB_DEVICE_HANDLE UsbDevice;

    /* Chip state */
    UCHAR           PermanentMacAddress[6];
    UCHAR           CurrentMacAddress[6];
    ULONG           Channel;
    ULONG           Rssi;

    /* Firmware */
    PVOID           FirmwareData;
    ULONG           FirmwareSize;

    /* Statistics */
    ULONG64         TxPackets;
    ULONG64         RxPackets;
    ULONG           TxErrors;
    ULONG           RxErrors;
} RTW_ADAPTER, *PRTW_ADAPTER;

/* ---- API -------------------------------------------------------------- */

NTSTATUS NTAPI RtwUsbInit(PRTW_ADAPTER Adapter);
VOID     NTAPI RtwUsbDeinit(PRTW_ADAPTER Adapter);

NTSTATUS NTAPI RtwUsbRead8(PRTW_ADAPTER Adapter, ULONG Offset, PUCHAR Value);
NTSTATUS NTAPI RtwUsbRead16(PRTW_ADAPTER Adapter, ULONG Offset, PUSHORT Value);
NTSTATUS NTAPI RtwUsbRead32(PRTW_ADAPTER Adapter, ULONG Offset, PULONG Value);

NTSTATUS NTAPI RtwUsbWrite8(PRTW_ADAPTER Adapter, ULONG Offset, UCHAR Value);
NTSTATUS NTAPI RtwUsbWrite16(PRTW_ADAPTER Adapter, ULONG Offset, USHORT Value);
NTSTATUS NTAPI RtwUsbWrite32(PRTW_ADAPTER Adapter, ULONG Offset, ULONG Value);

NTSTATUS NTAPI RtwUsbBulkIn(PRTW_ADAPTER Adapter, PVOID Buffer, ULONG Length, PULONG BytesRead);
NTSTATUS NTAPI RtwUsbBulkOut(PRTW_ADAPTER Adapter, PVOID Buffer, ULONG Length);

NTSTATUS NTAPI RtwUsbStartInterruptPipe(PRTW_ADAPTER Adapter);
VOID     NTAPI RtwUsbStopInterruptPipe(PRTW_ADAPTER Adapter);

NTSTATUS NTAPI RtwUsbReadFirmware(PRTW_ADAPTER Adapter, PCWSTR FirmwareName);
NTSTATUS NTAPI RtwUsbDownloadFirmware(PRTW_ADAPTER Adapter);

/* ---- Top-level init --------------------------------------------------- */

NTSTATUS NTAPI RtwInitSystem(VOID);
NTSTATUS NTAPI RtwStartRxThread(PRTW_ADAPTER Adapter);
VOID NTAPI RtwRegisterRxCallback(PVOID Callback);

#endif /* _RTW_USB_H_ */
