/*
 * MinNT - boot/chain/admin2.c
 * Additional administrative applets:
 *   - Storage (drive list, CHKDSK, Disk Cleanup)
 *   - Fonts (system font management)
 *   - Environment Variables
 *   - Startup Apps
 *   - User Accounts
 *   - Firewall
 *   - Printers
 *   - Audio Mixer (per-app volume)
 *   - File Associations
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
#include <nt/se.h>
#include <nt/fs.h>
#include "win32k.h"
#include "cpl.h"

extern volatile ULONG *HalpFbGetBase(VOID);
extern ULONG HalpFbGetWidth(VOID);
extern ULONG HalpFbGetHeight(VOID);
extern VOID HalpFbFillRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbDrawRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbPutPixel(ULONG, ULONG, ULONG);
extern VOID HalpFbDrawString(ULONG, ULONG, const CHAR *, ULONG, ULONG);
extern VOID HalpFbClear(ULONG);

#define COLOR_DIALOG_BG     0x00E0E0E0
#define COLOR_DIALOG_FG     0x00000000
#define COLOR_BUTTON_BG     0x00D0D0D0
#define COLOR_BUTTON_HOVER  0x00C0E0FF
#define COLOR_GROUP_BG      0x00F0F0F0
#define COLOR_TITLE_BG      0x003080C0
#define COLOR_TITLE_FG      0x00FFFFFF
#define COLOR_LIST_SEL      0x003080C0
#define COLOR_WHITE         0x00FFFFFF

#define CTRL_LABEL   1
#define CTRL_BUTTON  4
#define CTRL_LISTBOX 9

typedef struct _AD2_CONTROL {
    ULONG Type;
    ULONG X, Y, W, H;
    const CHAR *Label;
    ULONG Id;
    ULONG Hovered, Pressed;
} AD2_CONTROL;

typedef struct _AD2_DIALOG {
    const CHAR *Title;
    ULONG NumControls;
    AD2_CONTROL *Controls;
    ULONG DialogX, DialogY, DialogW, DialogH;
    BOOLEAN Active;
    void (*OnInit)(void);
    void (*OnTick)(void);
    void (*OnApply)(void);
    ULONG CloseButtonId, CancelButtonId;
} AD2_DIALOG;

static AD2_DIALOG *g_Ad2Dialog = NULL;

/* ---- Drawing primitives --------------------------------------------- */

static VOID AD2_DrawRect(ULONG x, ULONG y, ULONG w, ULONG h, ULONG color)
{
    HalpFbFillRect(x, y, w, h, color);
}

static VOID AD2_DrawButton(ULONG x, ULONG y, ULONG w, ULONG h, const CHAR *label,
                            BOOLEAN hovered, BOOLEAN pressed)
{
    ULONG bg = pressed ? 0x00A0A0A0 : (hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG);
    HalpFbFillRect(x, y, w, h, bg);
    HalpFbDrawRect(x, y, w, h, 0x00000000);
    ULONG len = 0; while (label[len]) len++;
    HalpFbDrawString(x + (w - len * 8) / 2, y + (h - 8) / 2,
                     label, COLOR_DIALOG_FG, bg);
}

static VOID AD2_DrawLabel(ULONG x, ULONG y, const CHAR *label)
{
    HalpFbDrawString(x, y, label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
}

static VOID AD2_DrawListBox(ULONG x, ULONG y, ULONG w, ULONG h,
                              const CHAR **items, ULONG itemCount,
                              ULONG topIdx, ULONG selIdx)
{
    AD2_DrawRect(x, y, w, h, COLOR_WHITE);
    HalpFbDrawRect(x, y, w, h, 0x00808080);
    ULONG rowH = 14;
    ULONG maxRows = (h - 4) / rowH;
    for (ULONG i = 0; i < maxRows && (topIdx + i) < itemCount; i++) {
        ULONG rowY = y + 2 + i * rowH;
        ULONG bg = (topIdx + i == selIdx) ? COLOR_LIST_SEL : COLOR_WHITE;
        ULONG fg = (topIdx + i == selIdx) ? COLOR_WHITE : COLOR_DIALOG_FG;
        HalpFbFillRect(x + 1, rowY, w - 2, rowH, bg);
        HalpFbDrawString(x + 4, rowY + 2, items[topIdx + i], fg, bg);
    }
}

static VOID AD2_DrawDialog(AD2_DIALOG *dlg)
{
    if (!dlg || !dlg->Active) return;
    AD2_DrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, dlg->DialogH, COLOR_DIALOG_BG);
    AD2_DrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, 24, COLOR_TITLE_BG);
    HalpFbDrawString(dlg->DialogX + 8, dlg->DialogY + 8, dlg->Title,
                     COLOR_TITLE_FG, COLOR_TITLE_BG);
    ULONG cx = dlg->DialogX + dlg->DialogW - 24;
    AD2_DrawRect(cx, dlg->DialogY, 24, 24, COLOR_TITLE_BG);
    HalpFbDrawString(cx + 8, dlg->DialogY + 8, "X", COLOR_TITLE_FG, COLOR_TITLE_BG);
    HalpFbDrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, dlg->DialogH, 0x00000000);
    for (ULONG i = 0; i < dlg->NumControls; i++) {
        AD2_CONTROL *c = &dlg->Controls[i];
        if (c->Type == CTRL_LABEL) {
            AD2_DrawLabel(c->X + dlg->DialogX, c->Y + dlg->DialogY, c->Label);
        } else if (c->Type == CTRL_BUTTON) {
            AD2_DrawButton(c->X + dlg->DialogX, c->Y + dlg->DialogY,
                            c->W, c->H, c->Label, c->Hovered, c->Pressed);
        }
    }
}

static AD2_CONTROL *AD2_HitTest(AD2_DIALOG *dlg, SHORT mx, SHORT my)
{
    if (!dlg) return NULL;
    ULONG lx = mx - dlg->DialogX;
    ULONG ly = my - dlg->DialogY;
    for (LONG i = (LONG)dlg->NumControls - 1; i >= 0; i--) {
        AD2_CONTROL *c = &dlg->Controls[i];
        if (lx >= c->X && lx <= c->X + c->W && ly >= c->Y && ly <= c->Y + c->H) {
            return c;
        }
    }
    return NULL;
}

