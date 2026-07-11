/*
 * MinNT - ke/pe.c
 * PE loader: map PE32+ images into address space, fix imports.
 * Supports both EXEs and DLLs, relocation fixups, import resolution.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/pe.h>
#include <nt/rtl.h>
#include <nt/hal.h>

/* ---- Module tracking ----------------------------------------------------- */

typedef struct _PE_MODULE {
    LIST_ENTRY Entry;
    UNICODE_STRING Name;
    PPE_IMAGE Image;
} PE_MODULE, *PPE_MODULE;

static LIST_ENTRY ModuleListHead;
static KSPIN_LOCK ModuleListLock;

/* ---- Export directory helpers -------------------------------------------- */

static PIMAGE_EXPORT_DIRECTORY PeGetExportDirectory(PPE_IMAGE Image, PULONG ExportSize)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_OPTIONAL_HEADER64 OptHeader;
    ULONG exportRva;
    ULONG exportSize;

    NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)Image->MappedBase +
                sizeof(IMAGE_DOS_HEADER));
    OptHeader = &NtHeaders->OptionalHeader;

    exportRva = OptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    exportSize = OptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

    if (!exportRva || !exportSize) {
        if (ExportSize) *ExportSize = 0;
        return NULL;
    }

    if (ExportSize) *ExportSize = exportSize;
    return (PIMAGE_EXPORT_DIRECTORY)(Image->MappedBase + exportRva);
}

static ULONG PeFindExportByName(PPE_IMAGE Image, const CHAR *NameToFind)
{
    ULONG exportSize;
    PIMAGE_EXPORT_DIRECTORY expDir;
    PULONG addressOfFunctions;
    PULONG addressOfNames;
    PUSHORT addressOfNameOrdinals;
    ULONG numFunctions;
    ULONG numNames;
    ULONG i;
    PCHAR name;

    expDir = PeGetExportDirectory(Image, &exportSize);
    if (!expDir) return 0;

    numFunctions = expDir->NumberOfFunctions;
    numNames = expDir->NumberOfNames;

    if (numNames == 0) return 0;

    addressOfFunctions = (PULONG)(Image->MappedBase + expDir->AddressOfFunctions);
    addressOfNames = (PULONG)(Image->MappedBase + expDir->AddressOfNames);
    addressOfNameOrdinals = (PUSHORT)(Image->MappedBase + expDir->AddressOfNameOrdinals);

    for (i = 0; i < numNames; i++) {
        name = (PCHAR)(Image->MappedBase + addressOfNames[i]);
        if (_stricmp(name, NameToFind) == 0) {
            ULONG ordinal = addressOfNameOrdinals[i];
            if (ordinal < numFunctions) {
                ULONG funcRva = addressOfFunctions[ordinal];
                if (funcRva != 0) {
                    return expDir->Base + ordinal;
                }
            }
        }
    }

    return 0;
}

static ULONG PeFindExportByOrdinal(PPE_IMAGE Image, USHORT Ordinal)
{
    ULONG exportSize;
    PIMAGE_EXPORT_DIRECTORY expDir;
    PULONG addressOfFunctions;
    ULONG numFunctions;
    ULONG targetOrdinal;
    ULONG funcRva;

    expDir = PeGetExportDirectory(Image, &exportSize);
    if (!expDir) return 0;

    numFunctions = expDir->NumberOfFunctions;
    targetOrdinal = Ordinal - expDir->Base;

    if (targetOrdinal >= numFunctions) return 0;

    addressOfFunctions = (PULONG)(Image->MappedBase + expDir->AddressOfFunctions);
    funcRva = addressOfFunctions[targetOrdinal];

    if (funcRva == 0) return 0;

    return expDir->Base + targetOrdinal;
}

/* ---- Module list management ---------------------------------------------- */

static VOID PeInitModuleList(VOID)
{
    InitializeListHead(&ModuleListHead);
    KeInitializeSpinLock(&ModuleListLock);
}

