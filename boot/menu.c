/*
 * MinNT - boot/menu.c
 * Boot menu UI - advanced startup menu.
 *
 * Shown when:
 *   - User pressed F8 / Shift+F8 during boot
 *   - Boot entry marked as diagnostic mode
 *   - Previous boot failed (auto-startup-recovery)
 *
 * The menu presents options:
 *   - Normal boot
 *   - Safe Mode
 *   - Safe Mode with Networking
 *   - Safe Mode with Command Prompt
 *   - Last Known Good Configuration
 *   - Recovery Environment
 *   - Boot normally (continue)
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/cm.h>
#include "win32k.h"

extern volatile ULONG *HalpFbGetBase(VOID);
extern ULONG HalpFbGetWidth(VOID);
extern ULONG HalpFbGetHeight(VOID);
extern VOID HalpFbFillRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbDrawRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbDrawString(ULONG, ULONG, const CHAR *, ULONG, ULONG);
extern VOID HalpFbClear(ULONG);

#define BOOT_MENU_OPTIONS 7

static const CHAR *g_BootMenuItems[BOOT_MENU_OPTIONS] = {
    "Normal boot (continue)",
    "Safe Mode",
    "Safe Mode with Networking",
    "Safe Mode with Command Prompt",
    "Last Known Good Configuration",
    "Recovery Environment",
    "Boot Configuration Editor"
};

static const ULONG g_BootMenuKeys[BOOT_MENU_OPTIONS] = {
    1,    /* F1 */
    2,    /* F2 */
    3,    /* F3 */
    4,    /* F4 */
    5,    /* F5 */
    6,    /* F6 */
    7     /* F7 */
};

static ULONG g_BootMenuSelection = 0;
static BOOLEAN g_BootMenuActive = FALSE;

VOID NTAPI BootMenuInit(VOID)
{
    g_BootMenuActive = TRUE;
    g_BootMenuSelection = 0;
    DbgPrint("BOOTMENU: advanced startup menu activated\n");
}

VOID NTAPI BootMenuShutdown(VOID)
{
    g_BootMenuActive = FALSE;
}

BOOLEAN NTAPI BootMenuIsActive(VOID)
{
    return g_BootMenuActive;
}

VOID NTAPI BootMenuRender(VOID)
{
    ULONG W = HalpFbGetWidth();
    ULONG H = HalpFbGetHeight();
    ULONG cx = W / 4;
    ULONG cy = H / 4;
    ULONG cw = W / 2;
    ULONG ch = (BOOT_MENU_OPTIONS + 4) * 16;

    if (!g_BootMenuActive) return;

    /* Background */
    HalpFbClear(0x00000000);

    /* Dialog box */
    HalpFbFillRect(cx, cy, cw, ch, 0x00C0C0C0);
    HalpFbDrawRect(cx, cy, cw, ch, 0x00FFFFFF);

    /* Title */
    HalpFbFillRect(cx, cy, cw, 24, 0x003080C0);
    HalpFbDrawString(cx + 8, cy + 8, "Advanced Boot Options",
                      0x00FFFFFF, 0x003080C0);

    /* Subtitle */
    HalpFbDrawString(cx + 8, cy + 30,
                      "Choose an option (F1-F7 or arrow keys + Enter):",
                      0x00000000, 0x00C0C0C0);

    /* Menu items */
    {
        ULONG i;
        ULONG y = cy + 56;
        for (i = 0; i < BOOT_MENU_OPTIONS; i++) {
            ULONG bg = (i == g_BootMenuSelection) ? 0x003080C0 : 0x00C0C0C0;
            ULONG fg = (i == g_BootMenuSelection) ? 0x00FFFFFF : 0x00000000;
            CHAR line[128];
            ULONG j = 0;
            /* F-key prefix */
            line[j++] = 'F';
            line[j++] = '0' + (g_BootMenuKeys[i] / 10);
            line[j++] = '0' + (g_BootMenuKeys[i] % 10);
            line[j++] = ' ';
            line[j++] = '-';
            line[j++] = ' ';
            const CHAR *label = g_BootMenuItems[i];
            while (*label && j < 120) line[j++] = *label++;
            line[j] = 0;
            HalpFbFillRect(cx + 16, y, cw - 32, 18, bg);
            HalpFbDrawString(cx + 24, y + 2, line, fg, bg);
            y += 20;
        }
    }

    /* Footer */
    {
        CHAR footer[64];
        ULONG j = 0;
        const CHAR *hint = "Selection: ";
        while (*hint && j < 60) footer[j++] = *hint++;
        footer[j++] = '0' + (g_BootMenuKeys[g_BootMenuSelection] / 10);
        footer[j++] = '0' + (g_BootMenuKeys[g_BootMenuSelection] % 10);
        footer[j] = 0;
        HalpFbDrawString(cx + 8, cy + ch - 24, footer,
                          0x00FF0000, 0x00C0C0C0);
    }
}

ULONG NTAPI BootMenuGetSelection(VOID)
{
    return g_BootMenuSelection;
}

NTSTATUS NTAPI BootMenuSetSelection(ULONG Selection)
{
    if (Selection >= BOOT_MENU_OPTIONS) return STATUS_INVALID_PARAMETER;
    g_BootMenuSelection = Selection;
    return STATUS_SUCCESS;
}

/* Handle a function key press. */
NTSTATUS NTAPI BootMenuHandleKey(UCHAR Key)
{
    ULONG i;
    /* F1-F7 */
    if (Key >= 1 && Key <= BOOT_MENU_OPTIONS) {
        g_BootMenuSelection = Key - 1;
        return STATUS_SUCCESS;
    }
    /* Number keys 1-7 */
    if (Key >= '1' && Key <= '7') {
        g_BootMenuSelection = Key - '1';
        return STATUS_SUCCESS;
    }
    /* Arrow keys (Up/Down) */
    for (i = 0; i < BOOT_MENU_OPTIONS; i++) {
        if (g_BootMenuKeys[i] == Key) {
            g_BootMenuSelection = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_PARAMETER;
}

/* Get the action that corresponds to the current selection. */
NTSTATUS NTAPI BootMenuGetActionName(PCHAR Buffer, ULONG BufferLen)
{
    if (!Buffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    {
        ULONG j = 0;
        const CHAR *label = g_BootMenuItems[g_BootMenuSelection];
        while (*label && j < BufferLen - 1) Buffer[j++] = *label++;
        Buffer[j] = 0;
    }
    return STATUS_SUCCESS;
}
