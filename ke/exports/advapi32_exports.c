/*
 * MinNT - ke/exports/advapi32_exports.c
 * advapi32.dll exports — registry, security, services.
 * Routes to Cm and Se kernel subsystems.
 */

#include <nt/ntdef.h>
#include <nt/ex.h>
#include <nt/ke.h>
#include <nt/cm.h>
#include <nt/se.h>
#include <nt/exe.h>
#include <nt/ob.h>
#include <ndk/obfuncs.h>
#include <stdarg.h>
#ifndef BYTE
typedef UCHAR BYTE;
#endif

#ifndef DWORD
typedef unsigned long DWORD;
#endif
#ifndef UINT
typedef unsigned int UINT;
#endif

/* ============================================================================
 * Registry — route to Cm subsystem
 * ========================================================================== */

__attribute__((ms_abi))
static LONG RegCreateKeyExA_msabi(ULONG_PTR hKey, const CHAR *lpSubKey,
    ULONG Reserved, CHAR *lpClass, ULONG dwOptions, ULONG samDesired,
    PVOID lpSecurityAttributes, PVOID phkResult, PVOID lpdwDisposition)
{
    /* Convert ANSI to Unicode */
    static WCHAR wsubkey[256];
    if (lpSubKey) {
        UINT i;
        for (i = 0; lpSubKey[i] && i < 255; i++) wsubkey[i] = (WCHAR)lpSubKey[i];
        wsubkey[i] = 0;
    }
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, wsubkey);
    PCM_KEY_NODE key;
    NTSTATUS s = CmCreateKey(&uniName, 0, &key);
    if (NT_SUCCESS(s)) {
        *(ULONG_PTR *)phkResult = (ULONG_PTR)key;
        if (lpdwDisposition) *(ULONG *)lpdwDisposition = 1; /* REG_CREATED_NEW_KEY */
        return 0; /* ERROR_SUCCESS */
    }
    return 1; /* ERROR_INVALID_FUNCTION */
}

__attribute__((ms_abi))
static LONG RegCreateKeyExW_msabi(ULONG_PTR hKey, const WCHAR *lpSubKey,
    ULONG Reserved, WCHAR *lpClass, ULONG dwOptions, ULONG samDesired,
    PVOID lpSecurityAttributes, PVOID phkResult, PVOID lpdwDisposition)
{
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, lpSubKey);
    PCM_KEY_NODE key;
    NTSTATUS s = CmCreateKey(&uniName, 0, &key);
    if (NT_SUCCESS(s)) {
        *(ULONG_PTR *)phkResult = (ULONG_PTR)key;
        if (lpdwDisposition) *(ULONG *)lpdwDisposition = 1;
        return 0;
    }
    return 1;
}

__attribute__((ms_abi))
static LONG RegOpenKeyExA_msabi(ULONG_PTR hKey, const CHAR *lpSubKey,
    ULONG ulOptions, ULONG samDesired, PVOID phkResult)
{
    static WCHAR wsubkey[256];
    if (lpSubKey) {
        UINT i;
        for (i = 0; lpSubKey[i] && i < 255; i++) wsubkey[i] = (WCHAR)lpSubKey[i];
        wsubkey[i] = 0;
    }
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, wsubkey);
    PCM_KEY_NODE key;
    NTSTATUS s = CmOpenKey(&uniName, samDesired, &key);
    if (NT_SUCCESS(s)) {
        *(ULONG_PTR *)phkResult = (ULONG_PTR)key;
        return 0;
    }
    return 2; /* ERROR_FILE_NOT_FOUND */
}

__attribute__((ms_abi))
static LONG RegOpenKeyExW_msabi(ULONG_PTR hKey, const WCHAR *lpSubKey,
    ULONG ulOptions, ULONG samDesired, PVOID phkResult)
{
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, lpSubKey);
    PCM_KEY_NODE key;
    NTSTATUS s = CmOpenKey(&uniName, samDesired, &key);
    if (NT_SUCCESS(s)) {
        *(ULONG_PTR *)phkResult = (ULONG_PTR)key;
        return 0;
    }
    return 2;
}

__attribute__((ms_abi))
static LONG RegCloseKey_msabi(ULONG_PTR hKey)
{
    /* No explicit close needed — in-memory hive */
    (void)hKey;
    return 0;
}