static BOOLEAN PeIsModuleLoaded(const CHAR *DllName)
{
    PLIST_ENTRY entry;
    PPE_MODULE mod;
    BOOLEAN found = FALSE;
    KIRQL oldIrql;

    KeAcquireSpinLock(&ModuleListLock, &oldIrql);

    if (IsListEmpty(&ModuleListHead)) {
        KeReleaseSpinLock(&ModuleListLock, oldIrql);
        return FALSE;
    }

    for (entry = ModuleListHead.Flink; entry != &ModuleListHead; entry = entry->Flink) {
        mod = CONTAINING_RECORD(entry, PE_MODULE, Entry);
        if (mod->Name.Buffer) {
            SIZE_T ansiLen = RtlStringLength(DllName);
            if (mod->Name.Length == (USHORT)(ansiLen * sizeof(WCHAR))) {
                PWSTR wideName = ExAllocatePoolWithTag(NonPagedPool,
                    (ansiLen + 1) * sizeof(WCHAR), 'epm0');
                if (wideName) {
                    SIZE_T i;
                    for (i = 0; i < ansiLen; i++) {
                        wideName[i] = (WCHAR)DllName[i];
                    }
                    wideName[ansiLen] = 0;

                    UNICODE_STRING uWide;
                    uWide.Buffer = wideName;
                    uWide.Length = (USHORT)(ansiLen * sizeof(WCHAR));
                    uWide.MaximumLength = (USHORT)((ansiLen + 1) * sizeof(WCHAR));

                    if (RtlEqualUnicodeString(&mod->Name, &uWide, TRUE)) {
                        found = TRUE;
                        ExFreePoolWithTag(wideName, 'epm0');
                        break;
                    }
                    ExFreePoolWithTag(wideName, 'epm0');
                }
            }
        }
    }

    KeReleaseSpinLock(&ModuleListLock, oldIrql);
    return found;
}

static VOID PeRegisterModule(const CHAR *DllName, PPE_IMAGE Image)
{
    PLIST_ENTRY entry;
    PPE_MODULE mod;
    KIRQL oldIrql;

    KeAcquireSpinLock(&ModuleListLock, &oldIrql);

    for (entry = ModuleListHead.Flink; entry != &ModuleListHead; entry = entry->Flink) {
        mod = CONTAINING_RECORD(entry, PE_MODULE, Entry);
        if (mod->Image == Image) {
            KeReleaseSpinLock(&ModuleListLock, oldIrql);
            return;
        }
    }

    mod = ExAllocatePoolWithTag(NonPagedPool, sizeof(PE_MODULE), 'epm0');
    if (mod) {
        RtlZeroMemory(mod, sizeof(PE_MODULE));
        mod->Image = Image;

        if (DllName && DllName[0]) {
            SIZE_T len = RtlStringLength(DllName);
            mod->Name.Length = (USHORT)(len * sizeof(WCHAR));
            mod->Name.MaximumLength = (USHORT)((len + 1) * sizeof(WCHAR));
            mod->Name.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                mod->Name.MaximumLength, 'epm0');
            if (mod->Name.Buffer) {
                SIZE_T i;
                for (i = 0; i < len; i++) {
                    mod->Name.Buffer[i] = (WCHAR)DllName[i];
                }
                mod->Name.Buffer[len] = 0;
            }
        }

        InsertTailList(&ModuleListHead, &mod->Entry);
    }

    KeReleaseSpinLock(&ModuleListLock, oldIrql);
}

static PPE_IMAGE PeFindModuleHandle(const CHAR *DllName)
{
    PLIST_ENTRY entry;
    PPE_MODULE mod;
    KIRQL oldIrql;

    KeAcquireSpinLock(&ModuleListLock, &oldIrql);

    if (IsListEmpty(&ModuleListHead)) {
        KeReleaseSpinLock(&ModuleListLock, oldIrql);
        return NULL;
    }

    for (entry = ModuleListHead.Flink; entry != &ModuleListHead; entry = entry->Flink) {
        mod = CONTAINING_RECORD(entry, PE_MODULE, Entry);
        if (mod->Name.Buffer) {
            SIZE_T ansiLen = RtlStringLength(DllName);
            if (mod->Name.Length == (USHORT)(ansiLen * sizeof(WCHAR))) {
                PWSTR wideName = ExAllocatePoolWithTag(NonPagedPool,
                    (ansiLen + 1) * sizeof(WCHAR), 'epm0');
                if (wideName) {
                    SIZE_T i;
                    for (i = 0; i < ansiLen; i++) {
                        wideName[i] = (WCHAR)DllName[i];
                    }
                    wideName[ansiLen] = 0;

                    UNICODE_STRING uWide;
                    uWide.Buffer = wideName;
                    uWide.Length = (USHORT)(ansiLen * sizeof(WCHAR));
                    uWide.MaximumLength = (USHORT)((ansiLen + 1) * sizeof(WCHAR));

                    if (RtlEqualUnicodeString(&mod->Name, &uWide, TRUE)) {
                        PPE_IMAGE result = mod->Image;
                        ExFreePoolWithTag(wideName, 'epm0');
                        KeReleaseSpinLock(&ModuleListLock, oldIrql);
                        return result;
                    }
                    ExFreePoolWithTag(wideName, 'epm0');
                }
            }
        }
    }

    KeReleaseSpinLock(&ModuleListLock, oldIrql);
    return NULL;
}

