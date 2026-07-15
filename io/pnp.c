/*
 * MinNT - io/pnp.c
 * Plug-and-Play manager: device tree, hardware ID matching, and a
 * driver store with signature verification.
 *
 * The PnP manager enumerates the ACPI/PCI/USB device tree, lets each
 * devnode advertise its compatible IDs, and matches incoming drivers
 * against those IDs using the INF-style ranking rules. The driver
 * store tracks the on-disk drivers and refuses to load any that
 * haven't been signed by the trusted signer key.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ob.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define PNP_MAX_DEVICES       128
#define PNP_MAX_COMPAT        8
#define PNP_MAX_DRIVERS       64
#define PNP_MAX_ID            64

typedef enum _PNP_DEVICE_STATE {
    PnpStateInvalid = 0,
    PnpStateEnumerating,
    PnpStatePresent,
    PnpStateStarted,
    PnpStateStopped,
    PnpStateRemoved,
} PNP_DEVICE_STATE;

typedef struct _PNP_DEVICE_NODE {
    ULONG Id;
    CHAR InstanceId[PNP_MAX_ID];
    CHAR CompatibleIds[PNP_MAX_COMPAT][PNP_MAX_ID];
    ULONG CompatibleCount;
    CHAR Location[64];
    PNP_DEVICE_STATE State;
    ULONG Parent;
    ULONG Children[8];
    ULONG ChildCount;
    ULONG DriverIndex; /* index into driver store, -1 if none */
    BOOLEAN InUse;
} PNP_DEVICE_NODE, *PPNP_DEVICE_NODE;

typedef struct _PNP_DRIVER {
    CHAR Name[PNP_MAX_ID];
    CHAR Provider[64];
    UCHAR Signature[64];      /* simulated SHA-style hash */
    ULONG SignatureLength;
    UCHAR PublicKey[64];      /* simulated RSA pubkey */
    ULONG PublicKeyLength;
    BOOLEAN Signed;
    BOOLEAN Trusted;
    CHAR CompatibleIds[PNP_MAX_COMPAT][PNP_MAX_ID];
    ULONG CompatibleCount;
    PVOID DriverEntry;
    BOOLEAN InUse;
} PNP_DRIVER, *PPNP_DRIVER;

static PNP_DEVICE_NODE g_Devices[PNP_MAX_DEVICES];
static PNP_DRIVER g_Drivers[PNP_MAX_DRIVERS];
static ULONG g_NextDeviceId = 1;

/* "Signature verification": a real driver store verifies the embedded
 * certificate chain against trusted root keys. We simulate this with a
 * fixed expected signature prefix. */
static const UCHAR TrustedSignerSig[32] = {
    0x4D, 0x69, 0x6E, 0x4E, 0x54, 0x20, 0x54, 0x72,
    0x75, 0x73, 0x74, 0x65, 0x64, 0x20, 0x52, 0x6F,
    0x6F, 0x74, 0x20, 0x43, 0x41, 0x20, 0x32, 0x30,
    0x32, 0x36, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21,
};

static BOOLEAN PnpVerifySignature(PPNP_DRIVER d)
{
    if (!d->Signed) return FALSE;
    if (d->SignatureLength < 32) return FALSE;
    for (ULONG i = 0; i < 32; i++) {
        if (d->Signature[i] != TrustedSignerSig[i]) return FALSE;
    }
    return TRUE;
}

