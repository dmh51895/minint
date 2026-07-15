/*
 * MinNT - win32k/consrv/consrv.h
 * Console server internal header
 */

#ifndef _CONSRV_H_
#define _CONSRV_H_

#include <nt/ntdef.h>
#include <nt/ke.h>
#include <nt/io.h>
#include <nt/mm.h>
#include <nt/conio.h>

typedef struct _CONSOLE {
    ULONG               type;
    HANDLE              hProcess;
    HANDLE              ScreenBuffer;
    HANDLE              ActiveBuffer;
    ULONG               OutputCP;
    ULONG               InputCP;
    ULONG               ConsoleCP;
    COORD               ConsoleSize;
    COORD               BufferSize;
    SMALL_RECT          VisibleBounds;
    COORD               CurrentVirtualY;
    POINTL              CursorPosition;
    BOOL                CursorVisible;
    ULONG               CursorSize;
    ULONG               ScreenAttributes;
    ULONG               PopupAttributes;
    BOOL                QuickEdit;
    BOOL                AutoPosition;
    ULONG               HistoryBufferCount;
    ULONG               HistoryEditBufSize;
    HANDLE              HistoryBuffer;
    CONSOLE_SCREEN_BUFFER_INFO Info;
} CONSOLE;

typedef struct _CONSOLE_SCREEN_BUFFER {
    ULONG               type;
    CONSOLE            *Console;
    COORD              dwSize;
    COORD              dwCursorPosition;
    ULONG              wAttributes;
    SMALL_RECT         srWindow;
    COORD              dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER;

typedef struct _INPUT_RECORD {
    USHORT              EventType;
    union {
        KEY_EVENT_RECORD KeyEvent;
        MOUSE_EVENT_RECORD MouseEvent;
        WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
        FOCUS_EVENT_RECORD FocusEvent;
        MENU_EVENT_RECORD MenuEvent;
    } Event;
} INPUT_RECORD;

typedef struct _CHAR_INFO {
    WCHAR              UnicodeChar;
    USHORT             Attributes;
} CHAR_INFO;

#define EVENT_FOCUS        0x0010
#define EVENT_KEY          0x0001
#define EVENT_MOUSE        0x0002
#define EVENT_MOUSE_MOVE   0x0004
#define EVENT_BUFFER_SIZE  0x0008
#define EVENT_MENU         0x0080

#define CONSRV_MODE_ANSI     0x01
#define CONSRV_MODE_OEM      0x02
#define CONSRV_MODE_LINE_INPUT 0x04

extern CONSOLE *gConsoleListHead;

#endif /* _CONSRV_H_ */