__attribute__((ms_abi))
static LONG RegSetValueExA_msabi(ULONG_PTR hKey, const CHAR *lpValueName,
    ULONG Reserved, ULONG dwType, const BYTE *lpData, ULONG cbData)
{
    static WCHAR wname[256];
    if (lpValueName) {
        UINT i;
        for (i = 0; lpValueName[i] && i < 255; i++) wname[i] = (WCHAR)lpValueName[i];
        wname[i] = 0;
    }
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, wname);
    NTSTATUS s = CmSetValue((PCM_KEY_NODE)hKey, &uniName, dwType, (PVOID)lpData, cbData);
    return NT_SUCCESS(s) ? 0 : 1;
}

__attribute__((ms_abi))
static LONG RegSetValueExW_msabi(ULONG_PTR hKey, const WCHAR *lpValueName,
    ULONG Reserved, ULONG dwType, const BYTE *lpData, ULONG cbData)
{
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, lpValueName);
    NTSTATUS s = CmSetValue((PCM_KEY_NODE)hKey, &uniName, dwType, (PVOID)lpData, cbData);
    return NT_SUCCESS(s) ? 0 : 1;
}

__attribute__((ms_abi))
static LONG RegQueryValueExA_msabi(ULONG_PTR hKey, const CHAR *lpValueName,
    PVOID lpReserved, PVOID lpType, PVOID lpData, PVOID lpcbData)
{
    static WCHAR wname[256];
    if (lpValueName) {
        UINT i;
        for (i = 0; lpValueName[i] && i < 255; i++) wname[i] = (WCHAR)lpValueName[i];
        wname[i] = 0;
    }
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, wname);
    ULONG type = 0, dataSize = 0;
    NTSTATUS s = CmQueryValue((PCM_KEY_NODE)hKey, &uniName, &type, NULL, 0, &dataSize);
    if (NT_SUCCESS(s)) {
        if (lpType) *(ULONG *)lpType = type;
        if (lpcbData) *(ULONG *)lpcbData = dataSize;
        if (lpData && dataSize > 0) {
            CmQueryValue((PCM_KEY_NODE)hKey, &uniName, &type, lpData, dataSize, &dataSize);
        }
        return 0;
    }
    return 2;
}

__attribute__((ms_abi))
static LONG RegQueryValueExW_msabi(ULONG_PTR hKey, const WCHAR *lpValueName,
    PVOID lpReserved, PVOID lpType, PVOID lpData, PVOID lpcbData)
{
    UNICODE_STRING uniName;
    RtlInitUnicodeString(&uniName, lpValueName);
    ULONG type = 0, dataSize = 0;
    NTSTATUS s = CmQueryValue((PCM_KEY_NODE)hKey, &uniName, &type, NULL, 0, &dataSize);
    if (NT_SUCCESS(s)) {
        if (lpType) *(ULONG *)lpType = type;
        if (lpcbData) *(ULONG *)lpcbData = dataSize;
        if (lpData && dataSize > 0) {
            CmQueryValue((PCM_KEY_NODE)hKey, &uniName, &type, lpData, dataSize, &dataSize);
        }
        return 0;
    }
    return 2;
}

__attribute__((ms_abi))
static LONG RegDeleteKeyA_msabi(ULONG_PTR hKey, const CHAR *lpSubKey)
{
    (void)hKey; (void)lpSubKey;
    return 0;
}

__attribute__((ms_abi))
static LONG RegDeleteKeyW_msabi(ULONG_PTR hKey, const WCHAR *lpSubKey)
{
    (void)hKey; (void)lpSubKey;
    return 0;
}

__attribute__((ms_abi))
static LONG RegDeleteValueA_msabi(ULONG_PTR hKey, const CHAR *lpValueName)
{
    (void)hKey; (void)lpValueName;
    return 0;
}

__attribute__((ms_abi))
static LONG RegDeleteValueW_msabi(ULONG_PTR hKey, const WCHAR *lpValueName)
{
    (void)hKey; (void)lpValueName;
    return 0;
}

__attribute__((ms_abi))
static LONG RegEnumKeyExA_msabi(ULONG_PTR hKey, DWORD dwIndex, CHAR *lpName,
    PVOID lpcchName, PVOID lpReserved, CHAR *lpClass, PVOID lpcchClass,
    PVOID lpftLastWriteTime)
{
    UNICODE_STRING uniName;
    NTSTATUS s = CmEnumerateSubKey((PCM_KEY_NODE)hKey, dwIndex, &uniName);
    if (NT_SUCCESS(s) && uniName.Buffer) {
        UINT i;
        for (i = 0; i < uniName.Length / 2 && lpName[i]; i++)
            lpName[i] = (CHAR)uniName.Buffer[i];
        lpName[i] = 0;
        if (lpcchName) *(ULONG *)lpcchName = i;
        return 0;
    }
    return 259; /* ERROR_NO_MORE_ITEMS */
}

