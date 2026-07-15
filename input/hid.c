/*
 * MinNT - input/hid.c
 * Human Interface Device (HID) class driver.
 *
 * The HID class driver provides a transport-agnostic interface for
 * keyboards, mice, game controllers, touchscreens, and other input
 * devices. Real HID uses a HID descriptor parsed by the class driver
 * to map reports to usage tables; here we model the standard usages
 * for mouse (buttons, X/Y), keyboard (scan codes), and consumer
 * controls (volume, browser back).
 *
 * Devices attach via the USB HID class driver (already present in
 * usb/hid_kbd.c and usb/hid_mouse.c) and register with this module.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define HID_MAX_DEVICES 32
#define HID_USAGE_MAX   64

/* Usage pages we care about */
#define HID_USAGE_PAGE_GENERIC_DESKTOP   0x01
#define HID_USAGE_PAGE_KEYBOARD          0x07
#define HID_USAGE_PAGE_CONSUMER          0x0C

/* Generic desktop usages */
#define HID_USAGE_POINTER    0x01
#define HID_USAGE_MOUSE      0x02
#define HID_USAGE_KEYBOARD   0x06
#define HID_USAGE_X          0x30
#define HID_USAGE_Y          0x31
#define HID_USAGE_WHEEL      0x38
#define HID_USAGE_GAMEPAD    0x05

typedef enum _HID_DEVICE_TYPE {
    HidTypeUnknown = 0,
    HidTypeMouse,
    HidTypeKeyboard,
    HidTypeGamepad,
    HidTypeTouch,
    HidTypeConsumer,
} HID_DEVICE_TYPE;

typedef struct _HID_USAGE_MAPPING {
    USHORT UsagePage;
    USHORT Usage;
    USHORT BitOffset;
    USHORT BitLength;
    BOOLEAN InUse;
} HID_USAGE_MAPPING, *PHID_USAGE_MAPPING;

typedef struct _HID_DEVICE {
    ULONG Id;
    HID_DEVICE_TYPE Type;
    UCHAR Report[256];
    ULONG ReportLength;
    HID_USAGE_MAPPING Mappings[HID_USAGE_MAX];
    ULONG MappingCount;
    BOOLEAN InUse;
    CHAR Name[64];
} HID_DEVICE, *PHID_DEVICE;

static HID_DEVICE g_Devices[HID_MAX_DEVICES];

NTSTATUS NTAPI HidRegisterDevice(HID_DEVICE_TYPE Type, const CHAR *Name,
                                 PULONG OutDeviceId)
{
    for (ULONG i = 0; i < HID_MAX_DEVICES; i++) {
        if (!g_Devices[i].InUse) {
            RtlZeroMemory(&g_Devices[i], sizeof(HID_DEVICE));
            g_Devices[i].InUse = TRUE;
            g_Devices[i].Type = Type;
            g_Devices[i].Id = i + 1;
            for (ULONG k = 0; k < 64; k++) {
                g_Devices[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            /* Default mappings based on device type. */
            if (Type == HidTypeMouse) {
                g_Devices[i].Mappings[0] = (HID_USAGE_MAPPING){
                    HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_X, 8, 8, TRUE };
                g_Devices[i].Mappings[1] = (HID_USAGE_MAPPING){
                    HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_Y, 16, 8, TRUE };
                g_Devices[i].Mappings[2] = (HID_USAGE_MAPPING){
                    HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_WHEEL, 24, 8, TRUE };
                g_Devices[i].MappingCount = 3;
            } else if (Type == HidTypeKeyboard) {
                for (ULONG k = 0; k < 6; k++) {
                    g_Devices[i].Mappings[k] = (HID_USAGE_MAPPING){
                        HID_USAGE_PAGE_KEYBOARD, 0x04 + k, 8 + k * 8, 8, TRUE };
                }
                g_Devices[i].MappingCount = 6;
            } else if (Type == HidTypeGamepad) {
                g_Devices[i].Mappings[0] = (HID_USAGE_MAPPING){
                    HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_X, 0, 16, TRUE };
                g_Devices[i].Mappings[1] = (HID_USAGE_MAPPING){
                    HID_USAGE_PAGE_GENERIC_DESKTOP, HID_USAGE_Y, 16, 16, TRUE };
                g_Devices[i].MappingCount = 2;
            }
            if (OutDeviceId) *OutDeviceId = g_Devices[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI HidSubmitReport(ULONG DeviceId, PVOID Report, ULONG Length)
{
    if (DeviceId == 0 || DeviceId > HID_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    PHID_DEVICE d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    if (Length > sizeof(d->Report)) Length = sizeof(d->Report);
    if (Report && Length) RtlCopyMemory(d->Report, Report, Length);
    d->ReportLength = Length;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HidGetUsageValue(ULONG DeviceId, USHORT UsagePage,
                                USHORT Usage, PULONG OutValue)
{
    if (DeviceId == 0 || DeviceId > HID_MAX_DEVICES || !OutValue) return STATUS_INVALID_PARAMETER;
    PHID_DEVICE d = &g_Devices[DeviceId - 1];
    if (!d->InUse) return STATUS_NOT_FOUND;
    for (ULONG i = 0; i < d->MappingCount; i++) {
        if (d->Mappings[i].UsagePage == UsagePage && d->Mappings[i].Usage == Usage) {
            PUCHAR p = d->Report + (d->Mappings[i].BitOffset / 8);
            USHORT bit = d->Mappings[i].BitOffset % 8;
            ULONG value = 0;
            for (USHORT k = 0; k < d->Mappings[i].BitLength; k++) {
                ULONG byte = p[(bit + k) / 8];
                ULONG mask = 1u << ((bit + k) % 8);
                if (byte & mask) value |= (1u << k);
            }
            *OutValue = value;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI HidUnregisterDevice(ULONG DeviceId)
{
    if (DeviceId == 0 || DeviceId > HID_MAX_DEVICES) return STATUS_INVALID_PARAMETER;
    if (!g_Devices[DeviceId - 1].InUse) return STATUS_NOT_FOUND;
    RtlZeroMemory(&g_Devices[DeviceId - 1], sizeof(HID_DEVICE));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HidInit(VOID)
{
    RtlZeroMemory(g_Devices, sizeof(g_Devices));
    /* Pre-register the standard USB keyboard and mouse. */
    HidRegisterDevice(HidTypeKeyboard, "USB Keyboard", NULL);
    HidRegisterDevice(HidTypeMouse, "USB Mouse", NULL);
    DbgPrint("HID: class driver initialized\n");
    return STATUS_SUCCESS;
}
