/*
 * MinNT - boot/chain/explorer.c
 * Explorer shell — graphical desktop with mouse interactivity.
 *
 * Features:
 * - PS/2 mouse: cursor, click detection, drag
 * - Start menu: popup with items, hover highlight
 * - Desktop icons: clickable, opens windows
 * - Window manager: draggable windows with chrome, close button,
 *                   WM_PAINT via GDI, WM_CLOSE works
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/ps.h>
#include <nt/rtl.h>
#include <nt/hal.h>
#include <nt/dispatcher.h>
#include "win32k.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define ICON_W             48
#define ICON_H             48
#define TASKBAR_H          40
#define START_BTN_W        80
#define START_BTN_H        32
#define TRAY_W             196
#define START_MENU_W       200
#define START_MENU_ITEM_H  28
#define START_MENU_PAD       4
#define START_MENU_TOTAL_H (START_MENU_ITEM_H * 6 + START_MENU_PAD * 2)
#define WIN_TITLEBAR_H     24
#define WIN_BORDER_W        2
#define WIN_MIN_W         200
#define WIN_MIN_H         120
#define MAX_WINDOWS         8
#define MAX_CLASS_NAME     128
#define MAX_WIN_TITLE     128

#define COLOR_DESKTOP_BG    0x008B4513
#define COLOR_TASKBAR       0x00C0C0C0
#define COLOR_TASKBAR_EDGE  0x00808080
#define COLOR_START_BTN     0x00A0A0A0
#define COLOR_START_TEXT    0x00000000
#define COLOR_TRAY          0x00D0D0D0
#define COLOR_ICON_BG       0x001030A0
#define COLOR_ICON_TEXT     0x00FFFFFF
#define COLOR_WHITE         0x00FFFFFF
#define COLOR_BLACK         0x00000000
#define COLOR_BLUE          0x00FF0000
#define COLOR_YELLOW        0x0000FFFF
#define COLOR_TITLEBAR      0x001030A0
#define COLOR_TITLEBAR_INACTIVE 0x00707070
#define COLOR_WIN_BG        0x00F0F0F0
#define COLOR_CLOSE_BTN     0x00BBBBBB
#define COLOR_CLOSE_HOVER   0x00DD4444
#define COLOR_CONTENT_BG    0x00FFFFFF

static const UCHAR MouseCursorBitmap[32] = {
    0xFF, 0xFF, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x80, 0x01, 0xFC, 0x01, 0xF8, 0x00,
    0xF0, 0x00, 0xE0, 0x00, 0xC0, 0x00, 0x00, 0x00,
};

/* ============================================================================
 * TYPES
 * ============================================================================ */

typedef struct _DESKTOP_ICON {
    ULONG X, Y;
    const CHAR *Label;
    BOOLEAN selected;
} DESKTOP_ICON;

typedef struct _WIN_O {
    ULONG       id;
    const CHAR *title;
    LONG        x, y;
    ULONG       cx, cy;
    BOOLEAN     open;
    BOOLEAN     active;
    BOOLEAN     dirty;
    ULONG       bg_width;
    ULONG       bg_height;
    volatile ULONG *bg_pixels;
    WINDOW     *hwnd;
} WIN_O;

/* ============================================================================
 * GLOBALS
 * ============================================================================ */

static DESKTOP_ICON Icons[] = {
    { 20,  20, "My Computer",   FALSE },
    { 20, 100, "My Documents",  FALSE },
    { 20, 180, "Recycle Bin",   FALSE },
    { 20, 260, "Internet",     FALSE },
    { 20, 340, "Network",      FALSE },
};
#define NUM_ICONS (sizeof(Icons)/sizeof(Icons[0]))

static const CHAR StartMenuItems[][20] = {
    "Programs", "Documents", "Settings", "Run...",
    "",          "Shut down",
};
static const BOOLEAN StartMenuSeparators[] = {
    FALSE, FALSE, FALSE, FALSE, TRUE, FALSE,
};
#define NUM_START_ITEMS 6

static SHORT MouseX = 512;
static SHORT MouseY = 384;
static UCHAR MousePrevButtons = 0;
static BOOLEAN MouseCursorVisible = FALSE;
static BOOLEAN StartMenuOpen = FALSE;
static SHORT StartMenuY;

static volatile ULONG StartMenuBg[START_MENU_W * START_MENU_TOTAL_H];

static WIN_O g_Windows[MAX_WINDOWS];
static ULONG g_NumOpenWindows = 0;
static BOOLEAN g_DragActive = FALSE;
static SHORT g_DragWinX, g_DragWinY;
static SHORT g_DragMouseX, g_DragMouseY;
static WIN_O *g_DragWin = NULL;

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

