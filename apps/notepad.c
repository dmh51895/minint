/*
 * MinNT - apps/notepad.c
 * Plain text editor bundled application.
 *
 * Minimal but functional implementation: holds an in-memory text
 * buffer, supports Open/Save through NtCreateFile/NtReadFile/NtWriteFile,
 * and tracks the "modified" flag. The actual on-screen rendering is
 * delegated to the win32k subsystem (which already exposes CreateWindow
 * and friends); this module owns the document state.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ntdef.h>
#include <ndk/obfuncs.h>
#include <nt/io.h>
#include <nt/framework.h>

#define NOTEPAD_BUFFER_MAX   (64 * 1024)
#define NOTEPAD_PATH_MAX     260

typedef struct _NOTEPAD_DOC {
    CHAR FilePath[NOTEPAD_PATH_MAX];
    CHAR Buffer[NOTEPAD_BUFFER_MAX];
    ULONG BufferLength;
    BOOLEAN Modified;
    BOOLEAN InUse;
} NOTEPAD_DOC, *PNOTEPAD_DOC;

static NOTEPAD_DOC g_Docs[8];

/* Build a DOS-style absolute path \DosDevices\X:\... from a CHAR path. */
static VOID NotepadBuildUnicodePath(const CHAR *narrow, PCHAR wide_buf, ULONG max)
{
    ULONG k = 0;
    if (narrow[0] && narrow[1] == ':') {
        const CHAR *prefix = "\\DosDevices\\";
        for (ULONG i = 0; prefix[i] && k < max - 1; i++) wide_buf[k++] = prefix[i];
        for (ULONG i = 0; i < 2 && narrow[i] && k < max - 1; i++) wide_buf[k++] = narrow[i];
        wide_buf[k++] = '\\';
        for (ULONG i = 2; narrow[i] && k < max - 1; i++) wide_buf[k++] = narrow[i];
    } else {
        for (ULONG i = 0; narrow[i] && k < max - 1; i++) wide_buf[k++] = narrow[i];
    }
    wide_buf[k] = 0;
}

/* Open a text file into the document. */
NTSTATUS NTAPI NotepadLoad(ULONG DocIndex, const CHAR *Path)
{
    if (DocIndex >= 8) return STATUS_INVALID_PARAMETER;
    PNOTEPAD_DOC d = &g_Docs[DocIndex];
    if (!d->InUse) return STATUS_INVALID_HANDLE;

    CHAR upath[NOTEPAD_PATH_MAX * 2];
    NotepadBuildUnicodePath(Path, upath, sizeof(upath));

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x2001F /* GENERIC_READ */, &oa, &isb,
                              NULL, 0, 7 /* FILE_SHARE_* */, 1 /* FILE_OPEN */,
                              0x40 /* FILE_SYNCHRONOUS_IO_NONALERT */, NULL, 0);
    if (!NT_SUCCESS(s)) return s;

    ULONG got = 0;
    while (got < sizeof(d->Buffer) - 1) {
        s = NtReadFile(h, NULL, NULL, NULL, &isb,
                       d->Buffer + got, sizeof(d->Buffer) - 1 - got,
                       NULL, NULL);
        if (!NT_SUCCESS(s) || isb.Information == 0) break;
        got += (ULONG)isb.Information;
    }
    NtClose(h);
    if (NT_SUCCESS(s) || s == 0xC0000011 /* STATUS_END_OF_FILE */) {
        d->Buffer[got] = 0;
        d->BufferLength = got;
        d->Modified = FALSE;
        for (ULONG k = 0; Path[k] && k < sizeof(d->FilePath) - 1; k++) {
            d->FilePath[k] = Path[k];
        }
        d->FilePath[sizeof(d->FilePath) - 1] = 0;
        return STATUS_SUCCESS;
    }
    return s;
}

