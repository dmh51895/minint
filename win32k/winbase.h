/*
 * MinNT - win32k/winbase.h
 * Windows base API definitions (Wine compatibility)
 */

#ifndef _WINBASE_H_
#define _WINBASE_H_

#include <nt/ntdef.h>

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

#define CREATE_NEW         1
#define CREATE_ALWAYS      2
#define OPEN_EXISTING      3
#define OPEN_ALWAYS        4
#define TRUNCATE_EXISTING  5

#define GENERIC_READ       0x80000000
#define GENERIC_WRITE      0x40000000
#define GENERIC_EXECUTE     0x20000000
#define GENERIC_ALL        0x10000000

#define FILE_ATTRIBUTE_NORMAL        0x00000080
#define FILE_ATTRIBUTE_HIDDEN        0x00000002
#define FILE_ATTRIBUTE_SYSTEM        0x00000004
#define FILE_ATTRIBUTE_DIRECTORY      0x00000010
#define FILE_ATTRIBUTE_ARCHIVE       0x00000020
#define FILE_ATTRIBUTE_READONLY     0x00000001

#define MB_OK                    0x00000000
#define MB_OKCANCEL              0x00000001
#define MB_ABORTRETRYIGNORE      0x00000002
#define MB_YESNOCANCEL           0x00000003
#define MB_YESNO                 0x00000004
#define MB_RETRYCANCEL          0x00000005

#define IDOK         1
#define IDCANCEL     2
#define IDABORT      3
#define IDRETRY      4
#define IDIGNORE     5
#define IDYES        6
#define IDNO         7
#define IDCLOSE      8
#define IDHELP       9

#define HANDLE WIN32K_POOL_HANDLE

typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    ULONG Offset;
    ULONG OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED;

typedef struct _SECURITY_ATTRIBUTES {
    ULONG nLength;
    PVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _WIN32_FIND_DATAW {
    ULONG dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    ULONG nFileSizeHigh;
    ULONG nFileSizeLow;
    ULONG dwReserved0;
    ULONG dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

#define FindFirstFileW(a,b) ((HANDLE)0)
#define FindNextFileW(a,b) 0
#define FindClose(a) 0
#define CreateFileW(a,b,c,d,e,f,g) ((HANDLE)-1)
#define ReadFile(a,b,c,d,e,f) 0
#define WriteFile(a,b,c,d,e,f) 0
#define CloseHandle(a) 1
#define GetLastError() 0

#endif /* _WINBASE_H_ */
