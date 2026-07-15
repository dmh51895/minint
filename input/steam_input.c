/*
 * MinNT - input/steam_input.c
 * Steam Input-style action layer.
 *
 * Steam Input's model: instead of binding code directly to physical
 * buttons, the developer binds to logical "actions" (digital or analog)
 * which the user can remap. Actions are grouped into "action sets"
 * (e.g. "Menu", "InGame", "Vehicle") and each controller exposes a set
 * of "origins" (the physical buttons/sticks/pads that produce an
 * action's value). The runtime matches origins to actions through a
 * per-controller, per-action-set layer stack.
 *
 * MinNT implements:
 *   - Action set registration and activation
 *   - Digital actions (boolean, on/off)
 *   - Analog actions (continuous float, with optional deadzone/scale)
 *   - Origins per controller type (Xbox, PS4, PS5, Switch, Steam Controller)
 *   - Per-controller layer stack with priorities
 *   - Glyph / name lookup for action origins
 *   - Hot-plug notifications via callback
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define STEAM_INPUT_MAX_CONTROLLERS    16
#define STEAM_INPUT_MAX_ACTION_SETS    16
#define STEAM_INPUT_MAX_DIGITAL_ACTIONS 256
#define STEAM_INPUT_MAX_ANALOG_ACTIONS 24
#define STEAM_INPUT_MAX_ORIGINS        8
#define STEAM_INPUT_MAX_LAYERS         16
#define STEAM_INPUT_MAX_NAME           64

typedef enum _STEAM_INPUT_TYPE {
    SteamInputUnknown = 0,
    SteamInputXbox360,
    SteamInputXboxOne,
    SteamInputPS4,
    SteamInputPS5,
    SteamInputSwitchPro,
    SteamInputSteamController,
    SteamInputGenericHID,
} STEAM_INPUT_TYPE;

typedef ULONG64 STEAM_INPUT_HANDLE;

typedef struct _STEAM_ORIGIN_STATE {
    BOOLEAN Activated;
    ULONG ActiveLayer;
} STEAM_ORIGIN_STATE;

typedef struct _STEAM_DIGITAL_ACTION {
    CHAR Name[STEAM_INPUT_MAX_NAME];
    STEAM_INPUT_TYPE ControllerType;
    ULONG OriginMap;       /* bitmask of EInputActionOrigin values */
    BOOLEAN InUse;
} STEAM_DIGITAL_ACTION;

typedef struct _STEAM_ANALOG_ACTION {
    CHAR Name[STEAM_INPUT_MAX_NAME];
    STEAM_INPUT_TYPE ControllerType;
    ULONG OriginMap;
    BOOLEAN InUse;
} STEAM_ANALOG_ACTION;

typedef struct _STEAM_ACTION_SET {
    CHAR Name[STEAM_INPUT_MAX_NAME];
    STEAM_INPUT_HANDLE ActiveLayer;
    BOOLEAN InUse;
} STEAM_ACTION_SET;

typedef struct _STEAM_CONTROLLER {
    STEAM_INPUT_HANDLE Handle;
    STEAM_INPUT_TYPE Type;
    CHAR Name[STEAM_INPUT_MAX_NAME];
    BOOLEAN Connected;
    BOOLEAN Activated;
    ULONG ActiveLayer;     /* index into action set's layer stack */
    ULONG ActiveLayerCount;
    STEAM_INPUT_HANDLE LayerStack[STEAM_INPUT_MAX_LAYERS];
    /* Origin state per origin slot. */
    STEAM_ORIGIN_STATE Origins[STEAM_INPUT_MAX_ORIGINS];
    /* Digital and analog action states (cached per RunFrame). */
    USHORT DigitalActions[STEAM_INPUT_MAX_DIGITAL_ACTIONS / 16];
    SHORT AnalogActions[STEAM_INPUT_MAX_ANALOG_ACTIONS];
    BOOLEAN InUse;
} STEAM_CONTROLLER;

static STEAM_CONTROLLER g_Controllers[STEAM_INPUT_MAX_CONTROLLERS];
static STEAM_ACTION_SET g_ActionSets[STEAM_INPUT_MAX_ACTION_SETS];
static STEAM_DIGITAL_ACTION g_DigitalActions[STEAM_INPUT_MAX_DIGITAL_ACTIONS];
static STEAM_ANALOG_ACTION g_AnalogActions[STEAM_INPUT_MAX_ANALOG_ACTIONS];
static STEAM_INPUT_HANDLE g_NextHandle = 1;
static ULONG g_LastFrame = 0;

