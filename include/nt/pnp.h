/*
 * MinNT - pnp.h
 * Plug & Play Manager declarations
 */

#ifndef _PNP_H_
#define _PNP_H_

#include <nt/ntdef.h>

/* PnP IRP minor codes */
#define IRP_MN_START_DEVICE 0
#define IRP_MN_REMOVE_DEVICE 2
#define IRP_MN_STOP_DEVICE 4
#define IRP_MN_QUERY_CAPABILITIES 9

/* Device node flags */
#define DNF_MADEUP 0x00000001
#define DNF_DUPLICATE 0x00000002
#define DNF_ENUMERATED 0x00000004

/* Function prototypes - PnP specific */
NTSTATUS NTAPI PnpInit(VOID);
NTSTATUS NTAPI PnpAddDevice(PVOID DriverObject, PVOID PhysicalDeviceObject);
NTSTATUS NTAPI PnpEnumerateDevices(VOID);
NTSTATUS NTAPI PnpStartDevice(PVOID DeviceObject);
NTSTATUS NTAPI PnpStopDevice(PVOID DeviceObject);
NTSTATUS NTAPI PnpRemoveDevice(PVOID DeviceObject);

#endif /* _PNP_H_ */
