#!/usr/bin/env python3
"""
Advanced ReactOS → MinNT Converter
Phase 2: Handles ReactOS-specific types, macros, and function stubs
"""

import os
import re
import sys
from pathlib import Path

# ReactOS types that need MinNT equivalents
REACTOS_TYPES = {
    "LIST_ENTRY": "LIST_ENTRY",
    "SINGLE_LIST_ENTRY": "SINGLE_LIST_ENTRY",
    "UNICODE_STRING": "UNICODE_STRING",
    "ANSI_STRING": "ANSI_STRING",
    "OBJECT_ATTRIBUTES": "OBJECT_ATTRIBUTES",
    "CLIENT_ID": "CLIENT_ID",
    "IO_STATUS_BLOCK": "IO_STATUS_BLOCK",
    "PEB": "PEB",
    "TEB": "TEB",
    "SECTION_IMAGE_INFORMATION": "SECTION_IMAGE_INFORMATION",
    "RTL_USER_PROCESS_INFORMATION": "RTL_USER_PROCESS_INFORMATION",
    "SECURITY_DESCRIPTOR": "SECURITY_DESCRIPTOR",
    "SECURITY_DESCRIPTOR_CONTROL": "SECURITY_DESCRIPTOR_CONTROL",
    "ACCESS_MASK": "ACCESS_MASK",
    "NTSTATUS": "NTSTATUS",
    "ULONG": "ULONG",
    "PULONG": "PULONG",
    "USHORT": "USHORT",
    "PUCHAR": "PUCHAR",
    "PVOID": "PVOID",
    "HANDLE": "HANDLE",
    "PHANDLE": "PHANDLE",
    "BOOLEAN": "BOOLEAN",
    "LARGE_INTEGER": "LARGE_INTEGER",
    "PLARGE_INTEGER": "PLARGE_INTEGER",
    "SIZE_T": "SIZE_T",
    "ULONG_PTR": "ULONG_PTR",
    "PWSTR": "PWSTR",
    "PCWSTR": "PCWSTR",
    "PCHAR": "PCHAR",
    "PCSTR": "PCSTR",
    "PSTR": "PSTR",
}