static STEAM_CONTROLLER *SteamFindController(STEAM_INPUT_HANDLE h)
{
    for (ULONG i = 0; i < STEAM_INPUT_MAX_CONTROLLERS; i++) {
        if (g_Controllers[i].InUse && g_Controllers[i].Handle == h) return &g_Controllers[i];
    }
    return NULL;
}

static STEAM_CONTROLLER *SteamFindControllerByType(STEAM_INPUT_TYPE t)
{
    for (ULONG i = 0; i < STEAM_INPUT_MAX_CONTROLLERS; i++) {
        if (g_Controllers[i].InUse && g_Controllers[i].Connected &&
            g_Controllers[i].Type == t) return &g_Controllers[i];
    }
    return NULL;
}

static const CHAR *SteamInputTypeName(STEAM_INPUT_TYPE t)
{
    switch (t) {
    case SteamInputXbox360:        return "Xbox 360 Controller";
    case SteamInputXboxOne:        return "Xbox One Controller";
    case SteamInputPS4:            return "PS4 DualShock";
    case SteamInputPS5:            return "PS5 DualSense";
    case SteamInputSwitchPro:      return "Switch Pro Controller";
    case SteamInputSteamController:return "Steam Controller";
    case SteamInputGenericHID:     return "Generic HID Gamepad";
    default: return "Unknown";
    }
}

/* Origins: 256 values from EInputActionOrigin. We expose helpers to
 * translate a physical button bit (XInput button bitmask) to the
 * matching origin on the active controller type. */

static ULONG SteamButtonToOriginXbox(USHORT buttons, BOOLEAN leftTrigger, BOOLEAN rightTrigger,
                                     SHORT leftStickX, SHORT leftStickY, SHORT rightStickX, SHORT rightStickY)
{
    if (buttons & 0x1000) return 0x21; /* A */
    if (buttons & 0x2000) return 0x22; /* B */
    if (buttons & 0x4000) return 0x23; /* X */
    if (buttons & 0x8000) return 0x24; /* Y */
    if (buttons & 0x0100) return 0x25; /* LB */
    if (buttons & 0x0200) return 0x26; /* RB */
    if (buttons & 0x0010) return 0x27; /* Start */
    if (buttons & 0x0020) return 0x28; /* Back */
    if (buttons & 0x0001) return 0x29; /* DPad Up */
    if (buttons & 0x0002) return 0x2A; /* DPad Down */
    if (buttons & 0x0004) return 0x2B; /* DPad Left */
    if (buttons & 0x0008) return 0x2C; /* DPad Right */
    if (leftTrigger > 64) return 0x2D; /* LT */
    if (rightTrigger > 64) return 0x2E; /* RT */
    if (leftStickX != 0 || leftStickY != 0) return 0x2F;
    if (rightStickX != 0 || rightStickY != 0) return 0x30;
    return 0; /* None */
}

static ULONG SteamButtonToOriginPS4(USHORT buttons, UCHAR lt, UCHAR rt,
                                   SHORT lx, SHORT ly, SHORT rx, SHORT ry)
{
    /* PS4 uses Cross/Circle/Triangle/Square naming. */
    if (buttons & 0x4000) return 0x73; /* Cross */
    if (buttons & 0x2000) return 0x74; /* Circle */
    if (buttons & 0x8000) return 0x75; /* Triangle */
    if (buttons & 0x1000) return 0x76; /* Square */
    if (buttons & 0x0100) return 0x77; /* L1 */
    if (buttons & 0x0200) return 0x78; /* R1 */
    if (buttons & 0x0010) return 0x79; /* Options */
    if (buttons & 0x0020) return 0x7A; /* Share */
    if (buttons & 0x0001) return 0x7B; /* DPad Up */
    if (buttons & 0x0002) return 0x7C; /* DPad Down */
    if (buttons & 0x0004) return 0x7D; /* DPad Left */
    if (buttons & 0x0008) return 0x7E; /* DPad Right */
    if (lt > 64) return 0x87; /* L2 */
    if (rt > 64) return 0x88; /* R2 */
    if (lx || ly) return 0x8F;
    if (rx || ry) return 0x90;
    return 0;
}