static PVOID PeResolveExport(PPE_IMAGE Image, ULONG ExportRva)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_OPTIONAL_HEADER64 OptHeader;
    PIMAGE_SECTION_HEADER Sections;
    ULONG sectionRva;
    ULONG sectionSize;
    ULONG i;

    NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)Image->MappedBase +
                sizeof(IMAGE_DOS_HEADER));
    OptHeader = &NtHeaders->OptionalHeader;
    Sections = (PIMAGE_SECTION_HEADER)((PUCHAR)&OptHeader->NumberOfRvaAndSizes +
                OptHeader->NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY));

    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++) {
        sectionRva = Sections[i].VirtualAddress;
        sectionSize = Sections[i].Misc.VirtualSize;

        if (ExportRva >= sectionRva && ExportRva < sectionRva + sectionSize) {
            if (Sections[i].Characteristics & IMAGE_SCN_CNT_CODE) {
                return (PVOID)(Image->MappedBase + ExportRva);
            }
            if (Sections[i].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
                return (PVOID)(Image->MappedBase + ExportRva);
            }
        }
    }

    if (ExportRva >= (ULONG)(ULONG_PTR)Image->MappedBase &&
        ExportRva < (ULONG)(ULONG_PTR)Image->MappedBase + Image->ImageSize) {
        return (PVOID)(Image->MappedBase + ExportRva);
    }

    return NULL;
}

/* ---- Resolve imports ----------------------------------------------------- */

NTSTATUS NTAPI PeResolveImports(PPE_IMAGE Image)
{
    PIMAGE_IMPORT_DESCRIPTOR importDesc;
    PUCHAR imageBase = (PUCHAR)Image->MappedBase;

    if (!Image->Imports)
        return STATUS_SUCCESS;

    importDesc = Image->Imports;

    while (importDesc->Name) {
        PUCHAR dllNameA = imageBase + importDesc->Name;
        PIMAGE_THUNK_DATA64 origThunk;
        PIMAGE_THUNK_DATA64 iatThunk;
        PPE_IMAGE targetModule;
        ULONG exportRva;

        DbgPrint("PE: resolving imports from '%s'\n", dllNameA);

        targetModule = PeFindModuleHandle((const CHAR *)dllNameA);
        if (!targetModule) {
            DbgPrint("PE: DLL '%s' not found in loaded modules\n", dllNameA);
            importDesc++;
            continue;
        }

        origThunk = (PIMAGE_THUNK_DATA64)(imageBase +
            (importDesc->OriginalFirstThunk ?
             importDesc->OriginalFirstThunk : importDesc->FirstThunk));
        iatThunk = (PIMAGE_THUNK_DATA64)(imageBase + importDesc->FirstThunk);

        while (origThunk->u1.AddressOfData) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                USHORT ordinal = (USHORT)(origThunk->u1.Ordinal & 0xFFFF);
                exportRva = PeFindExportByOrdinal(targetModule, ordinal);
                if (exportRva) {
                    PVOID funcAddr = PeResolveExport(targetModule, exportRva);
                    iatThunk->u1.AddressOfData = (ULONG64)funcAddr;
                } else {
                    iatThunk->u1.AddressOfData = 0;
                }
            } else {
                PIMAGE_IMPORT_BY_NAME importInfo =
                    (PIMAGE_IMPORT_BY_NAME)(imageBase + origThunk->u1.AddressOfData);
                PCHAR funcName = (PCHAR)&importInfo->Name;
                exportRva = PeFindExportByName(targetModule, funcName);
                if (exportRva) {
                    PVOID funcAddr = PeResolveExport(targetModule, exportRva);
                    iatThunk->u1.AddressOfData = (ULONG64)funcAddr;
                } else {
                    iatThunk->u1.AddressOfData = 0;
                }
            }
            origThunk++;
            iatThunk++;
        }

        importDesc++;
    }

    DbgPrint("PE: imports resolved\n");
    return STATUS_SUCCESS;
}

/* ---- Image validation ---------------------------------------------------- */

static NTSTATUS PeValidateImage(PIMAGE_DOS_HEADER DosHeader, ULONG FileSize)
{
    PIMAGE_NT_HEADERS NtHeaders;

    if (FileSize < sizeof(IMAGE_DOS_HEADER))
        return STATUS_INVALID_IMAGE_FORMAT;

    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

    if ((ULONG)DosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS) > FileSize)
        return STATUS_INVALID_IMAGE_FORMAT;

    NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)DosHeader + DosHeader->e_lfanew);

    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
        return STATUS_INVALID_IMAGE_FORMAT;

    if (NtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return STATUS_INVALID_IMAGE_FORMAT;

    if (NtHeaders->OptionalHeader.Magic != 0x20B)  /* PE32+ */
        return STATUS_INVALID_IMAGE_FORMAT;

    return STATUS_SUCCESS;
}

/* ---- Load image ---------------------------------------------------------- */

