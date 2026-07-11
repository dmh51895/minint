/*
 * MinNT - win32k/win32k.c
 * Win32k.sys - kernel-mode win32 subsystem driver
 *
 * Implements GDI and USER syscalls for the graphical shell.
 * W32pServiceTable is indexed by (syscall_number - 0x1000).
 *
 * Actual implementations are in gdikernel.c for:
 * - GdiFlush, GdiCreateCompatibleDC, GdiCreateCompatibleBitmap
 * - GdiSelectBitmap, GdiDeleteObjectApp, GdiGetDCDword
 * - GdiGetDCPoint, GdiGetAppClipBox, GdiPatBlt, GdiRectangle
 * - GdiBitBlt
 * - GdiSetPixel, GdiGetPixel, GdiLineTo
 * - GdiExtTextOutW
 * - GdiCreateSolidBrush
 * - GdiCreatePen
 * - GdiGetTextExtent
 * - GdiGetAndSetDCDword
 * - UserGetDC, UserReleaseDC
 * - GdiCreateRectRgn
 * - GdiOffsetRgn
 * - GdiGetRgnBox
 * - GdiSaveDC, GdiRestoreDC
 * - GdiGetDCObject
 * - GdiGetTextMetricsW
 * - GdiExtSelectClipRgn
 * - GdiCombineRgn
 * - GdiIntersectClipRect
 * - GdiStretchBlt
 * - GdiSelectFont, GdiMaskBlt, GdiAlphaBlend, GdiTransformPoints
 * - UserGetWindowRect, UserGetClientRect
 *
 * Actual implementations are in usermsg.c for:
 * - UserPeekMessage, UserGetMessage, UserPostMessage
 * - UserTranslateMessage, UserDispatchMessage
 *
 * Actual implementations are in userwnd.c for:
 * - UserRegisterClassEx, UserCreateWindowEx, UserDestroyWindow
 * - UserShowWindow, UserSetWindowPos
 */

#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>

#include "win32k.h"

#define WIN32K_SYSCALL_BASE 0x1000

typedef NTSTATUS (NTAPI *W32P_SERVICE)(ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR);

/*** Actual GDI implementations from gdikernel.c ***/
NTSTATUS APIENTRY GdiFlush(VOID);
NTSTATUS APIENTRY GdiCreateCompatibleDC(ULONG_PTR hdc);
NTSTATUS APIENTRY GdiCreateCompatibleBitmap(ULONG_PTR hdc, ULONG Width, ULONG Height);
NTSTATUS APIENTRY GdiSelectBitmap(ULONG_PTR hdc, ULONG_PTR hBitmap);
NTSTATUS APIENTRY GdiDeleteObjectApp(ULONG_PTR hObj);
NTSTATUS APIENTRY GdiGetDCDword(ULONG_PTR hdc, ULONG Index, ULONG Value);
NTSTATUS APIENTRY GdiGetDCPoint(ULONG_PTR hdc, ULONG Index, PW32K_POINT Point);
NTSTATUS APIENTRY GdiGetAppClipBox(ULONG_PTR hdc, PW32K_RECT Rect);
NTSTATUS APIENTRY GdiPatBlt(ULONG_PTR hdc, LONG Left, LONG Top, LONG Width, LONG Height, ULONG RasterOp);
NTSTATUS APIENTRY GdiRectangle(ULONG_PTR hdc, LONG Left, LONG Top, LONG Right, LONG Bottom);
NTSTATUS APIENTRY GdiBitBlt(ULONG_PTR hDCDest, LONG XDest, LONG YDest,
                            LONG Width, LONG Height, ULONG_PTR hDCSrc,
                            LONG XSrc, LONG YSrc, ULONG RasterOp);
NTSTATUS APIENTRY GdiSetPixel(ULONG_PTR hdc, LONG x, LONG y, ULONG Color);
NTSTATUS APIENTRY GdiGetPixel(ULONG_PTR hdc, LONG x, LONG y);
NTSTATUS APIENTRY GdiLineTo(ULONG_PTR hdc, LONG x, LONG y);
NTSTATUS APIENTRY GdiExtTextOutW(ULONG_PTR hdc, INT XStart, INT YStart,
                                  ULONG fuOptions, ULONG_PTR UnsafeRect,
                                  ULONG_PTR UnsafeString, INT Count,
                                  ULONG_PTR UnsafeDx);