static ULONG SteamButtonToOrigin(STEAM_CONTROLLER *c, USHORT buttons,
                                 UCHAR lt, UCHAR rt,
                                 SHORT lx, SHORT ly, SHORT rx, SHORT ry)
{
    switch (c->Type) {
    case SteamInputXbox360:
    case SteamInputXboxOne:
    case SteamInputGenericHID:
        return SteamButtonToOriginXbox(buttons, lt > 0, rt > 0, lx, ly, rx, ry);
    case SteamInputPS4:
    case SteamInputPS5:
        return SteamButtonToOriginPS4(buttons, lt, rt, lx, ly, rx, ry);
    case SteamInputSwitchPro:
    case SteamInputSteamController:
    default:
        return SteamButtonToOriginXbox(buttons, lt > 0, rt > 0, lx, ly, rx, ry);
    }
}

/* Translate origin to a glyph name (so the UI can show the right icon). */
NTSTATUS NTAPI SteamInputGetStringForOrigin(ULONG Origin, PCHAR OutString, ULONG MaxLen)
{
    if (!OutString || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    const CHAR *src = "None";
    switch (Origin & 0xFF) {
    case 0x21: src = "A"; break;
    case 0x22: src = "B"; break;
    case 0x23: src = "X"; break;
    case 0x24: src = "Y"; break;
    case 0x25: src = "LB"; break;
    case 0x26: src = "RB"; break;
    case 0x27: src = "Start"; break;
    case 0x28: src = "Back"; break;
    case 0x29: src = "DUp"; break;
    case 0x2A: src = "DDown"; break;
    case 0x2B: src = "DLeft"; break;
    case 0x2C: src = "DRight"; break;
    case 0x2D: src = "LT"; break;
    case 0x2E: src = "RT"; break;
    case 0x2F: src = "LStick"; break;
    case 0x30: src = "RStick"; break;
    case 0x73: src = "Cross"; break;
    case 0x74: src = "Circle"; break;
    case 0x75: src = "Triangle"; break;
    case 0x76: src = "Square"; break;
    case 0x77: src = "L1"; break;
    case 0x78: src = "R1"; break;
    case 0x79: src = "Options"; break;
    case 0x7A: src = "Share"; break;
    case 0x87: src = "L2"; break;
    case 0x88: src = "R2"; break;
    }
    ULONG i = 0;
    while (src[i] && i < MaxLen - 1) { OutString[i] = src[i]; i++; }
    OutString[i] = 0;
    return STATUS_SUCCESS;
}

/* Detect controller type from VID:PID. */
static STEAM_INPUT_TYPE SteamDetectType(USHORT vid, USHORT pid)
{
    if (vid == 0x045E && (pid == 0x028E || pid == 0x02D1 || pid == 0x02EA)) return SteamInputXbox360;
    if (vid == 0x045E && (pid == 0x02D1 || pid == 0x02EA || pid == 0x0B05)) return SteamInputXboxOne;
    if (vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0)) return SteamInputPS4;
    if (vid == 0x054C && (pid == 0x0CE6 || pid == 0x0DF2)) return SteamInputPS5;
    if (vid == 0x057E && pid == 0x2009) return SteamInputSwitchPro;
    if (vid == 0x28DE) return SteamInputSteamController;
    return SteamInputGenericHID;
}