NTSTATUS NTAPI PnpCreateDevice(const CHAR *InstanceId, ULONG ParentId,
                               PULONG OutDeviceId)
{
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (!g_Devices[i].InUse) {
            g_Devices[i].InUse = TRUE;
            g_Devices[i].Id = g_NextDeviceId++;
            for (ULONG k = 0; k < PNP_MAX_ID; k++) {
                g_Devices[i].InstanceId[k] = InstanceId[k];
                if (InstanceId[k] == 0) break;
            }
            g_Devices[i].Parent = ParentId;
            g_Devices[i].ChildCount = 0;
            g_Devices[i].DriverIndex = (ULONG)-1;
            g_Devices[i].State = PnpStateEnumerating;
            if (ParentId) {
                for (ULONG j = 0; j < PNP_MAX_DEVICES; j++) {
                    if (g_Devices[j].InUse && g_Devices[j].Id == ParentId) {
                        if (g_Devices[j].ChildCount < 8) {
                            g_Devices[j].Children[g_Devices[j].ChildCount++] = g_Devices[i].Id;
                        }
                        break;
                    }
                }
            }
            if (OutDeviceId) *OutDeviceId = g_Devices[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI PnpAddCompatibleId(ULONG DeviceId, const CHAR *Id)
{
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (g_Devices[i].InUse && g_Devices[i].Id == DeviceId) {
            if (g_Devices[i].CompatibleCount >= PNP_MAX_COMPAT) return STATUS_NO_MEMORY;
            ULONG k = g_Devices[i].CompatibleCount;
            for (ULONG n = 0; n < PNP_MAX_ID; n++) {
                g_Devices[i].CompatibleIds[k][n] = Id[n];
                if (Id[n] == 0) break;
            }
            g_Devices[i].CompatibleCount++;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI PnpRegisterDriver(const CHAR *Name, const CHAR *Provider,
                                 PVOID DriverEntry,
                                 const CHAR *CompatibleIds[], ULONG CompatibleCount,
                                 PUCHAR Signature, ULONG SignatureLength)
{
    for (ULONG i = 0; i < PNP_MAX_DRIVERS; i++) {
        if (!g_Drivers[i].InUse) {
            RtlZeroMemory(&g_Drivers[i], sizeof(PNP_DRIVER));
            for (ULONG k = 0; k < PNP_MAX_ID; k++) {
                g_Drivers[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            for (ULONG k = 0; k < 64; k++) {
                g_Drivers[i].Provider[k] = Provider[k];
                if (Provider[k] == 0) break;
            }
            g_Drivers[i].DriverEntry = DriverEntry;
            g_Drivers[i].InUse = TRUE;
            g_Drivers[i].Signed = Signature && SignatureLength > 0;
            if (g_Drivers[i].Signed) {
                g_Drivers[i].SignatureLength = SignatureLength < 64 ? SignatureLength : 64;
                for (ULONG k = 0; k < g_Drivers[i].SignatureLength; k++) {
                    g_Drivers[i].Signature[k] = Signature[k];
                }
            }
            g_Drivers[i].CompatibleCount = 0;
            for (ULONG c = 0; c < CompatibleCount && c < PNP_MAX_COMPAT; c++) {
                for (ULONG n = 0; n < PNP_MAX_ID; n++) {
                    g_Drivers[i].CompatibleIds[c][n] = CompatibleIds[c][n];
                    if (CompatibleIds[c][n] == 0) break;
                }
                g_Drivers[i].CompatibleCount++;
            }
            g_Drivers[i].Trusted = PnpVerifySignature(&g_Drivers[i]);
            DbgPrint("PNP: registered driver %s (%s), trusted=%u\n",
                     Name, Provider, g_Drivers[i].Trusted ? 1 : 0);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

static BOOLEAN PnpIdMatches(const CHAR *a, const CHAR *b)
{
    while (*a && *b) {
        if (*a != *b) return FALSE;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Find the best matching driver for a device. */
static LONG PnpFindDriverForDevice(ULONG DeviceId)
{
    PPNP_DEVICE_NODE dev = NULL;
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (g_Devices[i].InUse && g_Devices[i].Id == DeviceId) { dev = &g_Devices[i]; break; }
    }
    if (!dev) return -1;
    LONG best = -1;
    for (ULONG d = 0; d < PNP_MAX_DRIVERS; d++) {
        if (!g_Drivers[d].InUse) continue;
        if (!g_Drivers[d].Trusted) continue;
        for (ULONG c = 0; c < dev->CompatibleCount; c++) {
            for (ULONG k = 0; k < g_Drivers[d].CompatibleCount; k++) {
                if (PnpIdMatches(dev->CompatibleIds[c], g_Drivers[d].CompatibleIds[k])) {
                    if (best < 0 || (LONG)d < best) best = (LONG)d;
                }
            }
        }
    }
    return best;
}

NTSTATUS NTAPI PnpStartDevice(ULONG DeviceId)
{
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (g_Devices[i].InUse && g_Devices[i].Id == DeviceId) {
            LONG drv = PnpFindDriverForDevice(DeviceId);
            if (drv < 0) {
                DbgPrint("PNP: no driver for device %u (%s)\n",
                         DeviceId, g_Devices[i].InstanceId);
                return STATUS_NO_SUCH_DEVICE;
            }
            g_Devices[i].DriverIndex = (ULONG)drv;
            g_Devices[i].State = PnpStateStarted;
            DbgPrint("PNP: started device %u (%s) with driver %s\n",
                     DeviceId, g_Devices[i].InstanceId, g_Drivers[drv].Name);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI PnpStopDevice(ULONG DeviceId)
{
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (g_Devices[i].InUse && g_Devices[i].Id == DeviceId) {
            g_Devices[i].State = PnpStateStopped;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI PnpEnumerateChildDevices(ULONG ParentId, PULONG OutArray, PULONG OutCount)
{
    ULONG count = 0;
    ULONG cap = *OutCount;
    for (ULONG i = 0; i < PNP_MAX_DEVICES; i++) {
        if (g_Devices[i].InUse && g_Devices[i].Parent == ParentId) {
            if (count < cap) OutArray[count] = g_Devices[i].Id;
            count++;
        }
    }
    *OutCount = count;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI PnpInit(VOID)
{
    RtlZeroMemory(g_Devices, sizeof(g_Devices));
    RtlZeroMemory(g_Drivers, sizeof(g_Drivers));
    g_NextDeviceId = 1;

    /* Build a minimal PCI-shaped tree. */
    ULONG root, pci, ctrl;
    PnpCreateDevice("ACPI\\PNP0A03\\0", 0, &root);
    PnpCreateDevice("PCI\\VEN_8086\\DEV_1234\\0", root, &pci);
    PnpCreateDevice("PCI\\VEN_8086\\DEV_7000\\0", pci, &ctrl);
    PnpAddCompatibleId(ctrl, "PCI\\VEN_8086&DEV_7000");
    PnpAddCompatibleId(ctrl, "*PNP0600");
    return STATUS_SUCCESS;
}
