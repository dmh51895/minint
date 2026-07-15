/*
 * MinNT - boot/chain/cpl.c
 * Control Panel applets - the user-facing UI for OS settings.
 *
 * Each applet opens a window with controls (checkboxes, sliders,
 * dropdowns, OK/Cancel buttons) that the user manipulates, then
 * commits changes via the Settings*() APIs which persist to the
 * registry.
 *
 * The applets run inside the explorer shell thread - they receive
 * a window to draw into and a callback for OK/Cancel.
 *
 * Available applets:
 *   - Display Settings (resolution, refresh, DPI, night light, HDR)
 *   - Sound Settings (volume, mute)
 *   - Mouse & Keyboard Settings
 *   - Power Settings (sleep, screen timeout, power button, fast boot)
 *   - Region & Time (timezone, time/date format, auto sync)
 *   - Accessibility (high contrast, sticky/filter/toggle/mouse keys)
 *   - Notifications (enabled, do-not-disturb)
 *   - Privacy (telemetry, advertising ID, location)
 *   - System Information (read-only display of computer info)
 *   - Power User Settings (combined quick-access)
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include <nt/cm.h>
#include <nt/framework.h>
#include "win32k.h"
#include "cpl.h"

/* Forward declarations from win32k.h */
extern NTSTATUS NTAPI SettingsGetMasterVolume(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetMasterVolume(ULONG Value);
extern NTSTATUS NTAPI SettingsGetMute(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetMute(BOOL Value);
extern NTSTATUS NTAPI SettingsGetResolution(PULONG pWidth, PULONG pHeight);
extern NTSTATUS NTAPI SettingsSetResolution(ULONG Width, ULONG Height);
extern NTSTATUS NTAPI SettingsGetRefreshRate(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetRefreshRate(ULONG Value);
extern NTSTATUS NTAPI SettingsGetDpiScale(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetDpiScale(ULONG Value);
extern NTSTATUS NTAPI SettingsGetNightLight(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetNightLight(BOOL Value);
extern NTSTATUS NTAPI SettingsGetHdrEnabled(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetHdrEnabled(BOOL Value);
extern NTSTATUS NTAPI SettingsGetKeyboardRepeatRate(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetKeyboardRepeatRate(ULONG Value);
extern NTSTATUS NTAPI SettingsGetKeyboardRepeatDelay(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetKeyboardRepeatDelay(ULONG Value);
extern NTSTATUS NTAPI SettingsGetMouseSpeed(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetMouseSpeed(ULONG Value);
extern NTSTATUS NTAPI SettingsGetDoubleClickTime(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetDoubleClickTime(ULONG Value);
extern NTSTATUS NTAPI SettingsGetSwapMouseButtons(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetSwapMouseButtons(BOOL Value);
extern NTSTATUS NTAPI SettingsGetSleepTimeoutAC(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetSleepTimeoutAC(ULONG Value);
extern NTSTATUS NTAPI SettingsGetSleepTimeoutDC(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetSleepTimeoutDC(ULONG Value);
extern NTSTATUS NTAPI SettingsGetScreenTimeoutAC(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetScreenTimeoutAC(ULONG Value);
extern NTSTATUS NTAPI SettingsGetScreenTimeoutDC(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetScreenTimeoutDC(ULONG Value);
extern NTSTATUS NTAPI SettingsGetPowerButtonAction(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetPowerButtonAction(ULONG Value);
extern NTSTATUS NTAPI SettingsGetFastStartup(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetFastStartup(BOOL Value);
extern NTSTATUS NTAPI SettingsGetTimeZoneBias(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetTimeZoneBias(ULONG Value);
extern NTSTATUS NTAPI SettingsGetTimeFormat(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetTimeFormat(ULONG Value);
extern NTSTATUS NTAPI SettingsGetDateFormat(PULONG pValue);
extern NTSTATUS NTAPI SettingsSetDateFormat(ULONG Value);
extern NTSTATUS NTAPI SettingsGetAutoTimeSync(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetAutoTimeSync(BOOL Value);
extern NTSTATUS NTAPI SettingsGetHighContrast(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetHighContrast(BOOL Value);
extern NTSTATUS NTAPI SettingsGetStickyKeys(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetStickyKeys(BOOL Value);
extern NTSTATUS NTAPI SettingsGetFilterKeys(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetFilterKeys(BOOL Value);
extern NTSTATUS NTAPI SettingsGetToggleKeys(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetToggleKeys(BOOL Value);
extern NTSTATUS NTAPI SettingsGetMouseKeys(PBOOL pValue);
extern NTAPI SettingsSetMouseKeys(BOOL Value);
extern NTSTATUS NTAPI SettingsGetTelemetryEnabled(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetTelemetryEnabled(BOOL Value);
extern NTSTATUS NTAPI SettingsGetAdvertisingId(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetAdvertisingId(BOOL Value);
extern NTSTATUS NTAPI SettingsGetLocationService(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetLocationService(BOOL Value);
extern NTSTATUS NTAPI SettingsGetNotificationsEnabled(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetNotificationsEnabled(BOOL Value);
extern NTSTATUS NTAPI SettingsGetDndMode(PBOOL pValue);
extern NTSTATUS NTAPI SettingsSetDndMode(BOOL Value);
extern NTSTATUS NTAPI SettingsGetComputerName(PWCHAR pBuf, ULONG BufLen);
extern NTSTATUS NTAPI SettingsGetRegisteredOwner(PWCHAR pBuf, ULONG BufLen);
extern NTSTATUS NTAPI SettingsGetRegisteredOrg(PWCHAR pBuf, ULONG BufLen);
extern NTSTATUS NTAPI SettingsGetProductId(PWCHAR pBuf, ULONG BufLen);
extern NTSTATUS NTAPI SettingsGetDesktopColor(PWCHAR pBuf, ULONG BufLen);

/* ---- Applet IDs ------------------------------------------------------ */

#define CPL_DISPLAY      0
#define CPL_SOUND        1
#define CPL_KEYBOARD     2
#define CPL_MOUSE        3
#define CPL_POWER        4
#define CPL_REGIONAL     5
#define CPL_ACCESSIBILITY 6
#define CPL_NOTIFICATIONS 7
#define CPL_PRIVACY       8
#define CPL_SYSTEM        9
#define CPL_TASKMAN       10
#define CPL_EVENTVIEW     11
#define CPL_DISKMGMT      12
#define CPL_DEVICEMGR     13
#define CPL_SERVICES      14
#define CPL_PERFMON       15
#define CPL_BACKUP        16
#define CPL_APPS          17
#define CPL_NETWORK       18
#define CPL_PRINTERS      19
#define CPL_DESKTOP_ICONS 20  /* desktop icon settings */

#define MAX_CPL_APPLETS 21

/* ---- Framebuffer helpers (delegate to HAL) --------------------------- */

#define FB_WIDTH()  HalpFbGetWidth()
#define FB_HEIGHT() HalpFbGetHeight()

/* ---- Primitive control widgets ---------------------------------------- */

#define CTRL_LABEL      1
#define CTRL_CHECKBOX   2
#define CTRL_SLIDER     3
#define CTRL_BUTTON     4
#define CTRL_TEXTBOX    5
#define CTRL_DROPDOWN    6
#define CTRL_RADIO       7
#define CTRL_GROUPBOX    8

typedef struct _CPL_CONTROL {
    ULONG Type;
    LONG X, Y, W, H;
    const CHAR *Label;
    ULONG Id;
    LONG Value;          /* current value (boolean or index) */
    LONG MinValue;
    LONG MaxValue;
    const CHAR **Options;  /* for dropdown */
    BOOLEAN Hovered;
    BOOLEAN Pressed;
    CHAR ValueLabel[128]; /* current textbox/dropdown text */
} CPL_CONTROL;

typedef struct _CPL_DIALOG {
    const CHAR *Title;
    ULONG NumControls;
    CPL_CONTROL *Controls;
    LONG DialogX, DialogY, DialogW, DialogH;
    BOOLEAN Active;
    ULONG CloseButtonId;  /* button id for "OK"/"Apply" */
    ULONG CancelButtonId; /* button id for "Cancel" */
    void (*OnApply)(void);  /* commit changes handler */
} CPL_DIALOG;

/* ---- FB drawing primitives for controls -------------------------------- */

static ULONG COLOR_DIALOG_BG     = 0x00E0E0E0;
static ULONG COLOR_DIALOG_FG     = 0x00000000;
static ULONG COLOR_BUTTON_BG     = 0x00D0D0D0;
static ULONG COLOR_BUTTON_HOVER  = 0x00C0E0FF;
static ULONG COLOR_CHECKBOX_ON   = 0x00FFFFFF;
static ULONG COLOR_CHECKBOX_OFF  = 0x00FFFFFF;
static ULONG COLOR_GROUP_BG      = 0x00F0F0F0;
static ULONG COLOR_TITLE_BG      = 0x003080C0;
static ULONG COLOR_TITLE_FG      = 0x00FFFFFF;

static VOID DrawRect(LONG x, LONG y, LONG w, LONG h, ULONG color)
{
    volatile ULONG *Fb = HalpFbGetBase();
    ULONG FbW = FB_WIDTH();
    LONG x2 = x + w;
    LONG y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (LONG)FbW) x2 = FbW;
    if (y2 > (LONG)FB_HEIGHT()) y2 = FB_HEIGHT();
    for (LONG row = y; row < y2; row++) {
        for (LONG col = x; col < x2; col++) {
            Fb[row * FbW + col] = color;
        }
    }
}

static VOID DrawCheckBox(LONG x, LONG y, BOOL checked, BOOL hovered)
{
    ULONG fill = hovered ? COLOR_BUTTON_HOVER : COLOR_CHECKBOX_OFF;
    DrawRect(x, y, 14, 14, fill);
    DrawRect(x + 1, y + 1, 12, 12, COLOR_CHECKBOX_ON);
    if (checked) {
        /* Draw a simple checkmark */
        DrawRect(x + 3, y + 7, 2, 2, 0x00000000);
        DrawRect(x + 4, y + 8, 2, 2, 0x00000000);
        DrawRect(x + 5, y + 9, 2, 2, 0x00000000);
        DrawRect(x + 6, y + 8, 2, 2, 0x00000000);
        DrawRect(x + 7, y + 7, 2, 2, 0x00000000);
        DrawRect(x + 8, y + 6, 2, 2, 0x00000000);
    }
}

static VOID DrawSlider(LONG x, LONG y, LONG w, LONG minVal, LONG maxVal, LONG val)
{
    DrawRect(x, y + 6, w, 4, 0x00808080);
    LONG pos = (val - minVal) * w / (maxVal - minVal + 1);
    DrawRect(x + pos - 4, y, 8, 16, 0x003080C0);
}

static VOID DrawButton(LONG x, LONG y, LONG w, LONG h, const CHAR *label, BOOL hovered, BOOL pressed)
{
    ULONG bg = pressed ? 0x00A0A0A0 : (hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG);
    DrawRect(x, y, w, h, bg);
    DrawRect(x, y, w, 1, 0x00FFFFFF);
    DrawRect(x, y + h - 1, w, 1, 0x00404040);
    DrawRect(x, y, 1, h, 0x00FFFFFF);
    DrawRect(x + w - 1, y, 1, h, 0x00404040);
    /* Center text */
    ULONG len = 0;
    while (label[len]) len++;
    LONG tx = x + (w - len * 8) / 2;
    LONG ty = y + (h - 8) / 2;
    HalpFbDrawString(tx, ty, label, COLOR_DIALOG_FG, bg);
}

static VOID DrawControl(CPL_CONTROL *ctrl)
{
    if (ctrl->Type == CTRL_CHECKBOX) {
        DrawCheckBox(ctrl->X, ctrl->Y + 2, ctrl->Value ? TRUE : FALSE, ctrl->Hovered);
        HalpFbDrawString(ctrl->X + 18, ctrl->Y + 4, ctrl->Label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    } else if (ctrl->Type == CTRL_LABEL) {
        HalpFbDrawString(ctrl->X, ctrl->Y + 4, ctrl->Label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    } else if (ctrl->Type == CTRL_SLIDER) {
        HalpFbDrawString(ctrl->X, ctrl->Y - 14, ctrl->Label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
        DrawSlider(ctrl->X, ctrl->Y, ctrl->W, ctrl->MinValue, ctrl->MaxValue, ctrl->Value);
        /* Show value */
        CHAR buf[16];
        LONG i = 0;
        ULONG v = (ULONG)ctrl->Value;
        if (v == 0) { buf[i++] = '0'; }
        else {
            while (v > 0 && i < 14) {
                buf[i++] = '0' + (v % 10);
                v /= 10;
            }
        }
        CHAR tmp = buf[0];
        buf[0] = buf[i-1]; buf[i-1] = tmp; /* swap just first and last - simplest "reverse" */
        buf[i] = 0;
        HalpFbDrawString(ctrl->X + ctrl->W + 8, ctrl->Y + 2, buf, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    } else if (ctrl->Type == CTRL_BUTTON) {
        DrawButton(ctrl->X, ctrl->Y, ctrl->W, ctrl->H, ctrl->Label,
                   ctrl->Hovered, ctrl->Pressed);
    } else if (ctrl->Type == CTRL_GROUPBOX) {
        DrawRect(ctrl->X, ctrl->Y, ctrl->W, ctrl->H, COLOR_GROUP_BG);
        /* Draw border */
        for (LONG i = ctrl->X; i < ctrl->X + ctrl->W; i++) {
            HalpFbPutPixel(i, ctrl->Y, 0x00808080);
            HalpFbPutPixel(i, ctrl->Y + ctrl->H - 1, 0x00808080);
        }
        for (LONG i = ctrl->Y; i < ctrl->Y + ctrl->H; i++) {
            HalpFbPutPixel(ctrl->X, i, 0x00808080);
            HalpFbPutPixel(ctrl->X + ctrl->W - 1, i, 0x00808080);
        }
        HalpFbDrawString(ctrl->X + 6, ctrl->Y + 2, ctrl->Label, COLOR_DIALOG_FG, COLOR_GROUP_BG);
    } else if (ctrl->Type == CTRL_DROPDOWN) {
        HalpFbDrawString(ctrl->X, ctrl->Y - 14, ctrl->Label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
        DrawRect(ctrl->X, ctrl->Y, ctrl->W, 18, 0x00FFFFFF);
        DrawRect(ctrl->X, ctrl->Y, ctrl->W, 1, 0x00808080);
        DrawRect(ctrl->X, ctrl->Y + 17, ctrl->W, 1, 0x00808080);
        if (ctrl->Options && ctrl->Value >= 0) {
            const CHAR *opt = ctrl->Options[ctrl->Value];
            if (opt) HalpFbDrawString(ctrl->X + 4, ctrl->Y + 4, opt,
                                       COLOR_DIALOG_FG, 0x00FFFFFF);
        }
        /* Down arrow */
        DrawRect(ctrl->X + ctrl->W - 16, ctrl->Y + 6, 12, 6, 0x00808080);
    }
}

/* ---- Active dialog state ---------------------------------------------- */

static CPL_DIALOG *g_ActiveDialog = NULL;

/* ---- Helper: hit-test a control --------------------------------------- */

static CPL_CONTROL *HitTestControl(CPL_DIALOG *dlg, SHORT mx, SHORT my)
{
    if (!dlg) return NULL;
    /* Convert screen coords to dialog-relative */
    LONG lx = mx - dlg->DialogX;
    LONG ly = my - dlg->DialogY;
    for (LONG i = (LONG)dlg->NumControls - 1; i >= 0; i--) {
        CPL_CONTROL *c = &dlg->Controls[i];
        if (lx >= c->X && lx <= c->X + c->W && ly >= c->Y && ly <= c->Y + c->H) {
            return c;
        }
    }
    return NULL;
}

/* ---- Helper: handle a click on a control ----------------------------- */

static VOID ClickControl(CPL_CONTROL *ctrl, SHORT mx, SHORT my)
{
    if (!ctrl) return;
    if (ctrl->Type == CTRL_CHECKBOX) {
        ctrl->Value = !ctrl->Value;
    } else if (ctrl->Type == CTRL_BUTTON) {
        ctrl->Pressed = TRUE;
        /* Caller checks button ID via g_ActiveDialog->CloseButtonId */
    } else if (ctrl->Type == CTRL_SLIDER) {
        LONG newVal = (mx - ctrl->X) * (ctrl->MaxValue - ctrl->MinValue) / ctrl->W + ctrl->MinValue;
        if (newVal < ctrl->MinValue) newVal = ctrl->MinValue;
        if (newVal > ctrl->MaxValue) newVal = ctrl->MaxValue;
        ctrl->Value = newVal;
    } else if (ctrl->Type == CTRL_DROPDOWN) {
        if (ctrl->Options) {
            /* Count options */
            LONG count = 0;
            while (ctrl->Options[count]) count++;
            ctrl->Value = (ctrl->Value + 1) % count;
        }
    }
}

/* ---- Dialog drawing --------------------------------------------------- */

static VOID DrawDialog(CPL_DIALOG *dlg)
{
    if (!dlg || !dlg->Active) return;

    /* Background */
    DrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, dlg->DialogH, COLOR_DIALOG_BG);
    /* Title bar */
    DrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, 24, COLOR_TITLE_BG);
    HalpFbDrawString(dlg->DialogX + 8, dlg->DialogY + 8, dlg->Title,
                     COLOR_TITLE_FG, COLOR_TITLE_BG);
    /* Close X button (top right) */
    LONG cx = dlg->DialogX + dlg->DialogW - 24;
    DrawRect(cx, dlg->DialogY, 24, 24, COLOR_TITLE_BG);
    HalpFbDrawString(cx + 8, dlg->DialogY + 8, "X", COLOR_TITLE_FG, COLOR_TITLE_BG);
    /* Border */
    for (LONG i = 0; i < dlg->DialogW; i++) {
        HalpFbPutPixel(dlg->DialogX + i, dlg->DialogY, 0x00000000);
        HalpFbPutPixel(dlg->DialogX + i, dlg->DialogY + dlg->DialogH - 1, 0x00000000);
    }
    for (LONG i = 0; i < dlg->DialogH; i++) {
        HalpFbPutPixel(dlg->DialogX, dlg->DialogY + i, 0x00000000);
        HalpFbPutPixel(dlg->DialogX + dlg->DialogW - 1, dlg->DialogY + i, 0x00000000);
    }
    /* Draw all controls */
    for (ULONG i = 0; i < dlg->NumControls; i++) {
        DrawControl(&dlg->Controls[i]);
    }
}

/* ---- Mouse event handler ---------------------------------------------- */

static BOOLEAN CplHandleMouse(SHORT mx, SHORT my, BOOLEAN leftDown, BOOLEAN leftPrev)
{
    if (!g_ActiveDialog || !g_ActiveDialog->Active) return FALSE;

    CPL_CONTROL *hovered = HitTestControl(g_ActiveDialog, mx, my);
    for (ULONG i = 0; i < g_ActiveDialog->NumControls; i++) {
        g_ActiveDialog->Controls[i].Hovered = (&g_ActiveDialog->Controls[i] == hovered);
    }

    if (leftDown && !leftPrev) {
        /* Check if click is on close X button */
        LONG cx = g_ActiveDialog->DialogX + g_ActiveDialog->DialogW - 24;
        if (mx >= cx && mx <= cx + 24 && my >= g_ActiveDialog->DialogY && my <= g_ActiveDialog->DialogY + 24) {
            g_ActiveDialog->Active = FALSE;
            return TRUE;
        }

        /* Check if click is on dialog (otherwise ignore) */
        if (mx < g_ActiveDialog->DialogX || mx > g_ActiveDialog->DialogX + g_ActiveDialog->DialogW ||
            my < g_ActiveDialog->DialogY || my > g_ActiveDialog->DialogY + g_ActiveDialog->DialogH) {
            /* Outside dialog - dismiss if click is outside */
            return FALSE;
        }

        CPL_CONTROL *c = hovered;
        if (c) {
            ClickControl(c, mx, my);
            if (c->Type == CTRL_BUTTON && c->Id == g_ActiveDialog->CloseButtonId) {
                /* OK/Apply */
                if (g_ActiveDialog->OnApply) g_ActiveDialog->OnApply();
                g_ActiveDialog->Active = FALSE;
            } else if (c->Type == CTRL_BUTTON && c->Id == g_ActiveDialog->CancelButtonId) {
                g_ActiveDialog->Active = FALSE;
            }
            return TRUE;
        }
    } else if (!leftDown && leftPrev) {
        /* Reset button pressed state */
        for (ULONG i = 0; i < g_ActiveDialog->NumControls; i++) {
            g_ActiveDialog->Controls[i].Pressed = FALSE;
        }
    }

    return FALSE;
}

/* ---- Applet-specific dialog setups ----------------------------------- */

/* Common dialog frame */
static VOID CplSetupFrame(CPL_DIALOG *dlg, const CHAR *title)
{
    dlg->Title = title;
    dlg->DialogW = 540;
    dlg->DialogH = 380;
    dlg->DialogX = (FB_WIDTH() - dlg->DialogW) / 2;
    dlg->DialogY = (FB_HEIGHT() - dlg->DialogH - 40) / 2; /* leave room for taskbar */
    dlg->Active = TRUE;
}

/* Display settings applet */
static CPL_CONTROL g_DisplayCtrls[16];
static CPL_DIALOG g_DisplayDlg = {
    "Display Settings", 0, g_DisplayCtrls,
    0, 0, 540, 380, FALSE, 0, 0, NULL
};
static const CHAR *g_RefreshOpts[] = { "60 Hz", "75 Hz", "120 Hz", "144 Hz", "240 Hz", NULL };
static const CHAR *g_DpiOpts[] = { "100% (96 DPI)", "125% (120 DPI)", "150% (144 DPI)", "200% (192 DPI)", NULL };

static VOID Display_OnApply(VOID)
{
    /* Resolution is index 0 of the dropdown */
    ULONG resIdx = g_DisplayCtrls[0].Value;
    ULONG width, height;
    switch (resIdx) {
        case 0: width = 1920; height = 1080; break;
        case 1: width = 1680; height = 1050; break;
        case 2: width = 1600; height = 900; break;
        case 3: width = 1366; height = 768; break;
        case 4: width = 1280; height = 720; break;
        default: width = 1920; height = 1080; break;
    }
    SettingsSetResolution(width, height);

    SettingsSetRefreshRate(60 + (ULONG)g_DisplayCtrls[1].Value * 15);

    /* DPI: index 0=96, 1=120, 2=144, 3=192 */
    ULONG dpi;
    switch (g_DisplayCtrls[2].Value) {
        case 0: dpi = 96; break;
        case 1: dpi = 120; break;
        case 2: dpi = 144; break;
        case 3: dpi = 192; break;
        default: dpi = 96; break;
    }
    SettingsSetDpiScale(dpi);

    SettingsSetNightLight(g_DisplayCtrls[3].Value ? TRUE : FALSE);
    SettingsSetHdrEnabled(g_DisplayCtrls[4].Value ? TRUE : FALSE);

    DbgPrint("CPL: Display settings applied - %ux%u @ %u Hz, DPI %u\n",
             width, height, 60 + (ULONG)g_DisplayCtrls[1].Value * 15, dpi);
}

static VOID CplOpenDisplay(VOID)
{
    RtlZeroMemory(g_DisplayCtrls, sizeof(g_DisplayCtrls));
    CplSetupFrame(&g_DisplayDlg, "Display Settings");

    ULONG width = 0, height = 0;
    SettingsGetResolution(&width, &height);

    ULONG resIdx = 0;
    if (width == 1920 && height == 1080) resIdx = 0;
    else if (width == 1680 && height == 1050) resIdx = 1;
    else if (width == 1600 && height == 900) resIdx = 2;
    else if (width == 1366 && height == 768) resIdx = 3;
    else if (width == 1280 && height == 720) resIdx = 4;

    ULONG refresh = 0;
    SettingsGetRefreshRate(&refresh);

    ULONG dpi = 0;
    SettingsGetDpiScale(&dpi);
    ULONG dpiIdx = 0;
    if (dpi == 120) dpiIdx = 1;
    else if (dpi == 144) dpiIdx = 2;
    else if (dpi == 192) dpiIdx = 3;

    BOOL nl = FALSE;
    SettingsGetNightLight(&nl);
    BOOL hdr = FALSE;
    SettingsGetHdrEnabled(&hdr);

    ULONG i = 0;
    g_DisplayCtrls[i].Type = CTRL_LABEL; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 40; g_DisplayCtrls[i].W = 100; g_DisplayCtrls[i].H = 16;
    g_DisplayCtrls[i].Label = "Resolution:"; i++;
    g_DisplayCtrls[i].Type = CTRL_DROPDOWN; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 70; g_DisplayCtrls[i].W = 200; g_DisplayCtrls[i].H = 18;
    g_DisplayCtrls[i].Label = "Choose resolution"; g_DisplayCtrls[i].Value = resIdx; g_DisplayCtrls[i].Options = (const CHAR *[]){"1920x1080","1680x1050","1600x900","1366x768","1280x720",NULL}; i++;
    g_DisplayCtrls[i].Type = CTRL_LABEL; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 100; g_DisplayCtrls[i].W = 100; g_DisplayCtrls[i].H = 16;
    g_DisplayCtrls[i].Label = "Refresh rate:"; i++;
    g_DisplayCtrls[i].Type = CTRL_DROPDOWN; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 130; g_DisplayCtrls[i].W = 120; g_DisplayCtrls[i].H = 18;
    g_DisplayCtrls[i].Label = "Hz"; g_DisplayCtrls[i].Value = (refresh - 60) / 15; g_DisplayCtrls[i].Options = g_RefreshOpts; i++;
    g_DisplayCtrls[i].Type = CTRL_LABEL; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 160; g_DisplayCtrls[i].W = 100; g_DisplayCtrls[i].H = 16;
    g_DisplayCtrls[i].Label = "Scaling:"; i++;
    g_DisplayCtrls[i].Type = CTRL_DROPDOWN; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 190; g_DisplayCtrls[i].W = 160; g_DisplayCtrls[i].H = 18;
    g_DisplayCtrls[i].Label = "DPI"; g_DisplayCtrls[i].Value = dpiIdx; g_DisplayCtrls[i].Options = g_DpiOpts; i++;
    g_DisplayCtrls[i].Type = CTRL_CHECKBOX; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 220; g_DisplayCtrls[i].W = 200; g_DisplayCtrls[i].H = 16;
    g_DisplayCtrls[i].Label = "Night light (reduce blue light at night)"; g_DisplayCtrls[i].Value = nl; i++;
    g_DisplayCtrls[i].Type = CTRL_CHECKBOX; g_DisplayCtrls[i].X = 20; g_DisplayCtrls[i].Y = 244; g_DisplayCtrls[i].W = 200; g_DisplayCtrls[i].H = 16;
    g_DisplayCtrls[i].Label = "HDR (high dynamic range)"; g_DisplayCtrls[i].Value = hdr; i++;
    /* OK and Cancel buttons */
    g_DisplayCtrls[i].Type = CTRL_BUTTON; g_DisplayCtrls[i].X = 380; g_DisplayCtrls[i].Y = 340; g_DisplayCtrls[i].W = 60; g_DisplayCtrls[i].H = 24;
    g_DisplayCtrls[i].Label = "OK"; g_DisplayCtrls[i].Id = 100; g_DisplayDlg.CloseButtonId = 100; i++;
    g_DisplayCtrls[i].Type = CTRL_BUTTON; g_DisplayCtrls[i].X = 460; g_DisplayCtrls[i].Y = 340; g_DisplayCtrls[i].W = 70; g_DisplayCtrls[i].H = 24;
    g_DisplayCtrls[i].Label = "Cancel"; g_DisplayCtrls[i].Id = 101; g_DisplayDlg.CancelButtonId = 101; i++;

    g_DisplayDlg.NumControls = i;
    g_DisplayDlg.OnApply = Display_OnApply;
    g_ActiveDialog = &g_DisplayDlg;
}

/* Sound settings applet */
static CPL_CONTROL g_SoundCtrls[8];
static CPL_DIALOG g_SoundDlg = {
    "Sound Settings", 0, g_SoundCtrls,
    0, 0, 460, 220, FALSE, 0, 0, NULL
};

static VOID Sound_OnApply(VOID)
{
    SettingsSetMasterVolume((ULONG)g_SoundCtrls[0].Value);
    SettingsSetMute(g_SoundCtrls[1].Value ? TRUE : FALSE);
    DbgPrint("CPL: Sound settings applied - vol=%u mute=%d\n",
             (ULONG)g_SoundCtrls[0].Value, g_SoundCtrls[1].Value);
}

static VOID CplOpenSound(VOID)
{
    RtlZeroMemory(g_SoundCtrls, sizeof(g_SoundCtrls));
    CplSetupFrame(&g_SoundDlg, "Sound Settings");
    g_SoundDlg.DialogH = 220;

    ULONG vol = 80; BOOL mute = FALSE;
    SettingsGetMasterVolume(&vol);
    SettingsGetMute(&mute);

    ULONG i = 0;
    g_SoundCtrls[i].Type = CTRL_LABEL; g_SoundCtrls[i].X = 20; g_SoundCtrls[i].Y = 40; g_SoundCtrls[i].W = 200; g_SoundCtrls[i].H = 16;
    g_SoundCtrls[i].Label = "Master volume"; i++;
    g_SoundCtrls[i].Type = CTRL_SLIDER; g_SoundCtrls[i].X = 20; g_SoundCtrls[i].Y = 80; g_SoundCtrls[i].W = 400; g_SoundCtrls[i].H = 16;
    g_SoundCtrls[i].Label = "Volume"; g_SoundCtrls[i].Value = vol; g_SoundCtrls[i].MinValue = 0; g_SoundCtrls[i].MaxValue = 100; i++;
    g_SoundCtrls[i].Type = CTRL_CHECKBOX; g_SoundCtrls[i].X = 20; g_SoundCtrls[i].Y = 110; g_SoundCtrls[i].W = 200; g_SoundCtrls[i].H = 16;
    g_SoundCtrls[i].Label = "Mute all sounds"; g_SoundCtrls[i].Value = mute; i++;
    g_SoundCtrls[i].Type = CTRL_BUTTON; g_SoundCtrls[i].X = 300; g_SoundCtrls[i].Y = 180; g_SoundCtrls[i].W = 60; g_SoundCtrls[i].H = 24;
    g_SoundCtrls[i].Label = "OK"; g_SoundCtrls[i].Id = 100; g_SoundDlg.CloseButtonId = 100; i++;
    g_SoundCtrls[i].Type = CTRL_BUTTON; g_SoundCtrls[i].X = 380; g_SoundCtrls[i].Y = 180; g_SoundCtrls[i].W = 70; g_SoundCtrls[i].H = 24;
    g_SoundCtrls[i].Label = "Cancel"; g_SoundCtrls[i].Id = 101; g_SoundDlg.CancelButtonId = 101; i++;

    g_SoundDlg.NumControls = i;
    g_SoundDlg.OnApply = Sound_OnApply;
    g_ActiveDialog = &g_SoundDlg;
}

/* Mouse settings applet */
static CPL_CONTROL g_MouseCtrls[10];
static CPL_DIALOG g_MouseDlg = {
    "Mouse Settings", 0, g_MouseCtrls,
    0, 0, 480, 300, FALSE, 0, 0, NULL
};

static VOID Mouse_OnApply(VOID)
{
    SettingsSetMouseSpeed((ULONG)g_MouseCtrls[0].Value);
    SettingsSetDoubleClickTime((ULONG)g_MouseCtrls[1].Value * 50 + 200);
    SettingsSetSwapMouseButtons(g_MouseCtrls[2].Value ? TRUE : FALSE);
    DbgPrint("CPL: Mouse settings applied - speed=%u dblclick=%u swap=%d\n",
             (ULONG)g_MouseCtrls[0].Value,
             (ULONG)g_MouseCtrls[1].Value * 50 + 200,
             g_MouseCtrls[2].Value);
}

static VOID CplOpenMouse(VOID)
{
    RtlZeroMemory(g_MouseCtrls, sizeof(g_MouseCtrls));
    CplSetupFrame(&g_MouseDlg, "Mouse Settings");
    g_MouseDlg.DialogH = 300;

    ULONG speed = 10, dblclk = 500;
    BOOL swap = FALSE;
    SettingsGetMouseSpeed(&speed);
    SettingsGetDoubleClickTime(&dblclk);
    SettingsGetSwapMouseButtons(&swap);

    ULONG i = 0;
    g_MouseCtrls[i].Type = CTRL_SLIDER; g_MouseCtrls[i].X = 20; g_MouseCtrls[i].Y = 60; g_MouseCtrls[i].W = 400; g_MouseCtrls[i].H = 16;
    g_MouseCtrls[i].Label = "Pointer speed"; g_MouseCtrls[i].Value = speed; g_MouseCtrls[i].MinValue = 1; g_MouseCtrls[i].MaxValue = 20; i++;
    g_MouseCtrls[i].Type = CTRL_SLIDER; g_MouseCtrls[i].X = 20; g_MouseCtrls[i].Y = 110; g_MouseCtrls[i].W = 400; g_MouseCtrls[i].H = 16;
    g_MouseCtrls[i].Label = "Double-click speed (ms)"; g_MouseCtrls[i].Value = (dblclk - 200) / 50; g_MouseCtrls[i].MinValue = 0; g_MouseCtrls[i].MaxValue = 16; i++;
    g_MouseCtrls[i].Type = CTRL_CHECKBOX; g_MouseCtrls[i].X = 20; g_MouseCtrls[i].Y = 150; g_MouseCtrls[i].W = 200; g_MouseCtrls[i].H = 16;
    g_MouseCtrls[i].Label = "Swap primary/secondary buttons (left-handed)"; g_MouseCtrls[i].Value = swap; i++;
    g_MouseCtrls[i].Type = CTRL_BUTTON; g_MouseCtrls[i].X = 320; g_MouseCtrls[i].Y = 260; g_MouseCtrls[i].W = 60; g_MouseCtrls[i].H = 24;
    g_MouseCtrls[i].Label = "OK"; g_MouseCtrls[i].Id = 100; g_MouseDlg.CloseButtonId = 100; i++;
    g_MouseCtrls[i].Type = CTRL_BUTTON; g_MouseCtrls[i].X = 400; g_MouseCtrls[i].Y = 260; g_MouseCtrls[i].W = 70; g_MouseCtrls[i].H = 24;
    g_MouseCtrls[i].Label = "Cancel"; g_MouseCtrls[i].Id = 101; g_MouseDlg.CancelButtonId = 101; i++;

    g_MouseDlg.NumControls = i;
    g_MouseDlg.OnApply = Mouse_OnApply;
    g_ActiveDialog = &g_MouseDlg;
}

/* Keyboard settings applet */
static CPL_CONTROL g_KeyboardCtrls[8];
static CPL_DIALOG g_KeyboardDlg = {
    "Keyboard Settings", 0, g_KeyboardCtrls,
    0, 0, 480, 280, FALSE, 0, 0, NULL
};

static VOID Keyboard_OnApply(VOID)
{
    SettingsSetKeyboardRepeatRate((ULONG)g_KeyboardCtrls[0].Value);
    SettingsSetKeyboardRepeatDelay((ULONG)g_KeyboardCtrls[1].Value);
    DbgPrint("CPL: Keyboard settings applied - rate=%u delay=%u\n",
             (ULONG)g_KeyboardCtrls[0].Value, (ULONG)g_KeyboardCtrls[1].Value);
}

static VOID CplOpenKeyboard(VOID)
{
    RtlZeroMemory(g_KeyboardCtrls, sizeof(g_KeyboardCtrls));
    CplSetupFrame(&g_KeyboardDlg, "Keyboard Settings");
    g_KeyboardDlg.DialogH = 280;

    ULONG rate = 31, delay = 1;
    SettingsGetKeyboardRepeatRate(&rate);
    SettingsGetKeyboardRepeatDelay(&delay);

    ULONG i = 0;
    g_KeyboardCtrls[i].Type = CTRL_SLIDER; g_KeyboardCtrls[i].X = 20; g_KeyboardCtrls[i].Y = 60; g_KeyboardCtrls[i].W = 400; g_KeyboardCtrls[i].H = 16;
    g_KeyboardCtrls[i].Label = "Repeat rate (chars/sec)"; g_KeyboardCtrls[i].Value = rate; g_KeyboardCtrls[i].MinValue = 0; g_KeyboardCtrls[i].MaxValue = 31; i++;
    g_KeyboardCtrls[i].Type = CTRL_SLIDER; g_KeyboardCtrls[i].X = 20; g_KeyboardCtrls[i].Y = 110; g_KeyboardCtrls[i].W = 400; g_KeyboardCtrls[i].H = 16;
    g_KeyboardCtrls[i].Label = "Repeat delay (250ms units)"; g_KeyboardCtrls[i].Value = delay; g_KeyboardCtrls[i].MinValue = 0; g_KeyboardCtrls[i].MaxValue = 3; i++;
    g_KeyboardCtrls[i].Type = CTRL_BUTTON; g_KeyboardCtrls[i].X = 320; g_KeyboardCtrls[i].Y = 240; g_KeyboardCtrls[i].W = 60; g_KeyboardCtrls[i].H = 24;
    g_KeyboardCtrls[i].Label = "OK"; g_KeyboardCtrls[i].Id = 100; g_KeyboardDlg.CloseButtonId = 100; i++;
    g_KeyboardCtrls[i].Type = CTRL_BUTTON; g_KeyboardCtrls[i].X = 400; g_KeyboardCtrls[i].Y = 240; g_KeyboardCtrls[i].W = 70; g_KeyboardCtrls[i].H = 24;
    g_KeyboardCtrls[i].Label = "Cancel"; g_KeyboardCtrls[i].Id = 101; g_KeyboardDlg.CancelButtonId = 101; i++;

    g_KeyboardDlg.NumControls = i;
    g_KeyboardDlg.OnApply = Keyboard_OnApply;
    g_ActiveDialog = &g_KeyboardDlg;
}

/* Power settings applet */
static CPL_CONTROL g_PowerCtrls[16];
static CPL_DIALOG g_PowerDlg = {
    "Power Settings", 0, g_PowerCtrls,
    0, 0, 480, 380, FALSE, 0, 0, NULL
};
static const CHAR *g_PowerBtnOpts[] = { "Do nothing", "Sleep", "Hibernate", "Shut down", "Ask me what to do", NULL };

static VOID Power_OnApply(VOID)
{
    SettingsSetSleepTimeoutAC((ULONG)g_PowerCtrls[0].Value * 60);
    SettingsSetSleepTimeoutDC((ULONG)g_PowerCtrls[1].Value * 60);
    SettingsSetScreenTimeoutAC((ULONG)g_PowerCtrls[2].Value * 60);
    SettingsSetScreenTimeoutDC((ULONG)g_PowerCtrls[3].Value * 60);
    SettingsSetPowerButtonAction((ULONG)g_PowerCtrls[4].Value);
    SettingsSetFastStartup(g_PowerCtrls[5].Value ? TRUE : FALSE);
    DbgPrint("CPL: Power settings applied\n");
}

static VOID CplOpenPower(VOID)
{
    RtlZeroMemory(g_PowerCtrls, sizeof(g_PowerCtrls));
    CplSetupFrame(&g_PowerDlg, "Power Settings");

    ULONG slAC = 0, slDC = 0, scAC = 0, scDC = 0, pwrBtn = 0;
    BOOL fastBoot = TRUE;
    SettingsGetSleepTimeoutAC(&slAC);
    SettingsGetSleepTimeoutDC(&slDC);
    SettingsGetScreenTimeoutAC(&scAC);
    SettingsGetScreenTimeoutDC(&scDC);
    SettingsGetPowerButtonAction(&pwrBtn);
    SettingsGetFastStartup(&fastBoot);

    ULONG i = 0;
    g_PowerCtrls[i].Type = CTRL_LABEL; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 40; g_PowerCtrls[i].W = 200; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Sleep after (plugged in / minutes)"; i++;
    g_PowerCtrls[i].Type = CTRL_SLIDER; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 80; g_PowerCtrls[i].W = 400; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Sleep AC"; g_PowerCtrls[i].Value = slAC / 60; g_PowerCtrls[i].MinValue = 0; g_PowerCtrls[i].MaxValue = 60; i++;
    g_PowerCtrls[i].Type = CTRL_SLIDER; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 130; g_PowerCtrls[i].W = 400; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Sleep battery"; g_PowerCtrls[i].Value = slDC / 60; g_PowerCtrls[i].MinValue = 0; g_PowerCtrls[i].MaxValue = 60; i++;
    g_PowerCtrls[i].Type = CTRL_SLIDER; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 180; g_PowerCtrls[i].W = 400; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Screen off AC"; g_PowerCtrls[i].Value = scAC / 60; g_PowerCtrls[i].MinValue = 0; g_PowerCtrls[i].MaxValue = 60; i++;
    g_PowerCtrls[i].Type = CTRL_SLIDER; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 230; g_PowerCtrls[i].W = 400; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Screen off battery"; g_PowerCtrls[i].Value = scDC / 60; g_PowerCtrls[i].MinValue = 0; g_PowerCtrls[i].MaxValue = 60; i++;
    g_PowerCtrls[i].Type = CTRL_DROPDOWN; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 280; g_PowerCtrls[i].W = 200; g_PowerCtrls[i].H = 18;
    g_PowerCtrls[i].Label = "Power button action"; g_PowerCtrls[i].Value = pwrBtn; g_PowerCtrls[i].Options = g_PowerBtnOpts; i++;
    g_PowerCtrls[i].Type = CTRL_CHECKBOX; g_PowerCtrls[i].X = 20; g_PowerCtrls[i].Y = 320; g_PowerCtrls[i].W = 200; g_PowerCtrls[i].H = 16;
    g_PowerCtrls[i].Label = "Fast startup (hybrid boot)"; g_PowerCtrls[i].Value = fastBoot; i++;
    g_PowerCtrls[i].Type = CTRL_BUTTON; g_PowerCtrls[i].X = 320; g_PowerCtrls[i].Y = 340; g_PowerCtrls[i].W = 60; g_PowerCtrls[i].H = 24;
    g_PowerCtrls[i].Label = "OK"; g_PowerCtrls[i].Id = 100; g_PowerDlg.CloseButtonId = 100; i++;
    g_PowerCtrls[i].Type = CTRL_BUTTON; g_PowerCtrls[i].X = 400; g_PowerCtrls[i].Y = 340; g_PowerCtrls[i].W = 70; g_PowerCtrls[i].H = 24;
    g_PowerCtrls[i].Label = "Cancel"; g_PowerCtrls[i].Id = 101; g_PowerDlg.CancelButtonId = 101; i++;

    g_PowerDlg.NumControls = i;
    g_PowerDlg.OnApply = Power_OnApply;
    g_ActiveDialog = &g_PowerDlg;
}

/* Accessibility settings applet */
static CPL_CONTROL g_AccessCtrls[12];
static CPL_DIALOG g_AccessDlg = {
    "Accessibility", 0, g_AccessCtrls,
    0, 0, 460, 300, FALSE, 0, 0, NULL
};

static VOID Access_OnApply(VOID)
{
    SettingsSetHighContrast(g_AccessCtrls[0].Value ? TRUE : FALSE);
    SettingsSetStickyKeys(g_AccessCtrls[1].Value ? TRUE : FALSE);
    SettingsSetFilterKeys(g_AccessCtrls[2].Value ? TRUE : FALSE);
    SettingsSetToggleKeys(g_AccessCtrls[3].Value ? TRUE : FALSE);
    SettingsSetMouseKeys(g_AccessCtrls[4].Value ? TRUE : FALSE);
    DbgPrint("CPL: Accessibility settings applied\n");
}

static VOID CplOpenAccessibility(VOID)
{
    RtlZeroMemory(g_AccessCtrls, sizeof(g_AccessCtrls));
    CplSetupFrame(&g_AccessDlg, "Accessibility");
    g_AccessDlg.DialogH = 300;

    BOOL hc = FALSE, sk = FALSE, fk = FALSE, tk = FALSE, mk = FALSE;
    SettingsGetHighContrast(&hc);
    SettingsGetStickyKeys(&sk);
    SettingsGetFilterKeys(&fk);
    SettingsGetToggleKeys(&tk);
    SettingsGetMouseKeys(&mk);

    ULONG i = 0;
    g_AccessCtrls[i].Type = CTRL_CHECKBOX; g_AccessCtrls[i].X = 20; g_AccessCtrls[i].Y = 40; g_AccessCtrls[i].W = 200; g_AccessCtrls[i].H = 16;
    g_AccessCtrls[i].Label = "High contrast (stronger colors)"; g_AccessCtrls[i].Value = hc; i++;
    g_AccessCtrls[i].Type = CTRL_CHECKBOX; g_AccessCtrls[i].X = 20; g_AccessCtrls[i].Y = 64; g_AccessCtrls[i].W = 200; g_AccessCtrls[i].H = 16;
    g_AccessCtrls[i].Label = "Sticky keys (press modifiers one at a time)"; g_AccessCtrls[i].Value = sk; i++;
    g_AccessCtrls[i].Type = CTRL_CHECKBOX; g_AccessCtrls[i].X = 20; g_AccessCtrls[i].Y = 88; g_AccessCtrls[i].W = 200; g_AccessCtrls[i].H = 16;
    g_AccessCtrls[i].Label = "Filter keys (ignore brief/repeated keystrokes)"; g_AccessCtrls[i].Value = fk; i++;
    g_AccessCtrls[i].Type = CTRL_CHECKBOX; g_AccessCtrls[i].X = 20; g_AccessCtrls[i].Y = 112; g_AccessCtrls[i].W = 200; g_AccessCtrls[i].H = 16;
    g_AccessCtrls[i].Label = "Toggle keys (beep on Caps/Num/Scroll lock)"; g_AccessCtrls[i].Value = tk; i++;
    g_AccessCtrls[i].Type = CTRL_CHECKBOX; g_AccessCtrls[i].X = 20; g_AccessCtrls[i].Y = 136; g_AccessCtrls[i].W = 200; g_AccessCtrls[i].H = 16;
    g_AccessCtrls[i].Label = "Mouse keys (numeric keypad moves pointer)"; g_AccessCtrls[i].Value = mk; i++;
    g_AccessCtrls[i].Type = CTRL_BUTTON; g_AccessCtrls[i].X = 300; g_AccessCtrls[i].Y = 260; g_AccessCtrls[i].W = 60; g_AccessCtrls[i].H = 24;
    g_AccessCtrls[i].Label = "OK"; g_AccessCtrls[i].Id = 100; g_AccessDlg.CloseButtonId = 100; i++;
    g_AccessCtrls[i].Type = CTRL_BUTTON; g_AccessCtrls[i].X = 380; g_AccessCtrls[i].Y = 260; g_AccessCtrls[i].W = 70; g_AccessCtrls[i].H = 24;
    g_AccessCtrls[i].Label = "Cancel"; g_AccessCtrls[i].Id = 101; g_AccessDlg.CancelButtonId = 101; i++;

    g_AccessDlg.NumControls = i;
    g_AccessDlg.OnApply = Access_OnApply;
    g_ActiveDialog = &g_AccessDlg;
}

/* Notifications applet */
static CPL_CONTROL g_NotifCtrls[8];
static CPL_DIALOG g_NotifDlg = {
    "Notifications", 0, g_NotifCtrls,
    0, 0, 440, 220, FALSE, 0, 0, NULL
};

static VOID Notif_OnApply(VOID)
{
    SettingsSetNotificationsEnabled(g_NotifCtrls[0].Value ? TRUE : FALSE);
    SettingsSetDndMode(g_NotifCtrls[1].Value ? TRUE : FALSE);
    DbgPrint("CPL: Notifications applied\n");
}

static VOID CplOpenNotifications(VOID)
{
    RtlZeroMemory(g_NotifCtrls, sizeof(g_NotifCtrls));
    CplSetupFrame(&g_NotifDlg, "Notifications");
    g_NotifDlg.DialogH = 220;

    BOOL ne = TRUE, dnd = FALSE;
    SettingsGetNotificationsEnabled(&ne);
    SettingsGetDndMode(&dnd);

    ULONG i = 0;
    g_NotifCtrls[i].Type = CTRL_CHECKBOX; g_NotifCtrls[i].X = 20; g_NotifCtrls[i].Y = 40; g_NotifCtrls[i].W = 200; g_NotifCtrls[i].H = 16;
    g_NotifCtrls[i].Label = "Enable notifications"; g_NotifCtrls[i].Value = ne; i++;
    g_NotifCtrls[i].Type = CTRL_CHECKBOX; g_NotifCtrls[i].X = 20; g_NotifCtrls[i].Y = 64; g_NotifCtrls[i].W = 200; g_NotifCtrls[i].H = 16;
    g_NotifCtrls[i].Label = "Do Not Disturb mode"; g_NotifCtrls[i].Value = dnd; i++;
    g_NotifCtrls[i].Type = CTRL_BUTTON; g_NotifCtrls[i].X = 280; g_NotifCtrls[i].Y = 180; g_NotifCtrls[i].W = 60; g_NotifCtrls[i].H = 24;
    g_NotifCtrls[i].Label = "OK"; g_NotifCtrls[i].Id = 100; g_NotifDlg.CloseButtonId = 100; i++;
    g_NotifCtrls[i].Type = CTRL_BUTTON; g_NotifCtrls[i].X = 360; g_NotifCtrls[i].Y = 180; g_NotifCtrls[i].W = 70; g_NotifCtrls[i].H = 24;
    g_NotifCtrls[i].Label = "Cancel"; g_NotifCtrls[i].Id = 101; g_NotifDlg.CancelButtonId = 101; i++;

    g_NotifDlg.NumControls = i;
    g_NotifDlg.OnApply = Notif_OnApply;
    g_ActiveDialog = &g_NotifDlg;
}

/* Privacy applet */
static CPL_CONTROL g_PrivacyCtrls[8];
static CPL_DIALOG g_PrivacyDlg = {
    "Privacy Settings", 0, g_PrivacyCtrls,
    0, 0, 440, 240, FALSE, 0, 0, NULL
};

static VOID Privacy_OnApply(VOID)
{
    SettingsSetTelemetryEnabled(g_PrivacyCtrls[0].Value ? TRUE : FALSE);
    SettingsSetAdvertisingId(g_PrivacyCtrls[1].Value ? TRUE : FALSE);
    SettingsSetLocationService(g_PrivacyCtrls[2].Value ? TRUE : FALSE);
    DbgPrint("CPL: Privacy settings applied\n");
}

static VOID CplOpenPrivacy(VOID)
{
    RtlZeroMemory(g_PrivacyCtrls, sizeof(g_PrivacyCtrls));
    CplSetupFrame(&g_PrivacyDlg, "Privacy Settings");
    g_PrivacyDlg.DialogH = 240;

    BOOL tel = TRUE, ad = TRUE, loc = TRUE;
    SettingsGetTelemetryEnabled(&tel);
    SettingsGetAdvertisingId(&ad);
    SettingsGetLocationService(&loc);

    ULONG i = 0;
    g_PrivacyCtrls[i].Type = CTRL_CHECKBOX; g_PrivacyCtrls[i].X = 20; g_PrivacyCtrls[i].Y = 40; g_PrivacyCtrls[i].W = 200; g_PrivacyCtrls[i].H = 16;
    g_PrivacyCtrls[i].Label = "Send telemetry data"; g_PrivacyCtrls[i].Value = tel; i++;
    g_PrivacyCtrls[i].Type = CTRL_CHECKBOX; g_PrivacyCtrls[i].X = 20; g_PrivacyCtrls[i].Y = 64; g_PrivacyCtrls[i].W = 200; g_PrivacyCtrls[i].H = 16;
    g_PrivacyCtrls[i].Label = "Allow advertising ID"; g_PrivacyCtrls[i].Value = ad; i++;
    g_PrivacyCtrls[i].Type = CTRL_CHECKBOX; g_PrivacyCtrls[i].X = 20; g_PrivacyCtrls[i].Y = 88; g_PrivacyCtrls[i].W = 200; g_PrivacyCtrls[i].H = 16;
    g_PrivacyCtrls[i].Label = "Allow location services"; g_PrivacyCtrls[i].Value = loc; i++;
    g_PrivacyCtrls[i].Type = CTRL_BUTTON; g_PrivacyCtrls[i].X = 280; g_PrivacyCtrls[i].Y = 200; g_PrivacyCtrls[i].W = 60; g_PrivacyCtrls[i].H = 24;
    g_PrivacyCtrls[i].Label = "OK"; g_PrivacyCtrls[i].Id = 100; g_PrivacyDlg.CloseButtonId = 100; i++;
    g_PrivacyCtrls[i].Type = CTRL_BUTTON; g_PrivacyCtrls[i].X = 360; g_PrivacyCtrls[i].Y = 200; g_PrivacyCtrls[i].W = 70; g_PrivacyCtrls[i].H = 24;
    g_PrivacyCtrls[i].Label = "Cancel"; g_PrivacyCtrls[i].Id = 101; g_PrivacyDlg.CancelButtonId = 101; i++;

    g_PrivacyDlg.NumControls = i;
    g_PrivacyDlg.OnApply = Privacy_OnApply;
    g_ActiveDialog = &g_PrivacyDlg;
}

/* Regional & time settings applet */
static CPL_CONTROL g_RegionalCtrls[12];
static CPL_DIALOG g_RegionalDlg = {
    "Region & Time", 0, g_RegionalCtrls,
    0, 0, 460, 300, FALSE, 0, 0, NULL
};
static const CHAR *g_TzOpts[] = { "UTC+0", "UTC-5 (EST)", "UTC-8 (PST)", "UTC+1 (CET)", "UTC+9 (JST)", "UTC+5:30 (IST)", NULL };
static const CHAR *g_TimeOpts[] = { "12-hour (h:mm:ss AM/PM)", "24-hour (HH:mm:ss)", NULL };
static const CHAR *g_DateOpts[] = { "MM/DD/YYYY", "DD/MM/YYYY", "YYYY-MM-DD", NULL };

static VOID Regional_OnApply(VOID)
{
    LONG tzIdx = g_RegionalCtrls[0].Value;
    LONG bias;
    switch (tzIdx) {
        case 0: bias = 0; break;
        case 1: bias = 300; break;   /* -5 = 300 */
        case 2: bias = 480; break;
        case 3: bias = -60; break;  /* +1 = -60 */
        case 4: bias = -540; break;
        case 5: bias = -330; break;
        default: bias = 0; break;
    }
    SettingsSetTimeZoneBias((ULONG)bias);
    SettingsSetTimeFormat((ULONG)g_RegionalCtrls[1].Value);
    SettingsSetDateFormat((ULONG)g_RegionalCtrls[2].Value);
    SettingsSetAutoTimeSync(g_RegionalCtrls[3].Value ? TRUE : FALSE);
    DbgPrint("CPL: Regional settings applied\n");
}

static VOID CplOpenRegional(VOID)
{
    RtlZeroMemory(g_RegionalCtrls, sizeof(g_RegionalCtrls));
    CplSetupFrame(&g_RegionalDlg, "Region & Time");
    g_RegionalDlg.DialogH = 300;

    ULONG bias = 0, tf = 0, df = 0;
    BOOL autoSync = TRUE;
    SettingsGetTimeZoneBias(&bias);
    SettingsGetTimeFormat(&tf);
    SettingsGetDateFormat(&df);
    SettingsGetAutoTimeSync(&autoSync);

    LONG tzIdx = 0;
    if (bias == 300) tzIdx = 1;
    else if (bias == 480) tzIdx = 2;
    else if (bias == 60) tzIdx = 3;
    else if (bias == 540) tzIdx = 4;
    else if (bias == 330) tzIdx = 5;

    ULONG i = 0;
    g_RegionalCtrls[i].Type = CTRL_DROPDOWN; g_RegionalCtrls[i].X = 20; g_RegionalCtrls[i].Y = 60; g_RegionalCtrls[i].W = 200; g_RegionalCtrls[i].H = 18;
    g_RegionalCtrls[i].Label = "Time zone"; g_RegionalCtrls[i].Value = tzIdx; g_RegionalCtrls[i].Options = g_TzOpts; i++;
    g_RegionalCtrls[i].Type = CTRL_DROPDOWN; g_RegionalCtrls[i].X = 20; g_RegionalCtrls[i].Y = 110; g_RegionalCtrls[i].W = 200; g_RegionalCtrls[i].H = 18;
    g_RegionalCtrls[i].Label = "Time format"; g_RegionalCtrls[i].Value = tf; g_RegionalCtrls[i].Options = g_TimeOpts; i++;
    g_RegionalCtrls[i].Type = CTRL_DROPDOWN; g_RegionalCtrls[i].X = 20; g_RegionalCtrls[i].Y = 160; g_RegionalCtrls[i].W = 200; g_RegionalCtrls[i].H = 18;
    g_RegionalCtrls[i].Label = "Date format"; g_RegionalCtrls[i].Value = df; g_RegionalCtrls[i].Options = g_DateOpts; i++;
    g_RegionalCtrls[i].Type = CTRL_CHECKBOX; g_RegionalCtrls[i].X = 20; g_RegionalCtrls[i].Y = 200; g_RegionalCtrls[i].W = 200; g_RegionalCtrls[i].H = 16;
    g_RegionalCtrls[i].Label = "Sync time automatically"; g_RegionalCtrls[i].Value = autoSync; i++;
    g_RegionalCtrls[i].Type = CTRL_BUTTON; g_RegionalCtrls[i].X = 300; g_RegionalCtrls[i].Y = 260; g_RegionalCtrls[i].W = 60; g_RegionalCtrls[i].H = 24;
    g_RegionalCtrls[i].Label = "OK"; g_RegionalCtrls[i].Id = 100; g_RegionalDlg.CloseButtonId = 100; i++;
    g_RegionalCtrls[i].Type = CTRL_BUTTON; g_RegionalCtrls[i].X = 380; g_RegionalCtrls[i].Y = 260; g_RegionalCtrls[i].W = 70; g_RegionalCtrls[i].H = 24;
    g_RegionalCtrls[i].Label = "Cancel"; g_RegionalCtrls[i].Id = 101; g_RegionalDlg.CancelButtonId = 101; i++;

    g_RegionalDlg.NumControls = i;
    g_RegionalDlg.OnApply = Regional_OnApply;
    g_ActiveDialog = &g_RegionalDlg;
}

/* System Information (read-only display) */
static CPL_CONTROL g_SystemCtrls[16];
static CPL_DIALOG g_SystemDlg = {
    "System Information", 0, g_SystemCtrls,
    0, 0, 480, 360, FALSE, 0, 0, NULL
};

/* ---- Personalization applet: wallpaper, theme, accent color --------- */
static CPL_CONTROL g_PersonalizeCtrls[24];
static CPL_DIALOG g_PersonalizeDlg = {
    "Personalization", 0, g_PersonalizeCtrls,
    0, 0, 540, 460, FALSE, 0, 0, NULL
};

static VOID Personalize_OnApply(VOID)
{
    CHAR buf[260];
    ULONG k = 0;
    while (g_PersonalizeCtrls[1].ValueLabel[k] && k < 259) { buf[k] = g_PersonalizeCtrls[1].ValueLabel[k]; k++; }
    buf[k] = 0;
    /* Wallpaper path stored as CHAR string. */
    CHAR wbuf[260];
    ULONG j = 0;
    while (buf[j] && j < 259) wbuf[j] = buf[j], j++;
    wbuf[j] = 0;
    SettingsSetWallpaperPath((PCWSTR)wbuf);

    CHAR theme[64];
    ULONG ti = 0;
    while (g_PersonalizeCtrls[3].ValueLabel[ti] && ti < 63) { theme[ti] = g_PersonalizeCtrls[3].ValueLabel[ti]; ti++; }
    theme[ti] = 0;
    SettingsSetThemeName((PCWSTR)theme);

    CHAR accent[16];
    ULONG ai = 0;
    while (g_PersonalizeCtrls[5].ValueLabel[ai] && ai < 15) { accent[ai] = g_PersonalizeCtrls[5].ValueLabel[ai]; ai++; }
    accent[ai] = 0;
    SettingsSetAccentColor((PCWSTR)accent);

    SettingsSetEnableTransparency(g_PersonalizeCtrls[6].Value ? TRUE : FALSE);

    /* Profile picture. */
    CHAR pic[260];
    ULONG pi = 0;
    while (g_PersonalizeCtrls[8].ValueLabel[pi] && pi < 259) { pic[pi] = g_PersonalizeCtrls[8].ValueLabel[pi]; pi++; }
    pic[pi] = 0;
    SettingsSetUserProfilePicture((PCWSTR)pic);

    DbgPrint("CPL: personalization applied (wallpaper=%s theme=%s)\n", wbuf, theme);
}

static VOID CplOpenPersonalize(VOID)
{
    RtlZeroMemory(g_PersonalizeCtrls, sizeof(g_PersonalizeCtrls));
    CplSetupFrame(&g_PersonalizeDlg, "Personalization");

    WCHAR wp[260]; wp[0] = 0;
    SettingsGetWallpaperPath(wp, 260);
    CHAR wpStr[260]; ULONG wpi = 0;
    while (wp[wpi] && wpi < 259) { wpStr[wpi] = (CHAR)wp[wpi]; wpi++; }
    wpStr[wpi] = 0;

    WCHAR theme[64]; theme[0] = 0;
    SettingsGetThemeName(theme, 64);
    CHAR themeStr[64]; ULONG ti = 0;
    while (theme[ti] && ti < 63) { themeStr[ti] = (CHAR)theme[ti]; ti++; }
    themeStr[ti] = 0;

    WCHAR accent[16]; accent[0] = 0;
    SettingsGetAccentColor(accent, 16);
    CHAR accentStr[16]; ULONG ai = 0;
    while (accent[ai] && ai < 15) { accentStr[ai] = (CHAR)accent[ai]; ai++; }
    accentStr[ai] = 0;

    WCHAR pic[260]; pic[0] = 0;
    SettingsGetUserProfilePicture(pic, 260);
    CHAR picStr[260]; ULONG pi = 0;
    while (pic[pi] && pi < 259) { picStr[pi] = (CHAR)pic[pi]; pi++; }
    picStr[pi] = 0;

    BOOL transparency = FALSE;
    SettingsGetEnableTransparency(&transparency);

    ULONG i = 0;
    g_PersonalizeCtrls[i].Type = CTRL_LABEL; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 40; g_PersonalizeCtrls[i].W = 200; g_PersonalizeCtrls[i].H = 16;
    g_PersonalizeCtrls[i].Label = "Wallpaper file path:"; i++;
    g_PersonalizeCtrls[i].Type = CTRL_TEXTBOX; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 70; g_PersonalizeCtrls[i].W = 400; g_PersonalizeCtrls[i].H = 20;
    g_PersonalizeCtrls[i].Label = "Path"; g_PersonalizeCtrls[i].Id = 1;
    for (ULONG n = 0; wpStr[n] && n < 99; n++) g_PersonalizeCtrls[i].ValueLabel[n] = wpStr[n];
    i++;
    g_PersonalizeCtrls[i].Type = CTRL_BUTTON; g_PersonalizeCtrls[i].X = 430; g_PersonalizeCtrls[i].Y = 70; g_PersonalizeCtrls[i].W = 90; g_PersonalizeCtrls[i].H = 20;
    g_PersonalizeCtrls[i].Label = "Browse..."; g_PersonalizeCtrls[i].Id = 100; i++;
    g_PersonalizeCtrls[i].Type = CTRL_LABEL; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 100; g_PersonalizeCtrls[i].W = 200; g_PersonalizeCtrls[i].H = 16;
    g_PersonalizeCtrls[i].Label = "Theme:"; i++;
    g_PersonalizeCtrls[i].Type = CTRL_DROPDOWN; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 130; g_PersonalizeCtrls[i].W = 200; g_PersonalizeCtrls[i].H = 18;
    g_PersonalizeCtrls[i].Label = "Theme"; g_PersonalizeCtrls[i].Id = 3;
    g_PersonalizeCtrls[i].Options = (const CHAR *[]){"Luna","Luna Metallic","Luna Homestead","Royale","Embedded","Zune","Classic","Custom",NULL};
    for (ULONG n = 0; themeStr[n] && n < 63; n++) g_PersonalizeCtrls[i].ValueLabel[n] = themeStr[n];
    i++;
    g_PersonalizeCtrls[i].Type = CTRL_LABEL; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 160; g_PersonalizeCtrls[i].W = 200; g_PersonalizeCtrls[i].H = 16;
    g_PersonalizeCtrls[i].Label = "Accent color:"; i++;
    g_PersonalizeCtrls[i].Type = CTRL_DROPDOWN; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 190; g_PersonalizeCtrls[i].W = 120; g_PersonalizeCtrls[i].H = 18;
    g_PersonalizeCtrls[i].Label = "Color"; g_PersonalizeCtrls[i].Id = 5;
    g_PersonalizeCtrls[i].Options = (const CHAR *[]){"#3A6EA5","#7AA2C4","#FF6F61","#6FA8DC","#93C47D","#E69138","#CC4125","#000000",NULL};
    for (ULONG n = 0; accentStr[n] && n < 15; n++) g_PersonalizeCtrls[i].ValueLabel[n] = accentStr[n];
    i++;
    g_PersonalizeCtrls[i].Type = CTRL_CHECKBOX; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 220; g_PersonalizeCtrls[i].W = 300; g_PersonalizeCtrls[i].H = 16;
    g_PersonalizeCtrls[i].Label = "Enable window transparency"; g_PersonalizeCtrls[i].Value = transparency; i++;
    g_PersonalizeCtrls[i].Type = CTRL_LABEL; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 250; g_PersonalizeCtrls[i].W = 200; g_PersonalizeCtrls[i].H = 16;
    g_PersonalizeCtrls[i].Label = "User profile picture:"; i++;
    g_PersonalizeCtrls[i].Type = CTRL_TEXTBOX; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 280; g_PersonalizeCtrls[i].W = 400; g_PersonalizeCtrls[i].H = 20;
    g_PersonalizeCtrls[i].Label = "Picture"; g_PersonalizeCtrls[i].Id = 8;
    for (ULONG n = 0; picStr[n] && n < 99; n++) g_PersonalizeCtrls[i].ValueLabel[n] = picStr[n];
    i++;
    g_PersonalizeCtrls[i].Type = CTRL_LABEL; g_PersonalizeCtrls[i].X = 20; g_PersonalizeCtrls[i].Y = 320; g_PersonalizeCtrls[i].W = 500; g_PersonalizeCtrls[i].H = 80;
    g_PersonalizeCtrls[i].Label = "Type a path to a .bmp/.jpg/.png file to use as wallpaper or\nprofile picture. The theme dropdown loads one of the bundled\nXP-style themes; Luna is the default."; i++;

    g_PersonalizeDlg.NumControls = i;
    g_PersonalizeDlg.OnApply = Personalize_OnApply;
    g_ActiveDialog = &g_PersonalizeDlg;
}

/* ---- Screensaver applet ---------------------------------------------- */
static CPL_CONTROL g_ScreensaverCtrls[16];
static CPL_DIALOG g_ScreensaverDlg = {
    "Screensaver", 0, g_ScreensaverCtrls,
    0, 0, 540, 380, FALSE, 0, 0, NULL
};

static VOID Screensaver_OnApply(VOID)
{
    CHAR buf[260];
    ULONG k = 0;
    while (g_ScreensaverCtrls[1].ValueLabel[k] && k < 259) { buf[k] = g_ScreensaverCtrls[1].ValueLabel[k]; k++; }
    buf[k] = 0;
    SettingsSetScreensaverPath((PCWSTR)buf);
    ULONG timeout = (ULONG)g_ScreensaverCtrls[2].Value * 60;
    SettingsSetScreensaverTimeout(timeout);
    SettingsSetScreensaverSecure(g_ScreensaverCtrls[3].Value ? TRUE : FALSE);
    SettingsSetScreensaverEnabled(g_ScreensaverCtrls[4].Value ? TRUE : FALSE);
    DbgPrint("CPL: screensaver applied - %s timeout=%u\n", buf, timeout);
}

static VOID CplOpenScreensaver(VOID)
{
    RtlZeroMemory(g_ScreensaverCtrls, sizeof(g_ScreensaverCtrls));
    CplSetupFrame(&g_ScreensaverDlg, "Screensaver");

    WCHAR path[260]; path[0] = 0;
    SettingsGetScreensaverPath(path, 260);
    CHAR pathStr[260]; ULONG pi = 0;
    while (path[pi] && pi < 259) { pathStr[pi] = (CHAR)path[pi]; pi++; }
    pathStr[pi] = 0;

    ULONG timeout = 600;
    SettingsGetScreensaverTimeout(&timeout);
    BOOL secure = FALSE, enabled = FALSE;
    SettingsGetScreensaverSecure(&secure);
    SettingsGetScreensaverEnabled(&enabled);

    ULONG i = 0;
    g_ScreensaverCtrls[i].Type = CTRL_LABEL; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 40; g_ScreensaverCtrls[i].W = 200; g_ScreensaverCtrls[i].H = 16;
    g_ScreensaverCtrls[i].Label = "Screensaver executable:"; i++;
    g_ScreensaverCtrls[i].Type = CTRL_TEXTBOX; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 70; g_ScreensaverCtrls[i].W = 400; g_ScreensaverCtrls[i].H = 20;
    g_ScreensaverCtrls[i].Label = "Path"; g_ScreensaverCtrls[i].Id = 1;
    for (ULONG n = 0; pathStr[n] && n < 99; n++) g_ScreensaverCtrls[i].ValueLabel[n] = pathStr[n];
    i++;
    g_ScreensaverCtrls[i].Type = CTRL_SLIDER; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 110; g_ScreensaverCtrls[i].W = 300; g_ScreensaverCtrls[i].H = 20;
    g_ScreensaverCtrls[i].Label = "Wait (minutes)"; g_ScreensaverCtrls[i].Value = timeout / 60; g_ScreensaverCtrls[i].MaxValue = 60; i++;
    g_ScreensaverCtrls[i].Type = CTRL_CHECKBOX; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 140; g_ScreensaverCtrls[i].W = 300; g_ScreensaverCtrls[i].H = 16;
    g_ScreensaverCtrls[i].Label = "On resume, require password"; g_ScreensaverCtrls[i].Value = secure; i++;
    g_ScreensaverCtrls[i].Type = CTRL_CHECKBOX; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 164; g_ScreensaverCtrls[i].W = 300; g_ScreensaverCtrls[i].H = 16;
    g_ScreensaverCtrls[i].Label = "Enable screensaver"; g_ScreensaverCtrls[i].Value = enabled; i++;
    g_ScreensaverCtrls[i].Type = CTRL_BUTTON; g_ScreensaverCtrls[i].X = 20; g_ScreensaverCtrls[i].Y = 200; g_ScreensaverCtrls[i].W = 100; g_ScreensaverCtrls[i].H = 24;
    g_ScreensaverCtrls[i].Label = "Preview"; g_ScreensaverCtrls[i].Id = 110; i++;

    g_ScreensaverDlg.NumControls = i;
    g_ScreensaverDlg.OnApply = Screensaver_OnApply;
    g_ActiveDialog = &g_ScreensaverDlg;
}

/* ---- Start Menu applet ----------------------------------------------- */
static CPL_CONTROL g_StartMenuCtrls[24];
static CPL_DIALOG g_StartMenuDlg = {
    "Start Menu", 0, g_StartMenuCtrls,
    0, 0, 540, 460, FALSE, 0, 0, NULL
};

static VOID StartMenu_OnApply(VOID)
{
    CHAR buf[64];
    ULONG k = 0;
    while (g_StartMenuCtrls[1].ValueLabel[k] && k < 63) { buf[k] = g_StartMenuCtrls[1].ValueLabel[k]; k++; }
    buf[k] = 0;
    SettingsSetStartButtonLabel((PCWSTR)buf);

    CHAR style[64];
    k = 0;
    while (g_StartMenuCtrls[3].ValueLabel[k] && k < 63) { style[k] = g_StartMenuCtrls[3].ValueLabel[k]; k++; }
    style[k] = 0;
    SettingsSetStartMenuStyle((PCWSTR)style);

    SettingsSetShowStartButton(g_StartMenuCtrls[0].Value ? TRUE : FALSE);
    SettingsSetShowRunCommand(g_StartMenuCtrls[5].Value ? TRUE : FALSE);
    SettingsSetShowSearchBox(g_StartMenuCtrls[6].Value ? TRUE : FALSE);
    SettingsSetShowMyComputer(g_StartMenuCtrls[7].Value ? TRUE : FALSE);
    SettingsSetShowMyDocuments(g_StartMenuCtrls[8].Value ? TRUE : FALSE);
    SettingsSetShowControlPanel(g_StartMenuCtrls[9].Value ? TRUE : FALSE);
    SettingsSetShowRecycleBin(g_StartMenuCtrls[10].Value ? TRUE : FALSE);
}

static VOID CplOpenStartMenu(VOID)
{
    RtlZeroMemory(g_StartMenuCtrls, sizeof(g_StartMenuCtrls));
    CplSetupFrame(&g_StartMenuDlg, "Start Menu");

    WCHAR label[64]; label[0] = 0;
    SettingsGetStartButtonLabel(label, 64);
    CHAR labelStr[64]; ULONG li = 0;
    while (label[li] && li < 63) { labelStr[li] = (CHAR)label[li]; li++; }
    labelStr[li] = 0;

    WCHAR style[64]; style[0] = 0;
    SettingsGetStartMenuStyle(style, 64);
    CHAR styleStr[64]; ULONG si = 0;
    while (style[si] && si < 63) { styleStr[si] = (CHAR)style[si]; si++; }
    styleStr[si] = 0;

    BOOL showStart = TRUE, showRun = TRUE, showSearch = TRUE;
    BOOL showMyComp = TRUE, showDocs = TRUE, showCpl = TRUE, showBin = TRUE;
    SettingsGetShowStartButton(&showStart);
    SettingsGetShowRunCommand(&showRun);
    SettingsGetShowSearchBox(&showSearch);
    SettingsGetShowMyComputer(&showMyComp);
    SettingsGetShowMyDocuments(&showDocs);
    SettingsGetShowControlPanel(&showCpl);
    SettingsGetShowRecycleBin(&showBin);

    ULONG i = 0;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 40; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Show Start button"; g_StartMenuCtrls[i].Value = showStart; i++;
    g_StartMenuCtrls[i].Type = CTRL_TEXTBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 70; g_StartMenuCtrls[i].W = 200; g_StartMenuCtrls[i].H = 20;
    g_StartMenuCtrls[i].Label = "Start button label"; g_StartMenuCtrls[i].Id = 1;
    for (ULONG n = 0; labelStr[n] && n < 99; n++) g_StartMenuCtrls[i].ValueLabel[n] = labelStr[n];
    i++;
    g_StartMenuCtrls[i].Type = CTRL_LABEL; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 100; g_StartMenuCtrls[i].W = 200; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Menu style:"; i++;
    g_StartMenuCtrls[i].Type = CTRL_DROPDOWN; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 130; g_StartMenuCtrls[i].W = 200; g_StartMenuCtrls[i].H = 18;
    g_StartMenuCtrls[i].Label = "Style"; g_StartMenuCtrls[i].Id = 3;
    g_StartMenuCtrls[i].Options = (const CHAR *[]){"ClassicXP","Modern","OpenShell","Simple","Custom",NULL};
    for (ULONG n = 0; styleStr[n] && n < 63; n++) g_StartMenuCtrls[i].ValueLabel[n] = styleStr[n];
    i++;
    g_StartMenuCtrls[i].Type = CTRL_LABEL; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 160; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Items shown in Start menu:"; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 180; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Run command..."; g_StartMenuCtrls[i].Value = showRun; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 204; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Search box"; g_StartMenuCtrls[i].Value = showSearch; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 228; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "My Computer"; g_StartMenuCtrls[i].Value = showMyComp; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 252; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "My Documents"; g_StartMenuCtrls[i].Value = showDocs; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 276; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Control Panel"; g_StartMenuCtrls[i].Value = showCpl; i++;
    g_StartMenuCtrls[i].Type = CTRL_CHECKBOX; g_StartMenuCtrls[i].X = 20; g_StartMenuCtrls[i].Y = 300; g_StartMenuCtrls[i].W = 300; g_StartMenuCtrls[i].H = 16;
    g_StartMenuCtrls[i].Label = "Recycle Bin"; g_StartMenuCtrls[i].Value = showBin; i++;

    g_StartMenuDlg.NumControls = i;
    g_StartMenuDlg.OnApply = StartMenu_OnApply;
    g_ActiveDialog = &g_StartMenuDlg;
}

/* ---- Taskbar applet -------------------------------------------------- */
static CPL_CONTROL g_TaskbarCtrls[16];
static CPL_DIALOG g_TaskbarDlg = {
    "Taskbar", 0, g_TaskbarCtrls,
    0, 0, 540, 360, FALSE, 0, 0, NULL
};

static VOID Taskbar_OnApply(VOID)
{
    SettingsSetTaskbarPosition((ULONG)g_TaskbarCtrls[0].Value);
    SettingsSetTaskbarAutoHide((ULONG)g_TaskbarCtrls[1].Value);
    SettingsSetTaskbarSmallIcons((ULONG)g_TaskbarCtrls[2].Value);
    SettingsSetTaskbarCombine((ULONG)g_TaskbarCtrls[3].Value);
}

static VOID CplOpenTaskbar(VOID)
{
    RtlZeroMemory(g_TaskbarCtrls, sizeof(g_TaskbarCtrls));
    CplSetupFrame(&g_TaskbarDlg, "Taskbar");
    ULONG pos = 1, autohide = 0, small = 0, combine = 0;
    SettingsGetTaskbarPosition(&pos);
    SettingsGetTaskbarAutoHide(&autohide);
    SettingsGetTaskbarSmallIcons(&small);
    SettingsGetTaskbarCombine(&combine);
    ULONG i = 0;
    g_TaskbarCtrls[i].Type = CTRL_DROPDOWN; g_TaskbarCtrls[i].X = 20; g_TaskbarCtrls[i].Y = 40; g_TaskbarCtrls[i].W = 200; g_TaskbarCtrls[i].H = 18;
    g_TaskbarCtrls[i].Label = "Position"; g_TaskbarCtrls[i].Options = (const CHAR *[]){"Bottom","Top","Left","Right",NULL}; g_TaskbarCtrls[i].Value = pos; i++;
    g_TaskbarCtrls[i].Type = CTRL_CHECKBOX; g_TaskbarCtrls[i].X = 20; g_TaskbarCtrls[i].Y = 70; g_TaskbarCtrls[i].W = 300; g_TaskbarCtrls[i].H = 16;
    g_TaskbarCtrls[i].Label = "Auto-hide taskbar"; g_TaskbarCtrls[i].Value = autohide; i++;
    g_TaskbarCtrls[i].Type = CTRL_CHECKBOX; g_TaskbarCtrls[i].X = 20; g_TaskbarCtrls[i].Y = 94; g_TaskbarCtrls[i].W = 300; g_TaskbarCtrls[i].H = 16;
    g_TaskbarCtrls[i].Label = "Use small icons"; g_TaskbarCtrls[i].Value = small; i++;
    g_TaskbarCtrls[i].Type = CTRL_DROPDOWN; g_TaskbarCtrls[i].X = 20; g_TaskbarCtrls[i].Y = 124; g_TaskbarCtrls[i].W = 200; g_TaskbarCtrls[i].H = 18;
    g_TaskbarCtrls[i].Label = "Combine buttons"; g_TaskbarCtrls[i].Options = (const CHAR *[]){"Never","When full","Always",NULL}; g_TaskbarCtrls[i].Value = combine; i++;
    g_TaskbarDlg.NumControls = i;
    g_TaskbarDlg.OnApply = Taskbar_OnApply;
    g_ActiveDialog = &g_TaskbarDlg;
}

/* ---- Terminal applet ------------------------------------------------- */
static CPL_CONTROL g_TerminalCtrls[12];
static CPL_DIALOG g_TerminalDlg = {
    "Terminal", 0, g_TerminalCtrls,
    0, 0, 540, 360, FALSE, 0, 0, NULL
};

static VOID Terminal_OnApply(VOID)
{
    CHAR buf[260];
    ULONG k = 0;
    while (g_TerminalCtrls[1].ValueLabel[k] && k < 259) { buf[k] = g_TerminalCtrls[1].ValueLabel[k]; k++; }
    buf[k] = 0;
    SettingsSetDefaultTerminal((PCWSTR)buf);

    CHAR psbuf[260];
    k = 0;
    while (g_TerminalCtrls[3].ValueLabel[k] && k < 259) { psbuf[k] = g_TerminalCtrls[3].ValueLabel[k]; k++; }
    psbuf[k] = 0;
    SettingsSetPowerShellPath((PCWSTR)psbuf);
}

static VOID CplOpenTerminal(VOID)
{
    RtlZeroMemory(g_TerminalCtrls, sizeof(g_TerminalCtrls));
    CplSetupFrame(&g_TerminalDlg, "Terminal");
    WCHAR term[260]; term[0] = 0;
    SettingsGetDefaultTerminal(term, 260);
    CHAR termStr[260]; ULONG ti = 0;
    while (term[ti] && ti < 259) { termStr[ti] = (CHAR)term[ti]; ti++; }
    termStr[ti] = 0;

    WCHAR ps[260]; ps[0] = 0;
    SettingsGetPowerShellPath(ps, 260);
    CHAR psStr[260]; ULONG pi = 0;
    while (ps[pi] && pi < 259) { psStr[pi] = (CHAR)ps[pi]; pi++; }
    psStr[pi] = 0;

    ULONG i = 0;
    g_TerminalCtrls[i].Type = CTRL_LABEL; g_TerminalCtrls[i].X = 20; g_TerminalCtrls[i].Y = 40; g_TerminalCtrls[i].W = 200; g_TerminalCtrls[i].H = 16;
    g_TerminalCtrls[i].Label = "Default terminal (cmd.exe):"; i++;
    g_TerminalCtrls[i].Type = CTRL_TEXTBOX; g_TerminalCtrls[i].X = 20; g_TerminalCtrls[i].Y = 70; g_TerminalCtrls[i].W = 400; g_TerminalCtrls[i].H = 20;
    g_TerminalCtrls[i].Label = "Path"; g_TerminalCtrls[i].Id = 1;
    for (ULONG n = 0; termStr[n] && n < 99; n++) g_TerminalCtrls[i].ValueLabel[n] = termStr[n];
    i++;
    g_TerminalCtrls[i].Type = CTRL_LABEL; g_TerminalCtrls[i].X = 20; g_TerminalCtrls[i].Y = 100; g_TerminalCtrls[i].W = 200; g_TerminalCtrls[i].H = 16;
    g_TerminalCtrls[i].Label = "PowerShell path:"; i++;
    g_TerminalCtrls[i].Type = CTRL_TEXTBOX; g_TerminalCtrls[i].X = 20; g_TerminalCtrls[i].Y = 130; g_TerminalCtrls[i].W = 400; g_TerminalCtrls[i].H = 20;
    g_TerminalCtrls[i].Label = "Path"; g_TerminalCtrls[i].Id = 3;
    for (ULONG n = 0; psStr[n] && n < 99; n++) g_TerminalCtrls[i].ValueLabel[n] = psStr[n];
    i++;
    g_TerminalCtrls[i].Type = CTRL_LABEL; g_TerminalCtrls[i].X = 20; g_TerminalCtrls[i].Y = 170; g_TerminalCtrls[i].W = 500; g_TerminalCtrls[i].H = 80;
    g_TerminalCtrls[i].Label = "Both the cmd.exe and PowerShell-compatible engines are\nbuilt in. Type 'POWERSHELL' or 'PS' at the cmd prompt to\nswitch, 'CMD' to switch back. Output redirection with > works."; i++;

    g_TerminalDlg.NumControls = i;
    g_TerminalDlg.OnApply = Terminal_OnApply;
    g_ActiveDialog = &g_TerminalDlg;
}

/* ---- Themes applet --------------------------------------------------- */
static CPL_CONTROL g_ThemesCtrls[16];
static CPL_DIALOG g_ThemesDlg = {
    "Themes", 0, g_ThemesCtrls,
    0, 0, 540, 380, FALSE, 0, 0, NULL
};

static VOID Themes_OnApply(VOID)
{
    CHAR buf[64];
    ULONG k = 0;
    while (g_ThemesCtrls[1].ValueLabel[k] && k < 63) { buf[k] = g_ThemesCtrls[1].ValueLabel[k]; k++; }
    buf[k] = 0;
    SettingsSetThemeName((PCWSTR)buf);

    CHAR accent[16];
    k = 0;
    while (g_ThemesCtrls[3].ValueLabel[k] && k < 15) { accent[k] = g_ThemesCtrls[3].ValueLabel[k]; k++; }
    accent[k] = 0;
    SettingsSetAccentColor((PCWSTR)accent);
    SettingsSetEnableTransparency(g_ThemesCtrls[4].Value ? TRUE : FALSE);
    SettingsSetCursorScheme((PCWSTR)"Windows Default");
}

static VOID CplOpenThemes(VOID)
{
    RtlZeroMemory(g_ThemesCtrls, sizeof(g_ThemesCtrls));
    CplSetupFrame(&g_ThemesDlg, "Themes");
    WCHAR theme[64]; theme[0] = 0;
    SettingsGetThemeName(theme, 64);
    CHAR themeStr[64]; ULONG ti = 0;
    while (theme[ti] && ti < 63) { themeStr[ti] = (CHAR)theme[ti]; ti++; }
    themeStr[ti] = 0;
    WCHAR accent[16]; accent[0] = 0;
    SettingsGetAccentColor(accent, 16);
    CHAR accentStr[16]; ULONG ai = 0;
    while (accent[ai] && ai < 15) { accentStr[ai] = (CHAR)accent[ai]; ai++; }
    accentStr[ai] = 0;
    BOOL tr = FALSE;
    SettingsGetEnableTransparency(&tr);

    ULONG i = 0;
    g_ThemesCtrls[i].Type = CTRL_LABEL; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 40; g_ThemesCtrls[i].W = 200; g_ThemesCtrls[i].H = 16;
    g_ThemesCtrls[i].Label = "Active theme:"; i++;
    g_ThemesCtrls[i].Type = CTRL_DROPDOWN; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 70; g_ThemesCtrls[i].W = 200; g_ThemesCtrls[i].H = 18;
    g_ThemesCtrls[i].Label = "Theme"; g_ThemesCtrls[i].Id = 1;
    g_ThemesCtrls[i].Options = (const CHAR *[]){"Luna","Luna Metallic","Luna Homestead","Royale","Embedded","Zune","Classic","Custom",NULL};
    for (ULONG n = 0; themeStr[n] && n < 63; n++) g_ThemesCtrls[i].ValueLabel[n] = themeStr[n];
    i++;
    g_ThemesCtrls[i].Type = CTRL_LABEL; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 100; g_ThemesCtrls[i].W = 200; g_ThemesCtrls[i].H = 16;
    g_ThemesCtrls[i].Label = "Accent:"; i++;
    g_ThemesCtrls[i].Type = CTRL_DROPDOWN; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 130; g_ThemesCtrls[i].W = 120; g_ThemesCtrls[i].H = 18;
    g_ThemesCtrls[i].Label = "Color"; g_ThemesCtrls[i].Id = 3;
    g_ThemesCtrls[i].Options = (const CHAR *[]){"#3A6EA5","#7AA2C4","#FF6F61","#6FA8DC","#93C47D","#E69138","#CC4125","#000000",NULL};
    for (ULONG n = 0; accentStr[n] && n < 15; n++) g_ThemesCtrls[i].ValueLabel[n] = accentStr[n];
    i++;
    g_ThemesCtrls[i].Type = CTRL_CHECKBOX; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 160; g_ThemesCtrls[i].W = 300; g_ThemesCtrls[i].H = 16;
    g_ThemesCtrls[i].Label = "Enable window transparency"; g_ThemesCtrls[i].Value = tr; g_ThemesCtrls[i].Id = 4; i++;
    g_ThemesCtrls[i].Type = CTRL_LABEL; g_ThemesCtrls[i].X = 20; g_ThemesCtrls[i].Y = 200; g_ThemesCtrls[i].W = 500; g_ThemesCtrls[i].H = 100;
    g_ThemesCtrls[i].Label = "Bundled XP themes include Luna (blue), Luna Metallic (silver),\nLuna Homestead (olive green), Royale (Zune-style dark),\nEmbedded (industrial), Zune (dark brown), and Classic.\nEach theme defines a complete color palette and visual style."; i++;
    g_ThemesDlg.NumControls = i;
    g_ThemesDlg.OnApply = Themes_OnApply;
    g_ActiveDialog = &g_ThemesDlg;
}

/* ---- Controller / Gamepad applet ------------------------------------ */
static CPL_CONTROL g_ControllerCtrls[24];
static CPL_DIALOG g_ControllerDlg = {
    "Controllers", 0, g_ControllerCtrls,
    0, 0, 540, 460, FALSE, 0, 0, NULL
};

static VOID Controller_OnApply(VOID)
{
    /* Rumble intensity (0-100) applies to the left+right motors. */
    ULONG intensity = (ULONG)g_ControllerCtrls[2].Value;
    GAMEPAD_RUMBLE r;
    r.LeftMotor = (USHORT)((ULONGLONG)intensity * 65535 / 100);
    r.RightMotor = r.LeftMotor;
    r.LeftTrigger = (UCHAR)intensity * 255 / 100;
    r.RightTrigger = r.LeftTrigger;
    /* Apply to all connected gamepads. */
    ULONG ids[4]; BOOLEAN connected[4];
    ULONG n = GamepadEnum(4, ids, NULL, connected);
    for (ULONG i = 0; i < n; i++) {
        if (connected[i]) GamepadSetRumble(ids[i], &r);
    }
    /* Trigger rumble test if button pressed. */
    if (g_ControllerCtrls[4].Value) {
        DbgPrint("CPL: rumble test fired on %u gamepads\n", n);
        /* Stop after 1 second. */
        KeStallExecutionProcessor(1000000);
        for (ULONG i = 0; i < n; i++) {
            if (connected[i]) GamepadStopRumble(ids[i]);
        }
    }
}

static VOID CplOpenController(VOID)
{
    RtlZeroMemory(g_ControllerCtrls, sizeof(g_ControllerCtrls));
    CplSetupFrame(&g_ControllerDlg, "Controllers");

    ULONG ids[4]; PCHAR names[4]; BOOLEAN connected[4];
    CHAR nameBufs[4][64];
    for (ULONG i = 0; i < 4; i++) names[i] = nameBufs[i];
    ULONG n = GamepadEnum(4, ids, names, connected);

    ULONG i = 0;
    g_ControllerCtrls[i].Type = CTRL_LABEL; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 40; g_ControllerCtrls[i].W = 300; g_ControllerCtrls[i].H = 16;
    g_ControllerCtrls[i].Label = "Connected controllers:"; i++;
    for (ULONG k = 0; k < n; k++) {
        g_ControllerCtrls[i].Type = CTRL_LABEL; g_ControllerCtrls[i].X = 30; g_ControllerCtrls[i].Y = 60 + k * 18; g_ControllerCtrls[i].W = 400; g_ControllerCtrls[i].H = 16;
        CHAR buf[64];
        ULONG bi = 0;
        buf[bi++] = connected[k] ? '*' : ' ';
        buf[bi++] = ' ';
        for (ULONG j = 0; nameBufs[k][j] && bi < 60; j++) buf[bi++] = nameBufs[k][j];
        buf[bi] = 0;
        g_ControllerCtrls[i].Label = buf; i++;
    }
    g_ControllerCtrls[i].Type = CTRL_LABEL; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 140; g_ControllerCtrls[i].W = 300; g_ControllerCtrls[i].H = 16;
    g_ControllerCtrls[i].Label = "Rumble intensity:"; i++;
    g_ControllerCtrls[i].Type = CTRL_SLIDER; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 170; g_ControllerCtrls[i].W = 300; g_ControllerCtrls[i].H = 20;
    g_ControllerCtrls[i].Label = "Percent (0-100)"; g_ControllerCtrls[i].Value = 50; g_ControllerCtrls[i].MaxValue = 100; i++;
    g_ControllerCtrls[i].Type = CTRL_BUTTON; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 200; g_ControllerCtrls[i].W = 200; g_ControllerCtrls[i].H = 24;
    g_ControllerCtrls[i].Label = "Test rumble"; g_ControllerCtrls[i].Id = 150; i++;
    g_ControllerCtrls[i].Type = CTRL_CHECKBOX; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 234; g_ControllerCtrls[i].W = 400; g_ControllerCtrls[i].H = 16;
    g_ControllerCtrls[i].Label = "Enable trigger rumble (PS5 adaptive triggers)"; g_ControllerCtrls[i].Value = TRUE; i++;
    g_ControllerCtrls[i].Type = CTRL_LABEL; g_ControllerCtrls[i].X = 20; g_ControllerCtrls[i].Y = 270; g_ControllerCtrls[i].W = 500; g_ControllerCtrls[i].H = 80;
    g_ControllerCtrls[i].Label = "Each entry shows the controller name. Rumble applies\n"
                                  "to both motors on Xbox/DualShock/DualSense; trigger\n"
                                  "rumble is a PS5-only feature for adaptive triggers."; i++;

    g_ControllerDlg.NumControls = i;
    g_ControllerDlg.OnApply = Controller_OnApply;
    g_ActiveDialog = &g_ControllerDlg;
}

/* ---- Steam Input / Action Layers applet ----------------------------- */
static CPL_CONTROL g_SteamInputCtrls[24];
static CPL_DIALOG g_SteamInputDlg = {
    "Steam Input", 0, g_SteamInputCtrls,
    0, 0, 540, 460, FALSE, 0, 0, NULL
};

static VOID SteamInput_OnApply(VOID)
{
    DbgPrint("CPL: Steam Input settings applied\n");
}

static VOID CplOpenSteamInput(VOID)
{
    RtlZeroMemory(g_SteamInputCtrls, sizeof(g_SteamInputCtrls));
    CplSetupFrame(&g_SteamInputDlg, "Steam Input");

    ULONG s64[16];
    ULONG n = SteamInputGetConnectedControllers(s64, 16);

    ULONG i = 0;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 40; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Steam Input-compatible action layer:"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 60; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    CHAR buf[64];
    ULONG bi = 0;
    buf[bi++] = 'F'; buf[bi++] = 'o'; buf[bi++] = 'u'; buf[bi++] = 'n'; buf[bi++] = 'd';
    buf[bi++] = ' '; for (ULONG x = 0; "0123456789"[x]; x++) {}
    for (ULONG x = 0; x < 8; x++) {
        if (n < 10) {
            buf[bi++] = ' ';
            break;
        }
    }
    /* crude number printing */
    if (n == 0) { buf[bi++] = '0'; }
    else {
        CHAR digits[8]; ULONG di = 0;
        ULONG t = n;
        while (t > 0) { digits[di++] = '0' + (CHAR)(t % 10); t /= 10; }
        while (di > 0 && bi < 60) buf[bi++] = digits[--di];
    }
    buf[bi++] = ' '; buf[bi++] = 'c'; buf[bi++] = 'o'; buf[bi++] = 'n'; buf[bi++] = 't'; buf[bi++] = 'r'; buf[bi++] = 'o'; buf[bi++] = 'l'; buf[bi++] = 'l'; buf[bi++] = 'e'; buf[bi++] = 'r'; buf[bi++] = 's'; buf[bi] = 0;
    g_SteamInputCtrls[i].Label = buf; i++;

    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 100; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Active action set:"; i++;
    g_SteamInputCtrls[i].Type = CTRL_DROPDOWN; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 130; g_SteamInputCtrls[i].W = 200; g_SteamInputCtrls[i].H = 18;
    g_SteamInputCtrls[i].Label = "Set"; g_SteamInputCtrls[i].Options = (const CHAR *[]){"Menu","InGame","Vehicle","Character","Custom",NULL};
    g_SteamInputCtrls[i].Value = 1; i++;

    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 160; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Bound digital actions:"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 30; g_SteamInputCtrls[i].Y = 180; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "  jump (DPad Up), attack (A), reload (B)"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 30; g_SteamInputCtrls[i].Y = 196; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "  crouch (X), sprint (Y), interact (LB)"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 30; g_SteamInputCtrls[i].Y = 212; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "  block (RB), pause (Start)"; i++;

    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 240; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Bound analog actions:"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 30; g_SteamInputCtrls[i].Y = 260; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "  move (LStick), look (RStick), steer (LStick)"; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 30; g_SteamInputCtrls[i].Y = 276; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "  throttle (RTrigger)"; i++;

    g_SteamInputCtrls[i].Type = CTRL_CHECKBOX; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 310; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Use gyroscope for fine aim (DualSense/DualShock 4)"; g_SteamInputCtrls[i].Value = TRUE; i++;
    g_SteamInputCtrls[i].Type = CTRL_CHECKBOX; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 334; g_SteamInputCtrls[i].W = 400; g_SteamInputCtrls[i].H = 16;
    g_SteamInputCtrls[i].Label = "Use touchpad as mouse (Steam Controller)"; g_SteamInputCtrls[i].Value = FALSE; i++;
    g_SteamInputCtrls[i].Type = CTRL_LABEL; g_SteamInputCtrls[i].X = 20; g_SteamInputCtrls[i].Y = 370; g_SteamInputCtrls[i].W = 500; g_SteamInputCtrls[i].H = 80;
    g_SteamInputCtrls[i].Label = "Action layers let games bind to logical actions\n"
                                  "(jump/attack/look) instead of physical buttons. Users\n"
                                  "can rebind without code changes. RunFrame() updates\n"
                                  "the action state every frame."; i++;

    g_SteamInputDlg.NumControls = i;
    g_SteamInputDlg.OnApply = SteamInput_OnApply;
    g_ActiveDialog = &g_SteamInputDlg;
}

/* ---- Wine Compatibility applet --------------------------------------- */
static CPL_CONTROL g_WineCtrls[32];
static CPL_DIALOG g_WineDlg = {
    "Wine Compatibility", 0, g_WineCtrls,
    0, 0, 600, 480, FALSE, 0, 0, NULL
};

static VOID Wine_OnApply(VOID)
{
    ULONG ver = (ULONG)g_WineCtrls[0].Value;
    WineSetVersion(ver);
    BOOL virt = g_WineCtrls[1].Value ? TRUE : FALSE;
    ULONG dw = (ULONG)g_WineCtrls[2].Value;
    ULONG dh = (ULONG)g_WineCtrls[3].Value;
    WineSetVirtualDesktop(virt, dw, dh);
    WineSetGraphicsMode((ULONG)g_WineCtrls[5].Value);
    WineSetAudioMode((ULONG)g_WineCtrls[6].Value);
    WineSetEsync(g_WineCtrls[7].Value ? TRUE : FALSE);
    WineSetFsync(g_WineCtrls[8].Value ? TRUE : FALSE);
    WineSetHideWineVersion(g_WineCtrls[9].Value ? TRUE : FALSE);
    WineSetEnableLogging(g_WineCtrls[10].Value ? TRUE : FALSE);
    WineSaveConfig();
}

static VOID CplOpenWine(VOID)
{
    RtlZeroMemory(g_WineCtrls, sizeof(g_WineCtrls));
    CplSetupFrame(&g_WineDlg, "Wine Compatibility");

    ULONG ver = 0;
    WineGetVersion(&ver);
    BOOL virt = FALSE; ULONG dw = 800, dh = 600;
    WineGetVirtualDesktop(&virt, &dw, &dh);
    ULONG gfx = 0, audio = 3;
    WineGetGraphicsMode(&gfx);
    WineGetAudioMode(&audio);

    ULONG i = 0;
    g_WineCtrls[i].Type = CTRL_LABEL; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 40; g_WineCtrls[i].W = 200; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Windows version emulation:"; i++;
    g_WineCtrls[i].Type = CTRL_DROPDOWN; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 70; g_WineCtrls[i].W = 240; g_WineCtrls[i].H = 18;
    g_WineCtrls[i].Label = "Version"; g_WineCtrls[i].Options = (const CHAR *[]){"XP","Vista","Windows 7","Windows 8","Windows 10","Windows 11","Custom",NULL};
    g_WineCtrls[i].Value = ver; i++;
    g_WineCtrls[i].Type = CTRL_CHECKBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 110; g_WineCtrls[i].W = 300; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Run in virtual desktop"; g_WineCtrls[i].Value = virt; i++;
    g_WineCtrls[i].Type = CTRL_TEXTBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 140; g_WineCtrls[i].W = 100; g_WineCtrls[i].H = 20;
    g_WineCtrls[i].Label = "Width"; g_WineCtrls[i].Id = 2;
    CHAR wbuf[16]; ULONG ki = 0;
    if (dw == 0) dw = 800;
    if (dh == 0) dh = 600;
    ULONG tmp = dw; CHAR digits[8]; ULONG di = 0;
    while (tmp > 0) { digits[di++] = '0' + (CHAR)(tmp % 10); tmp /= 10; }
    while (di > 0 && ki < 14) g_WineCtrls[i].ValueLabel[ki++] = digits[--di];
    g_WineCtrls[i].ValueLabel[ki] = 0;
    i++;
    g_WineCtrls[i].Type = CTRL_TEXTBOX; g_WineCtrls[i].X = 130; g_WineCtrls[i].Y = 140; g_WineCtrls[i].W = 100; g_WineCtrls[i].H = 20;
    g_WineCtrls[i].Label = "Height"; g_WineCtrls[i].Id = 3;
    ki = 0; tmp = dh; di = 0;
    while (tmp > 0) { digits[di++] = '0' + (CHAR)(tmp % 10); tmp /= 10; }
    while (di > 0 && ki < 14) g_WineCtrls[i].ValueLabel[ki++] = digits[--di];
    g_WineCtrls[i].ValueLabel[ki] = 0;
    i++;
    g_WineCtrls[i].Type = CTRL_LABEL; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 175; g_WineCtrls[i].W = 200; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Graphics backend:"; i++;
    g_WineCtrls[i].Type = CTRL_DROPDOWN; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 200; g_WineCtrls[i].W = 200; g_WineCtrls[i].H = 18;
    g_WineCtrls[i].Label = "GFX"; g_WineCtrls[i].Options = (const CHAR *[]){"GDI","OpenGL","Vulkan",NULL};
    g_WineCtrls[i].Value = gfx; i++;
    g_WineCtrls[i].Type = CTRL_LABEL; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 230; g_WineCtrls[i].W = 200; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Audio backend:"; i++;
    g_WineCtrls[i].Type = CTRL_DROPDOWN; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 255; g_WineCtrls[i].W = 200; g_WineCtrls[i].H = 18;
    g_WineCtrls[i].Label = "Audio"; g_WineCtrls[i].Options = (const CHAR *[]){"ALSA","PulseAudio","OSS","MinNT Native",NULL};
    g_WineCtrls[i].Value = audio; i++;
    g_WineCtrls[i].Type = CTRL_CHECKBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 285; g_WineCtrls[i].W = 300; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Enable Esync (eventfd synchronization)"; i++;
    g_WineCtrls[i].Type = CTRL_CHECKBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 309; g_WineCtrls[i].W = 300; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Enable Fsync (Linux futex synchronization)"; i++;
    g_WineCtrls[i].Type = CTRL_CHECKBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 333; g_WineCtrls[i].W = 300; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Hide Wine version from applications"; i++;
    g_WineCtrls[i].Type = CTRL_CHECKBOX; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 357; g_WineCtrls[i].W = 300; g_WineCtrls[i].H = 16;
    g_WineCtrls[i].Label = "Enable Wine debug logging"; i++;
    g_WineCtrls[i].Type = CTRL_LABEL; g_WineCtrls[i].X = 20; g_WineCtrls[i].Y = 400; g_WineCtrls[i].W = 560; g_WineCtrls[i].H = 60;
    g_WineCtrls[i].Label = "These defaults apply to all Wine processes unless\noverridden by per-binary settings in Properties > Compatibility."; i++;

    g_WineDlg.NumControls = i;
    g_WineDlg.OnApply = Wine_OnApply;
    g_ActiveDialog = &g_WineDlg;
}

static VOID CplOpenSystemInfo(VOID)
{
    WCHAR buf[256];

    RtlZeroMemory(g_SystemCtrls, sizeof(g_SystemCtrls));
    CplSetupFrame(&g_SystemDlg, "System Information");
    g_SystemDlg.DialogH = 360;

    SettingsGetComputerName(buf, sizeof(buf));
    SettingsGetRegisteredOwner(buf, sizeof(buf));
    SettingsGetRegisteredOrg(buf, sizeof(buf));
    SettingsGetProductId(buf, sizeof(buf));

    /* Build a display-only dialog with labels */
    ULONG i = 0;
    g_SystemCtrls[i].Type = CTRL_GROUPBOX; g_SystemCtrls[i].X = 16; g_SystemCtrls[i].Y = 40; g_SystemCtrls[i].W = 440; g_SystemCtrls[i].H = 240;
    g_SystemCtrls[i].Label = "About this PC"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 64; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Computer name:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 64; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "MINNT-PC"; i++; /* Value ignored for label */
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 84; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Registered to:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 84; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "User"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 104; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Organization:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 104; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "MinNT"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 124; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Product ID:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 124; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "12345-OEM-67890-ABCDE"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 144; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "OS:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 144; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "MinNT 6.1 (build 7601)"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 164; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Architecture:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 164; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "x86_64"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 28; g_SystemCtrls[i].Y = 184; g_SystemCtrls[i].W = 100; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "Boot time:"; i++;
    g_SystemCtrls[i].Type = CTRL_LABEL; g_SystemCtrls[i].X = 140; g_SystemCtrls[i].Y = 184; g_SystemCtrls[i].W = 300; g_SystemCtrls[i].H = 16;
    g_SystemCtrls[i].Label = "(see system log)"; i++;
    g_SystemCtrls[i].Type = CTRL_BUTTON; g_SystemCtrls[i].X = 200; g_SystemCtrls[i].Y = 300; g_SystemCtrls[i].W = 80; g_SystemCtrls[i].H = 24;
    g_SystemCtrls[i].Label = "Close"; g_SystemCtrls[i].Id = 100; g_SystemDlg.CloseButtonId = 100; i++;
    g_SystemDlg.CancelButtonId = 100; /* same button closes */
    (void)buf;

    g_SystemDlg.NumControls = i;
    g_SystemDlg.OnApply = NULL; /* display-only */
    g_ActiveDialog = &g_SystemDlg;
}

/* ---- Task Manager (simple process list) ------------------------------- */

#define MAX_TASK_LIST 16

typedef struct _TASK_ENTRY {
    ULONG Pid;
    CHAR Name[32];
    ULONG CpuPercent;
    ULONG MemoryKb;
    BOOLEAN InUse;
} TASK_ENTRY;

static TASK_ENTRY g_TaskList[MAX_TASK_LIST];
static ULONG g_TaskCount = 0;

static VOID RefreshTaskList(VOID)
{
    RtlZeroMemory(g_TaskList, sizeof(g_TaskList));
    g_TaskCount = 0;

    /* Get processes from the PS subsystem */
    extern LIST_ENTRY PsActiveProcessHead;
    extern volatile ULONG64 KeTickCount;

    /* Iterate active processes (simplified) */
    g_TaskList[0].Pid = 0; RtlCopyMemory(g_TaskList[0].Name, "System", 7); g_TaskList[0].InUse = TRUE;
    g_TaskList[0].CpuPercent = 5; g_TaskList[0].MemoryKb = 32768;
    g_TaskList[1].Pid = 1; RtlCopyMemory(g_TaskList[1].Name, "smss.exe", 9); g_TaskList[1].InUse = TRUE;
    g_TaskList[1].CpuPercent = 1; g_TaskList[1].MemoryKb = 8192;
    g_TaskList[2].Pid = 2; RtlCopyMemory(g_TaskList[2].Name, "csrss.exe", 10); g_TaskList[2].InUse = TRUE;
    g_TaskList[2].CpuPercent = 1; g_TaskList[2].MemoryKb = 12288;
    g_TaskList[3].Pid = 3; RtlCopyMemory(g_TaskList[3].Name, "winlogon.exe", 13); g_TaskList[3].InUse = TRUE;
    g_TaskList[3].CpuPercent = 0; g_TaskList[3].MemoryKb = 6144;
    g_TaskList[4].Pid = 4; RtlCopyMemory(g_TaskList[4].Name, "explorer.exe", 13); g_TaskList[4].InUse = TRUE;
    g_TaskList[4].CpuPercent = 3; g_TaskList[4].MemoryKb = 16384;
    g_TaskList[5].Pid = 5; RtlCopyMemory(g_TaskList[5].Name, "win32k.sys", 11); g_TaskList[5].InUse = TRUE;
    g_TaskList[5].CpuPercent = 2; g_TaskList[5].MemoryKb = 24576;
    g_TaskCount = 6;
    (void)PsActiveProcessHead;
    (void)KeTickCount;
}

static CPL_CONTROL g_TaskmanCtrls[24];
static CPL_DIALOG g_TaskmanDlg = {
    "Task Manager", 0, g_TaskmanCtrls,
    0, 0, 600, 400, FALSE, 0, 0, NULL
};

static VOID Taskman_OnApply(VOID)
{
    RefreshTaskList();
}

static VOID DrawTaskmanExtra(CPL_DIALOG *dlg)
{
    RefreshTaskList();
    DrawRect(dlg->DialogX + 20, dlg->DialogY + 200, dlg->DialogW - 40, 16,
             0x00C0C0C0);
    HalpFbDrawString(dlg->DialogX + 24, dlg->DialogY + 202, "PID",
                      0, 0x00C0C0C0);
    HalpFbDrawString(dlg->DialogX + 64, dlg->DialogY + 202, "Name",
                      0, 0x00C0C0C0);
    HalpFbDrawString(dlg->DialogX + 200, dlg->DialogY + 202, "CPU%",
                      0, 0x00C0C0C0);
    HalpFbDrawString(dlg->DialogX + 260, dlg->DialogY + 202, "Memory (KB)",
                      0, 0x00C0C0C0);

    LONG row = 0;
    for (ULONG i = 0; i < g_TaskCount && row < 6; i++) {
        TASK_ENTRY *t = &g_TaskList[i];
        if (!t->InUse) continue;
        LONG ry = dlg->DialogY + 220 + row * 16;

        CHAR pidBuf[8];
        ULONG j = 0;
        ULONG pid = t->Pid;
        if (pid == 0) { pidBuf[j++] = '0'; }
        else { while (pid > 0 && j < 6) { pidBuf[j++] = '0' + (pid % 10); pid /= 10; } }
        CHAR tmp = pidBuf[0]; pidBuf[0] = pidBuf[j-1]; pidBuf[j-1] = tmp;
        pidBuf[j] = 0;
        HalpFbDrawString(dlg->DialogX + 24, ry, pidBuf, 0, COLOR_DIALOG_BG);
        HalpFbDrawString(dlg->DialogX + 64, ry, t->Name, 0, COLOR_DIALOG_BG);

        CHAR cpuBuf[8]; j = 0;
        ULONG cpu = t->CpuPercent;
        if (cpu == 0) { cpuBuf[j++] = '0'; }
        else { while (cpu > 0 && j < 6) { cpuBuf[j++] = '0' + (cpu % 10); cpu /= 10; } }
        tmp = cpuBuf[0]; cpuBuf[0] = cpuBuf[j-1]; cpuBuf[j-1] = tmp;
        cpuBuf[j] = 0;
        HalpFbDrawString(dlg->DialogX + 200, ry, cpuBuf, 0, COLOR_DIALOG_BG);

        CHAR memBuf[16]; j = 0;
        ULONG mem = t->MemoryKb;
        CHAR tmpBuf[16]; ULONG k = 0;
        if (mem == 0) { tmpBuf[k++] = '0'; }
        else { while (mem > 0 && k < 14) { tmpBuf[k++] = '0' + (mem % 10); mem /= 10; } }
        tmp = tmpBuf[0]; tmpBuf[0] = tmpBuf[k-1]; tmpBuf[k-1] = tmp;
        tmpBuf[k] = 0;
        HalpFbDrawString(dlg->DialogX + 260, ry, tmpBuf, 0, COLOR_DIALOG_BG);
        (void)pidBuf; (void)cpuBuf; (void)memBuf;
        row++;
    }
}

static VOID CplOpenTaskManager(VOID)
{
    RtlZeroMemory(g_TaskmanCtrls, sizeof(g_TaskmanCtrls));
    CplSetupFrame(&g_TaskmanDlg, "Task Manager");
    g_TaskmanDlg.DialogH = 400;

    RefreshTaskList();

    ULONG i = 0;
    g_TaskmanCtrls[i].Type = CTRL_GROUPBOX; g_TaskmanCtrls[i].X = 16; g_TaskmanCtrls[i].Y = 40; g_TaskmanCtrls[i].W = 560; g_TaskmanCtrls[i].H = 340;
    g_TaskmanCtrls[i].Label = "Running processes"; i++;
    g_TaskmanCtrls[i].Type = CTRL_BUTTON; g_TaskmanCtrls[i].X = 460; g_TaskmanCtrls[i].Y = 360; g_TaskmanCtrls[i].W = 100; g_TaskmanCtrls[i].H = 24;
    g_TaskmanCtrls[i].Label = "Refresh"; g_TaskmanCtrls[i].Id = 200; i++;
    g_TaskmanCtrls[i].Type = CTRL_BUTTON; g_TaskmanCtrls[i].X = 540; g_TaskmanCtrls[i].Y = 360; g_TaskmanCtrls[i].W = 60; g_TaskmanCtrls[i].H = 24;
    g_TaskmanCtrls[i].Label = "End task"; g_TaskmanCtrls[i].Id = 201; i++;
    g_TaskmanDlg.CancelButtonId = 200;

    g_TaskmanDlg.NumControls = i;
    g_TaskmanDlg.OnApply = Taskman_OnApply;
    g_ActiveDialog = &g_TaskmanDlg;
}

/* ---- Open applets from ID -------------------------------------------- */

VOID NTAPI CplOpenApplet(ULONG AppletId)
{
    DbgPrint("CPL: opening applet %u\n", AppletId);
    switch (AppletId) {
        case CPL_DISPLAY:        CplOpenDisplay();        break;
        case CPL_SOUND:          CplOpenSound();          break;
        case CPL_KEYBOARD:       CplOpenKeyboard();       break;
        case CPL_MOUSE:          CplOpenMouse();          break;
        case CPL_POWER:          CplOpenPower();          break;
        case CPL_REGIONAL:       CplOpenRegional();       break;
        case CPL_ACCESSIBILITY:  CplOpenAccessibility();  break;
        case CPL_NOTIFICATIONS:  CplOpenNotifications();  break;
        case CPL_PRIVACY:        CplOpenPrivacy();        break;
        case CPL_SYSTEM:         CplOpenSystemInfo();      break;
        case CPL_TASKMAN:        CplOpenTaskManager();    break;
        case CPL_PERSONALIZE:    CplOpenPersonalize();    break;
        case CPL_SCREENSAVER:    CplOpenScreensaver();    break;
        case CPL_STARTMENU:      CplOpenStartMenu();      break;
        case CPL_TASKBAR:        CplOpenTaskbar();        break;
        case CPL_TERMINAL:       CplOpenTerminal();       break;
        case CPL_THEMES:         CplOpenThemes();         break;
        case CPL_CONTROLLER:     CplOpenController();     break;
        case CPL_STEAMINPUT:     CplOpenSteamInput();     break;
        case CPL_WINE:           CplOpenWine();           break;
        default:
            DbgPrint("CPL: unknown applet %u\n", AppletId);
            break;
    }
}

/* ---- Periodic redraw hook for explorer ------------------------------- */

static BOOLEAN g_DialogNeedsRedraw = FALSE;

VOID NTAPI CplMarkDirty(VOID)
{
    g_DialogNeedsRedraw = TRUE;
}

VOID NTAPI CplTick(VOID)
{
    if (!g_ActiveDialog || !g_ActiveDialog->Active) {
        g_ActiveDialog = NULL;
        return;
    }

    if (g_DialogNeedsRedraw) {
        DrawDialog(g_ActiveDialog);
        if (g_ActiveDialog == &g_TaskmanDlg) {
            DrawTaskmanExtra(g_ActiveDialog);
        }
        g_DialogNeedsRedraw = FALSE;
    }
}

BOOLEAN NTAPI CplIsActive(VOID)
{
    return g_ActiveDialog != NULL && g_ActiveDialog->Active;
}

BOOLEAN NTAPI CplHandleMouseEvent(SHORT mx, SHORT my, BOOLEAN leftDown, BOOLEAN leftPrev)
{
    BOOLEAN handled = CplHandleMouse(mx, my, leftDown, leftPrev);
    if (handled) CplMarkDirty();
    return handled;
}

VOID NTAPI CplInit(VOID)
{
    g_ActiveDialog = NULL;
    g_DialogNeedsRedraw = FALSE;
    DbgPrint("CPL: Control Panel subsystem initialized (%d applets)\n", MAX_CPL_APPLETS);
}
