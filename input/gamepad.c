/*
 * MinNT - input/gamepad.c
 * Game controller / gamepad input and calibration.
 *
 * Supports XInput-compatible controllers (Xbox layout):
 *   - 2 analog sticks (X, Y axis, 0-65535)
 *   - 2 analog triggers (LT, RT)
 *   - D-pad (up/down/left/right)
 *   - 4 face buttons (A, B, X, Y)
 *   - 4 shoulder buttons (LB, RB, L3, R3)
 *   - 2 stick buttons (L3, R3)
 *   - Start/Back buttons
 *
 * Calibration: deadzone (ignore small stick drift) and range (min/max
 * per axis) stored per-controller.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/framework.h>

#define MAX_GAMEPADS 4

/* GAMEPAD_STATE and GAMEPAD_CALIBRATION are defined in framework.h */

/* XInput button bitmask */
#define XINPUT_GAMEPAD_DPAD_UP        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008
#define XINPUT_GAMEPAD_START          0x0010
#define XINPUT_GAMEPAD_BACK           0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040  /* L3 */
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080  /* R3 */
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100  /* LB */
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200  /* RB */
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000

typedef struct _GAMEPAD {
    ULONG Id;
    WCHAR Name[64];
    BOOLEAN Connected;
    GAMEPAD_TYPE Type;
    USHORT VendorId;
    USHORT ProductId;
    GAMEPAD_STATE State;
    GAMEPAD_CALIBRATION Calibration;
    GAMEPAD_RUMBLE Rumble;
    BOOLEAN InUse;
} GAMEPAD, *PGAMEPAD;

typedef BOOLEAN (*GAMEPAD_HOTPLUG_CALLBACK)(ULONG Id, BOOLEAN Connected, PVOID Context);

typedef struct _HOTPLUG_ENTRY {
    GAMEPAD_HOTPLUG_CALLBACK Callback;
    PVOID Context;
    BOOLEAN InUse;
} HOTPLUG_ENTRY;

#define HOTPLUG_MAX_CALLBACKS 8

static GAMEPAD g_Gamepads[MAX_GAMEPADS];
static KSPIN_LOCK g_GamepadLock;
static HOTPLUG_ENTRY g_HotplugCallbacks[HOTPLUG_MAX_CALLBACKS];

/* Apply deadzone + range calibration to a stick axis value. */
static SHORT ApplyCalibration(SHORT raw, SHORT center, SHORT min, SHORT max)
{
    LONG diff = (LONG)raw - (LONG)center;
    LONG absDiff = diff < 0 ? -diff : diff;
    LONG deadzone = g_Gamepads[0].Calibration.Deadzone;
    if (absDiff < deadzone) return 0;
    if (diff > 0) {
        return (SHORT)(((LONG)(diff - deadzone) * 32767) /
                       (max - center - deadzone));
    } else {
        return (SHORT)(((LONG)(diff + deadzone) * -32768) /
                       (center - min - deadzone));
    }
}

NTSTATUS NTAPI GamepadInit(VOID)
{
    RtlZeroMemory(g_Gamepads, sizeof(g_Gamepads));
    KeInitializeSpinLock(&g_GamepadLock);
    DbgPrint("GAMEPAD: input subsystem initialized (%d slots)\n", MAX_GAMEPADS);
    return STATUS_SUCCESS;
}