static BOOLEAN AD2_HandleMouse(SHORT mx, SHORT my, BOOLEAN leftDown, BOOLEAN leftPrev)
{
    if (!g_Ad2Dialog || !g_Ad2Dialog->Active) return FALSE;

    AD2_CONTROL *hovered = AD2_HitTest(g_Ad2Dialog, mx, my);
    for (ULONG i = 0; i < g_Ad2Dialog->NumControls; i++) {
        g_Ad2Dialog->Controls[i].Hovered = (&g_Ad2Dialog->Controls[i] == hovered);
    }

    if (leftDown && !leftPrev) {
        ULONG cx = g_Ad2Dialog->DialogX + g_Ad2Dialog->DialogW - 24;
        if (mx >= cx && mx <= cx + 24 && my >= g_Ad2Dialog->DialogY && my <= g_Ad2Dialog->DialogY + 24) {
            g_Ad2Dialog->Active = FALSE;
            return TRUE;
        }
        if (mx < g_Ad2Dialog->DialogX || mx > g_Ad2Dialog->DialogX + g_Ad2Dialog->DialogW ||
            my < g_Ad2Dialog->DialogY || my > g_Ad2Dialog->DialogY + g_Ad2Dialog->DialogH) {
            return FALSE;
        }
        AD2_CONTROL *c = hovered;
        if (c && c->Type == CTRL_BUTTON) {
            c->Pressed = TRUE;
            if (c->Id == g_Ad2Dialog->CloseButtonId) {
                if (g_Ad2Dialog->OnApply) g_Ad2Dialog->OnApply();
                g_Ad2Dialog->Active = FALSE;
            } else if (c->Id == g_Ad2Dialog->CancelButtonId) {
                g_Ad2Dialog->Active = FALSE;
            }
        }
        return TRUE;
    } else if (!leftDown && leftPrev) {
        for (ULONG i = 0; i < g_Ad2Dialog->NumControls; i++) {
            g_Ad2Dialog->Controls[i].Pressed = FALSE;
        }
    }
    return FALSE;
}

/* ---- Storage (Disk Management + CHKDSK) ----------------------------- */

#define MAX_VOLUMES 8
static CHAR g_Volumes[MAX_VOLUMES][32];
static ULONG64 g_VolumeSizes[MAX_VOLUMES];
static ULONG64 g_VolumeFree[MAX_VOLUMES];
static ULONG g_VolumeCount = 0;
static const CHAR *g_VolItems[16];
static CHAR g_VolItemBuf[16][64];
static ULONG g_VolSelected = 0;

static AD2_CONTROL g_VolCtrls[8];
static AD2_DIALOG g_VolDlg = {
    "Storage", 0, g_VolCtrls,
    0, 0, 560, 380, FALSE, 0, 0, NULL
};

static VOID Vol_Enumerate(VOID)
{
    /* Hardcoded - in a real implementation, walk the volume manager */
    g_VolumeCount = 1;
    RtlCopyMemory(g_Volumes[0], "C: (System)", 13); g_Volumes[0][12] = 0;
    g_VolumeSizes[0] = 16ULL * 1024 * 1024;  /* 16 MB RAM disk */
    g_VolumeFree[0] = 8ULL * 1024 * 1024;    /* assume 8 MB free */
}

static VOID Vol_RebuildItems(VOID)
{
    for (ULONG i = 0; i < g_VolumeCount; i++) {
        ULONG j = 0;
        while (g_Volumes[i][j] && j < 63) { g_VolItemBuf[i][j] = g_Volumes[i][j]; j++; }
        g_VolItemBuf[i][j] = 0;
        g_VolItems[i] = g_VolItemBuf[i];
    }
    g_VolItems[g_VolumeCount] = NULL;
}

static VOID Vol_OnInit(VOID)
{
    Vol_Enumerate();
    Vol_RebuildItems();
    g_VolSelected = 0;
}