# ReactOS macros that need replacement
REACTOS_MACROS = {
    # Debug macros
    "DPRINT": "DbgPrint",
    "DPRINT1": "DbgPrint",
    "DPRINT2": "DbgPrint",
    "DPRINT3": "DbgPrint",
    "DPRINTF": "DbgPrint",
    "DPRINTC": "DbgPrint",
    "DPRINTFS": "DbgPrint",
    
    # Assertion macros
    "ASSERT": "((void)0)",
    "ASSERTNT": "((void)0)",
    "ASSERT_IRQL": "((void)0)",
    "ASSERT_IRQL_LESS_OR_EQUAL": "((void)0)",
    
    # Memory macros
    "RtlZeroMemory": "RtlZeroMemory",
    "RtlCopyMemory": "RtlCopyMemory",
    "RtlMoveMemory": "RtlMoveMemory",
    "RtlFillMemory": "RtlFillMemory",
    
    # List macros
    "InitializeListHead": "InitializeListHead",
    "InsertTailList": "InsertTailList",
    "InsertHeadList": "InsertHeadList",
    "RemoveHeadList": "RemoveHeadList",
    "RemoveTailList": "RemoveTailList",
    "RemoveEntryList": "RemoveEntryList",
    "IsListEmpty": "IsListEmpty",
    "CONTAINING_RECORD": "CONTAINING_RECORD",
    "FIELD_OFFSET": "offsetof",
    
    # String macros
    "RtlInitUnicodeString": "RtlInitUnicodeString",
    "RtlInitAnsiString": "RtlInitAnsiString",
    "RtlCopyUnicodeString": "RtlCopyUnicodeString",
    "RtlEqualUnicodeString": "RtlEqualUnicodeString",
    "RtlCompareUnicodeString": "RtlCompareUnicodeString",
    "RtlAppendUnicodeStringToString": "RtlAppendUnicodeStringToString",
    
    # Object macros
    "InitializeObjectAttributes": "InitializeObjectAttributes",
    
    # Process macros
    "NtCurrentProcess": "((HANDLE)(LONG_PTR)-1)",
    "NtCurrentThread": "((HANDLE)(LONG_PTR)-2)",
    "NtCurrentPeb": "NtCurrentPeb",
    
    # Status macros
    "NT_SUCCESS": "NT_SUCCESS",
    "NT_ERROR": "NT_ERROR",
    "NT_WARNING": "NT_WARNING",
    "NT_INFORMATION": "NT_INFORMATION",
    
    # Pool macros
    "POOL_TYPE": "POOL_TYPE",
    "NonPagedPool": "NonPagedPool",
    "PagedPool": "PagedPool",
    
    # Security macros
    "SECURITY_DESCRIPTOR_REVISION": "1",
    "SECURITY_DESCRIPTOR_MIN_LENGTH": "40",
    "ACL_REVISION": "2",
    "ACL_REVISION2": "2",
    
    # Object macros
    "OBJ_CASE_INSENSITIVE": "0x00000040",
    "OBJ_OPENIF": "0x00000020",
    "OBJ_PERMANENT": "0x00000010",
    "OBJ_EXCLUSIVE": "0x00000020",
    "OBJ_INHERIT": "0x00000002",
    
    # Access masks
    "GENERIC_READ": "0x80000000",
    "GENERIC_WRITE": "0x40000000",
    "GENERIC_EXECUTE": "0x20000000",
    "GENERIC_ALL": "0x10000000",
    "READ_CONTROL": "0x00020000",
    "WRITE_DAC": "0x00040000",
    "WRITE_OWNER": "0x00080000",
    "SYNCHRONIZE": "0x00100000",
    
    # Registry
    "KEY_READ": "0x00020019",
    "KEY_WRITE": "0x00020006",
    "KEY_ALL_ACCESS": "0x000F003F",
    "KEY_CREATE_SUB_KEY": "0x00000004",
    "KEY_ENUMERATE_SUB_KEYS": "0x00000008",
    "KEY_QUERY_VALUE": "0x00000001",
    "KEY_SET_VALUE": "0x00000002",
    "KEY_NOTIFY": "0x00000010",
    "KEY_CREATE_LINK": "0x00000020",
    
    # Registry value types
    "REG_NONE": "0",
    "REG_SZ": "1",
    "REG_EXPAND_SZ": "2",
    "REG_BINARY": "3",
    "REG_DWORD": "4",
    "REG_DWORD_LITTLE_ENDIAN": "4",
    "REG_DWORD_BIG_ENDIAN": "5",
    "REG_LINK": "6",
    "REG_MULTI_SZ": "7",
    "REG_RESOURCE_LIST": "8",
    "REG_FULL_RESOURCE_DESCRIPTOR": "9",
    "REG_RESOURCE_REQUIREMENTS_LIST": "10",
    
    # Section
    "SEC_BASED": "0x00200000",
    "SEC_RESERVE": "0x00400000",
    "SEC_COMMIT": "0x00800000",
    "SEC_NO_CHANGE": "0x01000000",
    
    # Page protection
    "PAGE_NOACCESS": "0x01",
    "PAGE_READONLY": "0x02",
    "PAGE_READWRITE": "0x04",
    "PAGE_EXECUTE": "0x10",
    "PAGE_EXECUTE_READ": "0x20",
    "PAGE_EXECUTE_READWRITE": "0x40",
    "PAGE_GUARD": "0x100",
    "PAGE_NOCACHE": "0x200",
    "PAGE_WRITECOMBINE": "0x400",
    
    # Memory allocation
    "MEM_COMMIT": "0x00001000",
    "MEM_RESERVE": "0x00002000",
    "MEM_RELEASE": "0x00008000",
    "MEM_FREE": "0x00010000",
    "MEM_PRIVATE": "0x00020000",
    "MEM_MAPPED": "0x00040000",
    "MEM_TOP_DOWN": "0x00100000",
    
    # Section
    "SECTION_QUERY": "0x0001",
    "SECTION_MAP_WRITE": "0x0002",
    "SECTION_MAP_READ": "0x0004",
    "SECTION_MAP_EXECUTE": "0x0008",
    "SECTION_EXTEND_SIZE": "0x0010",
    "SECTION_MAP_EXECUTE_EXPLICIT": "0x0020",
    "SECTION_ALL_ACCESS": "0x001F0FFF",
    
    # Object
    "DIRECTORY_QUERY": "0x0001",
    "DIRECTORY_TRAVERSE": "0x0002",
    "DIRECTORY_CREATE_OBJECT": "0x0004",
    "DIRECTORY_CREATE_SUBDIRECTORY": "0x0008",
    "DIRECTORY_ALL_ACCESS": "0x000F001F",
    
    # Symbolic link
    "SYMBOLIC_LINK_QUERY": "0x0001",
    "SYMBOLIC_LINK_ALL_ACCESS": "0x001F0001",
    
    # Port
    "PORT_CONNECT": "0x0001",
    "PORT_ALL_ACCESS": "0x001F0000",
    
    # Token
    "TOKEN_QUERY": "0x0008",
    "TOKEN_ADJUST_PRIVILEGES": "0x0020",
    "TOKEN_ADJUST_GROUPS": "0x0040",
    "TOKEN_ADJUST_DEFAULT": "0x0080",
    "TOKEN_ADJUST_SESSIONID": "0x0100",
    "TOKEN_ALL_ACCESS": "0x001F003F",
    
    # Process
    "PROCESS_TERMINATE": "0x0001",
    "PROCESS_CREATE_THREAD": "0x0002",
    "PROCESS_VM_OPERATION": "0x0008",
    "PROCESS_VM_READ": "0x0010",
    "PROCESS_VM_WRITE": "0x0020",
    "PROCESS_DUP_HANDLE": "0x0040",
    "PROCESS_CREATE_PROCESS": "0x0080",
    "PROCESS_SET_QUOTA": "0x0100",
    "PROCESS_SET_INFORMATION": "0x0200",
    "PROCESS_QUERY_INFORMATION": "0x0400",
    "PROCESS_SUSPEND_RESUME": "0x0800",
    "PROCESS_QUERY_LIMITED_INFORMATION": "0x1000",
    "PROCESS_ALL_ACCESS": "0x001FFFFF",
    
    # Thread
    "THREAD_TERMINATE": "0x0001",
    "THREAD_SUSPEND_RESUME": "0x0002",
    "THREAD_GET_CONTEXT": "0x0008",
    "THREAD_SET_CONTEXT": "0x0010",
    "THREAD_SET_INFORMATION": "0x0020",
    "THREAD_QUERY_INFORMATION": "0x0040",
    "THREAD_SET_THREAD_TOKEN": "0x0080",
    "THREAD_IMPERSONATE": "0x0100",
    "THREAD_DIRECT_IMPERSONATION": "0x0200",
    "THREAD_SET_LIMITED_INFORMATION": "0x0400",
    "THREAD_QUERY_LIMITED_INFORMATION": "0x0800",
    "THREAD_ALL_ACCESS": "0x001FFFFF",
    
    # File
    "FILE_READ_DATA": "0x0001",
    "FILE_LIST_DIRECTORY": "0x0001",
    "FILE_WRITE_DATA": "0x0002",
    "FILE_ADD_FILE": "0x0002",
    "FILE_APPEND_DATA": "0x0004",
    "FILE_ADD_SUBDIRECTORY": "0x0004",
    "FILE_CREATE_PIPE_INSTANCE": "0x0004",
    "FILE_READ_EA": "0x0008",
    "FILE_WRITE_EA": "0x0010",
    "FILE_EXECUTE": "0x0020",
    "FILE_TRAVERSE": "0x0020",
    "FILE_DELETE_CHILD": "0x0040",
    "FILE_READ_ATTRIBUTES": "0x0080",
    "FILE_WRITE_ATTRIBUTES": "0x0100",
    "FILE_ALL_ACCESS": "0x001F01FF",
    "FILE_GENERIC_READ": "0x00120089",
    "FILE_GENERIC_WRITE": "0x00120116",
    "FILE_GENERIC_EXECUTE": "0x001200A0",
    "FILE_GENERIC_ALL": "0x001F01FF",
    
    # File attributes
    "FILE_ATTRIBUTE_READONLY": "0x00000001",
    "FILE_ATTRIBUTE_HIDDEN": "0x00000002",
    "FILE_ATTRIBUTE_SYSTEM": "0x00000004",
    "FILE_ATTRIBUTE_DIRECTORY": "0x00000010",
    "FILE_ATTRIBUTE_ARCHIVE": "0x00000020",
    "FILE_ATTRIBUTE_DEVICE": "0x00000040",
    "FILE_ATTRIBUTE_NORMAL": "0x00000080",
    "FILE_ATTRIBUTE_TEMPORARY": "0x00000100",
    "FILE_ATTRIBUTE_SPARSE_FILE": "0x00000200",
    "FILE_ATTRIBUTE_REPARSE_POINT": "0x00000400",
    "FILE_ATTRIBUTE_COMPRESSED": "0x00000800",
    "FILE_ATTRIBUTE_OFFLINE": "0x00001000",
    "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED": "0x00002000",
    "FILE_ATTRIBUTE_ENCRYPTED": "0x00004000",
    
    # File share
    "FILE_SHARE_READ": "0x00000001",
    "FILE_SHARE_WRITE": "0x00000002",
    "FILE_SHARE_DELETE": "0x00000004",
    
    # File creation
    "FILE_SUPERSEDE": "0x00000000",
    "FILE_CREATE": "0x00000001",
    "FILE_OPEN": "0x00000002",
    "FILE_OPEN_IF": "0x00000003",
    "FILE_OVERWRITE": "0x00000004",
    "FILE_OVERWRITE_IF": "0x00000005",
    "FILE_MAXIMUM_DISPOSITION": "0x00000005",
    
    # File disposition
    "FILE_DIRECTORY_FILE": "0x00000001",
    "FILE_WRITE_THROUGH": "0x00000002",
    "FILE_SEQUENTIAL_ONLY": "0x00000004",
    "FILE_NO_INTERMEDIATE_BUFFERING": "0x00000008",
    "FILE_SYNCHRONOUS_IO_NONALERT": "0x00000010",
    "FILE_SYNCHRONOUS_IO_ALERT": "0x00000020",
    "FILE_NON_DIRECTORY_FILE": "0x00000040",
    "FILE_CREATE_TREE_CONNECTION": "0x00000080",
    "FILE_NO_EA Knowledge": "0x00000400",
    "FILE_RANDOM_ACCESS": "0x00000800",
    "FILE_DELETE_ON_CLOSE": "0x00001000",
    "FILE_OPEN_BY_FILE_ID": "0x00002000",
    "FILE_OPEN_FOR_BACKUP_INTENT": "0x00004000",
    "FILE_NO_COMPRESSION": "0x00008000",
    "FILE_OPEN_REQUIRING_OPLOCK": "0x00010000",
    "FILE_DISPOSITION_EX": "0x00020000",
    "FILE_OPEN_FOR_FREE_SPACE_QUERY": "0x00080000",
    
    # File I/O status
    "FILE_OPENED": "0",
    "FILE_OVERWRITTEN": "1",
    "FILE_SUPERSEDED": "2",
    "FILE_EXISTS": "3",
    "FILE_DOES_NOT_EXIST": "4",
    
    # File information class
    "FileDirectoryInformation": "1",
    "FileFullDirectoryInformation": "2",
    "FileBothDirectoryInformation": "3",
    "FileBasicInformation": "4",
    "FileStandardInformation": "5",
    "FileInternalInformation": "6",
    "FileEaInformation": "7",
    "FileAccessInformation": "8",
    "FileNameInformation": "9",
    "FileRenameInformation": "10",
    "FileLinkInformation": "11",
    "FileDispositionInformation": "13",
    "FilePositionInformation": "14",
    "FileFullEaInformation": "15",
    "FileModeInformation": "16",
    "FileAlignmentInformation": "17",
    "FileAllInformation": "18",
    "FileAllocationInformation": "19",
    "FileEndOfFileInformation": "20",
    "FileAlternateNameInformation": "21",
    "FileStreamInformation": "22",
    "FilePipeInformation": "23",
    "FilePipeLocalInformation": "24",
    "FilePipeRemoteInformation": "25",
    "FileMailslotQueryInformation": "26",
    "FileMailslotSetInformation": "27",
    "FileCompressionInformation": "28",
    "FileObjectIdInformation": "29",
    "FileCompletionInformation": "30",
    "FileMoveClusterInformation": "31",
    "FileQuotaInformation": "32",
    "FileReparsePointInformation": "33",
    "FileNetworkOpenInformation": "34",
    "FileAttributeTagInformation": "35",
    "FileTrackingInformation": "36",
    "FileIdBothDirectoryInformation": "37",
    "FileIdFullDirectoryInformation": "38",
    "FileValidDataLengthInformation": "39",
    "FileShortNameInformation": "40",
    "FileIoCompletionInformation": "41",
    "FileIoStatusBlockRangeInformation": "42",
    "FileIoPriorityHintInformation": "43",
    "FileSfioReserveInformation": "44",
    "FileSfioVolumeInformation": "45",
    "FileHardLinkInformation": "46",
    "FileProcessIdsUsingFileInformation": "47",
    "FileNormalizedNameInformation": "48",
    "FileNetworkPhysicalNameInformation": "49",
    "FileIdGlobalTxDirectoryInformation": "50",
    "FileIsRemoteDeviceInformation": "51",
    "FileUnusedInformation": "52",
    "FileNumaNodeInformation": "53",
    "FileStandardLinkInformation": "54",
    "FileRemoteProtocolInformation": "55",
    "FileRenameInformationBypassAccessCheck": "56",
    "FileLinkInformationBypassAccessCheck": "57",
    "FileVolumeNameInformation": "58",
    "FileIdInformation": "59",
    "FileIdExtdDirectoryInformation": "60",
    "FileReplaceCompletionInformation": "61",
    "FileHardLinkFullIdInformation": "62",
    "FileIdExtdDirectoryRestartInformation": "63",
    "FileMaximumInformation": "64",
    
    # Pipe
    "FILE_PIPE_BYTE_TYPE": "1",
    "FILE_PIPE_MESSAGE_TYPE": "2",
    "FILE_PIPE_BYTE_MODE": "1",
    "FILE_PIPE_MESSAGE_MODE": "2",
    "FILE_PIPE_QUEUE_OPERATION": "0",
    "FILE_PIPE_NONBLOCKING": "1",
    "FILE_PIPE_INBOUND": "0",
    "FILE_PIPE_OUTBOUND": "1",
    "FILE_PIPE双向": "2",
    "FILE_PIPE_CLIENT_END": "0",
    "FILE_PIPE_SERVER_END": "1",
    
    # Mailslot
    "FILE_PIPE_BYTE_TYPE": "1",
    "FILE_PIPE_MESSAGE_TYPE": "2",
    "FILE_PIPE_BYTE_MODE": "1",
    "FILE_PIPE_MESSAGE_MODE": "2",
    "FILE_PIPE_QUEUE_OPERATION": "0",
    "FILE_PIPE_NONBLOCKING": "1",
    "FILE_PIPE_INBOUND": "0",
    "FILE_PIPE_OUTBOUND": "1",
    "FILE_PIPE双向": "2",
    "FILE_PIPE_CLIENT_END": "0",
    "FILE_PIPE_SERVER_END": "1",
    
    # Event
    "EVENT_MODIFY_STATE": "0x0002",
    "EVENT_ALL_ACCESS": "0x001F0003",
    "SynchronizationEvent": "0",
    "NotificationEvent": "1",
    
    # Semaphore
    "SEMAPHORE_MODIFY_STATE": "0x0002",
    "SEMAPHORE_ALL_ACCESS": "0x001F0003",
    
    # Mutex
    "MUTANT_MODIFY_STATE": "0x0001",
    "MUTANT_ALL_ACCESS": "0x001F0001",
    
    # Timer
    "TIMER_MODIFY_STATE": "0x0001",
    "TIMER_ALL_ACCESS": "0x001F0003",
    
    # Profile
    "PROFILE_CONTROL": "0x0001",
    "PROFILE_ALL_ACCESS": "0x001F0001",
    
    # Keyed event
    "KEYEDEVENT_WAIT": "0x0001",
    "KEYEDEVENT_WAKE": "0x0002",
    "KEYEDEVENT_ALL_ACCESS": "0x001F0003",
    
    # IoCompletion
    "IO_COMPLETION_MODIFY_STATE": "0x0002",
    "IO_COMPLETION_ALL_ACCESS": "0x001F0003",
    
    # Channel
    "CHANNEL_QUERY": "0x0001",
    "CHANNEL_ALL_ACCESS": "0x001F0003",
    
    # Timer APC
    "TIMER_OR_APCCALLBACK": "0",
    "TIMER_DPC": "1",
    "TIMER_RUNDOWN": "2",
    "TIMER_NULL_DPC": "3",
    
    # Worker thread
    "WorkerThreadMinimum": "0",
    "WorkerThreadMaximum": "1",
    
    # System
    "SystemBasicInformation": "0",
    "SystemPerformanceInformation": "1",
    "SystemTimeOfDay": "2",
    "SystemProcessInformation": "3",
    "SystemProcessorPerformanceInformation": "4",
    "SystemHandleInformation": "16",
    "SystemPagefileInformation": "18",
    "SystemModuleInformation": "11",
    "SystemInterruptInformation": "23",
    "SystemExceptionInformation": "33",
    "SystemKernelDebuggerInformation": "36",
    "SystemRegistryQuotaInformation": "37",
    "SystemSuperfetchInformation": "80",
    
    # Process
    "ProcessBasicInformation": "0",
    "ProcessQuotaLimits": "1",
    "ProcessIoCounters": "2",
    "ProcessVmCounters": "3",
    "ProcessTimes": "4",
    "ProcessBasePriority": "5",
    "ProcessRaisePriority": "6",
    "ProcessDebugPort": "7",
    "ProcessExceptionPort": "8",
    "ProcessAccessToken": "9",
    "ProcessLdtInformation": "10",
    "ProcessLdtSize": "11",
    "ProcessDefaultHardErrorMode": "12",
    "ProcessHandlePort": "13",
    "ProcessPriorityBoost": "14",
    "ProcessDeviceMap": "15",
    "ProcessSessionInformation": "16",
    "ProcessForegroundInformation": "17",
    "ProcessWow64Information": "18",
    "ProcessImageFileName": "27",
    "ProcessBreakOnTermination": "29",
    "ProcessDebugObjectHandle": "30",
    "ProcessDebugFlags": "31",
    "ProcessHandleTracing": "32",
    "ProcessIoPriority": "33",
    "ProcessCycleTime": "34",
    "ProcessPagePriority": "35",
    "ProcessInstrumentationCallback": "36",
    "ProcessThreadStackAllocation": "37",
    "ProcessWorkingSetWatch": "38",
    "ProcessCouponInstrumentation": "39",
    "ProcessReservationManagement": "40",
    "ProcessFreezeProcess": "41",
    "ProcessFreezeThaw": "42",
    "ProcessCancelledStrongHandle": "43",
    "ProcessInstrumentationInformation": "44",
    
    # Thread
    "ThreadBasicInformation": "0",
    "ThreadTimes": "1",
    "ThreadPriority": "2",
    "ThreadBasePriority": "3",
    "ThreadQuerySetWin32StartAddress": "9",
    "ThreadIsIoPending": "16",
    "ThreadHideFromDebugger": "17",
    "ThreadBreakOnTermination": "28",
    "ThreadInstrumentationInformation": "40",
    
    # Memory
    "MemoryBasicInformation": "0",
    "MemoryWorkingSetInformation": "1",
    "MemorySectionName": "2",
    "MemoryBasicVlmInformation": "3",
    "MemoryBasicInformationEx": "4",
    "MemorySharedMemoryInformation": "5",
    "MemoryImageInformation": "6",
    
    # Section
    "SectionBasicInformation": "0",
    "SectionImageInformation": "1",
    "SectionRelocationInformation": "2",
    "SectionOriginalBaseAddress": "3",
    
    # File system
    "FileFsVolumeInformation": "1",
    "FileFsLabelInformation": "2",
    "FileFsSizeInformation": "3",
    "FileFsDeviceInformation": "4",
    "FileFsAttributeInformation": "5",
    "FileFsControlInformation": "6",
    "FileFsFullSizeInformation": "7",
    "FileFsObjectIdInformation": "8",
    "FileFsDriverPathInformation": "9",
    "FileFsVolumeFlagsInformation": "10",
    "FileFsSectorSizeInformation": "11",
    "FileFsDataCopyInformation": "12",
    "FileFsMetadataSizeInformation": "13",
    "FileFsFullSizeInformationEx": "14",
    
    # NLS
    "NlsAnsiCodePage": "0",
    "NlsOemCodePage": "1",
    "NlsUnicodeCaseTable": "2",
    "NlsDefaultCodePage": "3",
    "NlsOemDefaultCodePage": "4",
    "NlsExtendedAnsiCodePage": "5",
    "NlsCodePage": "6",
    "NlsMaxCodePage": "7",
    
    # Locale
    "LOCALE_USER_DEFAULT": "0x0400",
    "LOCALE_SYSTEM_DEFAULT": "0x0800",
    "LOCALE_INVARIANT": "0x007F",
    
    # Codepage
    "CP_ACP": "0",
    "CP_OEMCP": "1",
    "CP_UTF7": "65000",
    "CP_UTF8": "65001",
    
    # Time
    "TIME_ZONE_ID_UNKNOWN": "0",
    "TIME_ZONE_ID_STANDARD": "1",
    "TIME_ZONE_ID_DAYLIGHT": "2",
    
    # DbgPrint
    "DPFLTR_DEFAULT_ID": "0",
    "DPFLTR_IHVDRIVER_ID": "77",
    "DPFLTR_PS_ID": "3",
    "DPFLTR_KRNL_ID": "7",
    "DPFLTR_MJ_ID": "10",
    "DPFLTR_SYSTEM_ID": "0",
    
    # Debug
    "DEBUG_CHANNEL": "0",
    "DEBUG_LEVEL": "0",
    "DEBUG_FUNCTION": "0",
    "DEBUG_FILE": "0",
    "DEBUG_LINE": "0",
    "DEBUG_FORMAT": "0",
    "DEBUG_TIMER": "0",
    "DEBUG_STRING": "0",
    "DEBUG_PRIORITY": "0",
    "DEBUG_MODULE": "0",
    "DEBUG_EXPORT": "0",
    "DEBUG_IMPORT": "0",
    "DEBUG_REFERENCE": "0",
    "DEBUG_EVENT": "0",
    "DEBUG_LOCK": "0",
    "DEBUG_RESOURCE": "0",
    "DEBUG_MEMORY": "0",
    "DEBUG_REGISTRY": "0",
    "DEBUG_FILESYSTEM": "0",
    "DEBUG Storage": "0",
    "DEBUG_STRING": "0",
    "DEBUG_PRIORITY": "0",
    "DEBUG_MODULE": "0",
    "DEBUG_EXPORT": "0",
    "DEBUG_IMPORT": "0",
    "DEBUG_REFERENCE": "0",
    "DEBUG_EVENT": "0",
    "DEBUG_LOCK": "0",
    "DEBUG_RESOURCE": "0",
    "DEBUG_MEMORY": "0",
    "DEBUG_REGISTRY": "0",
    "DEBUG_FILESYSTEM": "0",
    "DEBUG_STORAGE": "0",
    
    # Misc
    "UNREFERENCED_PARAMETER": "UNREFERENCED_PARAMETER",
    "KSPIN_LOCK": "KSPIN_LOCK",
    "KIRQL": "KIRQL",
    "KAFFINITY": "ULONG_PTR",
    "GROUP_AFFINITY": "GROUP_AFFINITY",
    "CONTEXT": "CONTEXT",
    "EXCEPTION_RECORD": "EXCEPTION_RECORD",
    "EXCEPTION_POINTERS": "EXCEPTION_POINTERS",
    "EXCEPTION_DISPOSITION": "ULONG",
    "KDPC": "KDPC",
    "PKDPC": "PKDPC",
    "IO_TIMER": "IO_TIMER",
    "PIO_TIMER": "PIO_TIMER",
    "FILE_OBJECT": "FILE_OBJECT",
    "PFILE_OBJECT": "PFILE_OBJECT",
    "DEVICE_OBJECT": "DEVICE_OBJECT",
    "PDEVICE_OBJECT": "PDEVICE_OBJECT",
    "DRIVER_OBJECT": "DRIVER_OBJECT",
    "PDRIVER_OBJECT": "PDRIVER_OBJECT",
    "DEVICE_EXTENSION": "DEVICE_EXTENSION",
    "PDEVICE_EXTENSION": "PDEVICE_EXTENSION",
    "IRP": "IRP",
    "PIRP": "PIRP",
    "MDL": "MDL",
    "PMDL": "PMDL",
    "EPROCESS": "EPROCESS",
    "PEPROCESS": "PEPROCESS",
    "ETHREAD": "ETHREAD",
    "PETHREAD": "PETHREAD",
    "KTHREAD": "KTHREAD",
    "PKTHREAD": "PKTHREAD",
    "KPROCESS": "KPROCESS",
    "PKPROCESS": "PKPROCESS",
    "OBJECT_TYPE": "POBJECT_TYPE",
    "POBJECT_TYPE": "POBJECT_TYPE",
    "OBJECT_HEADER": "OBJECT_HEADER",
    "POBJECT_HEADER": "POBJECT_HEADER",
    "OBJECT_HANDLE_TABLE": "OBJECT_HANDLE_TABLE",
    "POBJECT_HANDLE_TABLE": "POBJECT_HANDLE_TABLE",
    "HANDLE_TABLE": "HANDLE_TABLE",
    "PHANDLE_TABLE": "PHANDLE_TABLE",
    "OBJECT_DIRECTORY": "OBJECT_DIRECTORY",
    "POBJECT_DIRECTORY": "POBJECT_DIRECTORY",
    "OBJECT_SYMBOLIC_LINK": "OBJECT_SYMBOLIC_LINK",
    "POBJECT_SYMBOLIC_LINK": "POBJECT_SYMBOLIC_LINK",
    "OBJECT_TYPE_LIST": "OBJECT_TYPE_LIST",
    "POBJECT_TYPE_LIST": "POBJECT_TYPE_LIST",
    "ACCESS_ALLOWED_ACE": "ACCESS_ALLOWED_ACE",
    "PACCESS_ALLOWED_ACE": "PACCESS_ALLOWED_ACE",
    "ACCESS_DENIED_ACE": "ACCESS_DENIED_ACE",
    "PACCESS_DENIED_ACE": "PACCESS_DENIED_ACE",
    "SYSTEM_MANDATORY_LABEL_ACE": "SYSTEM_MANDATORY_LABEL_ACE",
    "PSYSTEM_MANDATORY_LABEL_ACE": "PSYSTEM_MANDATORY_LABEL_ACE",
    "ACE_HEADER": "ACE_HEADER",
    "PACE_HEADER": "PACE_HEADER",
    "LUID": "LUID",
    "PLUID": "PLUID",
    "LUID_AND_ATTRIBUTES": "LUID_AND_ATTRIBUTES",
    "PLUID_AND_ATTRIBUTES": "PLUID_AND_ATTRIBUTES",
    "TOKEN": "TOKEN",
    "PTOKEN": "PTOKEN",
    "TOKEN_SOURCE": "TOKEN_SOURCE",
    "PTOKEN_SOURCE": "PTOKEN_SOURCE",
    "TOKEN_CONTROL": "TOKEN_CONTROL",
    "PTOKEN_CONTROL": "PTOKEN_CONTROL",
    "TOKEN_DEFAULT_DACL": "TOKEN_DEFAULT_DACL",
    "PTOKEN_DEFAULT_DACL": "PTOKEN_DEFAULT_DACL",
    "SECURITY_ATTRIBUTES": "SECURITY_ATTRIBUTES",
    "PSECURITY_ATTRIBUTES": "PSECURITY_ATTRIBUTES",
    "TOKEN_USER": "TOKEN_USER",
    "PTOKEN_USER": "PTOKEN_USER",
    "TOKEN_GROUPS": "TOKEN_GROUPS",
    "PTOKEN_GROUPS": "PTOKEN_GROUPS",
    "TOKEN_PRIVILEGES": "TOKEN_PRIVILEGES",
    "PTOKEN_PRIVILEGES": "PTOKEN_PRIVILEGES",
    "TOKEN_OWNER": "TOKEN_OWNER",
    "PTOKEN_OWNER": "PTOKEN_OWNER",
    "TOKEN_PRIMARY_GROUP": "TOKEN_PRIMARY_GROUP",
    "PTOKEN_PRIMARY_GROUP": "PTOKEN_PRIMARY_GROUP",
    "TOKEN_DEFAULT_DACL": "TOKEN_DEFAULT_DACL",
    "PTOKEN_DEFAULT_DACL": "PTOKEN_DEFAULT_DACL",
    "TOKEN_STATISTICS": "TOKEN_STATISTICS",
    "PTOKEN_STATISTICS": "PTOKEN_STATISTICS",
    "TOKEN_GROUPS_AND_PRIVILEGES": "TOKEN_GROUPS_AND_PRIVILEGES",
    "PTOKEN_GROUPS_AND_PRIVILEGES": "PTOKEN_GROUPS_AND_PRIVILEGES",
    "TOKEN_INFORMATION_CLASS": "TOKEN_INFORMATION_CLASS",
    "SECURITY_IMPERSONATION_LEVEL": "SECURITY_IMPERSONATION_LEVEL",
    "SECURITY_CONTEXT_TRACKING_MODE": "SECURITY_CONTEXT_TRACKING_MODE",
    "TOKEN_TYPE": "TOKEN_TYPE",
    "TOKEN_ELEVATION_TYPE": "TOKEN_ELEVATION_TYPE",
    "TOKEN_INFORMATION_CLASS": "TOKEN_INFORMATION_CLASS",
    "SECURITY_DESCRIPTOR": "SECURITY_DESCRIPTOR",
    "PISECURITY_DESCRIPTOR": "PISECURITY_DESCRIPTOR",
    "SECURITY_DESCRIPTOR_RELATIVE": "SECURITY_DESCRIPTOR_RELATIVE",
    "PISECURITY_DESCRIPTOR_RELATIVE": "PISECURITY_DESCRIPTOR_RELATIVE",
    "SECURITY_QUALITY_OF_SERVICE": "SECURITY_QUALITY_OF_SERVICE",
    "PSECURITY_QUALITY_OF_SERVICE": "PSECURITY_QUALITY_OF_SERVICE",
    "SECURITY_CLIENT_CONTEXT": "SECURITY_CLIENT_CONTEXT",
    "PSECURITY_CLIENT_CONTEXT": "PSECURITY_CLIENT_CONTEXT",
    "SECURITY_DESCRIPTOR": "SECURITY_DESCRIPTOR",
    "PISECURITY_DESCRIPTOR": "PISECURITY_DESCRIPTOR",
    "SECURITY_DESCRIPTOR_RELATIVE": "SECURITY_DESCRIPTOR_RELATIVE",
    "PISECURITY_DESCRIPTOR_RELATIVE": "PISECURITY_DESCRIPTOR_RELATIVE",
    "SECURITY_QUALITY_OF_SERVICE": "SECURITY_QUALITY_OF_SERVICE",
    "PSECURITY_QUALITY_OF_SERVICE": "PSECURITY_QUALITY_OF_SERVICE",
    "SECURITY_CLIENT_CONTEXT": "SECURITY_CLIENT_CONTEXT",
    "PSECURITY_CLIENT_CONTEXT": "PSECURITY_CLIENT_CONTEXT",
}