static VOID DrawDesktop(VOID);
static VOID DrawMouseCursor(SHORT OldX, SHORT OldY);
static VOID InvertIconArea(DESKTOP_ICON *ic);
static LONG HitTestIcons(SHORT mx, SHORT my);
static BOOLEAN HitTestStartBtn(SHORT mx, SHORT my);
static VOID DrawStartMenu(VOID);
static VOID CloseStartMenu(VOID);
static LONG HitTestStartMenu(SHORT mx, SHORT my);

static VOID InitWindowManager(VOID);
static WIN_O *AllocWindowSlot(ULONG id, const CHAR *title, ULONG cx, ULONG cy);
static VOID FreeWindowSlot(WIN_O *win);
static VOID DrawWindowFrame(WIN_O *win);
static VOID DrawAllWindows(VOID);
static LONG HitTestWindows(SHORT mx, SHORT my);
static WIN_O *FindWindowById(ULONG id);

static LONG_PTR WndprocMyComputer(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam);
static LONG_PTR WndprocDefault(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam);

/* ============================================================================
 * DESKTOP DRAWING
 * ============================================================================ */

static VOID DrawDesktop(VOID)
{
    ULONG W = HalpFbGetWidth();
    ULONG H = HalpFbGetHeight();
    ULONG TaskbarY = H - TASKBAR_H;

    HalpFbClear(COLOR_DESKTOP_BG);

    HalpFbFillRect(0, TaskbarY, W, TASKBAR_H, COLOR_TASKBAR);
    HalpFbFillRect(0, TaskbarY, W, 2, COLOR_WHITE);
    HalpFbFillRect(0, TaskbarY + TASKBAR_H - 2, W, 2, COLOR_TASKBAR_EDGE);

    HalpFbFillRect(4, TaskbarY + 4, START_BTN_W, TASKBAR_H - 8, COLOR_START_BTN);
    HalpFbDrawRect(4, TaskbarY + 4, START_BTN_W, TASKBAR_H - 8, COLOR_BLACK);
    HalpFbDrawStringCentered(4, TaskbarY + 12, START_BTN_W, "Start", COLOR_START_TEXT, COLOR_START_BTN);

    ULONG TrayX = W - TRAY_W;
    HalpFbFillRect(TrayX, TaskbarY + 4, TRAY_W, TASKBAR_H - 8, COLOR_TRAY);
    HalpFbDrawRect(TrayX, TaskbarY + 4, TRAY_W, TASKBAR_H - 8, COLOR_TASKBAR_EDGE);
    HalpFbDrawString(TrayX + 8, TaskbarY + 12, "MinNT 6.1", COLOR_BLACK, COLOR_TRAY);

    for (ULONG i = 0; i < NUM_ICONS; i++) {
        DESKTOP_ICON *ic = &Icons[i];
        UCHAR bg = ic->selected ? COLOR_YELLOW : COLOR_ICON_BG;
        HalpFbFillRect(ic->X, ic->Y, ICON_W, ICON_H, bg);
        HalpFbDrawRect(ic->X, ic->Y, ICON_W, ICON_H, COLOR_WHITE);
        HalpFbDrawString(ic->X, ic->Y + 52, ic->Label, COLOR_ICON_TEXT, COLOR_DESKTOP_BG);
    }

    if (W > 400) {
        HalpFbDrawString(120, 20, "MinNT 6.1", COLOR_WHITE, COLOR_DESKTOP_BG);
        HalpFbDrawString(120, 40, "Click icon to open | Start menu for programs", COLOR_YELLOW, COLOR_DESKTOP_BG);
    }
}

/* ============================================================================
 * MOUSE CURSOR
 * ============================================================================ */

static VOID DrawMouseCursor(SHORT OldX, SHORT OldY)
{
    UNREFERENCED_PARAMETER(OldX);
    UNREFERENCED_PARAMETER(OldY);

    volatile ULONG *Fb = HalpFbGetBase();
    ULONG W = HalpFbGetWidth();
    ULONG H = HalpFbGetHeight();
    SHORT mx = MouseX;
    SHORT my = MouseY;

    for (ULONG ry = 0; ry < 16; ry++) {
        for (ULONG rx = 0; rx < 16; rx++) {
            UCHAR bits = MouseCursorBitmap[ry * 2 + rx / 8];
            if ((bits & (0x80 >> (rx % 8))) == 0) continue;
            ULONG px = (ULONG)mx + rx;
            ULONG py = (ULONG)my + ry;
            if (px >= W || py >= H) continue;
            Fb[py * W + px] ^= 0x00FFFFFF;
        }
    }
}

/* ============================================================================
 * ICON MANAGEMENT
 * ============================================================================ */

static VOID InvertIconArea(DESKTOP_ICON *ic)
{
    UCHAR bg = ic->selected ? COLOR_YELLOW : COLOR_ICON_BG;
    HalpFbFillRect(ic->X, ic->Y, ICON_W, ICON_H, bg);
    HalpFbDrawRect(ic->X, ic->Y, ICON_W, ICON_H, COLOR_WHITE);
    HalpFbDrawString(ic->X, ic->Y + 52, ic->Label, COLOR_ICON_TEXT, COLOR_DESKTOP_BG);
}

