/*
 * MinNT - rtl/rtlsupp.c
 * Runtime library support: CRT functions, RTL stubs, Nt system call stubs.
 * These are needed by the ReactOS-converted SMSS/CSRSS/Winlogon subsystem
 * files until the real implementations are wired in.
 */

#include <nt/ke.h>
#include <nt/dispatcher.h>
#include <nt/mm.h>
#include <nt/ob.h>
#include <ndk/obfuncs.h>
#include <nt/ex.h>
#include <nt/rtl.h>
#include <ndk/psfuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/setypes.h>
#include <ndk/lpcfuncs.h>
#include <nt/cm.h>
#include <nt/fs.h>
#include <nt/lpc.h>

/* External from fs/fs.c (static functions — we duplicate the logic) */
static NTSTATUS FsOpenFile(PCWSTR FileName, PFILE_OBJECT *OutFile);
static PFAT16_DIR_ENTRY FsCreateFileEntry(PCWSTR FileName, ULONG Size);

/* wcslen declaration from rtl.c */
SIZE_T wcslen(const wchar_t *s);

/* DbgPrint from hal.h */
VOID DbgPrint(const CHAR *Format, ...);

/* ── Global for PSEH2 stubs ─────────────────────────────────────────── */
volatile ULONG __seh_code;

/* ── Debug Macros ────────────────────────────────────────────────────── */
VOID NTAPI DbgBreakPoint(VOID)
{
    DbgPrint("DbgBreakPoint called\n");
}

/* ── Interlocked operations (GCC builtins) ──────────────────────────── */
LONG _InterlockedExchangeAdd(LONG volatile *Addend, LONG Value)
{
    return __sync_fetch_and_add(Addend, Value);
}

/* ── String functions (simple implementations) ──────────────────────── */
PWCHAR wcscpy(PWCHAR dst, PCWSTR src)
{
    PWCHAR d = dst;
    while ((*d++ = *src++));
    return dst;
}

PWCHAR wcsstr(PCWSTR str, PCWSTR substr)
{
    while (*str) {
        PCWSTR s = str;
        PCWSTR t = substr;
        while (*t && *s == *t) { s++; t++; }
        if (!*t) return (PWCHAR)str;
        str++;
    }
    return NULL;
}

SIZE_T RtlWStringLength(const WCHAR *S)
{
    SIZE_T len = 0;
    while (S[len]) len++;
    return len;
}

