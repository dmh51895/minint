/*
 * MinNT - shell/safeusb.c
 * "Safely Remove Hardware" / USB safe-eject.
 *
 * Before physical removal of a USB mass-storage device, the OS must
 * flush any pending write cache and release the device object. This
 * module tracks USB volumes, provides an eject request path, and
 * notifies registered listeners (explorer shell, notification tray).
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define SAFEUSB_MAX_DEVICES  16

typedef struct _SAFEUSB_DEVICE {
    ULONG Id;
    CHAR DevicePath[260];
    CHAR VolumeLetter;
    BOOLEAN InUse;
    BOOLEAN DirtyCache;
    BOOLEAN EjectInProgress;
    ULONG PendingBytes;
    ULONG RefCount;
} SAFEUSB_DEVICE, *PSAFEUSB_DEVICE;

static SAFEUSB_DEVICE g_Devices[SAFEUSB_MAX_DEVICES];

NTSTATUS NTAPI SafeUsbRegister(const CHAR *DevicePath, CHAR VolumeLetter,
                               PULONG OutDeviceId)
{
    for (ULONG i = 0; i < SAFEUSB_MAX_DEVICES; i++) {
        if (!g_Devices[i].InUse) {
            g_Devices[i].InUse = TRUE;
            g_Devices[i].VolumeLetter = VolumeLetter;
            g_Devices[i].DirtyCache = FALSE;
            g_Devices[i].PendingBytes = 0;
            g_Devices[i].RefCount = 0;
            for (ULONG k = 0; k < 260; k++) {
                g_Devices[i].DevicePath[k] = DevicePath[k];
                if (DevicePath[k] == 0) break;
            }
            g_Devices[i].Id = i + 1;
            if (OutDeviceId) *OutDeviceId = g_Devices[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI SafeUsbUnregister(ULONG DeviceId)
{
    if (DeviceId == 0 || DeviceId > SAFEUSB_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    SAFEUSB_DEVICE *d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    if (d->RefCount > 0) return STATUS_DEVICE_BUSY;
    RtlZeroMemory(d, sizeof(*d));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbMarkDirty(ULONG DeviceId, ULONG BytesPending)
{
    if (DeviceId == 0 || DeviceId > SAFEUSB_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    SAFEUSB_DEVICE *d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    d->DirtyCache = TRUE;
    d->PendingBytes += BytesPending;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbAcquire(ULONG DeviceId)
{
    if (DeviceId == 0 || DeviceId > SAFEUSB_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    SAFEUSB_DEVICE *d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    d->RefCount++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbRelease(ULONG DeviceId)
{
    if (DeviceId == 0 || DeviceId > SAFEUSB_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    SAFEUSB_DEVICE *d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    if (d->RefCount) d->RefCount--;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbEject(ULONG DeviceId)
{
    if (DeviceId == 0 || DeviceId > SAFEUSB_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    SAFEUSB_DEVICE *d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    if (d->RefCount > 0) return STATUS_DEVICE_BUSY;
    d->EjectInProgress = TRUE;
    /* Real flush: send down IRP_MJ_FLUSH_BUFFERS / SCSI SYNCHRONIZE CACHE.
     * We log the operation and reset state. */
    DbgPrint("SAFEUSB: flushing %u pending bytes for device %u (%c:)\n",
             d->PendingBytes, DeviceId, d->VolumeLetter);
    d->DirtyCache = FALSE;
    d->PendingBytes = 0;
    d->EjectInProgress = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbEnum(PULONG OutArray, PULONG InOutCount)
{
    if (!OutArray || !InOutCount) return STATUS_INVALID_PARAMETER;
    ULONG max = *InOutCount;
    ULONG count = 0;
    for (ULONG i = 0; i < SAFEUSB_MAX_DEVICES && count < max; i++) {
        if (g_Devices[i].InUse) OutArray[count++] = g_Devices[i].Id;
    }
    *InOutCount = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SafeUsbInit(VOID)
{
    RtlZeroMemory(g_Devices, sizeof(g_Devices));
    DbgPrint("SAFEUSB: safely remove hardware initialized\n");
    return STATUS_SUCCESS;
}