static LONG HitTestIcons(SHORT mx, SHORT my)
{
    for (LONG i = (LONG)NUM_ICONS - 1; i >= 0; i--) {
        DESKTOP_ICON *ic = &Icons[i];
        if (mx >= (SHORT)ic->X && mx < (SHORT)(ic->X + ICON_W) &&
            my >= (SHORT)ic->Y && my < (SHORT)(ic->Y + ICON_H)) {
            return i;
        }
    }
    return -1;
}

static BOOLEAN HitTestStartBtn(SHORT mx, SHORT my)
{
    ULONG H = HalpFbGetHeight();
    ULONG TaskbarY = H - TASKBAR_H;
    return (mx >= 4 && mx < 4 + START_BTN_W &&
            my >= (SHORT)TaskbarY + 4 && my < (SHORT)TaskbarY + 4 + START_BTN_H);
}

/* ============================================================================
 * START MENU
 * ============================================================================ */

static VOID SaveStartMenuBg(ULONG x, ULONG y, ULONG w, ULONG h)
{
    volatile ULONG *Fb = HalpFbGetBase();
    ULONG FbW = HalpFbGetWidth();
    for (ULONG row = 0; row < h; row++) {
        for (ULONG col = 0; col < w; col++) {
            StartMenuBg[row * w + col] = Fb[(y + row) * FbW + (x + col)];
        }
    }
}

static VOID RestoreStartMenuBg(ULONG x, ULONG y, ULONG w, ULONG h)
{
    volatile ULONG *Fb = HalpFbGetBase();
    ULONG FbW = HalpFbGetWidth();
    for (ULONG row = 0; row < h; row++) {
        for (ULONG col = 0; col < w; col++) {
            Fb[(y + row) * FbW + (x + col)] = StartMenuBg[row * w + col];
        }
    }
}

static VOID DrawStartMenu(VOID)
{
    ULONG H = HalpFbGetHeight();
    ULONG TaskbarY = H - TASKBAR_H;
    StartMenuY = (SHORT)(TaskbarY - START_MENU_TOTAL_H);
    ULONG x = 4;
    ULONG y = (ULONG)StartMenuY;
    ULONG menuH = START_MENU_TOTAL_H;

    SaveStartMenuBg(x, y, START_MENU_W, menuH);
    HalpFbFillRect(x, y, START_MENU_W, menuH, COLOR_TASKBAR);
    HalpFbDrawRect(x, y, START_MENU_W, menuH, COLOR_BLACK);

    ULONG iy = y + START_MENU_PAD;
    for (ULONG i = 0; i < NUM_START_ITEMS; i++) {
        if (StartMenuSeparators[i]) {
            HalpFbFillRect(x + 4, iy + START_MENU_ITEM_H / 2,
                           (ULONG)(START_MENU_W - 8), 1, COLOR_TASKBAR_EDGE);
        } else {
            BOOLEAN hover = ((SHORT)MouseX >= (SHORT)x && (SHORT)MouseX < (SHORT)(x + START_MENU_W) &&
                             (SHORT)MouseY >= (SHORT)iy && (SHORT)MouseY < (SHORT)(iy + START_MENU_ITEM_H));
            ULONG bg = hover ? COLOR_ICON_BG : COLOR_TASKBAR;
            ULONG fg = hover ? COLOR_WHITE : COLOR_BLACK;
            HalpFbFillRect(x + 2, iy + 2, (ULONG)(START_MENU_W - 4), (ULONG)(START_MENU_ITEM_H - 4), bg);
            HalpFbDrawString(x + 8, iy + 6, StartMenuItems[i], fg, bg);
        }
        iy += START_MENU_ITEM_H;
    }

    StartMenuOpen = TRUE;
}

static VOID CloseStartMenu(VOID)
{
    if (!StartMenuOpen) return;
    RestoreStartMenuBg(4, (ULONG)StartMenuY, START_MENU_W, START_MENU_TOTAL_H);
    StartMenuOpen = FALSE;
}

static LONG HitTestStartMenu(SHORT mx, SHORT my)
{
    if (!StartMenuOpen) return -1;
    if (mx < 4 || mx >= 4 + (SHORT)START_MENU_W) return -1;
    if (my < StartMenuY || my >= (SHORT)((ULONG)StartMenuY + START_MENU_TOTAL_H)) return -1;

    LONG item = (my - StartMenuY - START_MENU_PAD) / START_MENU_ITEM_H;
    if (item < 0 || item >= (LONG)NUM_START_ITEMS) return -1;
    if (StartMenuSeparators[item]) return -1;
    return item;
}

static VOID HandleStartMenuItem(LONG item)
{
    DbgPrint("EXPLORER: Start menu [%d] '%s'\n", item, StartMenuItems[item]);
}

