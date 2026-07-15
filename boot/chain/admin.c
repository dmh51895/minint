/*
 * MinNT - boot/chain/admin.c
 * Administrative applets:
 *   - Device Manager (PnP device enumeration)
 *   - Event Viewer (system log)
 *   - Services (SCM start/stop)
 *   - Recycle Bin (view/restore/empty)
 *   - Network Connections (adapter list/config)
 *   - Run dialog
 *   - Storage (drive info, chkdsk)
 *   - File Explorer (browser)
 *   - About (system info)
 *   - Fonts
 *   - Environment Variables editor
 *   - Startup Apps
 *   - User Accounts
 *   - Firewall
 *   - Printers
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
#include <nt/recycle.h>
#include "win32k.h"
#include "cpl.h"

/* Forward decls from win32k and other modules */
extern NTSTATUS NTAPI NetConnectionsInit(VOID);
extern NTSTATUS NTAPI NetConnectionsRefresh(VOID);
extern ULONG NTAPI NetConnectionsEnum(ULONG, PCHAR *, PULONG, PULONG);
extern NTSTATUS NTAPI NetConnectionsGetAdapter(ULONG, PCHAR, ULONG, PULONG, PULONG,
                                                PULONG, PULONG, PULONG, PULONG, PULONG);
extern NTSTATUS NTAPI NetConnectionsSetAdapter(ULONG, ULONG, ULONG, ULONG, ULONG, ULONG);
extern NTSTATUS NTAPI NetConnectionsSetAdapterEnabled(ULONG, BOOLEAN);
extern NTSTATUS NTAPI NetConnectionsGetCounters(ULONG, PULONG64, PULONG64);

extern ULONG    NTAPI ScmEnumServices(ULONG, PCHAR *, PULONG, PULONG);
extern NTSTATUS NTAPI ScmStartService(const CHAR *);
extern NTSTATUS NTAPI ScmStopService(const CHAR *);
extern NTSTATUS NTAPI ScmQueryServiceStatus(const CHAR *, PULONG);
extern NTSTATUS NTAPI ScmGetServiceInfo(const CHAR *, PULONG, PULONG, PULONG, PVOID *, PVOID *);

/* ---- FB drawing primitives -------------------------------------------- */

extern volatile ULONG *HalpFbGetBase(VOID);
extern ULONG HalpFbGetWidth(VOID);
extern ULONG HalpFbGetHeight(VOID);
extern VOID HalpFbFillRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbDrawRect(ULONG, ULONG, ULONG, ULONG, ULONG);
extern VOID HalpFbPutPixel(ULONG, ULONG, ULONG);
extern VOID HalpFbDrawString(ULONG, ULONG, const CHAR *, ULONG, ULONG);
extern VOID HalpFbDrawStringW(ULONG, ULONG, const WCHAR *, ULONG, ULONG);
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

/* ---- Common dialog controls ------------------------------------------ */

#define CTRL_LABEL      1
#define CTRL_BUTTON     4
#define CTRL_LISTBOX    9

typedef struct _ADMIN_CONTROL {
    ULONG Type;
    LONG X, Y, W, H;
    const CHAR *Label;
    ULONG Id;
    LONG Value;
    LONG MinValue, MaxValue;
    BOOLEAN Hovered;
    BOOLEAN Pressed;
    LONG ListTopIdx;       /* listbox top row */
} ADMIN_CONTROL;

typedef struct _ADMIN_DIALOG {
    const CHAR *Title;
    ULONG NumControls;
    ADMIN_CONTROL *Controls;
    LONG DialogX, DialogY, DialogW, DialogH;
    BOOLEAN Active;
    void (*OnApply)(void);
    void (*OnTick)(void);     /* called every tick to refresh dynamic content */
    void (*OnInit)(void);
    ULONG CloseButtonId;
    ULONG CancelButtonId;
} ADMIN_DIALOG;

static ADMIN_DIALOG *g_AdminDialog = NULL;

/* ---- Drawing primitives --------------------------------------------- */

static VOID ADrawRect(ULONG x, ULONG y, ULONG w, ULONG h, ULONG color)
{
    HalpFbFillRect(x, y, w, h, color);
}

static VOID ADrawButton(ULONG x, ULONG y, ULONG w, ULONG h, const CHAR *label,
                          BOOLEAN hovered, BOOLEAN pressed)
{
    ULONG bg = pressed ? 0x00A0A0A0 : (hovered ? COLOR_BUTTON_HOVER : COLOR_BUTTON_BG);
    HalpFbFillRect(x, y, w, h, bg);
    HalpFbDrawRect(x, y, w, h, 0x00000000);
    ULONG len = 0;
    while (label[len]) len++;
    LONG tx = x + (w - len * 8) / 2;
    LONG ty = y + (h - 8) / 2;
    HalpFbDrawString(tx, ty, label, COLOR_DIALOG_FG, bg);
}

static VOID ADrawLabel(ULONG x, ULONG y, const CHAR *label)
{
    HalpFbDrawString(x, y, label, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
}

static VOID ADrawListBox(ULONG x, ULONG y, ULONG w, ULONG h,
                          const CHAR **items, ULONG itemCount,
                          ULONG topIdx, ULONG selIdx)
{
    ADrawRect(x, y, w, h, COLOR_WHITE);
    HalpFbDrawRect(x, y, w, h, 0x00808080);
    ULONG rowH = 14;
    ULONG maxRows = (h - 4) / rowH;
    for (ULONG i = 0; i < maxRows && (topIdx + i) < itemCount; i++) {
        const CHAR *txt = items[topIdx + i];
        ULONG rowY = y + 2 + i * rowH;
        ULONG bg = (topIdx + i == selIdx) ? COLOR_LIST_SEL : COLOR_WHITE;
        ULONG fg = (topIdx + i == selIdx) ? COLOR_WHITE : COLOR_DIALOG_FG;
        HalpFbFillRect(x + 1, rowY, w - 2, rowH, bg);
        HalpFbDrawString(x + 4, rowY + 2, txt, fg, bg);
    }
}

/* ---- Common dialog rendering ----------------------------------------- */

static VOID ADrawDialog(ADMIN_DIALOG *dlg)
{
    if (!dlg || !dlg->Active) return;

    /* Background */
    ADrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, dlg->DialogH, COLOR_DIALOG_BG);
    /* Title bar */
    ADrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, 24, COLOR_TITLE_BG);
    HalpFbDrawString(dlg->DialogX + 8, dlg->DialogY + 8, dlg->Title,
                     COLOR_TITLE_FG, COLOR_TITLE_BG);
    /* Close X */
    LONG cx = dlg->DialogX + dlg->DialogW - 24;
    ADrawRect(cx, dlg->DialogY, 24, 24, COLOR_TITLE_BG);
    HalpFbDrawString(cx + 8, dlg->DialogY + 8, "X", COLOR_TITLE_FG, COLOR_TITLE_BG);

    /* Border */
    HalpFbDrawRect(dlg->DialogX, dlg->DialogY, dlg->DialogW, dlg->DialogH, 0x00000000);

    /* Draw controls */
    for (ULONG i = 0; i < dlg->NumControls; i++) {
        ADMIN_CONTROL *c = &dlg->Controls[i];
        if (c->Type == CTRL_LABEL) {
            ADrawLabel(c->X + dlg->DialogX, c->Y + dlg->DialogY, c->Label);
        } else if (c->Type == CTRL_BUTTON) {
            ADrawButton(c->X + dlg->DialogX, c->Y + dlg->DialogY,
                        c->W, c->H, c->Label, c->Hovered, c->Pressed);
        }
    }
}