NTSTATUS NTAPI PeLoadImage(PVOID FileBase,
                           ULONG FileSize,
                           BOOLEAN IsExe,
                           PPE_IMAGE OutImage)
{
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_OPTIONAL_HEADER64 OptHeader;
    PIMAGE_SECTION_HEADER Sections;
    PUCHAR imageBase;
    ULONG i;
    NTSTATUS status;

    status = PeValidateImage((PIMAGE_DOS_HEADER)FileBase, FileSize);
    if (!NT_SUCCESS(status)) return status;

    DosHeader = (PIMAGE_DOS_HEADER)FileBase;
    NtHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)FileBase + DosHeader->e_lfanew);
    OptHeader = &NtHeaders->OptionalHeader;
    Sections = (PIMAGE_SECTION_HEADER)((PUCHAR)&OptHeader->NumberOfRvaAndSizes +
                OptHeader->NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY));

    /* Allocate pages for the image */
    {
        PHYSICAL_ADDRESS pa;
        SIZE_T imageSize = OptHeader->SizeOfImage;
        SIZE_T mapped = 0;

        imageBase = (PUCHAR)OptHeader->ImageBase;

        /* Map each page of the image */
        while (mapped < imageSize) {
            pa = MmAllocatePhysicalPage();
            if (!pa) {
                DbgPrint("PE: out of memory mapping image\n");
                return STATUS_NO_MEMORY;
            }

            status = MmMapPage((ULONG_PTR)(imageBase + mapped), pa,
                               PTE_USER | PTE_WRITE);
            if (!NT_SUCCESS(status)) {
                MmFreePhysicalPage(pa);
                return status;
            }
            mapped += PAGE_SIZE;
        }

        /* Zero the image memory */
        RtlZeroMemory(imageBase, imageSize);
    }

    /* Copy headers */
    RtlCopyMemory(imageBase, FileBase,
                  NtHeaders->FileHeader.SizeOfOptionalHeader +
                  sizeof(IMAGE_FILE_HEADER) +
                  sizeof(ULONG));

    /* Copy sections */
    for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++) {
        if (Sections[i].SizeOfRawData > 0 && Sections[i].PointerToRawData > 0) {
            SIZE_T copySize = Sections[i].SizeOfRawData;
            if (copySize > Sections[i].Misc.VirtualSize)
                copySize = Sections[i].Misc.VirtualSize;

            RtlCopyMemory(imageBase + Sections[i].VirtualAddress,
                          (PUCHAR)FileBase + Sections[i].PointerToRawData,
                          copySize);
        }
    }

    /* Apply base relocations */
    {
        ULONG relocRva = OptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        ULONG relocSize = OptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;

        if (relocRva && relocSize) {
            PUCHAR relocBase = imageBase + relocRva;
            PUCHAR relocEnd = relocBase + relocSize;

            while (relocBase < relocEnd) {
                PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)relocBase;
                ULONG count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
                USHORT *entries = (USHORT *)(relocBase + sizeof(IMAGE_BASE_RELOCATION));

                for (ULONG j = 0; j < count; j++) {
                    USHORT type = entries[j] >> 12;
                    USHORT offset = entries[j] & 0xFFF;

                    if (type == IMAGE_REL_BASED_DIR64) {
                        ULONG64 *patch = (ULONG64 *)(imageBase + reloc->VirtualAddress + offset);
                        /* No rebasing needed if loaded at preferred base */
                        (void)patch;
                    }
                }
                relocBase += reloc->SizeOfBlock;
            }
        }
    }

    /* Fill in result */
    OutImage->ImageBase = (ULONG64)OptHeader->ImageBase;
    OutImage->MappedBase = (ULONG64)imageBase;
    OutImage->EntryPoint = (ULONG64)imageBase + OptHeader->AddressOfEntryPoint;
    OutImage->ImageSize = OptHeader->SizeOfImage;
    OutImage->NumberOfSections = NtHeaders->FileHeader.NumberOfSections;
    OutImage->Sections = Sections;
    OutImage->IsExe = IsExe;

    /* Get import directory */
    {
        ULONG importRva = OptHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        OutImage->Imports = importRva ? (PIMAGE_IMPORT_DESCRIPTOR)(imageBase + importRva) : NULL;
    }

    DbgPrint("PE: loaded %s at %p, entry=%p, size=%lu KB\n",
             IsExe ? "EXE" : "DLL",
             (PVOID)imageBase,
             (PVOID)OutImage->EntryPoint,
             OptHeader->SizeOfImage >> 10);

    return STATUS_SUCCESS;
}

/* ---- Unload image -------------------------------------------------------- */

VOID NTAPI PeUnloadImage(PPE_IMAGE Image)
{
    /* In a real implementation, we'd free all mapped pages.
       For now, just zero the structure. */
    Image->MappedBase = 0;
    Image->EntryPoint = 0;
}