__attribute__((ms_abi))
static LONG RegEnumKeyExW_msabi(ULONG_PTR hKey, DWORD dwIndex, WCHAR *lpName,
    PVOID lpcchName, PVOID lpReserved, WCHAR *lpClass, PVOID lpcchClass,
    PVOID lpftLastWriteTime)
{
    UNICODE_STRING uniName;
    NTSTATUS s = CmEnumerateSubKey((PCM_KEY_NODE)hKey, dwIndex, &uniName);
    if (NT_SUCCESS(s) && uniName.Buffer) {
        UINT i;
        for (i = 0; i < uniName.Length / 2; i++) lpName[i] = uniName.Buffer[i];
        lpName[uniName.Length / 2] = 0;
        if (lpcchName) *(ULONG *)lpcchName = uniName.Length / 2;
        return 0;
    }
    return 259;
}

/* ============================================================================
 * Security — route to Se subsystem
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL InitializeSecurityDescriptor_msabi(PVOID pSecurityDescriptor, ULONG dwRevision)
{
    if (pSecurityDescriptor) {
        RtlZeroMemory(pSecurityDescriptor, 32);
        /* Mark as valid with default owner/group */
        ((UCHAR *)pSecurityDescriptor)[0] = 1; /* Revision */
        ((USHORT *)pSecurityDescriptor)[1] = 0x8004; /* Control: SE_DACL_PRESENT | SE_SELF_RELATIVE */
    }
    return TRUE;
}

__attribute__((ms_abi))
static BOOL InitializeAcl_msabi(PVOID pAcl, ULONG nAclLength, ULONG dwAclRevision)
{
    if (pAcl) {
        RtlZeroMemory(pAcl, nAclLength);
        ((USHORT *)pAcl)[0] = (USHORT)dwAclRevision; /* AclRevision */
        ((USHORT *)pAcl)[1] = (USHORT)nAclLength;    /* AclSize */
    }
    return TRUE;
}