/* ============================================================================
 * WINDOW MANAGER
 * ============================================================================ */

static VOID InitWindowManager(VOID)
{
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        g_Windows[i].open = FALSE;
        g_Windows[i].hwnd = NULL;
        g_Windows[i].bg_pixels = NULL;
    }
    g_NumOpenWindows = 0;
    g_DragActive = FALSE;
    g_DragWin = NULL;
}

static WIN_O *AllocWindowSlot(ULONG id, const CHAR *title, ULONG cx, ULONG cy)
{
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        if (!g_Windows[i].open) {
            WIN_O *win = &g_Windows[i];
            RtlZeroMemory(win, sizeof(WIN_O));
            win->id = id;
            win->title = title;
            win->cx = cx;
            win->cy = cy;
            win->x = 100 + (i * 30);
            win->y = 80  + (i * 30);
            win->open = TRUE;
            win->active = TRUE;
            win->dirty = TRUE;
            win->bg_pixels = NULL;
            win->bg_width = 0;
            win->bg_height = 0;
            g_NumOpenWindows++;
            return win;
        }
    }
    return NULL;
}

static WIN_O *FindWindowById(ULONG id)
{
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        if (g_Windows[i].open && g_Windows[i].id == id)
            return &g_Windows[i];
    }
    return NULL;
}

static VOID SaveWindowBg(WIN_O *win)
{
    volatile ULONG *Fb = HalpFbGetBase();
    ULONG FbW = HalpFbGetWidth();
    ULONG w = win->cx;
    ULONG h = win->cy;

    if (win->bg_pixels) ExFreePool((PVOID)win->bg_pixels);

    win->bg_pixels = ExAllocatePool(NonPagedPool, w * h * sizeof(ULONG));
    if (!win->bg_pixels) return;

    win->bg_width = w;
    win->bg_height = h;

    for (ULONG row = 0; row < h; row++) {
        for (ULONG col = 0; col < w; col++) {
            win->bg_pixels[row * w + col] = Fb[(win->y + row) * FbW + (win->x + col)];
        }
    }
}

static VOID RestoreWindowBg(WIN_O *win)
{
    if (!win->bg_pixels) return;
    volatile ULONG *Fb = HalpFbGetBase();
    ULONG FbW = HalpFbGetWidth();
    ULONG w = win->bg_width;
    ULONG h = win->bg_height;

    for (ULONG row = 0; row < h; row++) {
        for (ULONG col = 0; col < w; col++) {
            Fb[(win->y + row) * FbW + (win->x + col)] = win->bg_pixels[row * w + col];
        }
    }

    ExFreePool((PVOID)win->bg_pixels);
    win->bg_pixels = NULL;
}

static VOID DrawWindowFrame(WIN_O *win)
{
    ULONG titlebar_h = WIN_TITLEBAR_H;
    ULONG border_w  = WIN_BORDER_W;
    UCHAR title_color = win->active ? COLOR_TITLEBAR : COLOR_TITLEBAR_INACTIVE;

    /* Background + border */
    HalpFbFillRect(win->x, win->y, win->cx, win->cy, COLOR_WIN_BG);
    HalpFbDrawRect(win->x, win->y, win->cx, win->cy, COLOR_BLACK);

    /* Title bar */
    HalpFbFillRect(win->x + border_w, win->y + border_w,
                   win->cx - border_w * 2, titlebar_h, title_color);
    HalpFbDrawRect(win->x + border_w, win->y + border_w,
                   win->cx - border_w * 2, titlebar_h, COLOR_BLACK);

    /* Title text */
    HalpFbDrawString(win->x + border_w + 4, win->y + border_w + 4,
                     win->title, COLOR_WHITE, title_color);

    /* Close button area: 20x16 in top-right of title bar */
    ULONG close_x = win->x + win->cx - border_w - 20 - 2;
    ULONG close_y = win->y + border_w + 2;
    BOOLEAN close_hover = ((SHORT)MouseX >= (SHORT)close_x && (SHORT)MouseX < (SHORT)(close_x + 20) &&
                            (SHORT)MouseY >= (SHORT)close_y && (SHORT)MouseY < (SHORT)(close_y + titlebar_h - 4));
    ULONG close_bg = close_hover ? COLOR_CLOSE_HOVER : COLOR_CLOSE_BTN;
    HalpFbFillRect(close_x, close_y, 20, titlebar_h - 4, close_bg);
    HalpFbDrawRect(close_x, close_y, 20, titlebar_h - 4, COLOR_BLACK);
    HalpFbDrawString(close_x + 4, close_y + 3, "X", COLOR_WHITE, close_bg);

    /* Content area separator line */
    ULONG content_y = win->y + border_w + titlebar_h;
    HalpFbFillRect(win->x + border_w, content_y,
                   win->cx - border_w * 2, 1, COLOR_TASKBAR_EDGE);
}