/* Persist the buffer to disk. */
NTSTATUS NTAPI NotepadSave(ULONG DocIndex)
{
    if (DocIndex >= 8) return STATUS_INVALID_PARAMETER;
    PNOTEPAD_DOC d = &g_Docs[DocIndex];
    if (!d->InUse || !d->FilePath[0]) return STATUS_INVALID_PARAMETER;

    CHAR upath[NOTEPAD_PATH_MAX * 2];
    NotepadBuildUnicodePath(d->FilePath, upath, sizeof(upath));

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, (PCWSTR)upath);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x4001F /* GENERIC_WRITE */, &oa, &isb,
                              NULL, 0x80 /* FILE_ATTRIBUTE_NORMAL */,
                              0, 4 /* FILE_OVERWRITE_IF */,
                              0x40 /* FILE_SYNCHRONOUS_IO_NONALERT */, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    ULONG wrote = 0;
    while (wrote < d->BufferLength) {
        s = NtWriteFile(h, NULL, NULL, NULL, &isb,
                        d->Buffer + wrote, d->BufferLength - wrote,
                        NULL, NULL);
        if (!NT_SUCCESS(s)) break;
        wrote += (ULONG)isb.Information;
    }
    NtClose(h);
    if (NT_SUCCESS(s)) d->Modified = FALSE;
    return s;
}

/* Append a typed character to the document. */
NTSTATUS NTAPI NotepadAppend(ULONG DocIndex, CHAR c)
{
    if (DocIndex >= 8) return STATUS_INVALID_PARAMETER;
    PNOTEPAD_DOC d = &g_Docs[DocIndex];
    if (!d->InUse) return STATUS_INVALID_HANDLE;
    if (d->BufferLength >= sizeof(d->Buffer) - 1) return STATUS_BUFFER_TOO_SMALL;
    d->Buffer[d->BufferLength++] = c;
    d->Buffer[d->BufferLength] = 0;
    d->Modified = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NotepadBackspace(ULONG DocIndex)
{
    if (DocIndex >= 8) return STATUS_INVALID_PARAMETER;
    PNOTEPAD_DOC d = &g_Docs[DocIndex];
    if (!d->InUse || d->BufferLength == 0) return STATUS_SUCCESS;
    d->BufferLength--;
    d->Buffer[d->BufferLength] = 0;
    d->Modified = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NotepadOpenDoc(PULONG OutIndex)
{
    for (ULONG i = 0; i < 8; i++) {
        if (!g_Docs[i].InUse) {
            RtlZeroMemory(&g_Docs[i], sizeof(NOTEPAD_DOC));
            g_Docs[i].InUse = TRUE;
            if (OutIndex) *OutIndex = i;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI NotepadCloseDoc(ULONG DocIndex)
{
    if (DocIndex >= 8) return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&g_Docs[DocIndex], sizeof(NOTEPAD_DOC));
    return STATUS_SUCCESS;
}

ULONG NTAPI NotepadGetLength(ULONG DocIndex)
{
    if (DocIndex >= 8) return 0;
    return g_Docs[DocIndex].InUse ? g_Docs[DocIndex].BufferLength : 0;
}

NTSTATUS NTAPI NotepadGetBuffer(ULONG DocIndex, PCHAR OutBuffer, ULONG MaxLen, PULONG OutLen)
{
    if (DocIndex >= 8 || !OutLen) return STATUS_INVALID_PARAMETER;
    PNOTEPAD_DOC d = &g_Docs[DocIndex];
    if (!d->InUse) return STATUS_INVALID_HANDLE;
    ULONG got = d->BufferLength;
    if (got > MaxLen - 1) got = MaxLen - 1;
    for (ULONG i = 0; i < got; i++) OutBuffer[i] = d->Buffer[i];
    OutBuffer[got] = 0;
    *OutLen = got;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI NotepadIsModified(ULONG DocIndex)
{
    if (DocIndex >= 8) return FALSE;
    return g_Docs[DocIndex].InUse ? g_Docs[DocIndex].Modified : FALSE;
}

NTSTATUS NTAPI NotepadInit(VOID)
{
    RtlZeroMemory(g_Docs, sizeof(g_Docs));
    DbgPrint("NOTEPAD: plain text editor initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NotepadOpenFile(const CHAR *Path)
{
    ULONG idx;
    NTSTATUS s = NotepadOpenDoc(&idx);
    if (!NT_SUCCESS(s)) return s;
    if (Path) s = NotepadLoad(idx, Path);
    return s;
}