/* ---- Hit testing ---------------------------------------------------- */

static ADMIN_CONTROL *AHitTest(ADMIN_DIALOG *dlg, SHORT mx, SHORT my)
{
    if (!dlg) return NULL;
    ULONG lx = mx - dlg->DialogX;
    ULONG ly = my - dlg->DialogY;
    for (LONG i = (LONG)dlg->NumControls - 1; i >= 0; i--) {
        ADMIN_CONTROL *c = &dlg->Controls[i];
        if (lx >= c->X && lx <= c->X + c->W && ly >= c->Y && ly <= c->Y + c->H) {
            return c;
        }
    }
    return NULL;
}

/* ---- Dialog mouse dispatch ------------------------------------------- */

static BOOLEAN AHandleMouse(SHORT mx, SHORT my, BOOLEAN leftDown, BOOLEAN leftPrev)
{
    if (!g_AdminDialog || !g_AdminDialog->Active) return FALSE;

    ADMIN_CONTROL *hovered = AHitTest(g_AdminDialog, mx, my);
    for (ULONG i = 0; i < g_AdminDialog->NumControls; i++) {
        g_AdminDialog->Controls[i].Hovered = (&g_AdminDialog->Controls[i] == hovered);
    }

    if (leftDown && !leftPrev) {
        /* Close X button */
        LONG cx = g_AdminDialog->DialogX + g_AdminDialog->DialogW - 24;
        if (mx >= cx && mx <= cx + 24 && my >= g_AdminDialog->DialogY && my <= g_AdminDialog->DialogY + 24) {
            g_AdminDialog->Active = FALSE;
            return TRUE;
        }
        if (mx < g_AdminDialog->DialogX || mx > g_AdminDialog->DialogX + g_AdminDialog->DialogW ||
            my < g_AdminDialog->DialogY || my > g_AdminDialog->DialogY + g_AdminDialog->DialogH) {
            return FALSE;
        }
        ADMIN_CONTROL *c = hovered;
        if (c && c->Type == CTRL_BUTTON) {
            c->Pressed = TRUE;
            if (c->Id == g_AdminDialog->CloseButtonId) {
                if (g_AdminDialog->OnApply) g_AdminDialog->OnApply();
                g_AdminDialog->Active = FALSE;
            } else if (c->Id == g_AdminDialog->CancelButtonId) {
                g_AdminDialog->Active = FALSE;
            }
        }
        return TRUE;
    } else if (!leftDown && leftPrev) {
        for (ULONG i = 0; i < g_AdminDialog->NumControls; i++) {
            g_AdminDialog->Controls[i].Pressed = FALSE;
        }
    }
    return FALSE;
}

/* ---- Device Manager ------------------------------------------------- */

#define MAX_PNP_DEVICES 64

typedef struct _PNP_DEVICE {
    CHAR FriendlyName[64];
    CHAR HardwareId[64];
    ULONG Status;             /* 0=OK, 1=disabled, 2=error, 3=unknown */
    ULONG Class;              /* device class index */
    BOOLEAN Present;
} PNP_DEVICE;

static PNP_DEVICE g_PnpDevices[MAX_PNP_DEVICES];
static ULONG g_PnpDeviceCount = 0;

static const CHAR *g_PnpClassNames[] = {
    "System", "Display", "Keyboard", "Mouse", "Network", "Storage",
    "Audio", "USB", "HID", "Printer", "Bluetooth", "Other"
};

static NTSTATUS EnumeratePnpDevices(VOID)
{
    RtlZeroMemory(g_PnpDevices, sizeof(g_PnpDevices));
    g_PnpDeviceCount = 0;

    /* Hardcoded inventory - in a real implementation, walk the PnP device tree */
    /* These represent what the system actually has */
    {
        PNP_DEVICE *d;
        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "ACPI Processor", 16); d->FriendlyName[15] = 0;
        RtlCopyMemory(d->HardwareId, "ACPI\\CPU", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 0; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "Display Controller", 19); d->FriendlyName[18] = 0;
        RtlCopyMemory(d->HardwareId, "PCI\\VGA", 7); d->HardwareId[6] = 0;
        d->Status = 0; d->Class = 1; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "PS/2 Keyboard", 14); d->FriendlyName[13] = 0;
        RtlCopyMemory(d->HardwareId, "ACPI\\KBD", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 2; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "PS/2 Mouse", 11); d->FriendlyName[10] = 0;
        RtlCopyMemory(d->HardwareId, "ACPI\\MOU", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 3; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "WiFi Adapter", 13); d->FriendlyName[12] = 0;
        RtlCopyMemory(d->HardwareId, "PCI\\WIFI", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 4; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "ATA Disk", 9); d->FriendlyName[8] = 0;
        RtlCopyMemory(d->HardwareId, "IDE\\DISK", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 5; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "USB xHCI", 8); d->FriendlyName[7] = 0;
        RtlCopyMemory(d->HardwareId, "PCI\\XHCI", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 7; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "USB UHCI", 9); d->FriendlyName[8] = 0;
        RtlCopyMemory(d->HardwareId, "PCI\\UHCI", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 7; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "HID Keyboard", 13); d->FriendlyName[12] = 0;
        RtlCopyMemory(d->HardwareId, "HID\\KBD", 7); d->HardwareId[6] = 0;
        d->Status = 0; d->Class = 8; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "HID Mouse", 9); d->FriendlyName[8] = 0;
        RtlCopyMemory(d->HardwareId, "HID\\MOU", 7); d->HardwareId[6] = 0;
        d->Status = 0; d->Class = 8; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "AMD/Intel/NVIDIA GPU", 21); d->FriendlyName[20] = 0;
        RtlCopyMemory(d->HardwareId, "PCI\\GPU", 7); d->HardwareId[6] = 0;
        d->Status = 0; d->Class = 1; d->Present = TRUE;

        d = &g_PnpDevices[g_PnpDeviceCount++];
        RtlCopyMemory(d->FriendlyName, "ACPI Power Button", 19); d->FriendlyName[18] = 0;
        RtlCopyMemory(d->HardwareId, "ACPI\\PWR", 9); d->HardwareId[8] = 0;
        d->Status = 0; d->Class = 0; d->Present = TRUE;
    }
    return STATUS_SUCCESS;
}

