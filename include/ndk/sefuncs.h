/* MinNT - Security functions stub */
#include <ndk/setypes.h>

#include <ndk/setypes.h>

#ifndef _NDK_SEFUNCS_H_
#define _NDK_SEFUNCS_H_

NTSTATUS NTAPI NtSetSecurityObject(HANDLE Handle, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR SecurityDescriptor);
NTSTATUS NTAPI NtQuerySecurityObject(HANDLE Handle, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG Length, PULONG ReturnLength);
NTSTATUS NTAPI SeSinglePrivilegeCheck(ULONG Privilege, BOOLEAN WasEnabled);

#endif
