/*
 * MinNT - win32k/profile.c
 * User profile management for Win32k.
 */

#include "precomp.h"
#include <ndk/rtlfuncs.h>

#define MAX_PROFILE_ENTRIES 64
#define MAX_PROFILE_NAME    128
#define MAX_PROFILE_VALUE   256

typedef struct _PROFILE_ENTRY {
    WCHAR Name[MAX_PROFILE_NAME];
    WCHAR Value[MAX_PROFILE_VALUE];
    BOOLEAN InUse;
} PROFILE_ENTRY, *PPROFILE_ENTRY;

static PROFILE_ENTRY g_ProfileTable[MAX_PROFILE_ENTRIES];

NTSTATUS NTAPI ProfileInit(VOID)
{
    RtlZeroMemory(g_ProfileTable, sizeof(g_ProfileTable));

    /* Set default profile values */
    RtlCopyMemory(g_ProfileTable[0].Name, L"Wallpaper", 10 * sizeof(WCHAR));
    RtlCopyMemory(g_ProfileTable[0].Value, L"", sizeof(WCHAR));
    g_ProfileTable[0].InUse = TRUE;

    RtlCopyMemory(g_ProfileTable[1].Name, L"ShellFolder", 12 * sizeof(WCHAR));
    RtlCopyMemory(g_ProfileTable[1].Value, L"C:\\Users\\Default\\Desktop", 27 * sizeof(WCHAR));
    g_ProfileTable[1].InUse = TRUE;

    DbgPrint("PROFILE: initialized (%d entries)\n", MAX_PROFILE_ENTRIES);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetProfileIntW(PCWSTR AppName, PCWSTR KeyName, INT Default, PINT pResult)
{
    ULONG i, appLen, keyLen;
    if (!pResult) return STATUS_INVALID_PARAMETER;

    *pResult = Default;

    appLen = 0;
    while (appLen < MAX_PROFILE_NAME - 1 && AppName[appLen]) appLen++;
    keyLen = 0;
    while (keyLen < MAX_PROFILE_NAME - 1 && KeyName[keyLen]) keyLen++;

    for (i = 0; i < MAX_PROFILE_ENTRIES; i++) {
        if (g_ProfileTable[i].InUse) {
            BOOLEAN appMatch = TRUE, keyMatch = TRUE;
            ULONG j;
            for (j = 0; j < appLen; j++) {
                if (g_ProfileTable[i].Name[j] != AppName[j]) { appMatch = FALSE; break; }
            }
            for (j = 0; j < keyLen; j++) {
                if (g_ProfileTable[i].Name[appLen + j] != KeyName[j]) { keyMatch = FALSE; break; }
            }
            if (appMatch && keyMatch) {
                /* Parse integer from wide value string */
                LONG val = 0;
                PWCHAR p = g_ProfileTable[i].Value;
                BOOLEAN neg = FALSE;
                if (*p == L'-') { neg = TRUE; p++; }
                while (*p >= L'0' && *p <= L'9') {
                    val = val * 10 + (*p - L'0');
                    p++;
                }
                if (neg) val = -val;
                *pResult = val;
                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserGetProfileStringW(PCWSTR AppName, PCWSTR KeyName, PCWSTR Default,
                                      PWCHAR ReturnedString, ULONG cchReturned, PULONG pcchReturned)
{
    ULONG i, appLen, keyLen;
    if (!ReturnedString || cchReturned == 0) return STATUS_INVALID_PARAMETER;

    appLen = 0;
    while (appLen < MAX_PROFILE_NAME - 1 && AppName[appLen]) appLen++;
    keyLen = 0;
    while (keyLen < MAX_PROFILE_NAME - 1 && KeyName[keyLen]) keyLen++;

    for (i = 0; i < MAX_PROFILE_ENTRIES; i++) {
        if (g_ProfileTable[i].InUse) {
            BOOLEAN appMatch = TRUE, keyMatch = TRUE;
            ULONG j, valLen;
            for (j = 0; j < appLen; j++) {
                if (g_ProfileTable[i].Name[j] != AppName[j]) { appMatch = FALSE; break; }
            }
            for (j = 0; j < keyLen; j++) {
                if (g_ProfileTable[i].Name[appLen + j] != KeyName[j]) { keyMatch = FALSE; break; }
            }
            if (appMatch && keyMatch) {
                valLen = 0;
                while (valLen < MAX_PROFILE_VALUE - 1 && g_ProfileTable[i].Value[valLen]) valLen++;
                if (valLen >= cchReturned) return STATUS_BUFFER_TOO_SMALL;
                RtlCopyMemory(ReturnedString, g_ProfileTable[i].Value, (valLen + 1) * sizeof(WCHAR));
                if (pcchReturned) *pcchReturned = valLen;
                return STATUS_SUCCESS;
            }
        }
    }

    /* Return default */
    if (Default) {
        ULONG defLen = 0;
        while (defLen < cchReturned - 1 && Default[defLen]) defLen++;
        RtlCopyMemory(ReturnedString, Default, (defLen + 1) * sizeof(WCHAR));
        if (pcchReturned) *pcchReturned = defLen;
    } else {
        ReturnedString[0] = 0;
        if (pcchReturned) *pcchReturned = 0;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI UserWriteProfileStringW(PCWSTR AppName, PCWSTR KeyName, PCWSTR String)
{
    ULONG i, appLen, keyLen, strLen;
    if (!AppName || !KeyName) return STATUS_INVALID_PARAMETER;

    appLen = 0;
    while (appLen < MAX_PROFILE_NAME - 1 && AppName[appLen]) appLen++;
    keyLen = 0;
    while (keyLen < MAX_PROFILE_NAME - 1 && KeyName[keyLen]) keyLen++;
    strLen = 0;
    while (strLen < MAX_PROFILE_VALUE - 1 && String && String[strLen]) strLen++;

    for (i = 0; i < MAX_PROFILE_ENTRIES; i++) {
        if (!g_ProfileTable[i].InUse) {
            RtlCopyMemory(g_ProfileTable[i].Name, AppName, appLen * sizeof(WCHAR));
            RtlCopyMemory(g_ProfileTable[i].Name + appLen, KeyName, keyLen * sizeof(WCHAR));
            g_ProfileTable[i].Name[appLen + keyLen] = 0;
            if (String) {
                RtlCopyMemory(g_ProfileTable[i].Value, String, (strLen + 1) * sizeof(WCHAR));
            } else {
                g_ProfileTable[i].Value[0] = 0;
            }
            g_ProfileTable[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS NTAPI UserGetProfileSectionW(PCWSTR AppName, PWCHAR Buffer, ULONG BufferLen, PULONG pBytesRet)
{
    ULONG i, appLen, totalLen = 0;
    if (!AppName || !Buffer) return STATUS_INVALID_PARAMETER;

    appLen = 0;
    while (appLen < MAX_PROFILE_NAME - 1 && AppName[appLen]) appLen++;

    for (i = 0; i < MAX_PROFILE_ENTRIES; i++) {
        if (g_ProfileTable[i].InUse) {
            BOOLEAN appMatch = TRUE;
            ULONG j, valLen;
            for (j = 0; j < appLen; j++) {
                if (g_ProfileTable[i].Name[j] != AppName[j]) { appMatch = FALSE; break; }
            }
            if (appMatch) {
                valLen = 0;
                while (valLen < MAX_PROFILE_VALUE - 1 && g_ProfileTable[i].Value[valLen]) valLen++;
                if (totalLen + valLen + 2 <= BufferLen) {
                    RtlCopyMemory(Buffer + totalLen, g_ProfileTable[i].Value, valLen * sizeof(WCHAR));
                    totalLen += valLen;
                    Buffer[totalLen++] = 0;
                }
            }
        }
    }
    Buffer[totalLen] = 0;
    if (pBytesRet) *pBytesRet = totalLen * sizeof(WCHAR);
    return STATUS_SUCCESS;
}