/* Connect a gamepad. Returns its ID. */
ULONG NTAPI GamepadConnect(const WCHAR *Name)
{
    ULONG i;
    KIRQL irql;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    for (i = 0; i < MAX_GAMEPADS; i++) {
        if (!g_Gamepads[i].InUse) {
            RtlZeroMemory(&g_Gamepads[i], sizeof(GAMEPAD));
            g_Gamepads[i].Id = i;
            if (Name) {
                ULONG j = 0;
                while (Name[j] && j < 63) g_Gamepads[i].Name[j] = Name[j], j++;
                g_Gamepads[i].Name[j] = 0;
            }
            /* Default calibration: full range with small deadzone */
            g_Gamepads[i].Calibration.Deadzone = 4096;
            g_Gamepads[i].Calibration.LeftStickXMin = -32768;
            g_Gamepads[i].Calibration.LeftStickXMax = 32767;
            g_Gamepads[i].Calibration.LeftStickYMin = -32768;
            g_Gamepads[i].Calibration.LeftStickYMax = 32767;
            g_Gamepads[i].Calibration.RightStickXMin = -32768;
            g_Gamepads[i].Calibration.RightStickXMax = 32767;
            g_Gamepads[i].Calibration.RightStickYMin = -32768;
            g_Gamepads[i].Calibration.RightStickYMax = 32767;
            g_Gamepads[i].Calibration.Valid = TRUE;
            g_Gamepads[i].Connected = TRUE;
            g_Gamepads[i].InUse = TRUE;
            KeReleaseSpinLock(&g_GamepadLock, &irql);
            DbgPrint("GAMEPAD: connected '%ws' as id %u\n", Name ? Name : L"Gamepad", i);
            return i;
        }
    }
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return (ULONG)-1;
}

/* Disconnect a gamepad. */
NTSTATUS NTAPI GamepadDisconnect(ULONG Id)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Connected = FALSE;
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    DbgPrint("GAMEPAD: disconnected id %u\n", Id);
    return STATUS_SUCCESS;
}

/* Update a gamepad's input state (called by the controller driver). */
NTSTATUS NTAPI GamepadUpdateState(ULONG Id, PGAMEPAD_STATE NewState)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!NewState) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    {
        GAMEPAD *g = &g_Gamepads[Id];
        g->State = *NewState;
        /* Apply deadzone calibration to stick axes */
        if (g->Calibration.Valid) {
            g->State.LeftStickX = ApplyCalibration(NewState->LeftStickX, 0,
                                                    g->Calibration.LeftStickXMin,
                                                    g->Calibration.LeftStickXMax);
            g->State.LeftStickY = ApplyCalibration(NewState->LeftStickY, 0,
                                                    g->Calibration.LeftStickYMin,
                                                    g->Calibration.LeftStickYMax);
            g->State.RightStickX = ApplyCalibration(NewState->RightStickX, 0,
                                                     g->Calibration.RightStickXMin,
                                                     g->Calibration.RightStickXMax);
            g->State.RightStickY = ApplyCalibration(NewState->RightStickY, 0,
                                                     g->Calibration.RightStickYMin,
                                                     g->Calibration.RightStickYMax);
        }
    }
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return STATUS_SUCCESS;
}

/* Read the current state. */
NTSTATUS NTAPI GamepadGetState(ULONG Id, PGAMEPAD_STATE OutState)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutState) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    *OutState = g_Gamepads[Id].State;
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return STATUS_SUCCESS;
}

/* Set calibration parameters (for the device driver to update). */
NTSTATUS NTAPI GamepadSetCalibration(ULONG Id, PGAMEPAD_CALIBRATION Calibration)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!Calibration) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Calibration = *Calibration;
    g_Gamepads[Id].Calibration.Valid = TRUE;
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return STATUS_SUCCESS;
}

/* Get current calibration. */
NTSTATUS NTAPI GamepadGetCalibration(ULONG Id, PGAMEPAD_CALIBRATION OutCalibration)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutCalibration) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    *OutCalibration = g_Gamepads[Id].Calibration;
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return STATUS_SUCCESS;
}

/* Check if a button is currently pressed. */
BOOLEAN NTAPI GamepadIsButtonDown(ULONG Id, USHORT ButtonMask)
{
    BOOLEAN result = FALSE;
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return FALSE;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    result = (g_Gamepads[Id].State.Buttons & ButtonMask) != 0;
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return result;
}

/* Enumerate connected gamepads. */
ULONG NTAPI GamepadEnum(ULONG MaxCount, ULONG *pIds, PCHAR *pNames, PBOOLEAN pConnected)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    for (i = 0; i < MAX_GAMEPADS && n < MaxCount; i++) {
        if (g_Gamepads[i].InUse) {
            if (pIds) pIds[n] = g_Gamepads[i].Id;
            if (pNames) {
                ULONG j = 0;
                while (g_Gamepads[i].Name[j] && j < 63) pNames[n][j] = (CHAR)g_Gamepads[i].Name[j], j++;
                pNames[n][j] = 0;
            }
            if (pConnected) pConnected[n] = g_Gamepads[i].Connected;
            n++;
        }
    }
    KeReleaseSpinLock(&g_GamepadLock, &irql);
    return n;
}