int _stricmp(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        char c1 = *s1 >= 'A' && *s1 <= 'Z' ? *s1 + 32 : *s1;
        char c2 = *s2 >= 'A' && *s2 <= 'Z' ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

int _wcsicmp(const unsigned short *s1, const unsigned short *s2)
{
    while (*s1 && *s2) {
        unsigned short c1 = *s1 >= L'A' && *s1 <= L'Z' ? *s1 + 32 : *s1;
        unsigned short c2 = *s2 >= L'A' && *s2 <= L'Z' ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

PWCHAR _wcsupr(PWCHAR str)
{
    PWCHAR p = str;
    while (*p) {
        if (*p >= L'a' && *p <= L'z') *p -= 32;
        p++;
    }
    return str;
}

/* ── Critical sections (spinlock-based, no recursion tracking) ────────── */
NTSTATUS NTAPI RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION Cs)
{
    if (!Cs) return STATUS_INVALID_PARAMETER;
    Cs->DebugInfo = NULL;
    Cs->LockCount = -1;
    Cs->RecursionCount = 0;
    Cs->OwningThread = NULL;
    Cs->LockSemaphore = NULL;
    Cs->SpinCount = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlEnterCriticalSection(PRTL_CRITICAL_SECTION Cs)
{
    if (!Cs) return STATUS_INVALID_PARAMETER;
    while (__sync_val_compare_and_swap(&Cs->LockCount, -1, 0) != -1) {
        __asm__ volatile("pause");
    }
    Cs->RecursionCount = 1;
    Cs->OwningThread = (HANDLE)(ULONG_PTR)KeGetCurrentThread();
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION Cs)
{
    if (!Cs) return STATUS_INVALID_PARAMETER;
    Cs->RecursionCount = 0;
    Cs->OwningThread = NULL;
    Cs->LockCount = -1;
    return STATUS_SUCCESS;
}

/* ── RTL string helpers ────────────────────────────────────────────── */
VOID NTAPI RtlInitAnsiString(PANSI_STRING Dst, PCSZ Src)
{
    if (Src) {
        ULONG len = 0;
        while (Src[len]) len++;
        Dst->Buffer = (PCHAR)Src;
        Dst->Length = (USHORT)len;
        Dst->MaximumLength = (USHORT)(len + 1);
    } else {
        Dst->Buffer = NULL;
        Dst->Length = 0;
        Dst->MaximumLength = 0;
    }
}

VOID NTAPI RtlInitEmptyAnsiString(PANSI_STRING Dst, PCHAR Buffer, USHORT MaximumLength)
{
    Dst->Buffer = Buffer;
    Dst->Length = 0;
    Dst->MaximumLength = MaximumLength;
    if (Buffer) Buffer[0] = 0;
}

NTSTATUS NTAPI RtlCreateUnicodeString(PUNICODE_STRING Dst, PCWSTR Src)
{
    if (!Dst || !Src) return STATUS_INVALID_PARAMETER;
    ULONG len = 0;
    while (Src[len]) len++;
    USHORT byteLen = (USHORT)(len * sizeof(WCHAR));
    Dst->Buffer = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool, byteLen + sizeof(WCHAR), 'rUSt');
    if (!Dst->Buffer) return STATUS_NO_MEMORY;
    wcscpy(Dst->Buffer, Src);
    Dst->Length = byteLen;
    Dst->MaximumLength = byteLen + sizeof(WCHAR);
    return STATUS_SUCCESS;
}

WCHAR NTAPI RtlUpcaseUnicodeChar(WCHAR c)
{
    return (c >= L'a' && c <= L'z') ? c - 32 : c;
}

BOOLEAN NTAPI RtlPrefixUnicodeString(PUNICODE_STRING Str1, PUNICODE_STRING Str2, BOOLEAN CaseInsensitive)
{
    if (!Str1 || !Str2 || Str1->Length > Str2->Length) return FALSE;
    ULONG cmpLen = Str1->Length / sizeof(WCHAR);
    for (ULONG i = 0; i < cmpLen; i++) {
        WCHAR c1 = Str1->Buffer[i];
        WCHAR c2 = Str2->Buffer[i];
        if (CaseInsensitive) {
            c1 = RtlUpcaseUnicodeChar(c1);
            c2 = RtlUpcaseUnicodeChar(c2);
        }
        if (c1 != c2) return FALSE;
    }
    return TRUE;
}

NTSTATUS NTAPI RtlUnicodeStringToInteger(PUNICODE_STRING Str, ULONG Base, PULONG Value)
{
    if (!Str || !Value) return STATUS_INVALID_PARAMETER;
    *Value = 0;
    ULONG len = Str->Length / sizeof(WCHAR);
    if (len == 0) return STATUS_SUCCESS;
    ULONG i = 0;
    BOOLEAN neg = FALSE;
    if (Str->Buffer[0] == L'-') { neg = TRUE; i++; }
    else if (Str->Buffer[0] == L'+') i++;
    for (; i < len; i++) {
        WCHAR c = Str->Buffer[i];
        ULONG digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = 10 + c - L'a';
        else if (c >= L'A' && c <= L'F') digit = 10 + c - L'A';
        else break;
        if (digit >= Base) break;
        *Value = *Value * Base + digit;
    }
    if (neg) *Value = (ULONG)(-(LONG)*Value);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlDosSearchPath_U(PCWSTR Path, PCWSTR FileName, PCWSTR Extension,
                                  ULONG OutLength, PWCHAR OutPart, PWCHAR *FilePart)
{
    UNICODE_STRING fullPath;
    NTSTATUS status;
    ULONG pathLen, nameLen, extLen;
    PWCHAR buf;

    if (!FileName || !OutPart || OutLength < sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;

    pathLen = Path ? wcslen(Path) : 0;
    nameLen = wcslen(FileName);
    extLen = Extension ? wcslen(Extension) : 0;

    /* Build path\FileName[Extension] */
    fullPath.Length = 0;
    fullPath.MaximumLength = (USHORT)((pathLen + 1 + nameLen + extLen + 1) * sizeof(WCHAR));
    fullPath.Buffer = ExAllocatePoolWithTag(NonPagedPool, fullPath.MaximumLength + sizeof(WCHAR), TAG_NAME);
    if (!fullPath.Buffer)
        return STATUS_NO_MEMORY;

    if (pathLen > 0) {
        RtlCopyMemory(fullPath.Buffer, Path, pathLen * sizeof(WCHAR));
        fullPath.Buffer[pathLen] = L'\\';
        RtlCopyMemory(&fullPath.Buffer[pathLen + 1], FileName, nameLen * sizeof(WCHAR));
        fullPath.Length = (USHORT)((pathLen + 1 + nameLen) * sizeof(WCHAR));
        if (extLen > 0) {
            RtlCopyMemory(&fullPath.Buffer[pathLen + 1 + nameLen], Extension, extLen * sizeof(WCHAR));
            fullPath.Length = (USHORT)((pathLen + 1 + nameLen + extLen) * sizeof(WCHAR));
        }
    } else {
        RtlCopyMemory(fullPath.Buffer, FileName, nameLen * sizeof(WCHAR));
        fullPath.Length = (USHORT)(nameLen * sizeof(WCHAR));
        if (extLen > 0) {
            RtlCopyMemory(&fullPath.Buffer[nameLen], Extension, extLen * sizeof(WCHAR));
            fullPath.Length = (USHORT)((nameLen + extLen) * sizeof(WCHAR));
        }
    }
    fullPath.Buffer[fullPath.Length / sizeof(WCHAR)] = L'\0';

    /* Check if file exists */
    status = FsOpenFile(fullPath.Buffer, NULL);
    if (NT_SUCCESS(status)) {
        /* Copy full path to output buffer */
        if (OutLength >= (fullPath.Length + sizeof(WCHAR))) {
            RtlCopyMemory(OutPart, fullPath.Buffer, fullPath.Length + sizeof(WCHAR));
            if (FilePart)
                *FilePart = OutPart;
            ExFreePoolWithTag(fullPath.Buffer, TAG_NAME);
            return STATUS_SUCCESS;
        }
        ExFreePoolWithTag(fullPath.Buffer, TAG_NAME);
        return STATUS_BUFFER_OVERFLOW;
    }

    ExFreePoolWithTag(fullPath.Buffer, TAG_NAME);
    return STATUS_NO_SUCH_FILE;
}

/* ── RTL StringCb functions (safe string stubs) ─────────────────────── */
NTSTATUS NTAPI RtlStringCbCopyW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszSrc)
{
    if (!pszDest || !pszSrc || cbDest < sizeof(WCHAR)) return STATUS_INVALID_PARAMETER;
    SIZE_T i = 0;
    while (pszSrc[i] && (i + 1) * sizeof(WCHAR) <= cbDest) {
        pszDest[i] = pszSrc[i];
        i++;
    }
    if (i * sizeof(WCHAR) < cbDest) {
        pszDest[i] = 0;
        return STATUS_SUCCESS;
    }
    pszDest[cbDest / sizeof(WCHAR) - 1] = 0;
    return STATUS_BUFFER_OVERFLOW;
}

NTSTATUS NTAPI RtlStringCbCatW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszSrc)
{
    if (!pszDest || !pszSrc || cbDest < sizeof(WCHAR)) return STATUS_INVALID_PARAMETER;
    SIZE_T destLen = 0;
    while (pszDest[destLen]) destLen++;
    SIZE_T srcIdx = 0;
    while (pszSrc[srcIdx] && (destLen + srcIdx + 1) * sizeof(WCHAR) < cbDest) {
        pszDest[destLen + srcIdx] = pszSrc[srcIdx];
        srcIdx++;
    }
    pszDest[destLen + srcIdx] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlStringCbPrintfW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszFormat, ...)
{
    /* Minimal printf: handles %s, %d, %u, %x, %p via varargs.
       In freestanding builds we access args directly. */
    SIZE_T pos = 0;
    PCWSTR fmt;
    ULONG_PTR *ap = (ULONG_PTR *)&pszFormat + 1;

    if (!pszDest || cbDest == 0) return STATUS_INVALID_PARAMETER;

    fmt = pszFormat;

    while (*fmt && (pos + 4) * sizeof(WCHAR) < cbDest) {
        if (*fmt == L'%') {
            fmt++;
            switch (*fmt) {
            case L's': {
                PCWSTR str = (PCWSTR)*ap++;
                if (!str) str = L"(null)";
                while (*str && (pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = *str++;
                break;
            }
            case L'd': {
                long val = (long)*ap++;
                char buf[32];
                int i = 0, neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                do { buf[i++] = '0' + (val % 10); val /= 10; } while (val && i < 31);
                if (neg && (pos + 1) * sizeof(WCHAR) < cbDest) pszDest[pos++] = L'-';
                while (--i >= 0 && (pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = buf[i];
                break;
            }
            case L'u': {
                unsigned long val = (unsigned long)*ap++;
                char buf[32];
                int i = 0;
                do { buf[i++] = '0' + (val % 10); val /= 10; } while (val && i < 31);
                while (--i >= 0 && (pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = buf[i];
                break;
            }
            case L'x': {
                unsigned long val = (unsigned long)*ap++;
                char hex[] = "0123456789abcdef";
                char buf[16];
                int i = 0;
                if (val == 0) { buf[i++] = '0'; }
                else {
                    while (val && i < 15) { buf[i++] = hex[val & 0xF]; val >>= 4; }
                }
                while (--i >= 0 && (pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = buf[i];
                break;
            }
            case L'p': {
                unsigned long val = (unsigned long)(ULONG_PTR)*ap++;
                char hex[] = "0123456789abcdef";
                char buf[16];
                int i = 0;
                buf[i++] = '0'; buf[i++] = 'x';
                for (int j = 28; j >= 0; j -= 4) {
                    unsigned nibble = (val >> j) & 0xF;
                    if (nibble || i < 10) buf[i++] = hex[nibble];
                }
                while (--i >= 0 && (pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = buf[i];
                break;
            }
            case L'%':
                if ((pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = L'%';
                break;
            default:
                if ((pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = L'%';
                if ((pos + 1) * sizeof(WCHAR) < cbDest)
                    pszDest[pos++] = *fmt;
                break;
            }
        } else {
            pszDest[pos++] = *fmt;
        }
        fmt++;
    }

    pszDest[pos] = L'\0';
    return STATUS_SUCCESS;
}

/* ── RTL misc ────────────────────────────────────────────────────────── */
LUID NTAPI RtlConvertUlongToLuid(ULONG Ulong)
{
    LUID l;
    l.LowPart = Ulong;
    l.HighPart = 0;
    return l;
}

NTSTATUS NTAPI RtlCreateEnvironment(BOOLEAN CloneCurrent, PVOID *Environment)
{
    /* Create a basic environment block: PATH=, SYSTEMROOT=, COMSPEC= */
    static const WCHAR pathVal[] = L"PATH=\\SystemRoot\\system32";
    static const WCHAR sysRootVal[] = L"SYSTEMROOT=\\SystemRoot";
    static const WCHAR comSpecVal[] = L"COMSPEC=\\SystemRoot\\system32\\cmd.exe";
    static const WCHAR nullTerminator[] = L"";
    SIZE_T totalLen;
    PVOID env;

    if (!Environment) return STATUS_INVALID_PARAMETER;

    totalLen = (wcslen(pathVal) + wcslen(sysRootVal) + wcslen(comSpecVal) +
                4) * sizeof(WCHAR);  /* 4 null terminators */

    env = ExAllocatePoolWithTag(NonPagedPool, totalLen, TAG_NAME);
    if (!env) return STATUS_NO_MEMORY;

    RtlCopyMemory(env, pathVal, wcslen(pathVal) * sizeof(WCHAR) + sizeof(WCHAR));
    RtlCopyMemory((PVOID)((ULONG_PTR)env + wcslen(pathVal) * sizeof(WCHAR)),
                  sysRootVal, wcslen(sysRootVal) * sizeof(WCHAR) + sizeof(WCHAR));
    RtlCopyMemory((PVOID)((ULONG_PTR)env + (wcslen(pathVal) + wcslen(sysRootVal) + 2) * sizeof(WCHAR)),
                  comSpecVal, wcslen(comSpecVal) * sizeof(WCHAR) + sizeof(WCHAR));
    /* Final null terminator */
    *(PWCHAR)((ULONG_PTR)env + totalLen - sizeof(WCHAR)) = L'\0';

    *Environment = env;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlQueryEnvironmentVariable_U(PVOID Environment, PUNICODE_STRING Name, PUNICODE_STRING Value)
{
    /* Minimal: just return STATUS_NOT_FOUND for now.
       Real impl would parse env block and match Name. */
    UNREFERENCED_PARAMETER(Environment);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

PVOID NTAPI RtlCreateTagHeap(PVOID HeapHandle, ULONG Flags, PCWSTR Tag, PCWSTR Desc)
{
    UNREFERENCED_PARAMETER(HeapHandle);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Tag);
    UNREFERENCED_PARAMETER(Desc);
    return HeapHandle ? HeapHandle : (PVOID)1;
}

NTSTATUS NTAPI RtlGetSetBootStatusData(ULONG Which, PVOID Buffer, ULONG BufferSize, PULONG ReturnSize)
{
    UNREFERENCED_PARAMETER(Which);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferSize);
    if (ReturnSize) *ReturnSize = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlLockBootStatusData(PVOID *Handle)
{
    UNREFERENCED_PARAMETER(Handle);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlUnlockBootStatusData(PVOID Handle)
{
    UNREFERENCED_PARAMETER(Handle);
    return STATUS_SUCCESS;
}

/* ── Nt system call stubs (definitions match ndk_shim.c declarations) ── */

/* ---- NtOpenFile: open a file via the I/O Manager ----------------------- */
NTSTATUS NTAPI NtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess,
                          POBJECT_ATTRIBUTES ObjectAttributes,
                          PIO_STATUS_BLOCK IoStatusBlock,
                          ULONG ShareAccess, ULONG OpenOptions)
{
    PVOID fileNameBuf;
    ULONG nameLen;
    NTSTATUS status;

    if (!FileHandle || !ObjectAttributes || !ObjectAttributes->ObjectName)
        return STATUS_INVALID_PARAMETER;

    /* Get the file name from object attributes */
    nameLen = ObjectAttributes->ObjectName->Length;
    fileNameBuf = ExAllocatePoolWithTag(NonPagedPool, nameLen + sizeof(WCHAR), TAG_NAME);
    if (!fileNameBuf)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(fileNameBuf, ObjectAttributes->ObjectName->Buffer, nameLen);
    ((WCHAR*)fileNameBuf)[nameLen / sizeof(WCHAR)] = L'\0';

    /* Call the real NtCreateFile which handles both open and create */
    status = NtCreateFile(FileHandle,
                          DesiredAccess,
                          NULL,  /* SecurityDescriptor */
                          IoStatusBlock,
                          NULL,  /* AllocationSize */
                          0,     /* FileAttributes */
                          ShareAccess,
                          OpenOptions,  /* CreateDisposition */
                          0,           /* CreateOptions */
                          fileNameBuf, /* EaBuffer */
                          nameLen);    /* EaLength */

    ExFreePoolWithTag(fileNameBuf, TAG_NAME);
    return status;
}

/* ---- NtQueryVolumeInformationFile: query filesystem volume info -------- */
NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID FsInformation, ULONG Length,
    ULONG FsInformationClass)
{
    PFILE_OBJECT file;
    PVOID body;
    NTSTATUS status;

    if (!FsInformation || Length == 0) return STATUS_INVALID_PARAMETER;

    status = ObReferenceObjectByHandle(FileHandle, NULL, &body);
    if (!NT_SUCCESS(status)) {
        if (IoStatusBlock) {
            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
        }
        return status;
    }
    file = (PFILE_OBJECT)body;

    switch ((FILE_FS_INFORMATION_CLASS)FsInformationClass) {
    case FileFsVolumeInformation: {
        PFILE_FS_VOLUME_INFORMATION info = (PFILE_FS_VOLUME_INFORMATION)FsInformation;
        if (Length < sizeof(FILE_FS_VOLUME_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_FS_VOLUME_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->VolumeCreationTime.QuadPart = 0;
        info->VolumeSerialNumber = 0x12345678;
        info->VolumeLabelLength = 0;
        info->SupportsObjects = FALSE;
        info->VolumeLabel[0] = L'\0';
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_FS_VOLUME_INFORMATION);
        }
        break;
    }
    case FileFsSizeInformation: {
        PFILE_FS_SIZE_INFORMATION info = (PFILE_FS_SIZE_INFORMATION)FsInformation;
        if (Length < sizeof(FILE_FS_SIZE_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_FS_SIZE_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->TotalAllocationUnits.QuadPart = 0x100000;
        info->AvailableAllocationUnits.QuadPart = 0x80000;
        info->SectorsPerAllocationUnit = 4;
        info->BytesPerSector = 512;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_FS_SIZE_INFORMATION);
        }
        break;
    }
    case FileFsDeviceInformation: {
        PFILE_FS_DEVICE_INFORMATION info = (PFILE_FS_DEVICE_INFORMATION)FsInformation;
        if (Length < sizeof(FILE_FS_DEVICE_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_FS_DEVICE_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->DeviceType = FILE_DEVICE_DISK;
        info->Characteristics = FILE_REMOTE_DEVICE | FILE_READ_ONLY_DEVICE;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_FS_DEVICE_INFORMATION);
        }
        break;
    }
    default:
        ObDereferenceObject(body);
        return STATUS_INVALID_PARAMETER;
    }

    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}

/* ---- NtQueryInformationFile: query file info --------------------------- */
NTSTATUS NTAPI NtQueryInformationFile(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
    PFILE_OBJECT file;
    PVOID body;
    NTSTATUS status;
    PFAT16_DIR_ENTRY entry;

    if (!FileInformation || Length == 0) return STATUS_INVALID_PARAMETER;

    status = ObReferenceObjectByHandle(FileHandle, NULL, &body);
    if (!NT_SUCCESS(status)) {
        if (IoStatusBlock) {
            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
        }
        return status;
    }
    file = (PFILE_OBJECT)body;
    entry = (PFAT16_DIR_ENTRY)file->FsContext;

    switch (FileInformationClass) {
    case FileBasicInformation: {
        PFILE_BASIC_INFORMATION info = (PFILE_BASIC_INFORMATION)FileInformation;
        if (Length < sizeof(FILE_BASIC_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_BASIC_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->CreationTime.QuadPart = 0;
        info->LastAccessTime.QuadPart = 0;
        info->LastWriteTime.QuadPart = 0;
        info->ChangeTime.QuadPart = 0;
        info->FileAttributes = entry ? FAT16_ATTR_ARCHIVE : 0;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_BASIC_INFORMATION);
        }
        break;
    }
    case FileStandardInformation: {
        PFILE_STANDARD_INFORMATION info = (PFILE_STANDARD_INFORMATION)FileInformation;
        if (Length < sizeof(FILE_STANDARD_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_STANDARD_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->AllocationSize.QuadPart = entry ? (ULONG64)entry->FileSize * 512 : 0;
        info->EndOfFile.QuadPart = entry ? (ULONG64)entry->FileSize : 0;
        info->NumberOfLinks = 1;
        info->DeletePending = file->DeletePending;
        info->Directory = FALSE;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_STANDARD_INFORMATION);
        }
        break;
    }
    case FilePositionInformation: {
        PFILE_POSITION_INFORMATION info = (PFILE_POSITION_INFORMATION)FileInformation;
        if (Length < sizeof(FILE_POSITION_INFORMATION)) {
            ObDereferenceObject(body);
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_BUFFER_TOO_SMALL;
                IoStatusBlock->Information = sizeof(FILE_POSITION_INFORMATION);
            }
            return STATUS_BUFFER_TOO_SMALL;
        }
        info->CurrentByteOffset.QuadPart = (ULONG64)file->CurrentOffset;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = sizeof(FILE_POSITION_INFORMATION);
        }
        break;
    }
    default:
        ObDereferenceObject(body);
        return STATUS_INVALID_PARAMETER;
    }

    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}

/* ---- NtSetInformationFile: set file info ------------------------------- */
NTSTATUS NTAPI NtSetInformationFile(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length,
    FILE_INFORMATION_CLASS FileInformationClass)
{
    PFILE_OBJECT file;
    PVOID body;
    NTSTATUS status;

    if (!FileInformation) return STATUS_INVALID_PARAMETER;

    status = ObReferenceObjectByHandle(FileHandle, NULL, &body);
    if (!NT_SUCCESS(status)) {
        if (IoStatusBlock) {
            IoStatusBlock->Status = status;
            IoStatusBlock->Information = 0;
        }
        return status;
    }
    file = (PFILE_OBJECT)body;

    switch (FileInformationClass) {
    case FileDispositionInformation: {
        PFILE_DISPOSITION_INFORMATION info = (PFILE_DISPOSITION_INFORMATION)FileInformation;
        if (Length < sizeof(FILE_DISPOSITION_INFORMATION)) {
            ObDereferenceObject(body);
            return STATUS_BUFFER_TOO_SMALL;
        }
        file->DeletePending = info->DeleteFile;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = info->DeleteFile;
        }
        break;
    }
    case FilePositionInformation: {
        PFILE_POSITION_INFORMATION info = (PFILE_POSITION_INFORMATION)FileInformation;
        if (Length < sizeof(FILE_POSITION_INFORMATION)) {
            ObDereferenceObject(body);
            return STATUS_BUFFER_TOO_SMALL;
        }
        file->CurrentOffset = (ULONG)info->CurrentByteOffset.QuadPart;
        if (IoStatusBlock) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            IoStatusBlock->Information = 0;
        }
        break;
    }
    default:
        ObDereferenceObject(body);
        return STATUS_INVALID_PARAMETER;
    }

    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}

/* ---- NtCreatePagingFile: create paging file (no-op for now) ------------ */
NTSTATUS NTAPI NtCreatePagingFile(PUNICODE_STRING PageFileName,
    PLARGE_INTEGER MinimumSize, PLARGE_INTEGER MaximumSize, ULONG Priority)
{
    UNREFERENCED_PARAMETER(PageFileName);
    UNREFERENCED_PARAMETER(MinimumSize);
    UNREFERENCED_PARAMETER(MaximumSize);
    UNREFERENCED_PARAMETER(Priority);
    /* Paging file creation is handled by the pagefile subsystem module.
       For now this is a no-op; the memory manager uses the RAM disk. */
    return STATUS_SUCCESS;
}

/* ---- NtCreateEvent: create a kernel event object ----------------------- */
NTSTATUS NTAPI NtCreateEvent(PHANDLE EventHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, ULONG EventType, BOOLEAN InitialState)
{
    PKEVENT event;
    NTSTATUS status;
    UNICODE_STRING name;

    if (!EventHandle) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(EventHandle, sizeof(HANDLE));

    /* Validate event type */
    if (EventType > SynchronizationEvent) {
        return STATUS_INVALID_PARAMETER;
    }

    /* Create the event object via the object manager */
    if (ObjectAttributes && ObjectAttributes->ObjectName) {
        name = *ObjectAttributes->ObjectName;
    } else {
        name.Length = 0;
        name.MaximumLength = 0;
        name.Buffer = NULL;
    }
    status = ObCreateObject(NULL, sizeof(KEVENT),
                            ObjectAttributes ? &name : NULL, (PVOID *)&event);
    if (!NT_SUCCESS(status)) return status;

    /* Initialize the event */
    KeInitializeEvent(event, EventType, InitialState);

    /* Insert handle */
    status = ObInsertHandle(event, EventHandle);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(event);
    }

    return status;
}

/* ---- NtSetEvent: set event to signaled state --------------------------- */
NTSTATUS NTAPI NtSetEvent(HANDLE EventHandle, PLONG PreviousState)
{
    PKEVENT event;
    NTSTATUS status;
    PVOID body;

    status = ObReferenceObjectByHandle(EventHandle, NULL, &body);
    if (!NT_SUCCESS(status)) return status;

    event = (PKEVENT)body;
    LONG prev = KeSetEvent(event, 0, FALSE);
    if (PreviousState) *PreviousState = prev;

    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}

/* ---- NtDelayExecution: delay thread execution -------------------------- */
NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
    LARGE_INTEGER timeout;

    if (!Interval) return STATUS_INVALID_PARAMETER;

    /* Convert 100-ns units (negative = relative, positive = absolute)
       to the KeWaitForSingleObject timeout format. */
    timeout.QuadPart = Interval->QuadPart;

    /* Use a kernel event as a wait object. Create one, signal it after
       the delay via KeDelayExecutionThread. */
    KEVENT delayEvent;
    KeInitializeEvent(&delayEvent, NotificationEvent, FALSE);

    /* KeWaitForSingleObject with a timeout handles relative delays.
       The Interval is in 100-ns units; negative means relative delay. */
    return KeWaitForSingleObject(&delayEvent, Executive, KernelMode, Alertable, &timeout);
}

/* ---- NtOpenProcess: open a handle to a process ------------------------- */
NTSTATUS NTAPI NtOpenProcess(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes, PCLIENT_ID ClientId)
{
    PEPROCESS process;
    NTSTATUS status;

    if (!ProcessHandle) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(ProcessHandle, sizeof(HANDLE));

    /* If ClientId is provided, look up process by PID */
    if (ClientId) {
        PLIST_ENTRY entry;
        process = NULL;

        entry = PsActiveProcessHead.Flink;
        while (entry != &PsActiveProcessHead) {
            PEPROCESS cur = CONTAINING_RECORD(entry, EPROCESS, ActiveProcessLinks);
            if (cur->UniqueProcessId == ClientId->UniqueProcess) {
                process = cur;
                break;
            }
            entry = entry->Flink;
        }

        if (!process) return STATUS_NO_SUCH_PROCESS;

        /* Insert a handle to the found process */
        status = ObInsertHandle(process, ProcessHandle);
        return status;
    }

    /* If ObjectAttributes is provided, look up by name */
    if (ObjectAttributes && ObjectAttributes->ObjectName) {
        PLIST_ENTRY entry;
        UNICODE_STRING targetName;
        RtlInitUnicodeString(&targetName, ObjectAttributes->ObjectName->Buffer);

        entry = PsActiveProcessHead.Flink;
        while (entry != &PsActiveProcessHead) {
            PEPROCESS cur = CONTAINING_RECORD(entry, EPROCESS, ActiveProcessLinks);
            UNICODE_STRING curName;
            CHAR nameBuf[16];
            RtlZeroMemory(nameBuf, sizeof(nameBuf));
            RtlCopyMemory(nameBuf, cur->ImageFileName, 15);
            RtlInitUnicodeString(&curName, nameBuf);

            if (RtlEqualUnicodeString(&targetName, &curName, FALSE)) {
                process = cur;
                break;
            }
            entry = entry->Flink;
        }

        if (!process) return STATUS_OBJECT_NAME_NOT_FOUND;
        status = ObInsertHandle(process, ProcessHandle);
        return status;
    }

    return STATUS_INVALID_PARAMETER;
}

/* ---- NtAdjustPrivilegesToken: adjust token privileges -------------------- */
NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE TokenHandle, BOOLEAN DisableAllPrivileges,
    PTOKEN_PRIVILEGES NewState, ULONG BufferLength, PTOKEN_PRIVILEGES PreviousState,
    PULONG ReturnLength)
{
    PTOKEN token;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(DisableAllPrivileges);
    UNREFERENCED_PARAMETER(NewState);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(PreviousState);
    UNREFERENCED_PARAMETER(ReturnLength);

    /* Validate token handle — accept any non-NULL handle as valid */
    if (!TokenHandle || TokenHandle == (HANDLE)0xFFFFFFFFFFFFFFFF ||
        TokenHandle == (HANDLE)0x1000) {
        return STATUS_SUCCESS;
    }

    /* Try to reference the object; if it fails, treat as valid token */
    status = ObReferenceObjectByHandle(TokenHandle, NULL, (PVOID *)&token);
    if (!NT_SUCCESS(status)) {
        /* Token handle not in object manager — treat as a valid synthetic token */
        return STATUS_SUCCESS;
    }

    ObDereferenceObject(token);
    return STATUS_SUCCESS;
}

/* ---- NtInitializeRegistry: initialize the registry --------------------- */
NTSTATUS NTAPI NtInitializeRegistry(ULONG Which)
{
    NTSTATUS status;

    /* CmInitSystem is called during kernel init; this is idempotent */
    status = CmInitSystem();
    if (!NT_SUCCESS(status)) return status;

    /* Which flag: 0 = both, 1 = system config, 2 = boot config */
    UNREFERENCED_PARAMETER(Which);

    return STATUS_SUCCESS;
}

/* ---- NtDeleteValueKey: delete a value from a registry key -------------- */
NTSTATUS NTAPI NtDeleteValueKey(HANDLE KeyHandle, PUNICODE_STRING ValueName)
{
    PCM_KEY_NODE key;
    NTSTATUS status;

    if (!ValueName) return STATUS_INVALID_PARAMETER;

    status = ObReferenceObjectByHandle(KeyHandle, NULL, (PVOID *)&key);
    if (!NT_SUCCESS(status)) return status;

    /* Walk the value list and remove the matching value */
    {
        PLIST_ENTRY e, next;
        PCM_KEY_VALUE value;

        for (e = key->ValueListHead.Flink;
             e != &key->ValueListHead;
             e = next) {
            next = e->Flink;
            value = CONTAINING_RECORD(e, CM_KEY_VALUE, ValueListEntry);
            if (RtlEqualUnicodeString(&value->Name, ValueName, TRUE)) {
                RemoveEntryList(e);
                key->ValueCount--;
                if (value->Data) {
                    ExFreePoolWithTag(value->Data, TAG_NAME);
                }
                ExFreePoolWithTag(value, TAG_PROC);
                ObDereferenceObject(key);
                return STATUS_SUCCESS;
            }
        }
    }

    ObDereferenceObject(key);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/* ── LPC stubs ─────────────────────────────────────────────────────────── */
NTSTATUS NTAPI NtCompleteConnectPort(HANDLE PortHandle)
{
    PLPC_PORT port;
    NTSTATUS status;
    PVOID body;

    status = ObReferenceObjectByHandle(PortHandle, NULL, &body);
    if (!NT_SUCCESS(status)) return status;

    port = (PLPC_PORT)body;

    /* Mark the port as connected by setting the flag */
    port->Flags |= LPC_PORT_FLAG_NEVER_DISCONNECT;

    /* Signal the message event so any waiting threads wake up */
    KeSetEvent(&port->MsgEvent, 0, FALSE);

    ObDereferenceObject(body);
    return STATUS_SUCCESS;
}



NTSTATUS NTAPI NtSetSystemInformation(ULONG InfoClass, PVOID Info, ULONG Length)
{
    /* System information classes are not used in MinNT runtime.
       This is called during subsystem init but has no effect. */
    UNREFERENCED_PARAMETER(InfoClass);
    UNREFERENCED_PARAMETER(Info);
    UNREFERENCED_PARAMETER(Length);
    return STATUS_SUCCESS;
}

/* ── LDR stubs ────────────────────────────────────────────────────────── */
NTSTATUS NTAPI LdrQueryImageFileExecutionOptions(PUNICODE_STRING SubsystemName,
    PCWSTR OptionName, ULONG Type, PVOID Buffer, ULONG BufferSize, PULONG ReturnLength)
{
    UNICODE_STRING path;
    UNICODE_STRING valueName;
    WCHAR pathBuf[MAX_PATH];
    ULONG actualLength;
    ULONG dataType;
    UCHAR dataBuffer[256];
    NTSTATUS status;

    if (!SubsystemName || !OptionName) return STATUS_INVALID_PARAMETER;

    /* Build the registry path: Image File Execution Options\<SubsystemName> */
    RtlZeroMemory(pathBuf, sizeof(pathBuf));
    ULONG idx = 0;
    const WCHAR ifeoPath[] = L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
    while (ifeoPath[idx] && idx < MAX_PATH - 1) {
        pathBuf[idx] = ifeoPath[idx];
        idx++;
    }
    ULONG subLen = SubsystemName->Length / sizeof(WCHAR);
    if (subLen >= MAX_PATH - idx) subLen = MAX_PATH - idx - 1;
    RtlCopyMemory(&pathBuf[idx], SubsystemName->Buffer, subLen * sizeof(WCHAR));
    idx += subLen;
    pathBuf[idx] = L'\0';

    RtlInitUnicodeString(&path, pathBuf);

    /* Look up the option value name */
    RtlInitUnicodeString(&valueName, OptionName);

    /* Query the registry for this value */
    status = CmQueryValue(CmGetRootKey(), &valueName, &dataType, dataBuffer,
                          sizeof(dataBuffer), &actualLength);

    if (!NT_SUCCESS(status)) {
        /* Path doesn't exist or value not found — return not found */
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    /* Copy the value to the output buffer */
    if (Type == REG_SZ || Type == REG_EXPAND_SZ) {
        /* String value — copy as wide string */
        if (BufferSize >= actualLength + sizeof(WCHAR)) {
            RtlCopyMemory(Buffer, dataBuffer, actualLength);
            ((PWSTR)Buffer)[actualLength / sizeof(WCHAR)] = L'\0';
        }
        if (ReturnLength) *ReturnLength = actualLength + sizeof(WCHAR);
    } else if (Type == REG_DWORD) {
        /* DWORD value */
        if (BufferSize >= sizeof(ULONG)) {
            RtlCopyMemory(Buffer, dataBuffer, sizeof(ULONG));
        }
        if (ReturnLength) *ReturnLength = sizeof(ULONG);
    } else {
        /* Binary data */
        if (BufferSize >= actualLength) {
            RtlCopyMemory(Buffer, dataBuffer, actualLength);
        }
        if (ReturnLength) *ReturnLength = actualLength;
    }

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI LdrVerifyImageMatchesChecksum(HANDLE FileHandle, ULONG Length)
{
    UNREFERENCED_PARAMETER(FileHandle);
    UNREFERENCED_PARAMETER(Length);
    return STATUS_SUCCESS;
}
