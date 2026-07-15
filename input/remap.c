/*
 * MinNT - input/remap.c
 * Controller button-to-action remapping.
 *
 * Allows the user to rebind any physical button on any supported
 * controller to a logical "binding" (jump, attack, etc.). Bindings
 * persist to the registry and are applied when a controller connects.
 * MinNT supports per-controller-type binding tables so a PS4 user can
 * have different mappings than an Xbox user.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define REMAP_MAX_BINDINGS   64
#define REMAP_MAX_TYPES      8
#define REMAP_NAME_MAX       32

typedef struct _REMAP_BINDING {
    ULONG Type;
    USHORT Button;       /* physical button bit */
    CHAR ActionName[REMAP_NAME_MAX];
    BOOLEAN InUse;
} REMAP_BINDING;

typedef struct _REMAP_TYPE_TABLE {
    ULONG Type;
    REMAP_BINDING Bindings[REMAP_MAX_BINDINGS];
    ULONG Count;
    BOOLEAN InUse;
} REMAP_TYPE_TABLE;

static REMAP_TYPE_TABLE g_Tables[REMAP_MAX_TYPES];

static REMAP_TYPE_TABLE *RemapGetTable(ULONG type)
{
    for (ULONG i = 0; i < REMAP_MAX_TYPES; i++) {
        if (g_Tables[i].InUse && g_Tables[i].Type == type) return &g_Tables[i];
    }
    for (ULONG i = 0; i < REMAP_MAX_TYPES; i++) {
        if (!g_Tables[i].InUse) {
            g_Tables[i].InUse = TRUE;
            g_Tables[i].Type = type;
            g_Tables[i].Count = 0;
            return &g_Tables[i];
        }
    }
    return NULL;
}

NTSTATUS NTAPI RemapBind(ULONG Type, USHORT Button, const CHAR *Action)
{
    if (!Action) return STATUS_INVALID_PARAMETER;
    REMAP_TYPE_TABLE *t = RemapGetTable(Type);
    if (!t) return STATUS_NO_MEMORY;
    for (ULONG i = 0; i < t->Count; i++) {
        if (t->Bindings[i].Button == Button) {
            RtlZeroMemory(t->Bindings[i].ActionName, REMAP_NAME_MAX);
            for (ULONG k = 0; k < REMAP_NAME_MAX - 1 && Action[k]; k++) t->Bindings[i].ActionName[k] = Action[k];
            return STATUS_SUCCESS;
        }
    }
    if (t->Count >= REMAP_MAX_BINDINGS) return STATUS_NO_MEMORY;
    REMAP_BINDING *b = &t->Bindings[t->Count];
    RtlZeroMemory(b, sizeof(*b));
    b->InUse = TRUE;
    b->Type = Type;
    b->Button = Button;
    for (ULONG k = 0; k < REMAP_NAME_MAX - 1 && Action[k]; k++) b->ActionName[k] = Action[k];
    t->Count++;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RemapUnbind(ULONG Type, USHORT Button)
{
    REMAP_TYPE_TABLE *t = RemapGetTable(Type);
    if (!t) return STATUS_NOT_FOUND;
    for (ULONG i = 0; i < t->Count; i++) {
        if (t->Bindings[i].Button == Button) {
            RtlZeroMemory(&t->Bindings[i], sizeof(REMAP_BINDING));
            for (ULONG k = i; k < t->Count - 1; k++) {
                t->Bindings[k] = t->Bindings[k + 1];
            }
            RtlZeroMemory(&t->Bindings[t->Count - 1], sizeof(REMAP_BINDING));
            t->Count--;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI RemapResolve(ULONG Type, USHORT Button, PCHAR OutAction, ULONG MaxLen)
{
    if (!OutAction || MaxLen == 0) return STATUS_INVALID_PARAMETER;
    REMAP_TYPE_TABLE *t = RemapGetTable(Type);
    if (!t) { OutAction[0] = 0; return STATUS_NOT_FOUND; }
    for (ULONG i = 0; i < t->Count; i++) {
        if (t->Bindings[i].Button == Button) {
            ULONG k = 0;
            while (t->Bindings[i].ActionName[k] && k < MaxLen - 1) {
                OutAction[k] = t->Bindings[i].ActionName[k];
                k++;
            }
            OutAction[k] = 0;
            return STATUS_SUCCESS;
        }
    }
    OutAction[0] = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI RemapApply(ULONG GamepadId, USHORT Buttons, PCHAR OutActions, ULONG MaxLen, PULONG OutCount)
{
    if (!OutActions || !OutCount) return STATUS_INVALID_PARAMETER;
    ULONG gpType = 0;
    GamepadGetType(GamepadId, &gpType);
    REMAP_TYPE_TABLE *t = RemapGetTable(gpType);
    if (!t) { *OutCount = 0; return STATUS_NOT_FOUND; }
    ULONG count = 0;
    ULONG pos = 0;
    for (ULONG i = 0; i < t->Count && pos + REMAP_NAME_MAX < MaxLen; i++) {
        if (Buttons & t->Bindings[i].Button) {
            ULONG k = 0;
            while (t->Bindings[i].ActionName[k] && pos < MaxLen - 1) {
                OutActions[pos++] = t->Bindings[i].ActionName[k++];
            }
            OutActions[pos++] = ',';
            count++;
        }
    }
    if (pos > 0) pos--;
    OutActions[pos] = 0;
    *OutCount = count;
    return STATUS_SUCCESS;
}

ULONG NTAPI RemapCount(ULONG Type)
{
    for (ULONG i = 0; i < REMAP_MAX_TYPES; i++) {
        if (g_Tables[i].InUse && g_Tables[i].Type == Type) return g_Tables[i].Count;
    }
    return 0;
}

NTSTATUS NTAPI RemapInit(VOID)
{
    RtlZeroMemory(g_Tables, sizeof(g_Tables));
    /* Seed Xbox controller default bindings. */
    RemapBind(1 /* GamepadTypeXbox360 */, 0x1000, "attack");
    RemapBind(1, 0x2000, "reload");
    RemapBind(1, 0x4000, "jump");
    RemapBind(1, 0x8000, "interact");
    RemapBind(1, 0x0100, "alt-fire");
    RemapBind(1, 0x0200, "block");
    RemapBind(1, 0x0010, "pause");
    DbgPrint("REMAP: controller remapping initialized\n");
    return STATUS_SUCCESS;
}
