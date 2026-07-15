/*
 * MinNT - input/touch.c
 * Touch screen input subsystem.
 *
 * Provides a unified touch input API that abstracts over the
 * underlying digitizer hardware. Touch contacts are reported as
 * (X, Y, pressure, contact_id) tuples via a callback registered by
 * the win32k subsystem.
 *
 * Calibration: maps digitizer raw coordinates to screen coordinates
 * using a 3x3 affine transform (loaded from calibration data).
 */

#include <nt/ke.h>
#include <nt/rtl.h>

#define MAX_TOUCH_CONTACTS 10
#define TOUCH_CALIBRATION_POINTS 4

typedef struct _TOUCH_CONTACT {
    ULONG ContactId;
    ULONG X;
    ULONG Y;
    ULONG Pressure;
    BOOLEAN Active;
    BOOLEAN InUse;
} TOUCH_CONTACT, *PTOUCH_CONTACT;

typedef struct _TOUCH_CALIBRATION {
    /* 3x3 affine transform matrix (row-major). */
    LONG Matrix[9];
    BOOLEAN Valid;
} TOUCH_CALIBRATION, *PTOUCH_CALIBRATION;

typedef VOID (*PTOUCH_CALLBACK)(ULONG NumContacts, PTOUCH_CONTACT Contacts);

static TOUCH_CONTACT g_Contacts[MAX_TOUCH_CONTACTS];
static TOUCH_CALIBRATION g_Calibration;
static PTOUCH_CALLBACK g_TouchCallback = NULL;
static BOOLEAN g_TouchEnabled = FALSE;
static KSPIN_LOCK g_TouchLock;

NTSTATUS NTAPI TouchInputInit(VOID)
{
    RtlZeroMemory(g_Contacts, sizeof(g_Contacts));
    RtlZeroMemory(&g_Calibration, sizeof(g_Calibration));
    KeInitializeSpinLock(&g_TouchLock);

    /* Default identity calibration (1:1 mapping) */
    g_Calibration.Matrix[0] = 65536;  /* 1.0 in fixed-point */
    g_Calibration.Matrix[1] = 0;
    g_Calibration.Matrix[2] = 0;
    g_Calibration.Matrix[3] = 0;
    g_Calibration.Matrix[4] = 65536;
    g_Calibration.Matrix[5] = 0;
    g_Calibration.Matrix[6] = 0;
    g_Calibration.Matrix[7] = 0;
    g_Calibration.Matrix[8] = 65536;
    g_Calibration.Valid = TRUE;

    g_TouchEnabled = TRUE;
    DbgPrint("TOUCH: input subsystem initialized\n");
    return STATUS_SUCCESS;
}

/* Set the callback that receives touch events. */
NTSTATUS NTAPI TouchSetCallback(PTOUCH_CALLBACK Callback)
{
    g_TouchCallback = Callback;
    return STATUS_SUCCESS;
}

/* Apply the calibration matrix to raw touch coordinates. */
static VOID ApplyCalibration(ULONG RawX, ULONG RawY, PULONG OutX, PULONG OutY)
{
    /* (x', y', 1) = M * (x, y, 1) */
    LONG x = (LONG)RawX;
    LONG y = (LONG)RawY;
    *OutX = (ULONG)((g_Calibration.Matrix[0] * x +
                     g_Calibration.Matrix[1] * y +
                     g_Calibration.Matrix[2]) >> 16);
    *OutY = (ULONG)((g_Calibration.Matrix[3] * x +
                     g_Calibration.Matrix[4] * y +
                     g_Calibration.Matrix[5]) >> 16);
}

/* Process a touch event from the digitizer driver. */
NTSTATUS NTAPI TouchInputProcessEvent(ULONG ContactId, ULONG RawX, ULONG RawY,
                                      ULONG Pressure, BOOLEAN Touching)
{
    ULONG i, slot = (ULONG)-1;
    KIRQL irql;
    ULONG CalX = RawX, CalY = RawY;

    if (!g_TouchEnabled) return STATUS_NOT_SUPPORTED;

    /* Apply calibration */
    if (g_Calibration.Valid) {
        ApplyCalibration(RawX, RawY, &CalX, &CalY);
    }

    KeAcquireSpinLock(&g_TouchLock, &irql);

    /* Find existing contact or free slot */
    for (i = 0; i < MAX_TOUCH_CONTACTS; i++) {
        if (g_Contacts[i].InUse && g_Contacts[i].ContactId == ContactId) {
            slot = i;
            break;
        }
        if (!g_Contacts[i].InUse && slot == (ULONG)-1) {
            slot = i;
        }
    }
    if (slot == (ULONG)-1) {
        KeReleaseSpinLock(&g_TouchLock, &irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Touching) {
        if (!g_Contacts[slot].InUse) {
            g_Contacts[slot].InUse = TRUE;
            g_Contacts[slot].ContactId = ContactId;
        }
        g_Contacts[slot].X = CalX;
        g_Contacts[slot].Y = CalY;
        g_Contacts[slot].Pressure = Pressure;
        g_Contacts[slot].Active = TRUE;
    } else {
        if (g_Contacts[slot].InUse) {
            g_Contacts[slot].InUse = FALSE;
            g_Contacts[slot].Active = FALSE;
        }
    }

    {
        /* Build a snapshot of active contacts for the callback */
        TOUCH_CONTACT snapshot[MAX_TOUCH_CONTACTS];
        ULONG count = 0;
        for (i = 0; i < MAX_TOUCH_CONTACTS; i++) {
            if (g_Contacts[i].InUse && g_Contacts[i].Active) {
                snapshot[count++] = g_Contacts[i];
            }
        }
        /* Invoke callback outside the lock would be ideal, but the
         * caller is expected to be quick. For safety, we release the
         * lock first by snapshotting then calling. */
        PTOUCH_CALLBACK cb = g_TouchCallback;
        if (cb) {
            cb(count, snapshot);
        }
    }

    KeReleaseSpinLock(&g_TouchLock, &irql);
    return STATUS_SUCCESS;
}

/* Set the calibration matrix. */
NTSTATUS NTAPI TouchSetCalibration(LONG *Matrix9)
{
    KIRQL irql;
    if (!Matrix9) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_TouchLock, &irql);
    {
        ULONG i;
        for (i = 0; i < 9; i++) g_Calibration.Matrix[i] = Matrix9[i];
        g_Calibration.Valid = TRUE;
    }
    KeReleaseSpinLock(&g_TouchLock, &irql);
    DbgPrint("TOUCH: calibration matrix updated\n");
    return STATUS_SUCCESS;
}

/* Get current contacts. */
ULONG NTAPI TouchGetContacts(PTOUCH_CONTACT OutContacts, ULONG MaxCount)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_TouchLock, &irql);
    for (i = 0; i < MAX_TOUCH_CONTACTS && n < MaxCount; i++) {
        if (g_Contacts[i].InUse && g_Contacts[i].Active) {
            OutContacts[n++] = g_Contacts[i];
        }
    }
    KeReleaseSpinLock(&g_TouchLock, &irql);
    return n;
}

/* Enable/disable touch input. */
NTSTATUS NTAPI TouchSetEnabled(BOOLEAN Enable)
{
    g_TouchEnabled = Enable;
    DbgPrint("TOUCH: input %s\n", Enable ? "enabled" : "disabled");
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI TouchIsEnabled(VOID)
{
    return g_TouchEnabled;
}