static VOID DrawAllWindows(VOID)
{
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        WIN_O *win = &g_Windows[i];
        if (!win->open) continue;
        DrawWindowFrame(win);
        if (win->dirty) {
            win->dirty = FALSE;
        }
    }
}

static LONG HitTestWindows(SHORT mx, SHORT my)
{
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        WIN_O *win = &g_Windows[i];
        if (!win->open) continue;
        if (mx >= (SHORT)win->x && mx < (SHORT)(win->x + (LONG)win->cx) &&
            my >= (SHORT)win->y && my < (SHORT)(win->y + (LONG)win->cy)) {
            return (LONG)i;
        }
    }
    return -1;
}

static BOOLEAN HitTestCloseBtn(WIN_O *win, SHORT mx, SHORT my)
{
    ULONG close_x = win->x + WIN_BORDER_W + win->cx - WIN_BORDER_W - 20 - 2;
    ULONG close_y = win->y + WIN_BORDER_W + 2;
    return (mx >= (SHORT)close_x && mx < (SHORT)(close_x + 20) &&
            my >= (SHORT)close_y && my < (SHORT)(close_y + WIN_TITLEBAR_H - 4));
}

static BOOLEAN HitTestTitleBar(WIN_O *win, SHORT mx, SHORT my)
{
    ULONG titlebar_y = win->y + WIN_BORDER_W;
    return (mx >= (SHORT)(win->x + WIN_BORDER_W) && mx < (SHORT)(win->x + (LONG)win->cx - WIN_BORDER_W) &&
            my >= (SHORT)titlebar_y && my < (SHORT)(titlebar_y + WIN_TITLEBAR_H));
}

static VOID FreeWindowSlot(WIN_O *win)
{
    RestoreWindowBg(win);
    win->open = FALSE;
    win->hwnd = NULL;
    g_NumOpenWindows--;
}

static VOID OpenWindowForIcon(ULONG icon_id)
{
    ULONG win_w = 420;
    ULONG win_h = 320;
    const CHAR *title = NULL;
    LONG_PTR wndResult = 0;

    switch (icon_id) {
        case 0: title = "My Computer";   break;
        case 1: title = "My Documents"; break;
        case 2: title = "Recycle Bin";   break;
        case 3: title = "Internet";      break;
        case 4: title = "Network";      break;
        default: return;
    }

    WIN_O *win = AllocWindowSlot(icon_id, title, win_w, win_h);
    if (!win) {
        DbgPrint("EXPLORER: no free window slot for '%s'\n", title);
        return;
    }

    SaveWindowBg(win);
    DrawWindowFrame(win);

    /* Post WM_CREATE to the window's message queue */
    UserPostMessage((ULONG_PTR)win->hwnd, WM_CREATE, 0, 0);

    DbgPrint("EXPLORER: opened window '%s' at (%d,%d) %ux%u\n",
             title, win->x, win->y, win->cx, win->cy);
}

/* ============================================================================
 * WINDOW PROCEDURES — each icon type has its own wndproc
 * ============================================================================ */

static LONG_PTR WndprocMyComputer(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (msg) {
        case WM_CREATE: {
            DbgPrint("WNDPROC: MyComputer WM_CREATE hwnd=%p\n", (PVOID)hwnd);
            break;
        }
        case WM_PAINT: {
            HDC hdc = (HDC)UserBeginPaint((ULONG_PTR)hwnd, NULL);
            if (hdc) {
                WIN_O *win = NULL;
                for (ULONG i = 0; i < MAX_WINDOWS; i++) {
                    if (g_Windows[i].hwnd == (WINDOW*)hwnd) { win = &g_Windows[i]; break; }
                }
                if (win) {
                    ULONG cx = win->cx - WIN_BORDER_W * 2;
                    ULONG cy = win->cy - WIN_BORDER_W - WIN_TITLEBAR_H;
                    ULONG ox = win->x + WIN_BORDER_W;
                    ULONG oy = win->y + WIN_BORDER_W + WIN_TITLEBAR_H;
                    GdiPatBlt((ULONG_PTR)hdc, WIN_BORDER_W, WIN_BORDER_W + WIN_TITLEBAR_H,
                              cx, cy, PATCOPY);
                    GdiExtTextOutW((ULONG_PTR)hdc, ox + 12, oy + 12, 0, NULL,
                                   (ULONG_PTR)L"My Computer", 12, NULL);
                    GdiExtTextOutW((ULONG_PTR)hdc, ox + 12, oy + 32, 0, NULL,
                                   (ULONG_PTR)L"Intel(R) Core(TM) i7-7700K", 29, NULL);
                    GdiExtTextOutW((ULONG_PTR)hdc, ox + 12, oy + 50, 0, NULL,
                                   (ULONG_PTR)L"8.00 GB RAM", 11, NULL);
                    GdiExtTextOutW((ULONG_PTR)hdc, ox + 12, oy + 68, 0, NULL,
                                   (ULONG_PTR)L"MinNT 6.1 Build 7601", 21, NULL);
                }
            }
            UserEndPaint((ULONG_PTR)hwnd, NULL);
            break;
        }
        case WM_CLOSE: {
            DbgPrint("WNDPROC: MyComputer WM_CLOSE hwnd=%p\n", (PVOID)hwnd);
            UserDestroyWindow((ULONG_PTR)hwnd);
            break;
        }
        default:
            break;
    }
    return 0;
}