NTSTATUS APIENTRY GdiCreateSolidBrush(ULONG Color);
NTSTATUS APIENTRY GdiCreatePen(ULONG Style, ULONG Width, ULONG Color);
NTSTATUS APIENTRY GdiGetTextExtent(ULONG_PTR hdc, ULONG_PTR lpwsz, INT cwc, ULONG_PTR psize, ULONG flOpts);
NTSTATUS APIENTRY GdiGetAndSetDCDword(ULONG_PTR hdc, ULONG Index, ULONG Value, ULONG_PTR Result);
NTSTATUS APIENTRY UserGetDC(ULONG_PTR hWnd);
NTSTATUS APIENTRY UserReleaseDC(ULONG_PTR hWnd, ULONG_PTR hdc);
NTSTATUS APIENTRY GdiCreateRectRgn(LONG LeftRect, LONG TopRect, LONG RightRect, LONG BottomRect);
NTSTATUS APIENTRY GdiOffsetRgn(ULONG_PTR hrgn, INT cx, INT cy);
NTSTATUS APIENTRY GdiGetRgnBox(ULONG_PTR hrgn, ULONG_PTR pRect);
NTSTATUS APIENTRY GdiSaveDC(ULONG_PTR hdc);
NTSTATUS APIENTRY GdiRestoreDC(ULONG_PTR hdc, INT iSaveLevel);
NTSTATUS APIENTRY GdiGetDCObject(ULONG_PTR hdc, INT ObjectType);
NTSTATUS APIENTRY GdiGetTextMetricsW(ULONG_PTR hdc, ULONG_PTR pTm, ULONG cj);
NTSTATUS APIENTRY GdiExtSelectClipRgn(ULONG_PTR hdc, ULONG_PTR hrgn, INT fnMode);
NTSTATUS APIENTRY GdiCombineRgn(ULONG_PTR hrgnDst, ULONG_PTR hrgnSrc1, ULONG_PTR hrgnSrc2, INT iMode);
HFONT NTAPI GdiSelectFont(ULONG_PTR hdc, HFONT hfont);
BOOL NTAPI GdiMaskBlt(ULONG_PTR hDCDest, LONG xDest, LONG yDest, LONG cxDest, LONG cyDest,
                       ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, ULONG rop, ULONG maskRop,
                       ULONG_PTR hbmMask, ULONG plane, ULONG_PTR hdcPaletteDest,
                       ULONG_PTR hdcPaletteSrc);
BOOL NTAPI GdiAlphaBlend(ULONG_PTR hDCDest, LONG xDest, LONG yDest, LONG cxDest, LONG cyDest,
                          ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc,
                          ULONG blendfn);
INT NTAPI GdiTransformPoints(ULONG_PTR hdc, ULONG_PTR pPtIn, ULONG_PTR pPtOut, INT cPts, ULONG iMode);
NTSTATUS APIENTRY GdiIntersectClipRect(ULONG_PTR hdc, LONG xLeft, LONG yTop, LONG xRight, LONG yBottom);
NTSTATUS APIENTRY GdiStretchBlt(ULONG_PTR hDCDest, LONG xDst, LONG yDst, LONG cxDst, LONG cyDst,
                                ULONG_PTR hDCSrc, LONG xSrc, LONG ySrc, LONG cxSrc, LONG cySrc,
                                ULONG dwRop, ULONG dwBackColor);

/*** Actual USER implementations from usermsg.c ***/
NTSTATUS APIENTRY UserPeekMessage(PW32K_MSG Msg, ULONG_PTR hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax, ULONG RemoveMsg);
NTSTATUS APIENTRY UserGetMessage(PW32K_MSG Msg, ULONG_PTR hWnd, ULONG MsgFilterMin, ULONG MsgFilterMax);
NTSTATUS APIENTRY UserPostMessage(ULONG_PTR hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
NTSTATUS APIENTRY UserTranslateMessage(PW32K_MSG Msg, ULONG Flags);
NTSTATUS APIENTRY UserDispatchMessage(PW32K_MSG Msg);
LONG_PTR APIENTRY UserSendMessage(ULONG_PTR hWnd, ULONG Msg, ULONG_PTR wParam, LONG_PTR lParam);
VOID NTAPI Win32kInitMessageQueue(VOID);
VOID NTAPI Win32kInitClassTable(VOID);
NTSTATUS APIENTRY UserRegisterClassEx(ULONG_PTR hWndParent, PW32K_WNDCLASS pWndClass);
ULONG_PTR NTAPI UserGetThreadState(ULONG Routine);
NTSTATUS APIENTRY UserSetThreadState(ULONG Routine, ULONG_PTR Flags);
NTSTATUS APIENTRY UserBeginPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs);
NTSTATUS APIENTRY UserEndPaint(ULONG_PTR hWnd, PW32K_PAINTSTRUCT pPs);
NTSTATUS APIENTRY UserCreateWindowEx(ULONG dwExStyle, ULONG_PTR plstrClassName,
                                     ULONG_PTR plstrClsVersion, ULONG_PTR plstrWindowName,
                                     ULONG dwStyle, LONG x, LONG y, LONG nWidth, LONG nHeight,
                                     ULONG_PTR hWndParent, ULONG_PTR hMenu, ULONG_PTR hInstance,
                                     ULONG_PTR lpParam, ULONG dwFlags, ULONG_PTR acbiBuffer);