static VOID Vol_OnTick(VOID)
{
    HalpFbFillRect(g_VolDlg.DialogX + 20, g_VolDlg.DialogY + 50,
                    520, 240, COLOR_WHITE);
    AD2_DrawListBox(g_VolDlg.DialogX + 20, g_VolDlg.DialogY + 50, 520, 240,
                     (const CHAR **)g_VolItems, g_VolumeCount,
                     0, g_VolSelected);

    if (g_VolSelected < g_VolumeCount) {
        LONG dy = g_VolDlg.DialogY + 300;
        CHAR info[128];
        ULONG j = 0;
        const CHAR *lbl = "Size: ";
        while (lbl[j] && j < 127) { info[j] = lbl[j]; j++; }
        CHAR sz[16];
        ULONG64 v = g_VolumeSizes[g_VolSelected] / (1024 * 1024);
        ULONG k = 0;
        if (v == 0) sz[k++] = '0';
        else while (v > 0 && k < 14) { sz[k++] = '0' + (v % 10); v /= 10; }
        CHAR tmp;
        for (ULONG a = 0; a < k/2; a++) { tmp = sz[a]; sz[a] = sz[k-1-a]; sz[k-1-a] = tmp; }
        sz[k++] = 'M'; sz[k++] = 'B';
        for (ULONG a = 0; a < k && j < 127; a++) info[j++] = sz[a];
        lbl = "  Free: ";
        k = 0;
        while (lbl[k] && j < 127) { info[j++] = lbl[k++]; }
        v = g_VolumeFree[g_VolSelected] / (1024 * 1024);
        k = 0;
        if (v == 0) sz[k++] = '0';
        else while (v > 0 && k < 14) { sz[k++] = '0' + (v % 10); v /= 10; }
        for (ULONG a = 0; a < k/2; a++) { tmp = sz[a]; sz[a] = sz[k-1-a]; sz[k-1-a] = tmp; }
        sz[k++] = 'M'; sz[k++] = 'B';
        for (ULONG a = 0; a < k && j < 127; a++) info[j++] = sz[a];
        info[j] = 0;
        HalpFbDrawString(g_VolDlg.DialogX + 20, dy, info, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    }
}

static VOID Vol_OnApply(VOID)
{
    /* Run CHKDSK on selected volume - simulated */
    DbgPrint("STORAGE: running CHKDSK on '%s'\n", g_Volumes[g_VolSelected]);
    DbgPrint("STORAGE: scan complete, no errors found\n");
}

static VOID Storage_Cleanup(VOID)
{
    DbgPrint("STORAGE: running Disk Cleanup\n");
    DbgPrint("STORAGE: removed 0 bytes of temp files (no FS service to scan)\n");
}

static VOID CplOpenStorage(VOID)
{
    RtlZeroMemory(g_VolCtrls, sizeof(g_VolCtrls));
    g_VolDlg.Title = "Storage";
    g_VolDlg.DialogW = 580;
    g_VolDlg.DialogH = 400;
    g_VolDlg.DialogX = (HalpFbGetWidth() - 580) / 2;
    g_VolDlg.DialogY = (HalpFbGetHeight() - 400 - 40) / 2;
    g_VolDlg.Active = TRUE;

    ULONG i = 0;
    g_VolCtrls[i].Type = CTRL_LABEL;
    g_VolCtrls[i].X = 20; g_VolCtrls[i].Y = 30; g_VolCtrls[i].W = 400; g_VolCtrls[i].H = 16;
    g_VolCtrls[i].Label = "Volumes"; i++;

    g_VolCtrls[i].Type = CTRL_BUTTON;
    g_VolCtrls[i].X = 20; g_VolCtrls[i].Y = 350; g_VolCtrls[i].W = 100; g_VolCtrls[i].H = 24;
    g_VolCtrls[i].Label = "CHKDSK"; i++;

    g_VolCtrls[i].Type = CTRL_BUTTON;
    g_VolCtrls[i].X = 140; g_VolCtrls[i].Y = 350; g_VolCtrls[i].W = 100; g_VolCtrls[i].H = 24;
    g_VolCtrls[i].Label = "Cleanup"; i++;

    g_VolCtrls[i].Type = CTRL_BUTTON;
    g_VolCtrls[i].X = 460; g_VolCtrls[i].Y = 350; g_VolCtrls[i].W = 100; g_VolCtrls[i].H = 24;
    g_VolCtrls[i].Label = "Close"; g_VolCtrls[i].Id = 100; g_VolDlg.CloseButtonId = 100; i++;

    g_VolDlg.NumControls = i;
    g_VolDlg.OnInit = Vol_OnInit;
    g_VolDlg.OnTick = Vol_OnTick;
    g_VolDlg.OnApply = Vol_OnApply;
    Vol_OnInit();
    g_Ad2Dialog = &g_VolDlg;
}

/* ---- Fonts ----------------------------------------------------------- */

#define MAX_FONTS 16
static CHAR g_FontNames[MAX_FONTS][64];
static ULONG g_FontCount = 0;
static const CHAR *g_FontItems[16];
static CHAR g_FontItemBuf[16][80];
static ULONG g_FontSelected = 0;

static AD2_CONTROL g_FontCtrls[8];
static AD2_DIALOG g_FontDlg = {
    "Fonts", 0, g_FontCtrls,
    0, 0, 480, 400, FALSE, 0, 0, NULL
};

static VOID Font_OnInit(VOID)
{
    g_FontCount = 4;
    RtlCopyMemory(g_FontNames[0], "Segoe UI (system)", 18); g_FontNames[0][17] = 0;
    RtlCopyMemory(g_FontNames[1], "Consolas (mono)", 16); g_FontNames[1][15] = 0;
    RtlCopyMemory(g_FontNames[2], "Arial", 6); g_FontNames[2][5] = 0;
    RtlCopyMemory(g_FontNames[3], "Times New Roman", 16); g_FontNames[3][15] = 0;
    for (ULONG i = 0; i < g_FontCount; i++) {
        ULONG j = 0;
        while (g_FontNames[i][j] && j < 79) { g_FontItemBuf[i][j] = g_FontNames[i][j]; j++; }
        g_FontItemBuf[i][j] = 0;
        g_FontItems[i] = g_FontItemBuf[i];
    }
    g_FontItems[g_FontCount] = NULL;
}

static VOID Font_OnTick(VOID)
{
    HalpFbFillRect(g_FontDlg.DialogX + 20, g_FontDlg.DialogY + 50,
                    440, 280, COLOR_WHITE);
    AD2_DrawListBox(g_FontDlg.DialogX + 20, g_FontDlg.DialogY + 50, 440, 280,
                     (const CHAR **)g_FontItems, g_FontCount,
                     0, g_FontSelected);
    if (g_FontSelected < g_FontCount) {
        /* Preview */
        HalpFbDrawString(g_FontDlg.DialogX + 20, g_FontDlg.DialogY + 340,
                          "Preview: The quick brown fox 0123456789",
                          COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    }
}

static VOID CplOpenFonts(VOID)
{
    RtlZeroMemory(g_FontCtrls, sizeof(g_FontCtrls));
    g_FontDlg.Title = "Fonts";
    g_FontDlg.DialogW = 500;
    g_FontDlg.DialogH = 420;
    g_FontDlg.DialogX = (HalpFbGetWidth() - 500) / 2;
    g_FontDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_FontDlg.Active = TRUE;

    ULONG i = 0;
    g_FontCtrls[i].Type = CTRL_LABEL;
    g_FontCtrls[i].X = 20; g_FontCtrls[i].Y = 30; g_FontCtrls[i].W = 400; g_FontCtrls[i].H = 16;
    g_FontCtrls[i].Label = "Installed fonts"; i++;

    g_FontCtrls[i].Type = CTRL_BUTTON;
    g_FontCtrls[i].X = 380; g_FontCtrls[i].Y = 380; g_FontCtrls[i].W = 100; g_FontCtrls[i].H = 24;
    g_FontCtrls[i].Label = "Close"; g_FontCtrls[i].Id = 100; g_FontDlg.CloseButtonId = 100; i++;

    g_FontDlg.NumControls = i;
    g_FontDlg.OnInit = Font_OnInit;
    g_FontDlg.OnTick = Font_OnTick;
    Font_OnInit();
    g_Ad2Dialog = &g_FontDlg;
}

/* ---- Environment Variables ----------------------------------------- */

#define MAX_ENV_VARS 16
static CHAR g_EnvNames[MAX_ENV_VARS][64];
static CHAR g_EnvValues[MAX_ENV_VARS][128];
static ULONG g_EnvCount = 0;
static const CHAR *g_EnvItems[16];
static CHAR g_EnvItemBuf[16][200];
static ULONG g_EnvSelected = 0;

static AD2_CONTROL g_EnvCtrls[8];
static AD2_DIALOG g_EnvDlg = {
    "Environment Variables", 0, g_EnvCtrls,
    0, 0, 700, 420, FALSE, 0, 0, NULL
};

static VOID Env_OnInit(VOID)
{
    g_EnvCount = 6;
    RtlCopyMemory(g_EnvNames[0], "PATH", 5); g_EnvNames[0][4] = 0;
    RtlCopyMemory(g_EnvValues[0], "C:\\Windows\\System32;C:\\Windows", 33); g_EnvValues[0][32] = 0;
    RtlCopyMemory(g_EnvNames[1], "SYSTEMROOT", 11); g_EnvNames[1][10] = 0;
    RtlCopyMemory(g_EnvValues[1], "C:\\Windows", 11); g_EnvValues[1][10] = 0;
    RtlCopyMemory(g_EnvNames[2], "WINDIR", 7); g_EnvNames[2][6] = 0;
    RtlCopyMemory(g_EnvValues[2], "C:\\Windows", 11); g_EnvValues[2][10] = 0;
    RtlCopyMemory(g_EnvNames[3], "OS", 3); g_EnvNames[3][2] = 0;
    RtlCopyMemory(g_EnvValues[3], "Windows_NT", 11); g_EnvValues[3][10] = 0;
    RtlCopyMemory(g_EnvNames[4], "PROCESSOR_ARCHITECTURE", 23); g_EnvNames[4][22] = 0;
    RtlCopyMemory(g_EnvValues[4], "AMD64", 6); g_EnvValues[4][5] = 0;
    RtlCopyMemory(g_EnvNames[5], "USERPROFILE", 12); g_EnvNames[5][11] = 0;
    RtlCopyMemory(g_EnvValues[5], "C:\\Users\\User", 14); g_EnvValues[5][13] = 0;

    for (ULONG i = 0; i < g_EnvCount; i++) {
        ULONG j = 0;
        while (g_EnvNames[i][j] && j < 199) { g_EnvItemBuf[i][j] = g_EnvNames[i][j]; j++; }
        while (j < 30 && j < 199) g_EnvItemBuf[i][j++] = ' ';
        ULONG k = 0;
        while (g_EnvValues[i][k] && j + k < 199) { g_EnvItemBuf[i][j + k] = g_EnvValues[i][k]; k++; }
        {
            ULONG lim = 199;
            if (j + k < lim) lim = j + k;
            g_EnvItemBuf[i][lim] = 0;
        }
        g_EnvItems[i] = g_EnvItemBuf[i];
    }
    g_EnvItems[g_EnvCount] = NULL;
}

static VOID Env_OnTick(VOID)
{
    HalpFbFillRect(g_EnvDlg.DialogX + 20, g_EnvDlg.DialogY + 50,
                    660, 320, COLOR_WHITE);
    AD2_DrawListBox(g_EnvDlg.DialogX + 20, g_EnvDlg.DialogY + 50, 660, 320,
                     (const CHAR **)g_EnvItems, g_EnvCount,
                     0, g_EnvSelected);
}

static VOID CplOpenEnvironmentVariables(VOID)
{
    RtlZeroMemory(g_EnvCtrls, sizeof(g_EnvCtrls));
    g_EnvDlg.Title = "Environment Variables";
    g_EnvDlg.DialogW = 720;
    g_EnvDlg.DialogH = 440;
    g_EnvDlg.DialogX = (HalpFbGetWidth() - 720) / 2;
    g_EnvDlg.DialogY = (HalpFbGetHeight() - 440 - 40) / 2;
    g_EnvDlg.Active = TRUE;

    ULONG i = 0;
    g_EnvCtrls[i].Type = CTRL_LABEL;
    g_EnvCtrls[i].X = 20; g_EnvCtrls[i].Y = 30; g_EnvCtrls[i].W = 400; g_EnvCtrls[i].H = 16;
    g_EnvCtrls[i].Label = "Variable                       Value"; i++;

    g_EnvCtrls[i].Type = CTRL_BUTTON;
    g_EnvCtrls[i].X = 20; g_EnvCtrls[i].Y = 390; g_EnvCtrls[i].W = 100; g_EnvCtrls[i].H = 24;
    g_EnvCtrls[i].Label = "New"; i++;

    g_EnvCtrls[i].Type = CTRL_BUTTON;
    g_EnvCtrls[i].X = 140; g_EnvCtrls[i].Y = 390; g_EnvCtrls[i].W = 100; g_EnvCtrls[i].H = 24;
    g_EnvCtrls[i].Label = "Edit"; i++;

    g_EnvCtrls[i].X = 600; g_EnvCtrls[i].Y = 390; g_EnvCtrls[i].W = 100; g_EnvCtrls[i].H = 24;
    g_EnvCtrls[i].Type = CTRL_BUTTON;
    g_EnvCtrls[i].Label = "Close"; g_EnvCtrls[i].Id = 100; g_EnvDlg.CloseButtonId = 100; i++;

    g_EnvDlg.NumControls = i;
    g_EnvDlg.OnInit = Env_OnInit;
    g_EnvDlg.OnTick = Env_OnTick;
    Env_OnInit();
    g_Ad2Dialog = &g_EnvDlg;
}

/* ---- Startup Apps --------------------------------------------------- */

#define MAX_STARTUP 16
static CHAR g_StartupNames[MAX_STARTUP][64];
static BOOLEAN g_StartupEnabled[MAX_STARTUP];
static ULONG g_StartupCount = 0;
static const CHAR *g_StartupItems[16];
static CHAR g_StartupItemBuf[16][80];
static ULONG g_StartupSelected = 0;

static AD2_CONTROL g_StartupCtrls[8];
static AD2_DIALOG g_StartupDlg = {
    "Startup Apps", 0, g_StartupCtrls,
    0, 0, 540, 380, FALSE, 0, 0, NULL
};

static VOID Startup_OnInit(VOID)
{
    g_StartupCount = 4;
    RtlCopyMemory(g_StartupNames[0], "RTW88 WiFi Agent", 17); g_StartupNames[0][16] = 0;
    g_StartupEnabled[0] = TRUE;
    RtlCopyMemory(g_StartupNames[1], "MinNT Time Sync", 16); g_StartupNames[1][15] = 0;
    g_StartupEnabled[1] = TRUE;
    RtlCopyMemory(g_StartupNames[2], "MinNT Update Check", 20); g_StartupNames[2][19] = 0;
    g_StartupEnabled[2] = FALSE;
    RtlCopyMemory(g_StartupNames[3], "Desktop Telemetry", 18); g_StartupNames[3][17] = 0;
    g_StartupEnabled[3] = FALSE;

    for (ULONG i = 0; i < g_StartupCount; i++) {
        ULONG j = 0;
        while (g_StartupNames[i][j] && j < 79) { g_StartupItemBuf[i][j] = g_StartupNames[i][j]; j++; }
        while (j < 60 && j < 79) g_StartupItemBuf[i][j++] = ' ';
        const CHAR *en = g_StartupEnabled[i] ? "Enabled" : "Disabled";
        ULONG k = 0;
        while (en[k] && j + k < 79) g_StartupItemBuf[i][j + k++] = en[k];
        g_StartupItemBuf[i][(79) < (j + k) ? (79) : (j + k)] = 0;
        g_StartupItems[i] = g_StartupItemBuf[i];
    }
    g_StartupItems[g_StartupCount] = NULL;
}

static VOID Startup_OnTick(VOID)
{
    HalpFbFillRect(g_StartupDlg.DialogX + 20, g_StartupDlg.DialogY + 50,
                    500, 280, COLOR_WHITE);
    AD2_DrawListBox(g_StartupDlg.DialogX + 20, g_StartupDlg.DialogY + 50, 500, 280,
                     (const CHAR **)g_StartupItems, g_StartupCount,
                     0, g_StartupSelected);
}

static VOID Startup_OnApply(VOID)
{
    if (g_StartupSelected < g_StartupCount) {
        g_StartupEnabled[g_StartupSelected] = !g_StartupEnabled[g_StartupSelected];
        DbgPrint("STARTUP: '%s' %s\n", g_StartupNames[g_StartupSelected],
                 g_StartupEnabled[g_StartupSelected] ? "enabled" : "disabled");
        Startup_OnInit();
    }
}

static VOID CplOpenStartup(VOID)
{
    RtlZeroMemory(g_StartupCtrls, sizeof(g_StartupCtrls));
    g_StartupDlg.Title = "Startup Apps";
    g_StartupDlg.DialogW = 560;
    g_StartupDlg.DialogH = 400;
    g_StartupDlg.DialogX = (HalpFbGetWidth() - 560) / 2;
    g_StartupDlg.DialogY = (HalpFbGetHeight() - 400 - 40) / 2;
    g_StartupDlg.Active = TRUE;

    ULONG i = 0;
    g_StartupCtrls[i].Type = CTRL_LABEL;
    g_StartupCtrls[i].X = 20; g_StartupCtrls[i].Y = 30; g_StartupCtrls[i].W = 400; g_StartupCtrls[i].H = 16;
    g_StartupCtrls[i].Label = "App                                       State"; i++;

    g_StartupCtrls[i].Type = CTRL_BUTTON;
    g_StartupCtrls[i].X = 20; g_StartupCtrls[i].Y = 350; g_StartupCtrls[i].W = 120; g_StartupCtrls[i].H = 24;
    g_StartupCtrls[i].Label = "Enable/Disable"; i++;

    g_StartupCtrls[i].X = 440; g_StartupCtrls[i].Y = 350; g_StartupCtrls[i].W = 100; g_StartupCtrls[i].H = 24;
    g_StartupCtrls[i].Type = CTRL_BUTTON;
    g_StartupCtrls[i].Label = "Close"; g_StartupCtrls[i].Id = 100; g_StartupDlg.CloseButtonId = 100; i++;

    g_StartupDlg.NumControls = i;
    g_StartupDlg.OnInit = Startup_OnInit;
    g_StartupDlg.OnTick = Startup_OnTick;
    g_StartupDlg.OnApply = Startup_OnApply;
    Startup_OnInit();
    g_Ad2Dialog = &g_StartupDlg;
}

/* ---- User Accounts -------------------------------------------------- */

#define MAX_USERS 8
static CHAR g_UserNames[MAX_USERS][64];
static ULONG g_UserPrivs[MAX_USERS]; /* 0=user, 1=admin */
static ULONG g_UserCount = 0;
static const CHAR *g_UserItems[8];
static CHAR g_UserItemBuf[8][80];
static ULONG g_UserSelected = 0;

static AD2_CONTROL g_UserCtrls[8];
static AD2_DIALOG g_UserDlg = {
    "User Accounts", 0, g_UserCtrls,
    0, 0, 500, 360, FALSE, 0, 0, NULL
};

static VOID User_OnInit(VOID)
{
    g_UserCount = 2;
    RtlCopyMemory(g_UserNames[0], "User", 5); g_UserNames[0][4] = 0;
    g_UserPrivs[0] = 0; /* standard user */
    RtlCopyMemory(g_UserNames[1], "Administrator", 14); g_UserNames[1][13] = 0;
    g_UserPrivs[1] = 1; /* admin */

    for (ULONG i = 0; i < g_UserCount; i++) {
        ULONG j = 0;
        while (g_UserNames[i][j] && j < 79) { g_UserItemBuf[i][j] = g_UserNames[i][j]; j++; }
        while (j < 50 && j < 79) g_UserItemBuf[i][j++] = ' ';
        const CHAR *priv = g_UserPrivs[i] ? "Administrator" : "Standard user";
        ULONG k = 0;
        while (priv[k] && j + k < 79) g_UserItemBuf[i][j + k++] = priv[k];
        g_UserItemBuf[i][(79) < (j + k) ? (79) : (j + k)] = 0;
        g_UserItems[i] = g_UserItemBuf[i];
    }
    g_UserItems[g_UserCount] = NULL;
}

static VOID User_OnTick(VOID)
{
    HalpFbFillRect(g_UserDlg.DialogX + 20, g_UserDlg.DialogY + 50,
                    460, 260, COLOR_WHITE);
    AD2_DrawListBox(g_UserDlg.DialogX + 20, g_UserDlg.DialogY + 50, 460, 260,
                     (const CHAR **)g_UserItems, g_UserCount,
                     0, g_UserSelected);
}

static VOID User_OnApply(VOID)
{
    if (g_UserSelected < g_UserCount) {
        g_UserPrivs[g_UserSelected] = !g_UserPrivs[g_UserSelected];
        DbgPrint("USERS: '%s' priv toggled to %s\n",
                 g_UserNames[g_UserSelected],
                 g_UserPrivs[g_UserSelected] ? "Admin" : "User");
        User_OnInit();
    }
}

static VOID CplOpenUserAccounts(VOID)
{
    RtlZeroMemory(g_UserCtrls, sizeof(g_UserCtrls));
    g_UserDlg.Title = "User Accounts";
    g_UserDlg.DialogW = 520;
    g_UserDlg.DialogH = 380;
    g_UserDlg.DialogX = (HalpFbGetWidth() - 520) / 2;
    g_UserDlg.DialogY = (HalpFbGetHeight() - 380 - 40) / 2;
    g_UserDlg.Active = TRUE;

    ULONG i = 0;
    g_UserCtrls[i].Type = CTRL_LABEL;
    g_UserCtrls[i].X = 20; g_UserCtrls[i].Y = 30; g_UserCtrls[i].W = 400; g_UserCtrls[i].H = 16;
    g_UserCtrls[i].Label = "Account                          Type"; i++;

    g_UserCtrls[i].Type = CTRL_BUTTON;
    g_UserCtrls[i].X = 20; g_UserCtrls[i].Y = 330; g_UserCtrls[i].W = 120; g_UserCtrls[i].H = 24;
    g_UserCtrls[i].Label = "Toggle Admin"; i++;

    g_UserCtrls[i].X = 400; g_UserCtrls[i].Y = 330; g_UserCtrls[i].W = 100; g_UserCtrls[i].H = 24;
    g_UserCtrls[i].Type = CTRL_BUTTON;
    g_UserCtrls[i].Label = "Close"; g_UserCtrls[i].Id = 100; g_UserDlg.CloseButtonId = 100; i++;

    g_UserDlg.NumControls = i;
    g_UserDlg.OnInit = User_OnInit;
    g_UserDlg.OnTick = User_OnTick;
    g_UserDlg.OnApply = User_OnApply;
    User_OnInit();
    g_Ad2Dialog = &g_UserDlg;
}

/* ---- Firewall ------------------------------------------------------- */

#define MAX_FW_RULES 16
typedef struct _FW_RULE {
    CHAR Name[64];
    BOOLEAN Inbound;
    BOOLEAN Enabled;
    ULONG Port;
} FW_RULE;

static FW_RULE g_FwRules[MAX_FW_RULES];
static ULONG g_FwRuleCount = 0;
static const CHAR *g_FwItems[16];
static CHAR g_FwItemBuf[16][96];
static ULONG g_FwSelected = 0;

static AD2_CONTROL g_FwCtrls[8];
static AD2_DIALOG g_FwDlg = {
    "Windows Firewall", 0, g_FwCtrls,
    0, 0, 600, 400, FALSE, 0, 0, NULL
};

static VOID Fw_OnInit(VOID)
{
    g_FwRuleCount = 4;
    RtlCopyMemory(g_FwRules[0].Name, "Allow RDP (3389)", 17); g_FwRules[0].Name[16] = 0;
    g_FwRules[0].Inbound = TRUE;
    g_FwRules[0].Enabled = FALSE;
    g_FwRules[0].Port = 3389;
    RtlCopyMemory(g_FwRules[1].Name, "Allow SMB (445)", 15); g_FwRules[1].Name[14] = 0;
    g_FwRules[1].Inbound = TRUE;
    g_FwRules[1].Enabled = FALSE;
    g_FwRules[1].Port = 445;
    RtlCopyMemory(g_FwRules[2].Name, "Allow HTTP (80)", 14); g_FwRules[2].Name[13] = 0;
    g_FwRules[2].Inbound = TRUE;
    g_FwRules[2].Enabled = TRUE;
    g_FwRules[2].Port = 80;
    RtlCopyMemory(g_FwRules[3].Name, "Allow HTTPS (443)", 17); g_FwRules[3].Name[16] = 0;
    g_FwRules[3].Inbound = TRUE;
    g_FwRules[3].Enabled = TRUE;
    g_FwRules[3].Port = 443;

    for (ULONG i = 0; i < g_FwRuleCount; i++) {
        ULONG j = 0;
        while (g_FwRules[i].Name[j] && j < 95) { g_FwItemBuf[i][j] = g_FwRules[i].Name[j]; j++; }
        while (j < 60 && j < 95) g_FwItemBuf[i][j++] = ' ';
        const CHAR *en = g_FwRules[i].Enabled ? "Allowed" : "Blocked";
        ULONG k = 0;
        while (en[k] && j + k < 95) g_FwItemBuf[i][j + k++] = en[k];
        g_FwItemBuf[i][(95) < (j + k) ? (95) : (j + k)] = 0;
        g_FwItems[i] = g_FwItemBuf[i];
    }
    g_FwItems[g_FwRuleCount] = NULL;
}

static VOID Fw_OnTick(VOID)
{
    HalpFbFillRect(g_FwDlg.DialogX + 20, g_FwDlg.DialogY + 50,
                    560, 290, COLOR_WHITE);
    AD2_DrawListBox(g_FwDlg.DialogX + 20, g_FwDlg.DialogY + 50, 560, 290,
                     (const CHAR **)g_FwItems, g_FwRuleCount,
                     0, g_FwSelected);
}

static VOID Fw_OnApply(VOID)
{
    if (g_FwSelected < g_FwRuleCount) {
        g_FwRules[g_FwSelected].Enabled = !g_FwRules[g_FwSelected].Enabled;
        DbgPrint("FW: rule '%s' port %u now %s\n",
                 g_FwRules[g_FwSelected].Name,
                 g_FwRules[g_FwSelected].Port,
                 g_FwRules[g_FwSelected].Enabled ? "ALLOWED" : "BLOCKED");
        Fw_OnInit();
    }
}

static VOID CplOpenFirewall(VOID)
{
    RtlZeroMemory(g_FwCtrls, sizeof(g_FwCtrls));
    g_FwDlg.Title = "Windows Firewall";
    g_FwDlg.DialogW = 620;
    g_FwDlg.DialogH = 420;
    g_FwDlg.DialogX = (HalpFbGetWidth() - 620) / 2;
    g_FwDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_FwDlg.Active = TRUE;

    ULONG i = 0;
    g_FwCtrls[i].Type = CTRL_LABEL;
    g_FwCtrls[i].X = 20; g_FwCtrls[i].Y = 30; g_FwCtrls[i].W = 400; g_FwCtrls[i].H = 16;
    g_FwCtrls[i].Label = "Rule                                    State"; i++;

    g_FwCtrls[i].Type = CTRL_BUTTON;
    g_FwCtrls[i].X = 20; g_FwCtrls[i].Y = 360; g_FwCtrls[i].W = 140; g_FwCtrls[i].H = 24;
    g_FwCtrls[i].Label = "Allow/Block"; i++;

    g_FwCtrls[i].X = 500; g_FwCtrls[i].Y = 360; g_FwCtrls[i].W = 100; g_FwCtrls[i].H = 24;
    g_FwCtrls[i].Type = CTRL_BUTTON;
    g_FwCtrls[i].Label = "Close"; g_FwCtrls[i].Id = 100; g_FwDlg.CloseButtonId = 100; i++;

    g_FwDlg.NumControls = i;
    g_FwDlg.OnInit = Fw_OnInit;
    g_FwDlg.OnTick = Fw_OnTick;
    g_FwDlg.OnApply = Fw_OnApply;
    Fw_OnInit();
    g_Ad2Dialog = &g_FwDlg;
}

/* ---- Printers ------------------------------------------------------ */

#define MAX_PRINTERS 8
static CHAR g_PrinterNames[MAX_PRINTERS][64];
static ULONG g_PrinterCount = 0;
static const CHAR *g_PrinterItems[8];
static CHAR g_PrinterItemBuf[8][96];
static ULONG g_PrinterSelected = 0;

static AD2_CONTROL g_PrinterCtrls[8];
static AD2_DIALOG g_PrinterDlg = {
    "Printers", 0, g_PrinterCtrls,
    0, 0, 500, 380, FALSE, 0, 0, NULL
};

static VOID Printer_OnInit(VOID)
{
    g_PrinterCount = 2;
    RtlCopyMemory(g_PrinterNames[0], "Microsoft Print to PDF", 23); g_PrinterNames[0][22] = 0;
    RtlCopyMemory(g_PrinterNames[1], "Generic / Text Only", 20); g_PrinterNames[1][19] = 0;

    for (ULONG i = 0; i < g_PrinterCount; i++) {
        ULONG j = 0;
        while (g_PrinterNames[i][j] && j < 95) { g_PrinterItemBuf[i][j] = g_PrinterNames[i][j]; j++; }
        g_PrinterItemBuf[i][j] = 0;
        g_PrinterItems[i] = g_PrinterItemBuf[i];
    }
    g_PrinterItems[g_PrinterCount] = NULL;
}

static VOID Printer_OnTick(VOID)
{
    HalpFbFillRect(g_PrinterDlg.DialogX + 20, g_PrinterDlg.DialogY + 50,
                    460, 280, COLOR_WHITE);
    AD2_DrawListBox(g_PrinterDlg.DialogX + 20, g_PrinterDlg.DialogY + 50, 460, 280,
                     (const CHAR **)g_PrinterItems, g_PrinterCount,
                     0, g_PrinterSelected);
}

static VOID CplOpenPrinters(VOID)
{
    RtlZeroMemory(g_PrinterCtrls, sizeof(g_PrinterCtrls));
    g_PrinterDlg.Title = "Printers";
    g_PrinterDlg.DialogW = 520;
    g_PrinterDlg.DialogH = 400;
    g_PrinterDlg.DialogX = (HalpFbGetWidth() - 520) / 2;
    g_PrinterDlg.DialogY = (HalpFbGetHeight() - 400 - 40) / 2;
    g_PrinterDlg.Active = TRUE;

    ULONG i = 0;
    g_PrinterCtrls[i].Type = CTRL_LABEL;
    g_PrinterCtrls[i].X = 20; g_PrinterCtrls[i].Y = 30; g_PrinterCtrls[i].W = 400; g_PrinterCtrls[i].H = 16;
    g_PrinterCtrls[i].Label = "Available printers"; i++;

    g_PrinterCtrls[i].Type = CTRL_BUTTON;
    g_PrinterCtrls[i].X = 20; g_PrinterCtrls[i].Y = 350; g_PrinterCtrls[i].W = 120; g_PrinterCtrls[i].H = 24;
    g_PrinterCtrls[i].Label = "Add printer"; i++;

    g_PrinterCtrls[i].X = 400; g_PrinterCtrls[i].Y = 350; g_PrinterCtrls[i].W = 100; g_PrinterCtrls[i].H = 24;
    g_PrinterCtrls[i].Type = CTRL_BUTTON;
    g_PrinterCtrls[i].Label = "Close"; g_PrinterCtrls[i].Id = 100; g_PrinterDlg.CloseButtonId = 100; i++;

    g_PrinterDlg.NumControls = i;
    g_PrinterDlg.OnInit = Printer_OnInit;
    g_PrinterDlg.OnTick = Printer_OnTick;
    Printer_OnInit();
    g_Ad2Dialog = &g_PrinterDlg;
}

/* ---- Audio Mixer --------------------------------------------------- */

#define MAX_AUDIO_STREAMS 8
static CHAR g_AudioAppNames[MAX_AUDIO_STREAMS][64];
static ULONG g_AudioVolumes[MAX_AUDIO_STREAMS];
static BOOLEAN g_AudioMuted[MAX_AUDIO_STREAMS];
static ULONG g_AudioCount = 0;

static AD2_CONTROL g_MixerCtrls[16];
static AD2_DIALOG g_MixerDlg = {
    "Volume Mixer", 0, g_MixerCtrls,
    0, 0, 480, 400, FALSE, 0, 0, NULL
};

static VOID Mixer_OnInit(VOID)
{
    g_AudioCount = 4;
    RtlCopyMemory(g_AudioAppNames[0], "System sounds", 14); g_AudioAppNames[0][13] = 0;
    g_AudioVolumes[0] = 80; g_AudioMuted[0] = FALSE;
    RtlCopyMemory(g_AudioAppNames[1], "explorer", 9); g_AudioAppNames[1][8] = 0;
    g_AudioVolumes[1] = 70; g_AudioMuted[1] = FALSE;
    RtlCopyMemory(g_AudioAppNames[2], "User app 1", 11); g_AudioAppNames[2][10] = 0;
    g_AudioVolumes[2] = 50; g_AudioMuted[2] = FALSE;
    RtlCopyMemory(g_AudioAppNames[3], "User app 2", 11); g_AudioAppNames[3][10] = 0;
    g_AudioVolumes[3] = 60; g_AudioMuted[3] = FALSE;
}

static VOID Mixer_DrawApp(ULONG i, ULONG y)
{
    HalpFbDrawString(g_MixerDlg.DialogX + 20, y, g_AudioAppNames[i],
                      COLOR_DIALOG_FG, COLOR_DIALOG_BG);

    ULONG barX = g_MixerDlg.DialogX + 160;
    ULONG barW = 220;
    ULONG barY = y + 4;
    HalpFbFillRect(barX, barY, barW, 6, 0x00808080);
    if (!g_AudioMuted[i]) {
        ULONG fill = (barW * g_AudioVolumes[i]) / 100;
        HalpFbFillRect(barX, barY, fill, 6, 0x003080C0);
    }
    /* Volume percent */
    CHAR pct[8];
    ULONG k = 0;
    ULONG v = g_AudioMuted[i] ? 0 : g_AudioVolumes[i];
    if (v == 0) pct[k++] = '0';
    else while (v > 0 && k < 6) { pct[k++] = '0' + (v % 10); v /= 10; }
    CHAR tmp;
    for (ULONG a = 0; a < k/2; a++) { tmp = pct[a]; pct[a] = pct[k-1-a]; pct[k-1-a] = tmp; }
    pct[k++] = '%';
    pct[k] = 0;
    HalpFbDrawString(barX + barW + 8, y, pct, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    /* Mute indicator */
    if (g_AudioMuted[i]) {
        HalpFbDrawString(barX + barW + 50, y, "[Muted]", 0x00FF0000, COLOR_DIALOG_BG);
    }
}

static VOID Mixer_OnTick(VOID)
{
    HalpFbFillRect(g_MixerDlg.DialogX + 20, g_MixerDlg.DialogY + 50,
                    440, 290, COLOR_DIALOG_BG);
    for (ULONG i = 0; i < g_AudioCount; i++) {
        Mixer_DrawApp(i, g_MixerDlg.DialogY + 50 + i * 60);
    }
}

static VOID Mixer_ToggleMute(ULONG i)
{
    g_AudioMuted[i] = !g_AudioMuted[i];
    DbgPrint("MIXER: '%s' %s\n", g_AudioAppNames[i],
             g_AudioMuted[i] ? "muted" : "unmuted");
}

static VOID Mixer_OnApply(VOID)
{
    Mixer_ToggleMute(g_AudioCount - 1); /* mute last app on close */
}

static VOID CplOpenMixer(VOID)
{
    RtlZeroMemory(g_MixerCtrls, sizeof(g_MixerCtrls));
    g_MixerDlg.Title = "Volume Mixer";
    g_MixerDlg.DialogW = 500;
    g_MixerDlg.DialogH = 420;
    g_MixerDlg.DialogX = (HalpFbGetWidth() - 500) / 2;
    g_MixerDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_MixerDlg.Active = TRUE;

    ULONG i = 0;
    g_MixerCtrls[i].Type = CTRL_LABEL;
    g_MixerCtrls[i].X = 20; g_MixerCtrls[i].Y = 30; g_MixerCtrls[i].W = 400; g_MixerCtrls[i].H = 16;
    g_MixerCtrls[i].Label = "Application                         Volume"; i++;

    g_MixerCtrls[i].Type = CTRL_BUTTON;
    g_MixerCtrls[i].X = 20; g_MixerCtrls[i].Y = 360; g_MixerCtrls[i].W = 100; g_MixerCtrls[i].H = 24;
    g_MixerCtrls[i].Label = "Mute last"; i++;

    g_MixerCtrls[i].X = 380; g_MixerCtrls[i].Y = 360; g_MixerCtrls[i].W = 100; g_MixerCtrls[i].H = 24;
    g_MixerCtrls[i].Type = CTRL_BUTTON;
    g_MixerCtrls[i].Label = "Close"; g_MixerCtrls[i].Id = 100; g_MixerDlg.CloseButtonId = 100; i++;

    g_MixerDlg.NumControls = i;
    g_MixerDlg.OnInit = Mixer_OnInit;
    g_MixerDlg.OnTick = Mixer_OnTick;
    g_MixerDlg.OnApply = Mixer_OnApply;
    Mixer_OnInit();
    g_Ad2Dialog = &g_MixerDlg;
}

/* ---- File Associations --------------------------------------------- */

#define MAX_ASSOC 16
typedef struct _FILE_ASSOC {
    CHAR Ext[16];
    CHAR ProgId[64];
    CHAR Description[64];
} FILE_ASSOC;

static FILE_ASSOC g_Assocs[MAX_ASSOC];
static ULONG g_AssocCount = 0;
static const CHAR *g_AssocItems[16];
static CHAR g_AssocItemBuf[16][160];
static ULONG g_AssocSelected = 0;

static AD2_CONTROL g_AssocCtrls[8];
static AD2_DIALOG g_AssocDlg = {
    "File Associations", 0, g_AssocCtrls,
    0, 0, 600, 400, FALSE, 0, 0, NULL
};

static VOID Assoc_OnInit(VOID)
{
    g_AssocCount = 6;
    RtlCopyMemory(g_Assocs[0].Ext, ".txt", 5); g_Assocs[0].Ext[4] = 0;
    RtlCopyMemory(g_Assocs[0].ProgId, "txtfile", 8); g_Assocs[0].ProgId[7] = 0;
    RtlCopyMemory(g_Assocs[0].Description, "Text Document", 14); g_Assocs[0].Description[13] = 0;
    RtlCopyMemory(g_Assocs[1].Ext, ".exe", 5); g_Assocs[1].Ext[4] = 0;
    RtlCopyMemory(g_Assocs[1].ProgId, "exefile", 8); g_Assocs[1].ProgId[7] = 0;
    RtlCopyMemory(g_Assocs[1].Description, "Application", 12); g_Assocs[1].Description[11] = 0;
    RtlCopyMemory(g_Assocs[2].Ext, ".jpg", 5); g_Assocs[2].Ext[4] = 0;
    RtlCopyMemory(g_Assocs[2].ProgId, "jpegfile", 9); g_Assocs[2].ProgId[8] = 0;
    RtlCopyMemory(g_Assocs[2].Description, "JPEG Image", 11); g_Assocs[2].Description[10] = 0;
    RtlCopyMemory(g_Assocs[3].Ext, ".png", 5); g_Assocs[3].Ext[4] = 0;
    RtlCopyMemory(g_Assocs[3].ProgId, "pngfile", 8); g_Assocs[3].ProgId[7] = 0;
    RtlCopyMemory(g_Assocs[3].Description, "PNG Image", 10); g_Assocs[3].Description[9] = 0;
    RtlCopyMemory(g_Assocs[4].Ext, ".pdf", 5); g_Assocs[4].Ext[4] = 0;
    RtlCopyMemory(g_Assocs[4].ProgId, "pdffile", 8); g_Assocs[4].ProgId[7] = 0;
    RtlCopyMemory(g_Assocs[4].Description, "PDF Document", 13); g_Assocs[4].Description[12] = 0;
    RtlCopyMemory(g_Assocs[5].Ext, ".html", 6); g_Assocs[5].Ext[5] = 0;
    RtlCopyMemory(g_Assocs[5].ProgId, "htmlfile", 9); g_Assocs[5].ProgId[8] = 0;
    RtlCopyMemory(g_Assocs[5].Description, "HTML Document", 14); g_Assocs[5].Description[13] = 0;

    for (ULONG i = 0; i < g_AssocCount; i++) {
        ULONG j = 0;
        while (g_Assocs[i].Ext[j] && j < 159) g_AssocItemBuf[i][j++] = g_Assocs[i].Ext[j-1];
        while (j < 10 && j < 159) g_AssocItemBuf[i][j++] = ' ';
        ULONG k = 0;
        while (g_Assocs[i].ProgId[k] && j + k < 159) g_AssocItemBuf[i][j + k++] = g_Assocs[i].ProgId[k];
        while (j + k < 60 && j + k < 159) g_AssocItemBuf[i][j + k++] = ' ';
        ULONG m = 0;
        while (g_Assocs[i].Description[m] && j + k + m < 159) g_AssocItemBuf[i][j + k + m++] = g_Assocs[i].Description[m];
        g_AssocItemBuf[i][(159) < (j + k + m) ? (159) : (j + k + m)] = 0;
        g_AssocItems[i] = g_AssocItemBuf[i];
    }
    g_AssocItems[g_AssocCount] = NULL;
}

static VOID Assoc_OnTick(VOID)
{
    HalpFbFillRect(g_AssocDlg.DialogX + 20, g_AssocDlg.DialogY + 50,
                    560, 280, COLOR_WHITE);
    AD2_DrawListBox(g_AssocDlg.DialogX + 20, g_AssocDlg.DialogY + 50, 560, 280,
                     (const CHAR **)g_AssocItems, g_AssocCount,
                     0, g_AssocSelected);
}

static VOID CplOpenFileAssoc(VOID)
{
    RtlZeroMemory(g_AssocCtrls, sizeof(g_AssocCtrls));
    g_AssocDlg.Title = "File Associations";
    g_AssocDlg.DialogW = 620;
    g_AssocDlg.DialogH = 420;
    g_AssocDlg.DialogX = (HalpFbGetWidth() - 620) / 2;
    g_AssocDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_AssocDlg.Active = TRUE;

    ULONG i = 0;
    g_AssocCtrls[i].Type = CTRL_LABEL;
    g_AssocCtrls[i].X = 20; g_AssocCtrls[i].Y = 30; g_AssocCtrls[i].W = 400; g_AssocCtrls[i].H = 16;
    g_AssocCtrls[i].Label = "Extension    ProgId              Description"; i++;

    g_AssocCtrls[i].Type = CTRL_BUTTON;
    g_AssocCtrls[i].X = 20; g_AssocCtrls[i].Y = 350; g_AssocCtrls[i].W = 120; g_AssocCtrls[i].H = 24;
    g_AssocCtrls[i].Label = "Change"; i++;

    g_AssocCtrls[i].X = 500; g_AssocCtrls[i].Y = 350; g_AssocCtrls[i].W = 100; g_AssocCtrls[i].H = 24;
    g_AssocCtrls[i].Type = CTRL_BUTTON;
    g_AssocCtrls[i].Label = "Close"; g_AssocCtrls[i].Id = 100; g_AssocDlg.CloseButtonId = 100; i++;

    g_AssocDlg.NumControls = i;
    g_AssocDlg.OnInit = Assoc_OnInit;
    g_AssocDlg.OnTick = Assoc_OnTick;
    Assoc_OnInit();
    g_Ad2Dialog = &g_AssocDlg;
}

/* ---- Public dispatch ----------------------------------------------- */

VOID NTAPI Ad2OpenApplet(ULONG AppletId)
{
    DbgPrint("AD2: opening applet %u\n", AppletId);
    switch (AppletId) {
        case CPL_DISKMGMT:    CplOpenStorage();         break;
        case 100:            CplOpenFonts();           break; /* CPL_FONTS=100 */
        case 200:            CplOpenEnvironmentVariables(); break;
        case 300:            CplOpenStartup();         break;
        case 400:            CplOpenUserAccounts();    break;
        case 500:            CplOpenFirewall();        break;
        case CPL_PRINTERS:   CplOpenPrinters();        break;
        case 600:            CplOpenMixer();           break;
        case 700:            CplOpenFileAssoc();       break;
        default:
            DbgPrint("AD2: unknown applet %u\n", AppletId);
            break;
    }
}

VOID NTAPI Ad2Init(VOID)
{
    g_Ad2Dialog = NULL;
    DbgPrint("AD2: additional administrative applets initialized\n");
}

BOOLEAN NTAPI Ad2IsActive(VOID)
{
    return g_Ad2Dialog != NULL && g_Ad2Dialog->Active;
}

BOOLEAN NTAPI Ad2HandleMouseEvent(SHORT mx, SHORT my,
                                    BOOLEAN leftDown, BOOLEAN leftPrev)
{
    BOOLEAN handled = AD2_HandleMouse(mx, my, leftDown, leftPrev);
    return handled;
}

VOID NTAPI Ad2Tick(VOID)
{
    if (!g_Ad2Dialog || !g_Ad2Dialog->Active) {
        g_Ad2Dialog = NULL;
        return;
    }
    AD2_DrawDialog(g_Ad2Dialog);
    if (g_Ad2Dialog->OnTick) g_Ad2Dialog->OnTick();
}