static LONG_PTR WndprocRecycleBin(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(wParam);

    switch (msg) {
        case WM_CREATE:
            DbgPrint("WNDPROC: RecycleBin WM_CREATE hwnd=%p\n", (PVOID)hwnd);
            break;
        case WM_PAINT: {
            HDC hdc = (HDC)UserBeginPaint((ULONG_PTR)hwnd, NULL);
            if (hdc) {
                GdiPatBlt((ULONG_PTR)hdc, WIN_BORDER_W, WIN_BORDER_W + WIN_TITLEBAR_H,
                          396, 296, PATCOPY);
                GdiExtTextOutW((ULONG_PTR)hdc, 112, 46, 0, NULL,
                               (ULONG_PTR)L"Recycle Bin", 12, NULL);
            }
            UserEndPaint((ULONG_PTR)hwnd, NULL);
            break;
        }
        case WM_CLOSE:
            UserDestroyWindow((ULONG_PTR)hwnd);
            break;
    }
    return 0;
}

static LONG_PTR NTAPI WndprocDefault(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    switch (msg) {
        case WM_CREATE:
            DbgPrint("WNDPROC: Default WM_CREATE hwnd=%p\n", (PVOID)hwnd);
            break;
        case WM_PAINT: {
            HDC hdc = (HDC)UserBeginPaint((ULONG_PTR)hwnd, NULL);
            if (hdc) {
                GdiPatBlt((ULONG_PTR)hdc, WIN_BORDER_W, WIN_BORDER_W + WIN_TITLEBAR_H,
                          396, 296, PATCOPY);
            }
            UserEndPaint((ULONG_PTR)hwnd, NULL);
            break;
        }
        case WM_CLOSE:
            UserDestroyWindow((ULONG_PTR)hwnd);
            break;
        default:
            break;
    }
    return 0;
}

static LONG_PTR WndprocDispatcher(HWND hwnd, ULONG msg, ULONG_PTR wParam, LONG_PTR lParam)
{
    switch (msg) {
        case 0x0001: return WndprocMyComputer(hwnd, msg, wParam, lParam);
        case 0x0002: return WndprocRecycleBin(hwnd, msg, wParam, lParam);
        default:    return WndprocDefault(hwnd, msg, wParam, lParam);
    }
}

/* ============================================================================
 * WINDOW CLASS REGISTRATION
 * ============================================================================ */

static WCHAR g_ClassName0[MAX_CLASS_NAME];
static WCHAR g_ClassName1[MAX_CLASS_NAME];
static WCHAR g_ClassName2[MAX_CLASS_NAME];
static PCWSTR g_WinName0 = L"My Computer";
static PCWSTR g_WinName1 = L"My Documents";
static PCWSTR g_WinName2 = L"Recycle Bin";

static VOID RegisterWindowClasses(VOID)
{
    static W32K_WNDCLASS wc;

    RtlInitUnicodeString(&g_ClassName0, L"WndClassMyComputer");
    RtlInitUnicodeString(&g_ClassName1, L"WndClassMyDocuments");
    RtlInitUnicodeString(&g_ClassName2, L"WndClassRecycleBin");

    /* My Computer class */
    RtlZeroMemory(&wc, sizeof(wc));
    wc.lpszClassName = (PCWSTR)g_ClassName0;
    wc.lpfnWndProc   = WndprocMyComputer;
    wc.hInstance     = 0;
    wc.style         = 0;
    wc.hbrBackground = (HBRUSH)COLOR_WIN_BG;
    UserRegisterClassEx(0, &wc);

    /* My Documents class */
    RtlZeroMemory(&wc, sizeof(wc));
    wc.lpszClassName = (PCWSTR)g_ClassName1;
    wc.lpfnWndProc   = WndprocDefault;
    wc.hInstance     = 0;
    wc.style         = 0;
    wc.hbrBackground = (HBRUSH)COLOR_WIN_BG;
    UserRegisterClassEx(0, &wc);

    /* Recycle Bin class */
    RtlZeroMemory(&wc, sizeof(wc));
    wc.lpszClassName = (PCWSTR)g_ClassName2;
    wc.lpfnWndProc   = WndprocRecycleBin;
    wc.hInstance     = 0;
    wc.style         = 0;
    wc.hbrBackground = (HBRUSH)COLOR_WIN_BG;
    UserRegisterClassEx(0, &wc);

    UNICODE_STRING winName0, winName1, winName2;
    RtlInitUnicodeString(&winName0, L"My Computer");
    RtlInitUnicodeString(&winName1, L"My Documents");
    RtlInitUnicodeString(&winName2, L"Recycle Bin");

    DbgPrint("EXPLORER: window classes registered\n");
}