# ReactOS-specific functions that need stubs
REACTOS_STUBS = {
    "RtlCreateTagHeap": "return NULL;",
    "RtlQueryRegistryValues": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCheckRegistryKey": "return STATUS_NOT_IMPLEMENTED;",
    "RtlAdjustPrivilege": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetEnvironmentVariable": "return STATUS_NOT_IMPLEMENTED;",
    "RtlQueryEnvironmentVariable_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDosPathNameToNtPathName_U": "return FALSE;",
    "RtlNtPathNameToDosPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetFullPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlInitializeHeap": "return NULL;",
    "RtlCreateHeap": "return NULL;",
    "RtlDestroyHeap": "return STATUS_SUCCESS;",
    "RtlAllocateHeap": "return NULL;",
    "RtlFreeHeap": "return TRUE;",
    "RtlSizeHeap": "return 0;",
    "RtlReAllocateHeap": "return NULL;",
    "RtlZeroHeap": "return STATUS_SUCCESS;",
    "RtlValidateHeap": "return TRUE;",
    "RtlGetProcessHeap": "return NULL;",
    "RtlGetDefaultHeap": "return NULL;",
    "RtlCreateHeap": "return NULL;",
    "RtlDestroyHeap": "return STATUS_SUCCESS;",
    "RtlAllocateHeap": "return NULL;",
    "RtlFreeHeap": "return TRUE;",
    "RtlSizeHeap": "return 0;",
    "RtlReAllocateHeap": "return NULL;",
    "RtlZeroHeap": "return STATUS_SUCCESS;",
    "RtlValidateHeap": "return TRUE;",
    "RtlGetProcessHeap": "return NULL;",
    "RtlGetDefaultHeap": "return NULL;",
    "RtlSetProcessHeap": "return NULL;",
    "RtlCreateTagHeap": "return NULL;",
    "RtlQueryRegistryValues": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCheckRegistryKey": "return STATUS_NOT_IMPLEMENTED;",
    "RtlAdjustPrivilege": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetEnvironmentVariable": "return STATUS_NOT_IMPLEMENTED;",
    "RtlQueryEnvironmentVariable_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDosPathNameToNtPathName_U": "return FALSE;",
    "RtlNtPathNameToDosPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetFullPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlInitializeHeap": "return NULL;",
    "RtlCreateHeap": "return NULL;",
    "RtlDestroyHeap": "return STATUS_SUCCESS;",
    "RtlAllocateHeap": "return NULL;",
    "RtlFreeHeap": "return TRUE;",
    "RtlSizeHeap": "return 0;",
    "RtlReAllocateHeap": "return NULL;",
    "RtlZeroHeap": "return STATUS_SUCCESS;",
    "RtlValidateHeap": "return TRUE;",
    "RtlGetProcessHeap": "return NULL;",
    "RtlGetDefaultHeap": "return NULL;",
    "RtlSetProcessHeap": "return NULL;",
    "RtlCreateTagHeap": "return NULL;",
    "RtlQueryRegistryValues": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCheckRegistryKey": "return STATUS_NOT_IMPLEMENTED;",
    "RtlAdjustPrivilege": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyEnvironment": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetEnvironmentVariable": "return STATUS_NOT_IMPLEMENTED;",
    "RtlQueryEnvironmentVariable_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDosPathNameToNtPathName_U": "return FALSE;",
    "RtlNtPathNameToDosPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetFullPathName_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlGetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlSetCurrentDirectory_U": "return STATUS_NOT_IMPLEMENTED;",
    "RtlCreateProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlDestroyProcessParameters": "return STATUS_NOT_IMPLEMENTED;",
    "RtlInitializeHeap": "return NULL;",
    "RtlCreateHeap": "return NULL;",
    "RtlDestroyHeap": "return STATUS_SUCCESS;",
    "RtlAllocateHeap": "return NULL;",
    "RtlFreeHeap": "return TRUE;",
    "RtlSizeHeap": "return 0;",
    "RtlReAllocateHeap": "return NULL;",
    "RtlZeroHeap": "return STATUS_SUCCESS;",
    "RtlValidateHeap": "return TRUE;",
    "RtlGetProcessHeap": "return NULL;",
    "RtlGetDefaultHeap": "return NULL;",
    "RtlSetProcessHeap": "return NULL;",
}