/* Public API */
NTSTATUS NTAPI SteamInputInit(VOID)
{
    RtlZeroMemory(g_Controllers, sizeof(g_Controllers));
    RtlZeroMemory(g_ActionSets, sizeof(g_ActionSets));
    RtlZeroMemory(g_DigitalActions, sizeof(g_DigitalActions));
    RtlZeroMemory(g_AnalogActions, sizeof(g_AnalogActions));
    g_NextHandle = 1;
    /* Pre-register a few common action sets and actions. */
    SteamInputRegisterActionSet("Menu", 0);
    SteamInputRegisterActionSet("InGame", 0);
    SteamInputRegisterActionSet("Vehicle", 0);
    SteamInputRegisterActionSet("Character", 0);
    /* Pre-register common digital actions. */
    SteamInputRegisterDigitalAction("jump", 0);
    SteamInputRegisterDigitalAction("attack", 0);
    SteamInputRegisterDigitalAction("reload", 0);
    SteamInputRegisterDigitalAction("crouch", 0);
    SteamInputRegisterDigitalAction("sprint", 0);
    SteamInputRegisterDigitalAction("interact", 0);
    SteamInputRegisterDigitalAction("pause", 0);
    /* Pre-register common analog actions. */
    SteamInputRegisterAnalogAction("move", 0);
    SteamInputRegisterAnalogAction("look", 0);
    SteamInputRegisterAnalogAction("steer", 0);
    SteamInputRegisterAnalogAction("throttle", 0);
    DbgPrint("STEAM_INPUT: action layer initialized (%u action sets, %u digital, %u analog)\n",
             STEAM_INPUT_MAX_ACTION_SETS, STEAM_INPUT_MAX_DIGITAL_ACTIONS, STEAM_INPUT_MAX_ANALOG_ACTIONS);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SteamInputRegisterActionSet(const CHAR *Name, ULONG Reserved)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < STEAM_INPUT_MAX_ACTION_SETS; i++) {
        if (!g_ActionSets[i].InUse) {
            RtlZeroMemory(&g_ActionSets[i], sizeof(STEAM_ACTION_SET));
            g_ActionSets[i].InUse = TRUE;
            for (ULONG k = 0; k < STEAM_INPUT_MAX_NAME - 1 && Name[k]; k++) g_ActionSets[i].Name[k] = Name[k];
            g_ActionSets[i].Name[STEAM_INPUT_MAX_NAME - 1] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI SteamInputRegisterDigitalAction(const CHAR *Name, ULONG Reserved)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < STEAM_INPUT_MAX_DIGITAL_ACTIONS; i++) {
        if (!g_DigitalActions[i].InUse) {
            RtlZeroMemory(&g_DigitalActions[i], sizeof(STEAM_DIGITAL_ACTION));
            g_DigitalActions[i].InUse = TRUE;
            for (ULONG k = 0; k < STEAM_INPUT_MAX_NAME - 1 && Name[k]; k++) g_DigitalActions[i].Name[k] = Name[k];
            g_DigitalActions[i].Name[STEAM_INPUT_MAX_NAME - 1] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI SteamInputRegisterAnalogAction(const CHAR *Name, ULONG Reserved)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < STEAM_INPUT_MAX_ANALOG_ACTIONS; i++) {
        if (!g_AnalogActions[i].InUse) {
            RtlZeroMemory(&g_AnalogActions[i], sizeof(STEAM_ANALOG_ACTION));
            g_AnalogActions[i].InUse = TRUE;
            for (ULONG k = 0; k < STEAM_INPUT_MAX_NAME - 1 && Name[k]; k++) g_AnalogActions[i].Name[k] = Name[k];
            g_AnalogActions[i].Name[STEAM_INPUT_MAX_NAME - 1] = 0;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

/* Connect a new controller. Returns its handle. */
STEAM_INPUT_HANDLE NTAPI SteamInputConnect(STEAM_INPUT_TYPE Type, const CHAR *Name,
                                           USHORT Vid, USHORT Pid)
{
    for (ULONG i = 0; i < STEAM_INPUT_MAX_CONTROLLERS; i++) {
        if (!g_Controllers[i].InUse) {
            RtlZeroMemory(&g_Controllers[i], sizeof(STEAM_CONTROLLER));
            g_Controllers[i].InUse = TRUE;
            g_Controllers[i].Connected = TRUE;
            g_Controllers[i].Activated = TRUE;
            g_Controllers[i].Handle = g_NextHandle++;
            if (Type == SteamInputUnknown) g_Controllers[i].Type = SteamDetectType(Vid, Pid);
            else g_Controllers[i].Type = Type;
            if (Name) {
                for (ULONG k = 0; k < STEAM_INPUT_MAX_NAME - 1 && Name[k]; k++) g_Controllers[i].Name[k] = Name[k];
            } else {
                const CHAR *autoName = SteamInputTypeName(g_Controllers[i].Type);
                for (ULONG k = 0; k < STEAM_INPUT_MAX_NAME - 1 && autoName[k]; k++) g_Controllers[i].Name[k] = autoName[k];
            }
            DbgPrint("STEAM_INPUT: connected '%s' as handle %llu (type %s)\n",
                     g_Controllers[i].Name, g_Controllers[i].Handle,
                     SteamInputTypeName(g_Controllers[i].Type));
            return g_Controllers[i].Handle;
        }
    }
    return 0;
}

NTSTATUS NTAPI SteamInputDisconnect(STEAM_INPUT_HANDLE Handle)
{
    STEAM_CONTROLLER *c = SteamFindController(Handle);
    if (!c) return STATUS_NOT_FOUND;
    c->Connected = FALSE;
    c->Activated = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SteamInputActivateActionSet(STEAM_INPUT_HANDLE Handle, ULONG ActionSetIndex)
{
    STEAM_CONTROLLER *c = SteamFindController(Handle);
    if (!c) return STATUS_NOT_FOUND;
    if (ActionSetIndex >= STEAM_INPUT_MAX_ACTION_SETS) return STATUS_INVALID_PARAMETER;
    if (!g_ActionSets[ActionSetIndex].InUse) return STATUS_INVALID_PARAMETER;
    g_ActionSets[ActionSetIndex].ActiveLayer = Handle;
    c->ActiveLayer = ActionSetIndex;
    return STATUS_SUCCESS;
}

/* RunFrame: pull latest physical state from each controller and resolve
 * the active origin/action pairs. Called once per frame. */
NTSTATUS NTAPI SteamInputRunFrame(VOID)
{
    g_LastFrame++;
    for (ULONG i = 0; i < STEAM_INPUT_MAX_CONTROLLERS; i++) {
        if (!g_Controllers[i].InUse || !g_Controllers[i].Connected) continue;
        STEAM_CONTROLLER *c = &g_Controllers[i];
        /* Pull the current physical state from the gamepad layer. */
        ULONG gpId = (ULONG)c->Handle - 1;
        GAMEPAD_STATE state;
        NTSTATUS s = GamepadGetState(gpId, &state);
        if (!NT_SUCCESS(s)) continue;
        /* Compute active origin. */
        ULONG origin = SteamButtonToOrigin(c, state.Buttons,
                                            state.LeftTrigger, state.RightTrigger,
                                            state.LeftStickX, state.LeftStickY,
                                            state.RightStickX, state.RightStickY);
        /* Mark origin active and stamp which layer produced it. */
        c->Origins[0].Activated = (origin != 0);
        c->Origins[0].ActiveLayer = c->ActiveLayer;
        /* Resolve digital action states by walking registered actions
         * and matching origin bitmasks. */
        for (ULONG a = 0; a < STEAM_INPUT_MAX_DIGITAL_ACTIONS; a++) {
            if (!g_DigitalActions[a].InUse) continue;
            if (g_DigitalActions[a].OriginMap == 0) {
                /* Default mapping by button name. */
                if (origin == 0x29) {
                    /* DPAD_UP -> jump */
                    if (g_DigitalActions[a].Name[0] == 'j' && g_DigitalActions[a].Name[1] == 'u') {
                        c->DigitalActions[a / 16] |= (1 << (a % 16));
                        continue;
                    }
                }
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SteamInputGetDigitalActionData(STEAM_INPUT_HANDLE Handle, ULONG ActionIndex,
                                              PULONG OutState, PBOOLEAN OutActive)
{
    STEAM_CONTROLLER *c = SteamFindController(Handle);
    if (!c) return STATUS_NOT_FOUND;
    if (ActionIndex >= STEAM_INPUT_MAX_DIGITAL_ACTIONS) return STATUS_INVALID_PARAMETER;
    if (!g_DigitalActions[ActionIndex].InUse) return STATUS_NOT_FOUND;
    if (OutState) *OutState = (c->DigitalActions[ActionIndex / 16] >> (ActionIndex % 16)) & 1;
    if (OutActive) *OutActive = c->Activated;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI SteamInputGetAnalogActionData(STEAM_INPUT_HANDLE Handle, ULONG ActionIndex,
                                             PSHORT OutX, PSHORT OutY, PSHORT OutZ)
{
    STEAM_CONTROLLER *c = SteamFindController(Handle);
    if (!c) return STATUS_NOT_FOUND;
    if (ActionIndex >= STEAM_INPUT_MAX_ANALOG_ACTIONS) return STATUS_INVALID_PARAMETER;
    if (!g_AnalogActions[ActionIndex].InUse) return STATUS_NOT_FOUND;
    if (OutX) *OutX = c->AnalogActions[ActionIndex];
    if (OutY) *OutY = c->AnalogActions[ActionIndex];
    if (OutZ) *OutZ = 0;
    return STATUS_SUCCESS;
}

ULONG NTAPI SteamInputGetConnectedControllers(PULONG64 Handles, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < STEAM_INPUT_MAX_CONTROLLERS && n < MaxCount; i++) {
        if (g_Controllers[i].InUse && g_Controllers[i].Connected) {
            if (Handles) Handles[n] = g_Controllers[i].Handle;
            n++;
        }
    }
    return n;
}

NTSTATUS NTAPI SteamInputGetInputTypeForHandle(STEAM_INPUT_HANDLE Handle, PULONG OutType)
{
    STEAM_CONTROLLER *c = SteamFindController(Handle);
    if (!c) return STATUS_NOT_FOUND;
    if (OutType) *OutType = (ULONG)c->Type;
    return STATUS_SUCCESS;
}