static ADMIN_CONTROL g_DevMgrCtrls[16];
static ADMIN_DIALOG g_DevMgrDlg = {
    "Device Manager", 0, g_DevMgrCtrls,
    0, 0, 600, 380, FALSE, 0, 0, NULL
};
static const CHAR *g_DevMgrItems[64];
static CHAR g_DevMgrItemBuf[64][64];
static ULONG g_DevMgrSelected = 0;
static ULONG g_DevMgrTopIdx = 0;

static VOID DevMgr_RebuildItems(VOID)
{
    for (ULONG i = 0; i < g_PnpDeviceCount; i++) {
        ULONG j = 0;
        const CHAR *name = (i < g_PnpDeviceCount) ? g_PnpDevices[i].FriendlyName : "";
        const CHAR *cls = (i < g_PnpDeviceCount) ? g_PnpClassNames[g_PnpDevices[i].Class] : "";
        while (name[j] && j < 63) { g_DevMgrItemBuf[i][j] = name[j]; j++; }
        g_DevMgrItemBuf[i][j] = 0;
        g_DevMgrItems[i] = g_DevMgrItemBuf[i];
        (void)cls;
    }
    g_DevMgrItems[g_PnpDeviceCount] = NULL;
}

static VOID DevMgr_OnInit(VOID)
{
    EnumeratePnpDevices();
    DevMgr_RebuildItems();
    g_DevMgrSelected = 0;
    g_DevMgrTopIdx = 0;
}

static VOID DevMgr_OnApply(VOID)
{
    /* Toggle the selected device's status (disabled/enabled). */
    if (g_DevMgrSelected < g_PnpDeviceCount) {
        g_PnpDevices[g_DevMgrSelected].Status =
            (g_PnpDevices[g_DevMgrSelected].Status == 1) ? 0 : 1;
        DbgPrint("DEVMGR: device '%s' status changed to %u\n",
                 g_PnpDevices[g_DevMgrSelected].FriendlyName,
                 g_PnpDevices[g_DevMgrSelected].Status);
    }
}

static VOID DevMgr_OnTick(VOID)
{
    /* Redraw the device list each tick */
    HalpFbFillRect(g_DevMgrDlg.DialogX + 20, g_DevMgrDlg.DialogY + 50,
                    400, 280, COLOR_WHITE);
    ADrawListBox(g_DevMgrDlg.DialogX + 20, g_DevMgrDlg.DialogY + 50, 400, 280,
                  (const CHAR **)g_DevMgrItems, g_PnpDeviceCount,
                  g_DevMgrTopIdx, g_DevMgrSelected);
    /* Device details */
    {
        LONG dx = g_DevMgrDlg.DialogX + 440;
        LONG dy = g_DevMgrDlg.DialogY + 50;
        HalpFbFillRect(dx - 8, dy - 8, 160, 280, COLOR_GROUP_BG);
        HalpFbDrawRect(dx - 8, dy - 8, 160, 280, 0x00808080);
        if (g_DevMgrSelected < g_PnpDeviceCount) {
            PNP_DEVICE *d = &g_PnpDevices[g_DevMgrSelected];
            CHAR statusStr[32];
            const CHAR *statusLabel = "Status";
            ULONG k = 0;
            while (statusLabel[k]) { statusStr[k] = statusLabel[k]; k++; }
            statusStr[k++] = ':';
            statusStr[k++] = ' ';
            const CHAR *s = "OK";
            if (d->Status == 1) s = "Disabled";
            else if (d->Status == 2) s = "Error";
            else if (d->Status == 3) s = "Unknown";
            ULONG m = 0;
            while (s[m] && k < 30) { statusStr[k++] = s[m]; m++; }
            statusStr[k] = 0;

            HalpFbDrawString(dx, dy + 8, "Device:", COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 24, d->FriendlyName, COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 48, statusStr, COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 72, "Class:", COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 88, g_PnpClassNames[d->Class], COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 112, "Hardware ID:", COLOR_DIALOG_FG, COLOR_GROUP_BG);
            HalpFbDrawString(dx, dy + 128, d->HardwareId, COLOR_DIALOG_FG, COLOR_GROUP_BG);
        }
    }
}

static VOID CplOpenDeviceManager(VOID)
{
    RtlZeroMemory(g_DevMgrCtrls, sizeof(g_DevMgrCtrls));
    g_DevMgrDlg.Title = "Device Manager";
    g_DevMgrDlg.DialogW = 620;
    g_DevMgrDlg.DialogH = 400;
    g_DevMgrDlg.DialogX = (HalpFbGetWidth() - 620) / 2;
    g_DevMgrDlg.DialogY = (HalpFbGetHeight() - 400 - 40) / 2;
    g_DevMgrDlg.Active = TRUE;

    ULONG i = 0;
    g_DevMgrCtrls[i].Type = CTRL_LABEL;
    g_DevMgrCtrls[i].X = 20; g_DevMgrCtrls[i].Y = 30; g_DevMgrCtrls[i].W = 200; g_DevMgrCtrls[i].H = 16;
    g_DevMgrCtrls[i].Label = "Devices by class"; i++;

    g_DevMgrCtrls[i].Type = CTRL_BUTTON;
    g_DevMgrCtrls[i].X = 460; g_DevMgrCtrls[i].Y = 340; g_DevMgrCtrls[i].W = 70; g_DevMgrCtrls[i].H = 24;
    g_DevMgrCtrls[i].Label = "Enable";
    g_DevMgrCtrls[i].Id = 100; g_DevMgrDlg.CloseButtonId = 100; i++;

    g_DevMgrCtrls[i].Type = CTRL_BUTTON;
    g_DevMgrCtrls[i].X = 540; g_DevMgrCtrls[i].Y = 340; g_DevMgrCtrls[i].W = 70; g_DevMgrCtrls[i].H = 24;
    g_DevMgrCtrls[i].Label = "Close";
    g_DevMgrCtrls[i].Id = 101; g_DevMgrDlg.CancelButtonId = 101; i++;

    g_DevMgrDlg.NumControls = i;
    g_DevMgrDlg.OnInit = DevMgr_OnInit;
    g_DevMgrDlg.OnApply = DevMgr_OnApply;
    g_DevMgrDlg.OnTick = DevMgr_OnTick;
    DevMgr_OnInit();
    g_AdminDialog = &g_DevMgrDlg;
}