class AdvancedConverter:
    def __init__(self):
        self.stats = {
            "macros_replaced": 0,
            "types_found": 0,
            "stubs_added": 0,
        }
    
    def replace_macros(self, content: str) -> str:
        """Replace ReactOS macros with MinNT equivalents."""
        for macro, replacement in REACTOS_MACROS.items():
            # Only replace whole words
            pattern = r"\b" + re.escape(macro) + r"\b"
            if re.search(pattern, content):
                content = re.sub(pattern, replacement, content)
                self.stats["macros_replaced"] += 1
        return content
    
    def add_stubs(self, content: str) -> str:
        """Add stub implementations for missing functions."""
        for func_name, stub_body in REACTOS_STUBS.items():
            if func_name in content and f"NTSTATUS NTAPI {func_name}" not in content:
                # Add stub at the end of the file
                stub = f"\n/* Stub: {func_name} */\n"
                stub += f"NTSTATUS NTAPI {func_name}(...)\n"
                stub += f"{{\n    {stub_body}\n}}\n"
                content += stub
                self.stats["stubs_added"] += 1
        return content
    
    def convert_file(self, input_path: Path, output_path: Path) -> bool:
        """Convert a single file with advanced processing."""
        try:
            with open(input_path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            
            # Apply advanced conversions
            content = self.replace_macros(content)
            content = self.add_stubs(content)
            
            # Write output
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, "w", encoding="utf-8") as f:
                f.write(content)
            
            return True
            
        except Exception as e:
            print(f"Error converting {input_path}: {e}")
            return False
    
    def convert_directory(self, source_dir: Path, output_dir: Path):
        """Convert all C files in a directory."""
        for ext in [".c", ".h"]:
            for source_file in source_dir.rglob(f"*{ext}"):
                relative_path = source_file.relative_to(source_dir)
                output_file = output_dir / relative_path
                self.convert_file(source_file, output_file)


def main():
    """Main advanced conversion."""
    print("Advanced ReactOS → MinNT Converter")
    print("=" * 50)
    
    # Process the already-converted directories
    converted_dirs = [
        Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/boot/chain/smss_real"),
        Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/boot/chain/csrss_real"),
        Path("/home/dheavy/molecular-ai-factory/Server-V2/NT-6-BASE/minint-src/minint/boot/chain/winlogon_real"),
    ]
    
    converter = AdvancedConverter()
    
    for converted_dir in converted_dirs:
        if converted_dir.exists():
            print(f"\nProcessing {converted_dir.name}...")
            converter.convert_directory(converted_dir, converted_dir)  # In-place conversion
    
    print(f"\nMacros replaced: {converter.stats["macros_replaced"]}")
    print(f"Stubs added: {converter.stats["stubs_added"]}")


if __name__ == "__main__":
    main()
