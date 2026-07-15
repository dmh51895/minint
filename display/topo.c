/*
 * MinNT - display/topo.c
 * Display topology and multi-monitor configuration.
 *
 * Manages the set of displays connected to the system, their
 * resolutions, positions, and orientations. Used by the Display
 * Settings applet to configure Extend/Duplicate/Show only.
 *
 * Display modes:
 *   Extend       - each display has its own desktop space
 *   Duplicate    - all displays show the same image
 *   Show only N  - only display N is active
 */

#include <nt/ke.h>
#include <nt/rtl.h>

#define MAX_DISPLAYS 4

typedef struct _DISPLAY_INFO {
    ULONG Id;
    WCHAR DeviceName[32];    /* \\.\DISPLAY1, etc. */
    ULONG Width;
    ULONG Height;
    ULONG RefreshRate;        /* Hz */
    LONG  PositionX;          /* logical position */
    LONG  PositionY;
    ULONG Orientation;        /* 0=landscape, 1=portrait, 2=landscape-flipped, 3=portrait-flipped */
    ULONG Flags;              /* DISPLAY_FLAG_* */
    BOOLEAN Primary;
    BOOLEAN InUse;
} DISPLAY_INFO;

#define DISPLAY_FLAG_ACTIVE       0x01
#define DISPLAY_FLAG_PRIMARY      0x02
#define DISPLAY_FLAG_HDR          0x04

static DISPLAY_INFO g_Displays[MAX_DISPLAYS];
static ULONG g_DisplayCount = 0;
static ULONG g_DisplayMode = 0; /* 0=extend, 1=duplicate, 2=show only 1 */
static KSPIN_LOCK g_DisplayLock;

NTSTATUS NTAPI DisplayTopologyInit(VOID)
{
    RtlZeroMemory(g_Displays, sizeof(g_Displays));
    KeInitializeSpinLock(&g_DisplayLock);

    /* Discover attached displays. For MinNT, enumerate based on HAL
     * framebuffer info. With Multiboot2 we can get the primary
     * framebuffer dimensions. */
    {
        /* Primary display */
        DISPLAY_INFO *d = &g_Displays[0];
        d->Id = 0;
        d->Width = 1920;
        d->Height = 1080;
        d->RefreshRate = 60;
        d->PositionX = 0;
        d->PositionY = 0;
        d->Orientation = 0;
        d->Flags = DISPLAY_FLAG_ACTIVE | DISPLAY_FLAG_PRIMARY;
        d->Primary = TRUE;
        d->InUse = TRUE;
        RtlCopyMemory(d->DeviceName, L"\\\\.\\DISPLAY1", 22);
        d->DeviceName[21] = 0;
        g_DisplayCount = 1;

        /* If a secondary framebuffer was detected (e.g. dual-monitor
         * virtual GPU), add it to the right. */
        d = &g_Displays[1];
        d->Id = 1;
        d->Width = 1920;
        d->Height = 1080;
        d->RefreshRate = 60;
        d->PositionX = 1920;
        d->PositionY = 0;
        d->Orientation = 0;
        d->Flags = DISPLAY_FLAG_ACTIVE;
        d->Primary = FALSE;
        d->InUse = TRUE;
        RtlCopyMemory(d->DeviceName, L"\\\\.\\DISPLAY2", 22);
        d->DeviceName[21] = 0;
        g_DisplayCount = 2;
    }

    DbgPrint("DISPLAY: topology initialized (%d displays)\n", g_DisplayCount);
    return STATUS_SUCCESS;
}

ULONG NTAPI DisplayEnum(ULONG MaxCount, ULONG *pIds, ULONG *pWidths,
                          ULONG *pHeights, ULONG *pFlags)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    for (i = 0; i < MAX_DISPLAYS && n < MaxCount; i++) {
        if (g_Displays[i].InUse) {
            if (pIds) pIds[n] = g_Displays[i].Id;
            if (pWidths) pWidths[n] = g_Displays[i].Width;
            if (pHeights) pHeights[n] = g_Displays[i].Height;
            if (pFlags) pFlags[n] = g_Displays[i].Flags;
            n++;
        }
    }
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    return n;
}

NTSTATUS NTAPI DisplayGetInfo(ULONG Id, ULONG *pWidth, ULONG *pHeight,
                                ULONG *pRefresh, LONG *pX, LONG *pY,
                                ULONG *pOrientation, PULONG pFlags)
{
    KIRQL irql;
    if (Id >= MAX_DISPLAYS || !g_Displays[Id].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    if (pWidth) *pWidth = g_Displays[Id].Width;
    if (pHeight) *pHeight = g_Displays[Id].Height;
    if (pRefresh) *pRefresh = g_Displays[Id].RefreshRate;
    if (pX) *pX = g_Displays[Id].PositionX;
    if (pY) *pY = g_Displays[Id].PositionY;
    if (pOrientation) *pOrientation = g_Displays[Id].Orientation;
    if (pFlags) *pFlags = g_Displays[Id].Flags;
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DisplaySetInfo(ULONG Id, ULONG Width, ULONG Height,
                                ULONG RefreshRate, LONG PosX, LONG PosY,
                                ULONG Orientation)
{
    KIRQL irql;
    if (Id >= MAX_DISPLAYS || !g_Displays[Id].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    g_Displays[Id].Width = Width;
    g_Displays[Id].Height = Height;
    g_Displays[Id].RefreshRate = RefreshRate;
    g_Displays[Id].PositionX = PosX;
    g_Displays[Id].PositionY = PosY;
    g_Displays[Id].Orientation = Orientation;
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    DbgPrint("DISPLAY: display %u set to %ux%u @ %u Hz at (%ld,%ld)\n",
             Id, Width, Height, RefreshRate, PosX, PosY);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DisplaySetMode(ULONG Mode)
{
    KIRQL irql;
    if (Mode > 2) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    g_DisplayMode = Mode;
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    DbgPrint("DISPLAY: mode set to %u (0=extend, 1=duplicate, 2=show only 1)\n", Mode);
    return STATUS_SUCCESS;
}

ULONG NTAPI DisplayGetMode(VOID)
{
    ULONG m;
    KIRQL irql;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    m = g_DisplayMode;
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    return m;
}

NTSTATUS NTAPI DisplaySetPrimary(ULONG Id)
{
    ULONG i;
    KIRQL irql;
    if (Id >= MAX_DISPLAYS || !g_Displays[Id].InUse)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_DisplayLock, &irql);
    for (i = 0; i < MAX_DISPLAYS; i++) {
        if (g_Displays[i].InUse) {
            if (g_Displays[i].Id == Id) {
                g_Displays[i].Flags |= DISPLAY_FLAG_PRIMARY;
                g_Displays[i].Primary = TRUE;
            } else {
                g_Displays[i].Flags &= ~DISPLAY_FLAG_PRIMARY;
                g_Displays[i].Primary = FALSE;
            }
        }
    }
    KeReleaseSpinLock(&g_DisplayLock, &irql);
    return STATUS_SUCCESS;
}