NTSTATUS APIENTRY UserShowWindow(ULONG_PTR hWnd, LONG nCmdShow);
NTSTATUS APIENTRY UserSetWindowPos(ULONG_PTR hWnd, ULONG_PTR hWndInsertAfter, LONG x,
                                    LONG y, LONG cx, LONG cy, ULONG fFlags);
NTSTATUS APIENTRY UserDestroyWindow(ULONG_PTR hWnd);
NTSTATUS APIENTRY UserInvalidateRect(ULONG_PTR hWnd, ULONG_PTR lpRect, BOOL bErase);
ULONG_PTR NTAPI UserGetForegroundWindow(VOID);
NTSTATUS APIENTRY UserSetForegroundWindow(ULONG_PTR hWnd);
ULONG_PTR NTAPI UserSetCapture(ULONG_PTR hWnd);
ULONG_PTR NTAPI UserSetFocus(ULONG_PTR hWnd);
ULONG NTAPI UserGetAsyncKeyState(INT vKey);
BOOL NTAPI UserGetWindowRect(ULONG_PTR hWnd, ULONG_PTR lpRect);
BOOL NTAPI UserGetClientRect(ULONG_PTR hWnd, ULONG_PTR lpRect);




static W32P_SERVICE W32pServiceTable[256];

VOID NTAPI Win32kInitTable(VOID)
{
    int i;
    for (i = 0; i < 256; i++)
        W32pServiceTable[i] = NULL;

    W32pServiceTable[0x00] = (W32P_SERVICE)UserGetThreadState;
    W32pServiceTable[0x01] = (W32P_SERVICE)UserPeekMessage;
    W32pServiceTable[0x06] = (W32P_SERVICE)UserGetMessage;
    W32pServiceTable[0x08] = (W32P_SERVICE)GdiBitBlt;
    W32pServiceTable[0x09] = (W32P_SERVICE)UserSendMessage;
    W32pServiceTable[0x0D] = (W32P_SERVICE)UserTranslateMessage;
    W32pServiceTable[0x0E] = (W32P_SERVICE)UserPostMessage;
    W32pServiceTable[0x0B] = (W32P_SERVICE)GdiSelectBitmap;
    W32pServiceTable[0x11] = (W32P_SERVICE)GdiFlush;
    W32pServiceTable[0x22] = (W32P_SERVICE)GdiDeleteObjectApp;
    W32pServiceTable[0x35] = (W32P_SERVICE)UserDispatchMessage;
    W32pServiceTable[0x37] = (W32P_SERVICE)GdiExtTextOutW;
    W32pServiceTable[0x38] = (W32P_SERVICE)GdiSelectFont;
    W32pServiceTable[0x39] = (W32P_SERVICE)GdiRestoreDC;
    W32pServiceTable[0x3A] = (W32P_SERVICE)GdiSaveDC;
    W32pServiceTable[0x40] = (W32P_SERVICE)GdiLineTo;
    W32pServiceTable[0x42] = (W32P_SERVICE)GdiGetAppClipBox;
    W32pServiceTable[0x47] = (W32P_SERVICE)GdiStretchBlt;
    W32pServiceTable[0x4A] = (W32P_SERVICE)GdiCreateCompatibleBitmap;
    W32pServiceTable[0x51] = (W32P_SERVICE)GdiGetTextExtent;
    W32pServiceTable[0x54] = (W32P_SERVICE)GdiCreateCompatibleDC;
    W32pServiceTable[0x56] = (W32P_SERVICE)GdiCreatePen;
    W32pServiceTable[0x59] = (W32P_SERVICE)GdiPatBlt;
    W32pServiceTable[0x5C] = (W32P_SERVICE)GdiGetTextMetricsW;
    W32pServiceTable[0x66] = (W32P_SERVICE)GdiGetRgnBox;
    W32pServiceTable[0x67] = (W32P_SERVICE)GdiGetAndSetDCDword;
    W32pServiceTable[0x68] = (W32P_SERVICE)GdiMaskBlt;
    W32pServiceTable[0x6C] = (W32P_SERVICE)GdiTransformPoints;
    W32pServiceTable[0x7D] = (W32P_SERVICE)GdiAlphaBlend;
    W32pServiceTable[0x7F] = (W32P_SERVICE)GdiOffsetRgn;
    W32pServiceTable[0x81] = (W32P_SERVICE)GdiGetDCPoint;
    W32pServiceTable[0x85] = (W32P_SERVICE)GdiCreateRectRgn;
    W32pServiceTable[0x91] = (W32P_SERVICE)GdiRectangle;
    W32pServiceTable[0x93] = (W32P_SERVICE)GdiExtSelectClipRgn;
    W32pServiceTable[0x95] = (W32P_SERVICE)GdiIntersectClipRect;
    W32pServiceTable[0x9B] = (W32P_SERVICE)GdiCombineRgn;
    W32pServiceTable[0x9D] = (W32P_SERVICE)GdiGetDCDword;
    W32pServiceTable[0xA7] = (W32P_SERVICE)GdiGetPixel;
    W32pServiceTable[0xAC] = (W32P_SERVICE)GdiSetPixel;
    W32pServiceTable[0xAF] = (W32P_SERVICE)GdiCreateSolidBrush;
    W32pServiceTable[0xB3] = (W32P_SERVICE)GdiGetTextMetricsW;
    W32pServiceTable[0x23] = (W32P_SERVICE)UserSetWindowPos;
    W32pServiceTable[0x57] = (W32P_SERVICE)UserShowWindow;
    W32pServiceTable[0x77] = (W32P_SERVICE)UserCreateWindowEx;
    W32pServiceTable[0x9E] = (W32P_SERVICE)UserDestroyWindow;
    W32pServiceTable[0xB4] = (W32P_SERVICE)UserRegisterClassEx;
    W32pServiceTable[0x16] = (W32P_SERVICE)UserBeginPaint;
    W32pServiceTable[0x18] = (W32P_SERVICE)UserEndPaint;
    W32pServiceTable[0x04] = (W32P_SERVICE)UserInvalidateRect;
    W32pServiceTable[0x2C] = (W32P_SERVICE)UserGetForegroundWindow;
    W32pServiceTable[0x2D] = (W32P_SERVICE)UserSetForegroundWindow;
    W32pServiceTable[0x43] = (W32P_SERVICE)UserGetAsyncKeyState;
    W32pServiceTable[0x48] = (W32P_SERVICE)UserSetCapture;
    W32pServiceTable[0x50] = (W32P_SERVICE)UserSetFocus;
    W32pServiceTable[0x49] = (W32P_SERVICE)UserGetWindowRect;
    W32pServiceTable[0x4B] = (W32P_SERVICE)UserGetClientRect;
}