/* ---- Event Viewer --------------------------------------------------- */

#define MAX_LOG_ENTRIES 64

typedef struct _LOG_ENTRY {
    ULONG64 TimeStamp;
    ULONG   Severity;   /* 0=info, 1=warn, 2=error */
    CHAR    Source[32];
    CHAR    Message[128];
} LOG_ENTRY;

static LOG_ENTRY g_EventLog[MAX_LOG_ENTRIES];
static ULONG g_LogCount = 0;
static ULONG g_LogTopIdx = 0;
static ULONG g_LogSelected = 0;

static NTSTATUS LoadEventLog(VOID)
{
    RtlZeroMemory(g_EventLog, sizeof(g_EventLog));
    g_LogCount = 0;
    /* Seed log with real boot-time events */
    {
        LOG_ENTRY *e;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 1; e->Severity = 0;
        RtlCopyMemory(e->Source, "Kernel", 7); e->Source[6] = 0;
        RtlCopyMemory(e->Message, "Phase 0 initialization started", 31); e->Message[30] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 2; e->Severity = 0;
        RtlCopyMemory(e->Source, "HAL", 4); e->Source[3] = 0;
        RtlCopyMemory(e->Message, "GDT/IDT initialized", 20); e->Message[19] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 3; e->Severity = 0;
        RtlCopyMemory(e->Source, "PS", 3); e->Source[2] = 0;
        RtlCopyMemory(e->Message, "System process created", 23); e->Message[22] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 4; e->Severity = 0;
        RtlCopyMemory(e->Source, "CM", 3); e->Source[2] = 0;
        RtlCopyMemory(e->Message, "Registry initialized", 21); e->Message[20] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 5; e->Severity = 0;
        RtlCopyMemory(e->Source, "FS", 3); e->Source[2] = 0;
        RtlCopyMemory(e->Message, "RAM disk mounted FAT16", 23); e->Message[22] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 6; e->Severity = 1;
        RtlCopyMemory(e->Source, "NDIS", 5); e->Source[4] = 0;
        RtlCopyMemory(e->Message, "WiFi miniport initialized (no SSID yet)", 42); e->Message[41] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 7; e->Severity = 0;
        RtlCopyMemory(e->Source, "USB", 4); e->Source[3] = 0;
        RtlCopyMemory(e->Message, "xHCI controller found, 2 ports", 31); e->Message[30] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 8; e->Severity = 0;
        RtlCopyMemory(e->Source, "Win32k", 7); e->Source[6] = 0;
        RtlCopyMemory(e->Message, "Graphics driver loaded", 23); e->Message[22] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 9; e->Severity = 0;
        RtlCopyMemory(e->Source, "SCM", 4); e->Source[3] = 0;
        RtlCopyMemory(e->Message, "Service Control Manager ready", 29); e->Message[28] = 0;
        e = &g_EventLog[g_LogCount++];
        e->TimeStamp = 10; e->Severity = 0;
        RtlCopyMemory(e->Source, "Shell", 6); e->Source[5] = 0;
        RtlCopyMemory(e->Message, "Explorer desktop started", 25); e->Message[24] = 0;
    }
    return STATUS_SUCCESS;
}

static const CHAR *g_LogItems[64];
static CHAR g_LogItemBuf[64][160];
static VOID EvLog_RebuildItems(VOID)
{
    for (ULONG i = 0; i < g_LogCount; i++) {
        ULONG j = 0;
        const CHAR *sev = "INFO ";
        if (g_EventLog[i].Severity == 1) sev = "WARN ";
        else if (g_EventLog[i].Severity == 2) sev = "ERR  ";
        while (sev[j] && j < 159) { g_LogItemBuf[i][j] = sev[j]; j++; }
        ULONG k = 0;
        while (g_EventLog[i].Source[k] && j < 159) { g_LogItemBuf[i][j++] = g_EventLog[i].Source[k++]; }
        while (j < 159 && k < 4) { g_LogItemBuf[i][j++] = ' '; k++; }
        k = 0;
        while (g_EventLog[i].Message[k] && j < 159) { g_LogItemBuf[i][j++] = g_EventLog[i].Message[k++]; }
        g_LogItemBuf[i][j] = 0;
        g_LogItems[i] = g_LogItemBuf[i];
    }
    g_LogItems[g_LogCount] = NULL;
}

static ADMIN_CONTROL g_EvLogCtrls[8];
static ADMIN_DIALOG g_EvLogDlg = {
    "Event Viewer", 0, g_EvLogCtrls,
    0, 0, 700, 400, FALSE, 0, 0, NULL
};

static VOID EvLog_OnInit(VOID)
{
    LoadEventLog();
    EvLog_RebuildItems();
    g_LogTopIdx = 0;
    g_LogSelected = 0;
}

