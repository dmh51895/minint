/*
 * MinNT - input/touchpad.c
 * Touchpad driver layer for Steam Controller-style gamepads.
 *
 * The Steam Controller has two touchpads that can act as buttons,
 * trackpads, or analog sticks depending on the input mode. MinNT's
 * touchpad layer models:
 *   - Per-touch state (id, position, pressure, isContact)
 *   - Tap detection (short contact < threshold ms)
 *   - Drag detection (movement during contact)
 *   - Configurable modes (mouse, joystick, dpad, scroll wheel)
 *
 * HidSubmitReport from the HID class driver feeds raw coordinates
 * here; TouchpadUpdate converts them into button / axis events
 * consumed by GamepadUpdateState.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define TOUCHPAD_MAX_DEVICES 4
#define TOUCHPAD_MAX_CONTACTS_PER_PAD 2
#define TOUCHPAD_RESOLUTION 32767

typedef enum _TOUCHPAD_MODE {
    TouchpadModeMouse = 0,
    TouchpadModeJoystick,
    TouchpadModeDPad,
    TouchpadModeScrollWheel,
    TouchpadModeAbsoluteMouse,
    TouchpadModeDisabled,
} TOUCHPAD_MODE;

typedef struct _TOUCHPAD_CONTACT {
    ULONG Id;
    BOOLEAN Active;
    SHORT X, Y;
    UCHAR Pressure;
    /* Tap detection. */
    BOOLEAN Tapped;
    /* Drag detection. */
    BOOLEAN Dragging;
    SHORT DragStartX, DragStartY;
    SHORT LastX, LastY;
    ULONG64 StartTime;
} TOUCHPAD_CONTACT;

typedef struct _TOUCHPAD {
    ULONG Id;
    TOUCHPAD_MODE Mode;
    USHORT Width, Height;
    BOOLEAN InUse;
    BOOLEAN ClickEnabled;
    TOUCHPAD_CONTACT Contacts[TOUCHPAD_MAX_CONTACTS_PER_PAD];
    BOOLEAN ButtonState;  /* combined click button state */
} TOUCHPAD;

static TOUCHPAD g_Pads[TOUCHPAD_MAX_DEVICES];

NTSTATUS NTAPI TouchpadInit(VOID)
{
    RtlZeroMemory(g_Pads, sizeof(g_Pads));
    DbgPrint("TOUCHPAD: Steam Controller touchpad layer initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TouchpadCreate(ULONG Width, ULONG Height, PULONG OutId)
{
    for (ULONG i = 0; i < TOUCHPAD_MAX_DEVICES; i++) {
        if (!g_Pads[i].InUse) {
            RtlZeroMemory(&g_Pads[i], sizeof(TOUCHPAD));
            g_Pads[i].InUse = TRUE;
            g_Pads[i].Id = i;
            g_Pads[i].Width = Width ? Width : 32767;
            g_Pads[i].Height = Height ? Height : 32767;
            g_Pads[i].Mode = TouchpadModeMouse;
            g_Pads[i].ClickEnabled = TRUE;
            if (OutId) *OutId = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI TouchpadSetMode(ULONG Id, ULONG Mode)
{
    if (Id >= TOUCHPAD_MAX_DEVICES || !g_Pads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (Mode > TouchpadModeDisabled) return STATUS_INVALID_PARAMETER;
    g_Pads[Id].Mode = (TOUCHPAD_MODE)Mode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TouchpadGetMode(ULONG Id, PULONG OutMode)
{
    if (Id >= TOUCHPAD_MAX_DEVICES || !g_Pads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutMode) return STATUS_INVALID_PARAMETER;
    *OutMode = (ULONG)g_Pads[Id].Mode;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TouchpadSubmitContact(ULONG Id, ULONG ContactId, SHORT X, SHORT Y, UCHAR Pressure, BOOLEAN Active)
{
    if (Id >= TOUCHPAD_MAX_DEVICES || !g_Pads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (ContactId >= TOUCHPAD_MAX_CONTACTS_PER_PAD) return STATUS_INVALID_PARAMETER;
    TOUCHPAD_CONTACT *c = &g_Pads[Id].Contacts[ContactId];
    if (!c->Active && Active) {
        /* New contact. */
        LARGE_INTEGER t0;
        KeQueryPerformanceCounter(&t0, NULL);
        c->StartTime = (ULONG64)t0.QuadPart;
        c->DragStartX = X;
        c->DragStartY = Y;
        c->LastX = X;
        c->LastY = Y;
        c->Tapped = TRUE; /* until proven otherwise */
        c->Dragging = FALSE;
    } else if (c->Active && Active) {
        /* Continuing contact. */
        if (c->Tapped) {
            LONG dx = X - c->DragStartX;
            LONG dy = Y - c->DragStartY;
            LONG absDx = dx < 0 ? -dx : dx;
            LONG absDy = dy < 0 ? -dy : dy;
            if (absDx > 100 || absDy > 100) c->Tapped = FALSE;
        }
        c->LastX = X;
        c->LastY = Y;
    } else if (c->Active && !Active) {
        /* Released. */
        LARGE_INTEGER now;
        KeQueryPerformanceCounter(&now, NULL);
        ULONG64 elapsed = (ULONG64)(now.QuadPart - c->StartTime);
        if (c->Tapped && elapsed < 200000) {
            /* Confirmed tap. */
            g_Pads[Id].ButtonState = TRUE;
        } else {
            g_Pads[Id].ButtonState = FALSE;
        }
    }
    c->Active = Active;
    c->X = X;
    c->Y = Y;
    c->Pressure = Pressure;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TouchpadGetAxis(ULONG Id, PSHORT OutX, PSHORT OutY)
{
    if (Id >= TOUCHPAD_MAX_DEVICES || !g_Pads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutX && !OutY) return STATUS_INVALID_PARAMETER;
    if (g_Pads[Id].Mode != TouchpadModeJoystick) return STATUS_NOT_SUPPORTED;
    if (g_Pads[Id].Contacts[0].Active) {
        if (OutX) *OutX = (SHORT)((LONG)g_Pads[Id].Contacts[0].X * 2 - (LONG)g_Pads[Id].Width);
        if (OutY) *OutY = (SHORT)((LONG)g_Pads[Id].Contacts[0].Y * 2 - (LONG)g_Pads[Id].Height);
    } else {
        if (OutX) *OutX = 0;
        if (OutY) *OutY = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TouchpadGetButton(ULONG Id, PBOOLEAN OutDown)
{
    if (Id >= TOUCHPAD_MAX_DEVICES || !g_Pads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutDown) return STATUS_INVALID_PARAMETER;
    *OutDown = g_Pads[Id].ButtonState;
    return STATUS_SUCCESS;
}

ULONG NTAPI TouchpadEnum(PULONG OutArray, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < TOUCHPAD_MAX_DEVICES && n < MaxCount; i++) {
        if (g_Pads[i].InUse) OutArray[n++] = i;
    }
    return n;
}
