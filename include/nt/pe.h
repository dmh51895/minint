/*
 * MinNT - pe.h
 * PE loader: map PE32+ executables/DLLs into address space, fix imports.
 * Minimal NT 6.x PE loader — enough to load ntdll.dll and user-mode exes.
 */

#ifndef _PE_H_
#define _PE_H_

#include <nt/ntdef.h>

/* ---- DOS Header ---------------------------------------------------------- */

typedef struct _IMAGE_DOS_HEADER {
    USHORT  e_magic;        /* MZ */
    USHORT  e_cblp;
    USHORT  e_cp;
    USHORT  e_crlc;
    USHORT  e_cparhdr;
    USHORT  e_minalloc;
    USHORT  e_maxalloc;
    USHORT  e_ss;
    USHORT  e_sp;
    USHORT  e_csum;
    USHORT  e_ip;
    USHORT  e_cs;
    USHORT  e_lfarlc;
    USHORT  e_ovno;
    USHORT  e_res[4];
    USHORT  e_oemid;
    USHORT  e_oeminfo;
    USHORT  e_res2[10];
    LONG    e_lfanew;       /* offset to PE signature */
} IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

#define IMAGE_DOS_SIGNATURE 0x5A4D      /* MZ */

/* ---- PE Signature -------------------------------------------------------- */

#define IMAGE_NT_SIGNATURE 0x00004550   /* PE */

/* ---- COFF File Header (PE32+) ------------------------------------------- */

typedef struct _IMAGE_FILE_HEADER {
    USHORT  Machine;
    USHORT  NumberOfSections;
    ULONG   TimeDateStamp;
    ULONG   PointerToSymbolTable;
    ULONG   NumberOfSymbols;
    USHORT  SizeOfOptionalHeader;
    USHORT  Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

#define IMAGE_FILE_MACHINE_AMD64    0x8664

/* ---- Optional Header (PE32+) -------------------------------------------- */

typedef struct _IMAGE_DATA_DIRECTORY {
    ULONG   VirtualAddress;
    ULONG   Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

/* Directory indices */
#define IMAGE_DIRECTORY_ENTRY_EXPORT    0
#define IMAGE_DIRECTORY_ENTRY_IMPORT    1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE  2
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_DIRECTORY_ENTRY_IAT       12

typedef struct _IMAGE_OPTIONAL_HEADER64 {
    USHORT  Magic;              /* 0x20B for PE32+ */
    UCHAR   MajorLinkerVersion;
    UCHAR   MinorLinkerVersion;
    ULONG   SizeOfCode;
    ULONG   SizeOfInitializedData;
    ULONG   SizeOfUninitializedData;
    ULONG   AddressOfEntryPoint;
    ULONG   BaseOfCode;
    ULONG64 ImageBase;
    ULONG   SectionAlignment;
    ULONG   FileAlignment;
    USHORT  MajorOperatingSystemVersion;
    USHORT  MinorOperatingSystemVersion;
    USHORT  MajorImageVersion;
    USHORT  MinorImageVersion;
    USHORT  MajorSubsystemVersion;
    USHORT  MinorSubsystemVersion;
    ULONG   Win32VersionValue;
    ULONG   SizeOfImage;
    ULONG   SizeOfHeaders;
    ULONG   CheckSum;
    USHORT  Subsystem;
    USHORT  DllCharacteristics;
    ULONG64 SizeOfStackReserve;
    ULONG64 SizeOfStackCommit;
    ULONG64 SizeOfHeapReserve;
    ULONG64 SizeOfHeapCommit;
    ULONG   LoaderFlags;
    ULONG   NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

typedef IMAGE_OPTIONAL_HEADER64 IMAGE_OPTIONAL_HEADER;

/* ---- NT Headers ---------------------------------------------------------- */

typedef struct _IMAGE_NT_HEADERS64 {
    ULONG Signature;                /* PE */
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64, *PIMAGE_NT_HEADERS64;

typedef IMAGE_NT_HEADERS64 IMAGE_NT_HEADERS;
typedef IMAGE_NT_HEADERS64 *PIMAGE_NT_HEADERS;

/* ---- Section Header ------------------------------------------------------ */

#define IMAGE_SIZEOF_SHORT_NAME 8

typedef struct _IMAGE_SECTION_HEADER {
    UCHAR   Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
        ULONG   PhysicalAddress;
        ULONG   VirtualSize;
    } Misc;
    ULONG   VirtualAddress;
    ULONG   SizeOfRawData;
    ULONG   PointerToRawData;
    ULONG   PointerToRelocations;
    ULONG   PointerToLinenumbers;
    USHORT  NumberOfRelocations;
    USHORT  NumberOfLinenumbers;
    ULONG   Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

/* Section flags */
#define IMAGE_SCN_CNT_CODE               0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040
#define IMAGE_SCN_MEM_EXECUTE            0x20000000
#define IMAGE_SCN_MEM_READ               0x40000000
#define IMAGE_SCN_MEM_WRITE              0x80000000

/* ---- Export Directory ---------------------------------------------------- */

typedef struct _IMAGE_EXPORT_DIRECTORY {
    ULONG   Characteristics;
    ULONG   TimeDateStamp;
    USHORT  MajorVersion;
    USHORT  MinorVersion;
    ULONG   Name;
    ULONG   Base;
    ULONG   NumberOfFunctions;
    ULONG   NumberOfNames;
    ULONG   AddressOfFunctions;
    ULONG   AddressOfNames;
    ULONG   AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

/* ---- Import Directory ---------------------------------------------------- */

typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    union {
        ULONG   Characteristics;
        ULONG   OriginalFirstThunk;
    };
    ULONG   TimeDateStamp;
    ULONG   ForwarderChain;
    ULONG   Name;
    ULONG   FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

typedef struct _IMAGE_THUNK_DATA64 {
    union {
        ULONG64 ForwarderString;
        ULONG64 Ordinal;
        ULONG64 AddressOfData;
    } u1;
} IMAGE_THUNK_DATA64, *PIMAGE_THUNK_DATA64;

#define IMAGE_ORDINAL_FLAG64 0x8000000000000000ULL

typedef struct _IMAGE_IMPORT_BY_NAME {
    USHORT  Hint;
    CHAR    Name[1];
} IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;

/* ---- Relocation ---------------------------------------------------------- */

typedef struct _IMAGE_BASE_RELOCATION {
    ULONG   VirtualAddress;
    ULONG   SizeOfBlock;
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;

#define IMAGE_REL_BASED_DIR64 10

/* ---- Loader result ------------------------------------------------------- */

typedef struct _PE_IMAGE {
    ULONG64     ImageBase;      /* preferred base address */
    ULONG64     MappedBase;     /* actual mapped address */
    ULONG64     EntryPoint;
    ULONG       ImageSize;
    ULONG       NumberOfSections;
    PIMAGE_SECTION_HEADER Sections;
    PIMAGE_IMPORT_DESCRIPTOR Imports;
    BOOLEAN     IsExe;          /* TRUE for EXE, FALSE for DLL */
} PE_IMAGE, *PPE_IMAGE;

/* ---- API ----------------------------------------------------------------- */

NTSTATUS NTAPI PeLoadImage(PVOID FileBase,
                           ULONG FileSize,
                           BOOLEAN IsExe,
                           PPE_IMAGE OutImage);

NTSTATUS NTAPI PeResolveImports(PPE_IMAGE Image);

VOID NTAPI PeUnloadImage(PPE_IMAGE Image);

#endif /* _PE_H_ */