/* Force feedback / rumble. */
NTSTATUS NTAPI GamepadSetRumble(ULONG Id, PGAMEPAD_RUMBLE Rumble)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!Rumble) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Rumble = *Rumble;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    DbgPrint("GAMEPAD: id=%u rumble L=%u R=%u LT=%u RT=%u\n",
             Id, Rumble->LeftMotor, Rumble->RightMotor,
             Rumble->LeftTrigger, Rumble->RightTrigger);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadGetRumble(ULONG Id, PGAMEPAD_RUMBLE OutRumble)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutRumble) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    *OutRumble = g_Gamepads[Id].Rumble;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadStopRumble(ULONG Id)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Rumble.LeftMotor = 0;
    g_Gamepads[Id].Rumble.RightMotor = 0;
    g_Gamepads[Id].Rumble.LeftTrigger = 0;
    g_Gamepads[Id].Rumble.RightTrigger = 0;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadSetType(ULONG Id, GAMEPAD_TYPE Type)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Type = Type;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadGetType(ULONG Id, PULONG OutType)
{
    KIRQL irql;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    if (!OutType) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    *OutType = (ULONG)g_Gamepads[Id].Type;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadDetectType(ULONG Id, USHORT Vid, USHORT Pid, PULONG OutType)
{
    if (!OutType) return STATUS_INVALID_PARAMETER;
    if (Id >= MAX_GAMEPADS || !g_Gamepads[Id].InUse) return STATUS_INVALID_PARAMETER;
    GAMEPAD_TYPE t = GamepadTypeGenericHID;
    if (Vid == 0x045E && (Pid == 0x028E || Pid == 0x02D1 || Pid == 0x02EA)) t = GamepadTypeXbox360;
    else if (Vid == 0x045E && Pid == 0x0B05) t = GamepadTypeXboxOne;
    else if (Vid == 0x054C && (Pid == 0x05C4 || Pid == 0x09CC || Pid == 0x0BA0)) t = GamepadTypePS4;
    else if (Vid == 0x054C && (Pid == 0x0CE6 || Pid == 0x0DF2)) t = GamepadTypePS5;
    else if (Vid == 0x057E && Pid == 0x2009) t = GamepadTypeSwitchPro;
    else if (Vid == 0x28DE) t = GamepadTypeSteamController;
    KIRQL irql;
    KeAcquireSpinLock(&g_GamepadLock, &irql);
    g_Gamepads[Id].Type = t;
    g_Gamepads[Id].VendorId = Vid;
    g_Gamepads[Id].ProductId = Pid;
    KeReleaseSpinLock(&g_GamepadLock, irql);
    *OutType = (ULONG)t;
    DbgPrint("GAMEPAD: detected type %u for VID=%04x PID=%04x\n", t, Vid, Pid);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI GamepadRegisterHotplugCallback(PVOID Callback)
{
    if (!Callback) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < HOTPLUG_MAX_CALLBACKS; i++) {
        if (!g_HotplugCallbacks[i].InUse) {
            g_HotplugCallbacks[i].InUse = TRUE;
            g_HotplugCallbacks[i].Callback = (GAMEPAD_HOTPLUG_CALLBACK)Callback;
            g_HotplugCallbacks[i].Context = NULL;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI GamepadNotifyHotplug(ULONG Id, BOOLEAN Connected)
{
    /* Fire all registered callbacks. */
    for (ULONG i = 0; i < HOTPLUG_MAX_CALLBACKS; i++) {
        if (g_HotplugCallbacks[i].InUse && g_HotplugCallbacks[i].Callback) {
            g_HotplugCallbacks[i].Callback(Id, Connected, g_HotplugCallbacks[i].Context);
        }
    }
    return STATUS_SUCCESS;
}