static VOID OpenWindowForIcon(ULONG icon_id);

static VOID RealOpenWindow(ULONG icon_id)
{
    PCWSTR class_name = NULL;
    PCWSTR win_title  = NULL;
    LONG x = 100 + (icon_id * 30);
    LONG y = 80  + (icon_id * 30);

    switch (icon_id) {
        case 0: class_name = (PCWSTR)g_ClassName0; win_title = g_WinName0; break;
        case 1: class_name = (PCWSTR)g_ClassName1; win_title = g_WinName1; break;
        case 2: class_name = (PCWSTR)g_ClassName2; win_title = g_WinName2; break;
        default: {
            return;
        }
    }

    /* Save background for all windows that are open */
    for (ULONG i = 0; i < MAX_WINDOWS; i++) {
        if (g_Windows[i].open) SaveWindowBg(&g_Windows[i]);
    }

    HWND hwnd = (HWND)UserCreateWindowEx(
        0,
        (ULONG_PTR)class_name,
        0,
        (ULONG_PTR)win_title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
        x, y,
        420, 320,
        0, 0, 0, 0, 0, 0
    );

    if ((ULONG_PTR)hwnd < 0x1000) {
        DbgPrint("EXPLORER: UserCreateWindowEx failed for icon %d\n", icon_id);
        return;
    }

    WIN_O *win = FindWindowById(icon_id);
    if (win) {
        win->hwnd = (WINDOW*)hwnd;
        DbgPrint("EXPLORER: RealOpenWindow(%lu) - win found, hwnd=%p\n", icon_id, (PVOID)(ULONG_PTR)hwnd);
        DrawWindowFrame(win);
        UserShowWindow((ULONG_PTR)hwnd, SW_SHOW);
        UserSetForegroundWindow((ULONG_PTR)hwnd);
        UserPostMessage((ULONG_PTR)hwnd, WM_PAINT, 0, 0);
    }

    DbgPrint("EXPLORER: opened window hwnd=%p title_ptr=%p\n", (PVOID)(ULONG_PTR)hwnd, win_title);
}

/* ============================================================================
 * DRAG — WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP
 * ============================================================================ */

static VOID StartDrag(WIN_O *win, SHORT mx, SHORT my)
{
    g_DragActive = TRUE;
    g_DragWin = win;
    g_DragWinX = (SHORT)win->x;
    g_DragWinY = (SHORT)win->y;
    g_DragMouseX = mx;
    g_DragMouseY = my;
}

static VOID UpdateDrag(SHORT mx, SHORT my)
{
    if (!g_DragActive || !g_DragWin) return;
    WIN_O *win = g_DragWin;

    SHORT dx = mx - g_DragMouseX;
    SHORT dy = my - g_DragMouseY;

    if (dx == 0 && dy == 0) return;

    RestoreWindowBg(win);
    win->x = (LONG)(g_DragWinX + dx);
    win->y = (LONG)(g_DragWinY + dy);
    g_DragWinX = (SHORT)win->x;
    g_DragWinY = (SHORT)win->y;
    SaveWindowBg(win);
    DrawWindowFrame(win);
}

static VOID EndDrag(VOID)
{
    g_DragActive = FALSE;
    g_DragWin = NULL;
}

/* ============================================================================
 * CLICK DISPATCHER — routes mouse clicks to appropriate handler
 * ============================================================================ */

static VOID HandleClick(SHORT mx, SHORT my)
{
    /* 1. Start menu items */
    LONG menuHit = HitTestStartMenu(mx, my);
    if (menuHit >= 0) {
        CloseStartMenu();
        HandleStartMenuItem(menuHit);
        return;
    }

    /* 2. Start button */
    if (HitTestStartBtn(mx, my)) {
        if (StartMenuOpen) CloseStartMenu();
        else DrawStartMenu();
        return;
    }

    /* 3. Close menu if open and click is elsewhere */
    if (StartMenuOpen) {
        CloseStartMenu();
        return;
    }

    /* 4. Windows — check close button first, then drag, then open windows */
    if (g_NumOpenWindows > 0) {
        LONG hit = HitTestWindows(mx, my);
        if (hit >= 0) {
            WIN_O *win = &g_Windows[hit];
            if (HitTestCloseBtn(win, mx, my)) {
                DbgPrint("EXPLORER: close button clicked on '%s'\n", win->title);
                FreeWindowSlot(win);
                return;
            }
            if (HitTestTitleBar(win, mx, my)) {
                StartDrag(win, mx, my);
                return;
            }
            /* Click in content area — post to window */
            if (win->hwnd) {
                UserPostMessage((ULONG_PTR)win->hwnd, WM_LBUTTONDOWN, 0, 0);
            }
            return;
        }
    }

    /* 5. Desktop icons */
    LONG iconHit = HitTestIcons(mx, my);
    if (iconHit >= 0) {
        for (ULONG j = 0; j < NUM_ICONS; j++)
            Icons[j].selected = FALSE;
        Icons[iconHit].selected = TRUE;
        for (ULONG j = 0; j < NUM_ICONS; j++)
            InvertIconArea(&Icons[j]);
        DbgPrint("EXPLORER: icon '%s' clicked — opening window\n", Icons[iconHit].Label);
        RealOpenWindow((ULONG)iconHit);
        return;
    }

    /* 6. Deselect all icons */
    BOOLEAN any_selected = FALSE;
    for (ULONG j = 0; j < NUM_ICONS; j++) {
        if (Icons[j].selected) any_selected = TRUE;
        Icons[j].selected = FALSE;
    }
    if (any_selected) {
        for (ULONG j = 0; j < NUM_ICONS; j++)
            InvertIconArea(&Icons[j]);
    }
}