__attribute__((ms_abi))
static BOOL SetSecurityDescriptorDacl_msabi(PVOID pSecurityDescriptor, BOOL bDaclPresent,
    PVOID pDacl, BOOL bDaclDefaulted)
{
    (void)pSecurityDescriptor; (void)bDaclPresent; (void)pDacl; (void)bDaclDefaulted;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL GetSecurityDescriptorDacl_msabi(PVOID pSecurityDescriptor, PVOID lpbDaclPresent,
    PVOID pDacl, PVOID lpbDaclDefaulted)
{
    if (lpbDaclPresent) *(BOOL *)lpbDaclPresent = FALSE;
    if (pDacl) *(PVOID *)pDacl = NULL;
    if (lpbDaclDefaulted) *(BOOL *)lpbDaclDefaulted = FALSE;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL SetSecurityDescriptorOwner_msabi(PVOID pSecurityDescriptor, PVOID pOwner, BOOL bOwnerDefaulted)
{
    (void)pSecurityDescriptor; (void)pOwner; (void)bOwnerDefaulted;
    return TRUE;
}

/* ============================================================================
 * Services (SCM) — stub-free no-ops
 * ========================================================================== */

__attribute__((ms_abi))
static ULONG_PTR OpenSCManagerA_msabi(const CHAR *lpMachineName, const CHAR *lpDatabaseName,
    ULONG dwDesiredAccess)
{
    return 0x50000000LL; /* fake SCM handle */
}

__attribute__((ms_abi))
static ULONG_PTR CreateServiceA_msabi(ULONG_PTR hSCManager, const CHAR *lpServiceName,
    const CHAR *lpDisplayName, ULONG dwDesiredAccess, ULONG dwServiceType,
    ULONG dwStartType, ULONG dwErrorControl, const CHAR *lpBinaryPathName,
    const CHAR *lpLoadOrderGroup, PVOID lpdwTagId, const CHAR *lpDependencies,
    const CHAR *lpServiceStartName, const CHAR *lpPassword)
{
    return 0x50000001LL; /* fake service handle */
}

__attribute__((ms_abi))
static BOOL CloseServiceHandle_msabi(ULONG_PTR hSCObject)
{
    (void)hSCObject;
    return TRUE;
}

/* ============================================================================
 * Event Log — route to DbgPrint
 * ========================================================================== */

__attribute__((ms_abi))
static ULONG_PTR RegisterEventSourceA_msabi(const CHAR *lpUNCServerName, const CHAR *lpSourceName)
{
    DbgPrint("EXE: RegisterEventSourceA(%s)\n", lpSourceName ? lpSourceName : "null");
    (void)lpUNCServerName;
    return 0x60000000LL; /* fake handle */
}

__attribute__((ms_abi))
static BOOL ReportEventA_msabi(ULONG_PTR hEventLog, USHORT wType, USHORT wCategory,
    DWORD dwEventID, PVOID lpUserSid, USHORT wNumStrings, DWORD dwDataSize,
    const CHAR *lpStrings, PVOID lpRawData)
{
    (void)hEventLog; (void)wType; (void)wCategory; (void)dwEventID;
    (void)lpUserSid; (void)wNumStrings; (void)dwDataSize; (void)lpRawData;
    if (lpStrings) DbgPrint("EXE: EventLog: %s\n", lpStrings);
    return TRUE;
}

__attribute__((ms_abi))
static BOOL DeregisterEventSource_msabi(ULONG_PTR hEventLog)
{
    (void)hEventLog;
    return TRUE;
}

/* ============================================================================
 * Token / Privilege — route to Se subsystem
 * ========================================================================== */

__attribute__((ms_abi))
static BOOL OpenProcessToken_msabi(ULONG_PTR ProcessHandle, ULONG DesiredAccess, PVOID TokenHandle)
{
    if (TokenHandle) *(ULONG_PTR *)TokenHandle = 0x70000000LL;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL AdjustTokenPrivileges_msabi(ULONG_PTR TokenHandle, BOOL DisableAllPrivileges,
    PVOID NewState, ULONG BufferLength, PVOID PreviousState, PVOID ReturnLength)
{
    (void)TokenHandle; (void)DisableAllPrivileges; (void)NewState;
    (void)BufferLength; (void)PreviousState; (void)ReturnLength;
    return TRUE;
}

__attribute__((ms_abi))
static BOOL LookupPrivilegeValueA_msabi(const CHAR *lpSystemName, const CHAR *lpName, PVOID lpLuid)
{
    if (lpLuid) {
        ULONG64 *luid = (ULONG64 *)lpLuid;
        *luid = 0; /* generic */
    }
    return TRUE;
}

/* ============================================================================
 * Registration
 * ========================================================================== */

VOID NTAPI Advapi32RegisterExports(VOID)
{
#define AREG(name, ptr) ExeRegisterExport("advapi32.dll", name, ptr)

    /* Registry */
    AREG("RegCreateKeyExA", RegCreateKeyExA_msabi);
    AREG("RegCreateKeyExW", RegCreateKeyExW_msabi);
    AREG("RegOpenKeyExA", RegOpenKeyExA_msabi);
    AREG("RegOpenKeyExW", RegOpenKeyExW_msabi);
    AREG("RegCloseKey", RegCloseKey_msabi);
    AREG("RegSetValueExA", RegSetValueExA_msabi);
    AREG("RegSetValueExW", RegSetValueExW_msabi);
    AREG("RegQueryValueExA", RegQueryValueExA_msabi);
    AREG("RegQueryValueExW", RegQueryValueExW_msabi);
    AREG("RegDeleteKeyA", RegDeleteKeyA_msabi);
    AREG("RegDeleteKeyW", RegDeleteKeyW_msabi);
    AREG("RegDeleteValueA", RegDeleteValueA_msabi);
    AREG("RegDeleteValueW", RegDeleteValueW_msabi);
    AREG("RegEnumKeyExA", RegEnumKeyExA_msabi);
    AREG("RegEnumKeyExW", RegEnumKeyExW_msabi);

    /* Security */
    AREG("InitializeSecurityDescriptor", InitializeSecurityDescriptor_msabi);
    AREG("InitializeAcl", InitializeAcl_msabi);
    AREG("SetSecurityDescriptorDacl", SetSecurityDescriptorDacl_msabi);
    AREG("GetSecurityDescriptorDacl", GetSecurityDescriptorDacl_msabi);
    AREG("SetSecurityDescriptorOwner", SetSecurityDescriptorOwner_msabi);

    /* Services */
    AREG("OpenSCManagerA", OpenSCManagerA_msabi);
    AREG("CreateServiceA", CreateServiceA_msabi);
    AREG("CloseServiceHandle", CloseServiceHandle_msabi);

    /* Event Log */
    AREG("RegisterEventSourceA", RegisterEventSourceA_msabi);
    AREG("ReportEventA", ReportEventA_msabi);
    AREG("DeregisterEventSource", DeregisterEventSource_msabi);

    /* Token / Privilege */
    AREG("OpenProcessToken", OpenProcessToken_msabi);
    AREG("AdjustTokenPrivileges", AdjustTokenPrivileges_msabi);
    AREG("LookupPrivilegeValueA", LookupPrivilegeValueA_msabi);

    DbgPrint("EXE: advapi32.dll exports registered (%lu total)\n", g_ExportCount);
}