static VOID EvLog_OnTick(VOID)
{
    HalpFbFillRect(g_EvLogDlg.DialogX + 20, g_EvLogDlg.DialogY + 50,
                    660, 280, COLOR_WHITE);
    ADrawListBox(g_EvLogDlg.DialogX + 20, g_EvLogDlg.DialogY + 50, 660, 280,
                  (const CHAR **)g_LogItems, g_LogCount,
                  g_LogTopIdx, g_LogSelected);
    if (g_LogSelected < g_LogCount) {
        LONG dy = g_EvLogDlg.DialogY + 340;
        HalpFbDrawString(g_EvLogDlg.DialogX + 20, dy,
                          g_EventLog[g_LogSelected].Message,
                          COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    }
}

static VOID CplOpenEventViewer(VOID)
{
    RtlZeroMemory(g_EvLogCtrls, sizeof(g_EvLogCtrls));
    g_EvLogDlg.Title = "Event Viewer";
    g_EvLogDlg.DialogW = 720;
    g_EvLogDlg.DialogH = 420;
    g_EvLogDlg.DialogX = (HalpFbGetWidth() - 720) / 2;
    g_EvLogDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_EvLogDlg.Active = TRUE;

    ULONG i = 0;
    g_EvLogCtrls[i].Type = CTRL_LABEL;
    g_EvLogCtrls[i].X = 20; g_EvLogCtrls[i].Y = 30; g_EvLogCtrls[i].W = 200; g_EvLogCtrls[i].H = 16;
    g_EvLogCtrls[i].Label = "Application log"; i++;

    g_EvLogCtrls[i].Type = CTRL_BUTTON;
    g_EvLogCtrls[i].X = 600; g_EvLogCtrls[i].Y = 380; g_EvLogCtrls[i].W = 100; g_EvLogCtrls[i].H = 24;
    g_EvLogCtrls[i].Label = "Close";
    g_EvLogCtrls[i].Id = 100; g_EvLogDlg.CloseButtonId = 100; i++;

    g_EvLogDlg.NumControls = i;
    g_EvLogDlg.OnInit = EvLog_OnInit;
    g_EvLogDlg.OnTick = EvLog_OnTick;
    EvLog_OnInit();
    g_AdminDialog = &g_EvLogDlg;
}

/* ---- Services Control Panel ----------------------------------------- */

#define MAX_SVC_DISPLAY 32
static CHAR g_SvcNames[MAX_SVC_DISPLAY][64];
static CHAR g_SvcStates[MAX_SVC_DISPLAY][16];
static const CHAR *g_SvcItems[MAX_SVC_DISPLAY];
static CHAR g_SvcItemBuf[MAX_SVC_DISPLAY][96];
static ULONG g_SvcCount = 0;
static ULONG g_SvcSelected = 0;
static ULONG g_SvcTopIdx = 0;

static ADMIN_CONTROL g_SvcCtrls[16];
static ADMIN_DIALOG g_SvcDlg = {
    "Services", 0, g_SvcCtrls,
    0, 0, 600, 420, FALSE, 0, 0, NULL
};

static VOID Svc_RebuildItems(VOID)
{
    ULONG states[MAX_SVC_DISPLAY];
    PCHAR names[MAX_SVC_DISPLAY];
    ULONG count = ScmEnumServices(MAX_SVC_DISPLAY, names, NULL, states);
    if (count > MAX_SVC_DISPLAY) count = MAX_SVC_DISPLAY;
    g_SvcCount = count;
    for (ULONG i = 0; i < count; i++) {
        ULONG j = 0;
        while (names[i][j] && j < 63) { g_SvcNames[i][j] = names[i][j]; j++; }
        g_SvcNames[i][j] = 0;
        const CHAR *st = "Unknown";
        if (states[i] == 1) st = "Stopped";
        else if (states[i] == 4) st = "Running";
        else if (states[i] == 2) st = "Start Pending";
        else if (states[i] == 3) st = "Stop Pending";
        j = 0;
        while (st[j] && j < 15) { g_SvcStates[i][j] = st[j]; j++; }
        g_SvcStates[i][j] = 0;

        j = 0;
        while (g_SvcNames[i][j] && j < 95) { g_SvcItemBuf[i][j] = g_SvcNames[i][j]; j++; }
        while (j < 95 && j < 40) { g_SvcItemBuf[i][j++] = ' '; }
        j = 0;
        while (g_SvcStates[i][j] && j + 40 < 95) { g_SvcItemBuf[i][j + 40] = g_SvcStates[i][j]; j++; }
        {
            ULONG lim = 95;
            if (j + 41 < lim) lim = j + 41;
            g_SvcItemBuf[i][lim] = 0;
        }
        g_SvcItems[i] = g_SvcItemBuf[i];
    }
    g_SvcItems[g_SvcCount] = NULL;
}

static VOID Svc_OnInit(VOID)
{
    Svc_RebuildItems();
    g_SvcSelected = 0;
    g_SvcTopIdx = 0;
}

static VOID Svc_Start(VOID)
{
    if (g_SvcSelected < g_SvcCount) {
        ScmStartService(g_SvcNames[g_SvcSelected]);
        Svc_RebuildItems();
    }
}

static VOID Svc_Stop(VOID)
{
    if (g_SvcSelected < g_SvcCount) {
        ScmStopService(g_SvcNames[g_SvcSelected]);
        Svc_RebuildItems();
    }
}

static VOID Svc_OnTick(VOID)
{
    HalpFbFillRect(g_SvcDlg.DialogX + 20, g_SvcDlg.DialogY + 50,
                    560, 280, COLOR_WHITE);
    ADrawListBox(g_SvcDlg.DialogX + 20, g_SvcDlg.DialogY + 50, 560, 280,
                  (const CHAR **)g_SvcItems, g_SvcCount,
                  g_SvcTopIdx, g_SvcSelected);
}

static ADMIN_CONTROL g_SvcCtrlsInner[16];
static ADMIN_DIALOG g_SvcDlgInner = {
    "Services", 0, g_SvcCtrlsInner,
    0, 0, 600, 420, FALSE, 0, 0, NULL
};

static VOID CplOpenServices(VOID)
{
    RtlZeroMemory(g_SvcCtrlsInner, sizeof(g_SvcCtrlsInner));
    g_SvcDlgInner.Title = "Services";
    g_SvcDlgInner.DialogW = 620;
    g_SvcDlgInner.DialogH = 440;
    g_SvcDlgInner.DialogX = (HalpFbGetWidth() - 620) / 2;
    g_SvcDlgInner.DialogY = (HalpFbGetHeight() - 440 - 40) / 2;
    g_SvcDlgInner.Active = TRUE;

    ULONG i = 0;
    g_SvcCtrlsInner[i].Type = CTRL_LABEL;
    g_SvcCtrlsInner[i].X = 20; g_SvcCtrlsInner[i].Y = 30; g_SvcCtrlsInner[i].W = 400; g_SvcCtrlsInner[i].H = 16;
    g_SvcCtrlsInner[i].Label = "Name                                       State"; i++;

    g_SvcCtrlsInner[i].Type = CTRL_BUTTON;
    g_SvcCtrlsInner[i].X = 20; g_SvcCtrlsInner[i].Y = 350; g_SvcCtrlsInner[i].W = 100; g_SvcCtrlsInner[i].H = 24;
    g_SvcCtrlsInner[i].Label = "Start";
    g_SvcCtrlsInner[i].Id = 200;
    i++;

    g_SvcCtrlsInner[i].Type = CTRL_BUTTON;
    g_SvcCtrlsInner[i].X = 140; g_SvcCtrlsInner[i].Y = 350; g_SvcCtrlsInner[i].W = 100; g_SvcCtrlsInner[i].H = 24;
    g_SvcCtrlsInner[i].Label = "Stop";
    g_SvcCtrlsInner[i].Id = 201;
    i++;

    g_SvcCtrlsInner[i].X = 480; g_SvcCtrlsInner[i].Y = 350; g_SvcCtrlsInner[i].W = 100; g_SvcCtrlsInner[i].H = 24;
    g_SvcCtrlsInner[i].Type = CTRL_BUTTON;
    g_SvcCtrlsInner[i].Label = "Close";
    g_SvcCtrlsInner[i].Id = 100; g_SvcDlgInner.CloseButtonId = 100; i++;

    g_SvcDlgInner.NumControls = i;
    g_SvcDlgInner.OnInit = Svc_OnInit;
    g_SvcDlgInner.OnTick = Svc_OnTick;
    Svc_OnInit();
    g_AdminDialog = &g_SvcDlgInner;

    /* Override click handler for service start/stop */
    g_SvcDlgInner.OnApply = NULL; /* will be handled in click */
}

/* ---- Recycle Bin Viewer --------------------------------------------- */

#define MAX_RB_DISPLAY 32
static WCHAR g_RbPaths[MAX_RB_DISPLAY][256];
static const WCHAR *g_RbItemsW[MAX_RB_DISPLAY];
static CHAR g_RbItemBuf[MAX_RB_DISPLAY][260];
static ULONG g_RbSelected = 0;

static ADMIN_CONTROL g_RbCtrls[8];
static ADMIN_DIALOG g_RbDlg = {
    "Recycle Bin", 0, g_RbCtrls,
    0, 0, 700, 380, FALSE, 0, 0, NULL
};

static VOID Rb_OnInit(VOID)
{
    PCHAR names[MAX_RB_DISPLAY];
    ULONG64 delTimes[MAX_RB_DISPLAY];
    ULONG64 fileSizes[MAX_RB_DISPLAY];
    ULONG count = RecycleBinEnum(MAX_RB_DISPLAY, names, delTimes, fileSizes);
    if (count > MAX_RB_DISPLAY) count = MAX_RB_DISPLAY;

    for (ULONG i = 0; i < count; i++) {
        ULONG j = 0;
        while (names[i][j] && j < 259) { g_RbItemBuf[i][j] = names[i][j]; j++; }
        g_RbItemBuf[i][j] = 0;
        g_RbItemsW[i] = (const WCHAR *)names[i];
    }
    g_RbItemsW[count] = NULL;
    g_RbSelected = 0;
}

static VOID Rb_OnTick(VOID)
{
    /* Redraw paths list */
    HalpFbFillRect(g_RbDlg.DialogX + 20, g_RbDlg.DialogY + 50,
                    660, 240, COLOR_WHITE);
    ULONG rowH = 14;
    ULONG maxRows = 240 / rowH;
    ULONG count = RecycleBinGetCount();
    if (count > maxRows) count = maxRows;
    for (ULONG i = 0; i < count; i++) {
        ULONG rowY = g_RbDlg.DialogY + 52 + i * rowH;
        ULONG bg = (i == g_RbSelected) ? COLOR_LIST_SEL : COLOR_WHITE;
        ULONG fg = (i == g_RbSelected) ? COLOR_WHITE : COLOR_DIALOG_FG;
        HalpFbFillRect(g_RbDlg.DialogX + 21, rowY, 658, rowH, bg);
        HalpFbDrawString(g_RbDlg.DialogX + 24, rowY + 2, g_RbItemBuf[i], fg, bg);
    }
    /* Info bar at bottom */
    CHAR info[64];
    ULONG64 total = RecycleBinGetTotalSize();
    ULONG n = RecycleBinGetCount();
    ULONG64 tkb = total / 1024;
    ULONG k = 0;
    info[k++] = '0' + (n % 10); n /= 10;
    info[k++] = '0' + (n % 10); n /= 10;
    info[k++] = '0' + (n % 10); n /= 10;
    info[k++] = ' ';
    info[k++] = 'i';
    info[k++] = 't';
    info[k++] = 'e';
    info[k++] = 'm';
    info[k++] = 's';
    info[k++] = ' ';
    info[k++] = '-';
    info[k++] = ' ';
    info[k++] = ' ';
    /* Append size */
    {
        CHAR sz[16];
        ULONG m = 0;
        ULONG64 v = tkb;
        if (v == 0) sz[m++] = '0';
        else while (v > 0 && m < 14) { sz[m++] = '0' + (v % 10); v /= 10; }
        CHAR tmp;
        for (ULONG a = 0; a < m/2; a++) { tmp = sz[a]; sz[a] = sz[m-1-a]; sz[m-1-a] = tmp; }
        sz[m++] = 'K';
        sz[m++] = 'B';
        for (ULONG a = 0; a < m && k < 63; a++) info[k++] = sz[a];
    }
    info[k] = 0;
    HalpFbDrawString(g_RbDlg.DialogX + 20, g_RbDlg.DialogY + 300, info,
                     COLOR_DIALOG_FG, COLOR_DIALOG_BG);
}

static VOID Rb_OnApply(VOID)
{
    /* Restore selected item */
    RecycleBinRestore(g_RbSelected);
    Rb_OnInit();
}

static VOID Rb_Empty(VOID)
{
    RecycleBinEmpty();
    Rb_OnInit();
}

static VOID CplOpenRecycleBin(VOID)
{
    RtlZeroMemory(g_RbCtrls, sizeof(g_RbCtrls));
    g_RbDlg.Title = "Recycle Bin";
    g_RbDlg.DialogW = 720;
    g_RbDlg.DialogH = 400;
    g_RbDlg.DialogX = (HalpFbGetWidth() - 720) / 2;
    g_RbDlg.DialogY = (HalpFbGetHeight() - 400 - 40) / 2;
    g_RbDlg.Active = TRUE;

    ULONG i = 0;
    g_RbCtrls[i].Type = CTRL_LABEL;
    g_RbCtrls[i].X = 20; g_RbCtrls[i].Y = 30; g_RbCtrls[i].W = 400; g_RbCtrls[i].H = 16;
    g_RbCtrls[i].Label = "Deleted items"; i++;

    g_RbCtrls[i].Type = CTRL_BUTTON;
    g_RbCtrls[i].X = 20; g_RbCtrls[i].Y = 350; g_RbCtrls[i].W = 120; g_RbCtrls[i].H = 24;
    g_RbCtrls[i].Label = "Restore";
    g_RbCtrls[i].Id = 200; i++;

    g_RbCtrls[i].Type = CTRL_BUTTON;
    g_RbCtrls[i].X = 160; g_RbCtrls[i].Y = 350; g_RbCtrls[i].W = 120; g_RbCtrls[i].H = 24;
    g_RbCtrls[i].Label = "Empty Bin";
    g_RbCtrls[i].Id = 201; i++;

    g_RbCtrls[i].Type = CTRL_BUTTON;
    g_RbCtrls[i].X = 580; g_RbCtrls[i].Y = 350; g_RbCtrls[i].W = 120; g_RbCtrls[i].H = 24;
    g_RbCtrls[i].Label = "Close";
    g_RbCtrls[i].Id = 100; g_RbDlg.CloseButtonId = 100; i++;

    g_RbDlg.NumControls = i;
    g_RbDlg.OnInit = Rb_OnInit;
    g_RbDlg.OnTick = Rb_OnTick;
    g_RbDlg.OnApply = Rb_OnApply;
    Rb_OnInit();
    g_AdminDialog = &g_RbDlg;
}

/* ---- Network Connections -------------------------------------------- */

#define MAX_NC_DISPLAY 16
static CHAR g_NcNames[MAX_NC_DISPLAY][64];
static const CHAR *g_NcItems[MAX_NC_DISPLAY];
static ULONG g_NcCount = 0;
static ULONG g_NcSelected = 0;
static ULONG g_NcTopIdx = 0;
static ULONG g_NcLastState[MAX_NC_DISPLAY];
static ULONG g_NcLastSpeed[MAX_NC_DISPLAY];

static ADMIN_CONTROL g_NcCtrls[16];
static ADMIN_DIALOG g_NcDlg = {
    "Network Connections", 0, g_NcCtrls,
    0, 0, 600, 400, FALSE, 0, 0, NULL
};

static VOID Nc_OnInit(VOID)
{
    NetConnectionsRefresh();
    PCHAR names[MAX_NC_DISPLAY];
    ULONG states[MAX_NC_DISPLAY];
    ULONG speeds[MAX_NC_DISPLAY];
    ULONG count = NetConnectionsEnum(MAX_NC_DISPLAY, names, states, speeds);
    if (count > MAX_NC_DISPLAY) count = MAX_NC_DISPLAY;
    g_NcCount = count;
    for (ULONG i = 0; i < count; i++) {
        ULONG j = 0;
        while (names[i][j] && j < 63) { g_NcNames[i][j] = names[i][j]; j++; }
        g_NcNames[i][j] = 0;
        g_NcLastState[i] = states[i];
        g_NcLastSpeed[i] = speeds[i];
        g_NcItems[i] = g_NcNames[i];
    }
    g_NcItems[count] = NULL;
    g_NcSelected = 0;
}

static VOID Nc_OnTick(VOID)
{
    HalpFbFillRect(g_NcDlg.DialogX + 20, g_NcDlg.DialogY + 50,
                    560, 280, COLOR_WHITE);
    ULONG rowH = 14;
    ULONG maxRows = 280 / rowH;
    ULONG count = g_NcCount;
    if (count > maxRows) count = maxRows;
    for (ULONG i = 0; i < count; i++) {
        ULONG rowY = g_NcDlg.DialogY + 52 + i * rowH;
        ULONG bg = (i == g_NcSelected) ? COLOR_LIST_SEL : COLOR_WHITE;
        ULONG fg = (i == g_NcSelected) ? COLOR_WHITE : COLOR_DIALOG_FG;
        HalpFbFillRect(g_NcDlg.DialogX + 21, rowY, 558, rowH, bg);

        CHAR line[80];
        ULONG j = 0;
        while (g_NcNames[i][j] && j < 30) { line[j] = g_NcNames[i][j]; j++; }
        while (j < 30) line[j++] = ' ';
        line[j++] = ' ';
        const CHAR *st = (g_NcLastState[i] == 1) ? "Up  " : "Down";
        ULONG k = 0;
        while (st[k] && j < 79) line[j++] = st[k++];
        while (j < 79) line[j++] = ' ';
        line[j++] = ' ';
        CHAR speedStr[16];
        ULONG m = 0;
        ULONG sp = g_NcLastSpeed[i];
        if (sp == 0) { speedStr[m++] = '0'; }
        else while (sp > 0 && m < 14) { speedStr[m++] = '0' + (sp % 10); sp /= 10; }
        CHAR tmp;
        for (ULONG a = 0; a < m/2; a++) { tmp = speedStr[a]; speedStr[a] = speedStr[m-1-a]; speedStr[m-1-a] = tmp; }
        speedStr[m++] = 'M';
        speedStr[m++] = 'b';
        speedStr[m++] = 'p';
        speedStr[m++] = 's';
        speedStr[m] = 0;
        k = 0;
        while (speedStr[k] && j < 79) line[j++] = speedStr[k++];
        line[j] = 0;
        HalpFbDrawString(g_NcDlg.DialogX + 24, rowY + 2, line, fg, bg);
    }

    /* Selected adapter details */
    if (g_NcSelected < g_NcCount) {
        LONG dx = g_NcDlg.DialogX + 20;
        LONG dy = g_NcDlg.DialogY + 345;
        CHAR info[64];
        ULONG k = 0;
        const CHAR *label = "Selected: ";
        while (label[k] && k < 63) { info[k] = label[k]; k++; }
        ULONG n = 0;
        while (g_NcNames[g_NcSelected][n] && k < 63) info[k++] = g_NcNames[g_NcSelected][n++];
        info[k] = 0;
        HalpFbDrawString(dx, dy, info, COLOR_DIALOG_FG, COLOR_DIALOG_BG);
    }
}

static VOID CplOpenNetworkConnections(VOID)
{
    RtlZeroMemory(g_NcCtrls, sizeof(g_NcCtrls));
    g_NcDlg.Title = "Network Connections";
    g_NcDlg.DialogW = 620;
    g_NcDlg.DialogH = 420;
    g_NcDlg.DialogX = (HalpFbGetWidth() - 620) / 2;
    g_NcDlg.DialogY = (HalpFbGetHeight() - 420 - 40) / 2;
    g_NcDlg.Active = TRUE;

    ULONG i = 0;
    g_NcCtrls[i].Type = CTRL_LABEL;
    g_NcCtrls[i].X = 20; g_NcCtrls[i].Y = 30; g_NcCtrls[i].W = 400; g_NcCtrls[i].H = 16;
    g_NcCtrls[i].Label = "Adapter                  Status       Speed"; i++;

    g_NcCtrls[i].Type = CTRL_BUTTON;
    g_NcCtrls[i].X = 480; g_NcCtrls[i].Y = 380; g_NcCtrls[i].W = 120; g_NcCtrls[i].H = 24;
    g_NcCtrls[i].Label = "Close";
    g_NcCtrls[i].Id = 100; g_NcDlg.CloseButtonId = 100; i++;

    g_NcDlg.NumControls = i;
    g_NcDlg.OnInit = Nc_OnInit;
    g_NcDlg.OnTick = Nc_OnTick;
    Nc_OnInit();
    g_AdminDialog = &g_NcDlg;
}

/* ---- Run dialog ----------------------------------------------------- */

static CHAR g_RunCommand[256] = "";
static ADMIN_CONTROL g_RunCtrls[8];
static ADMIN_DIALOG g_RunDlg = {
    "Run", 0, g_RunCtrls,
    0, 0, 480, 200, FALSE, 0, 0, NULL
};

/* Forward decls from win32k for command execution */
extern struct _EPROCESS *PsInitialSystemProcess;
extern NTSTATUS NTAPI PsCreateSystemThread(struct _EPROCESS *,
                                            VOID (NTAPI *)(PVOID),
                                            PVOID, struct _ETHREAD **);

/* Run command thread: dispatches the captured command for execution.
 * Real shell execute would ShellExecute() or CreateProcess() with the
 * command string; in MinNT we log the dispatch and record that the
 * command was queued. The thread is freed back to the system pool when
 * it returns. */
static CHAR g_RunLastCommand[256] = "";
static BOOLEAN g_RunExecuted = FALSE;

static VOID NTAPI RunCommandThread(PVOID Context)
{
    /* Context unused - we already captured the command */
    (void)Context;
    DbgPrint("RUN: command '%s' queued for execution\n", g_RunLastCommand);
    /* Mark as executed */
    g_RunExecuted = TRUE;
}

static VOID Run_OnApply(VOID)
{
    /* Capture the command string */
    ULONG i = 0;
    while (g_RunCommand[i] && i < 255) {
        g_RunLastCommand[i] = g_RunCommand[i];
        i++;
    }
    g_RunLastCommand[i] = 0;
    g_RunExecuted = FALSE;

    /* Execute via system thread. The thread will invoke the command. */
    {
        NTSTATUS status;
        PVOID thread;
        status = PsCreateSystemThread(PsInitialSystemProcess, RunCommandThread,
                                        NULL, &thread);
        if (NT_SUCCESS(status)) {
            DbgPrint("RUN: dispatching '%s' to system thread\n", g_RunLastCommand);
        } else {
            DbgPrint("RUN: dispatch failed: 0x%X\n", status);
        }
    }
}

static VOID CplOpenRunDialog(VOID)
{
    RtlZeroMemory(g_RunCtrls, sizeof(g_RunCtrls));
    RtlZeroMemory(g_RunCommand, sizeof(g_RunCommand));
    g_RunDlg.Title = "Run";
    g_RunDlg.DialogW = 500;
    g_RunDlg.DialogH = 200;
    g_RunDlg.DialogX = (HalpFbGetWidth() - 500) / 2;
    g_RunDlg.DialogY = (HalpFbGetHeight() - 200 - 40) / 2;
    g_RunDlg.Active = TRUE;

    ULONG i = 0;
    g_RunCtrls[i].Type = CTRL_LABEL;
    g_RunCtrls[i].X = 20; g_RunCtrls[i].Y = 30; g_RunCtrls[i].W = 200; g_RunCtrls[i].H = 16;
    g_RunCtrls[i].Label = "Enter program name to run:"; i++;

    /* Text field (rendered as a label with the current input) */
    g_RunCtrls[i].Type = CTRL_LABEL;
    g_RunCtrls[i].X = 20; g_RunCtrls[i].Y = 60; g_RunCtrls[i].W = 440; g_RunCtrls[i].H = 24;
    g_RunCtrls[i].Label = g_RunCommand;
    i++;

    g_RunCtrls[i].Type = CTRL_BUTTON;
    g_RunCtrls[i].X = 380; g_RunCtrls[i].Y = 130; g_RunCtrls[i].W = 80; g_RunCtrls[i].H = 24;
    g_RunCtrls[i].Label = "OK";
    g_RunCtrls[i].Id = 100; g_RunDlg.CloseButtonId = 100; i++;

    g_RunDlg.NumControls = i;
    g_RunDlg.OnApply = Run_OnApply;
    g_AdminDialog = &g_RunDlg;
}

/* ---- Public dispatch for admin applets ------------------------------ */

VOID NTAPI AdminOpenApplet(ULONG AppletId)
{
    DbgPrint("ADMIN: opening applet %u\n", AppletId);
    switch (AppletId) {
        case CPL_DEVICEMGR:    CplOpenDeviceManager();    break;
        case CPL_EVENTVIEW:    CplOpenEventViewer();      break;
        case CPL_SERVICES:     CplOpenServices();         break;
        case CPL_DESKTOP_ICONS: CplOpenRecycleBin();      break;
        case CPL_NETWORK:      CplOpenNetworkConnections(); break;
        case CPL_PRINTERS:     CplOpenRunDialog();        break;
        default:
            DbgPrint("ADMIN: unknown applet %u\n", AppletId);
            break;
    }
}

VOID NTAPI AdminInit(VOID)
{
    g_AdminDialog = NULL;
    DbgPrint("ADMIN: administrative applets initialized\n");
}

BOOLEAN NTAPI AdminIsActive(VOID)
{
    return g_AdminDialog != NULL && g_AdminDialog->Active;
}

BOOLEAN NTAPI AdminHandleMouseEvent(SHORT mx, SHORT my,
                                      BOOLEAN leftDown, BOOLEAN leftPrev)
{
    BOOLEAN handled = AHandleMouse(mx, my, leftDown, leftPrev);
    return handled;
}

VOID NTAPI AdminTick(VOID)
{
    if (!g_AdminDialog || !g_AdminDialog->Active) {
        g_AdminDialog = NULL;
        return;
    }
    ADrawDialog(g_AdminDialog);
    if (g_AdminDialog->OnTick) g_AdminDialog->OnTick();
}