NTSTATUS NTAPI Win32kSyscallDispatcher(ULONG SyscallNumber,
                                       ULONG_PTR Arg1, ULONG_PTR Arg2, ULONG_PTR Arg3,
                                       ULONG_PTR Arg4, ULONG_PTR Arg5, ULONG_PTR Arg6,
                                       ULONG_PTR Arg7, ULONG_PTR Arg8, ULONG_PTR Arg9,
                                       ULONG_PTR Arg10, ULONG_PTR Arg11, ULONG_PTR Arg12,
                                       ULONG_PTR Arg13, ULONG_PTR Arg14, ULONG_PTR Arg15)
{
    ULONG Index = SyscallNumber - WIN32K_SYSCALL_BASE;

    if (Index >= 256) {
        DbgPrint("WIN32K: syscall 0x%04X out of range\n", SyscallNumber);
        return STATUS_INVALID_HANDLE;
    }

    if (!W32pServiceTable[Index]) {
        DbgPrint("WIN32K: syscall 0x%04X (index 0x%02X) unregistered\n", SyscallNumber, Index);
        return STATUS_INVALID_HANDLE;
    }

    return W32pServiceTable[Index](Arg1, Arg2, Arg3, Arg4, Arg5, Arg6,
                                    Arg7, Arg8, Arg9, Arg10, Arg11, Arg12,
                                    Arg13, Arg14, Arg15);
}

DRIVER_OBJECT *Win32kDriverObject = NULL;

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("WIN32K: DriverEntry called\n");

    Win32kDriverObject = DriverObject;
    Win32kInitTable();
    Win32kInitMessageQueue();
    Win32kInitClassTable();

    DbgPrint("WIN32K: W32pServiceTable at %p\n", W32pServiceTable);
    DbgPrint("WIN32K: Win32kSyscallDispatcher at %p\n", Win32kSyscallDispatcher);
    DbgPrint("WIN32K: loaded\n");

    return STATUS_SUCCESS;
}