/* ============================================================================
 * EXPLORER THREAD
 * ============================================================================ */

VOID NTAPI ExplorerThread(PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    SHORT OldMX = 512, OldMY = 384;

    DbgPrint("EXPLORER: starting graphical desktop...\n");

    if (!HalpFbIsActive()) {
        DbgPrint("EXPLORER: no framebuffer, text mode fallback\n");
        DbgPrint("EXPLORER: Welcome to MinNT!\n");
        for (;;) KiDispatchNextThread();
    }

    InitWindowManager();
    RegisterWindowClasses();
    DrawDesktop();
    MouseCursorVisible = TRUE;
    DbgPrint("EXPLORER: desktop rendered %ux%u\n",
             HalpFbGetWidth(), HalpFbGetHeight());

    for (;;) {
        SHORT curX = HalpMouseGetX();
        SHORT curY = HalpMouseGetY();

        /* Mouse moved */
        if (curX != MouseX || curY != MouseY) {
            if (MouseCursorVisible) DrawMouseCursor(OldMX, OldMY);
            MouseX = curX; MouseY = curY;
            OldMX = MouseX; OldMY = MouseY;
            DrawMouseCursor(OldMX, OldMY);
            MouseCursorVisible = TRUE;
        }

        /* Drag update — only when mouse has actually moved during drag */
        if (g_DragActive) {
            if (curX != g_DragMouseX || curY != g_DragMouseY) {
                UpdateDrag(curX, curY);
                g_DragMouseX = curX;
                g_DragMouseY = curY;
            }
        }

        /* Mouse button events */
        UCHAR status;
        CHAR dx, dy;
        if (HalpMouseGetEvent(&status, &dx, &dy)) {
            BOOLEAN leftDown = (status & 0x01) != 0;
            BOOLEAN leftPrev = (MousePrevButtons & 0x01) != 0;

            if (leftDown && !leftPrev) {
                HandleClick(MouseX, MouseY);
            }

            if (!leftDown && leftPrev) {
                EndDrag();
            }

            MousePrevButtons = status;
        }

        /* Keyboard — for headless testing (simulates mouse clicks) */
        if (HalpKbdHasKey()) {
            CHAR c = HalpKbdGetChar();
            if (c == '\r' || c == '\n') {
                DbgPrint("EXPLORER: key enter\n");
            } else if (c >= '1' && c <= '5') {
                ULONG iconIdx = (c - '1');
                if (iconIdx < NUM_ICONS) {
                    DbgPrint("EXPLORER: key '%c' -> simulating click on icon %lu '%s'\n",
                             c, iconIdx, Icons[iconIdx].Label);
                    HandleClick((SHORT)Icons[iconIdx].X + 24, (SHORT)Icons[iconIdx].Y + 24);
                }
            } else if (c == 's' || c == 'S') {
                if (StartMenuOpen) CloseStartMenu();
                else DrawStartMenu();
                DbgPrint("EXPLORER: key 'S' -> %s Start menu\n",
                         StartMenuOpen ? "opening" : "closing");
            } else if (c == 'd' || c == 'D') {
                DbgPrint("EXPLORER: key 'D' -> deselect all icons\n");
                for (ULONG j = 0; j < NUM_ICONS; j++) Icons[j].selected = FALSE;
                for (ULONG j = 0; j < NUM_ICONS; j++) InvertIconArea(&Icons[j]);
            } else if (c == 0x1B) {
                DbgPrint("EXPLORER: F12 -> RealOpenWindow(0) directly\n");
                RealOpenWindow(0);
            } else if (c == 'o' || c == 'O') {
                for (ULONG i = 0; i < NUM_ICONS; i++) {
                    DbgPrint("EXPLORER: key 'O' -> RealOpenWindow(%lu)\n", i);
                    RealOpenWindow(i);
                }
            }
        }

        KiDispatchNextThread();
    }
}