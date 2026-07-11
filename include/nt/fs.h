/*
 * MinNT - fs.h
 * File system interface: RAM disk + FAT16, NtCreateFile/NtReadFile/NtWriteFile.
 * Minimal NT 6.x file I/O — enough for SMSS/CSRSS to load binaries.
 */

#ifndef _FS_H_
#define _FS_H_

#include <nt/ntdef.h>
#include <nt/io.h>
#include <nt/se.h>

/* ---- File objects -------------------------------------------------------- */

#define FILE_SUPERSEDE           0x00000000
#define FILE_CREATE              0x00000002
#define FILE_OPEN_IF             0x00000003
#define FILE_OPEN                0x00000001

#define FILE_DIRECTORY_FILE      0x00000001
#define FILE_NON_DIRECTORY_FILE  0x00000040

typedef struct _FILE_OBJECT {
    USHORT          Type;           /* IO_TYPE_FILE */
    USHORT          Size;
    UNICODE_STRING  FileName;
    ULONG           CurrentOffset;
    ULONG           Flags;
    PVOID           FsContext;      /* file system specific */
    PVOID           DeviceObject;
    BOOLEAN         DeletePending;
    BOOLEAN         ReadAccess;
    BOOLEAN         WriteAccess;
} FILE_OBJECT, *PFILE_OBJECT;

#define IO_TYPE_FILE 0x4649  /* 'FI' */

/* ---- FAT16 structures --------------------------------------------------- */

#pragma pack(push, 1)
typedef struct _FAT16_BPB {
    UCHAR   JumpBoot[3];
    UCHAR   OEMName[8];
    USHORT  BytesPerSector;
    UCHAR   SectorsPerCluster;
    USHORT  ReservedSectors;
    UCHAR   NumberOfFATs;
    USHORT  RootEntryCount;
    USHORT  TotalSectors16;
    UCHAR   Media;
    USHORT  SectorsPerFAT;
    USHORT  SectorsPerTrack;
    USHORT  NumberOfHeads;
    ULONG   HiddenSectors;
    ULONG   TotalSectors32;
    UCHAR   DriveNumber;
    UCHAR   Reserved1;
    UCHAR   BootSignature;
    ULONG   VolumeID;
    UCHAR   VolumeLabel[11];
    UCHAR   FileSystemType[8];
} FAT16_BPB, *PFAT16_BPB;

typedef struct _FAT16_DIR_ENTRY {
    UCHAR   FileName[8];
    UCHAR   FileExt[3];
    UCHAR   Attributes;
    UCHAR   Reserved;
    UCHAR   CreateTime;
    USHORT  CreateDate;
    USHORT  AccessDate;
    USHORT  FirstClusterHigh;
    USHORT  ModifyTime;
    USHORT  ModifyDate;
    USHORT  FirstCluster;
    ULONG   FileSize;
} FAT16_DIR_ENTRY, *PFAT16_DIR_ENTRY;
#pragma pack(pop)

#define FAT16_ATTR_READ_ONLY  0x01
#define FAT16_ATTR_HIDDEN     0x02
#define FAT16_ATTR_SYSTEM     0x04
#define FAT16_ATTR_VOLUME_ID  0x08
#define FAT16_ATTR_DIRECTORY  0x10
#define FAT16_ATTR_ARCHIVE    0x20

/* ---- RAM disk ------------------------------------------------------------ */

#define RAMDISK_SECTORS_PER_TRACK  63
#define RAMDISK_HEADS              16
#define RAMDISK_SECTOR_SIZE        512

NTSTATUS NTAPI FsInitSystem(VOID);

NTSTATUS NTAPI FsCreateRamDisk(ULONG SizeInMB);

NTSTATUS NTAPI FsMountFat16(VOID);

/* ---- File I/O API -------------------------------------------------------- */

NTSTATUS NTAPI NtCreateFile(PHANDLE OutFileHandle,
                            ACCESS_MASK DesiredAccess,
                            PISECURITY_DESCRIPTOR SecurityDescriptor,
                            PVOID IoStatusBlock,
                            PULONG64 AllocationSize,
                            ULONG FileAttributes,
                            ULONG ShareAccess,
                            ULONG CreateDisposition,
                            ULONG CreateOptions,
                            PVOID EaBuffer,
                            ULONG EaLength);

NTSTATUS NTAPI NtReadFile(HANDLE FileHandle,
                          PVOID Event,
                          PVOID ApcRoutine,
                          PVOID ApcContext,
                          PVOID IoStatusBlock,
                          PVOID Buffer,
                          ULONG Length,
                          PULONG64 ByteOffset,
                          PULONG Key);

NTSTATUS NTAPI NtWriteFile(HANDLE FileHandle,
                           PVOID Event,
                           PVOID ApcRoutine,
                           PVOID ApcContext,
                           PVOID IoStatusBlock,
                           PVOID Buffer,
                           ULONG Length,
                           PULONG64 ByteOffset,
                           PULONG Key);

NTSTATUS NTAPI NtClose(HANDLE Handle);

NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE FileHandle,
                                    PVOID IoStatusBlock,
                                    PVOID FileInformation,
                                    ULONG Length,
                                    ULONG FileInformationClass,
                                    BOOLEAN ReturnSingleEntry,
                                    PUNICODE_STRING FileName,
                                    BOOLEAN RestartScan);

/* ---- File information classes -------------------------------------------- */

#define FileDirectoryInformation       1
#define FileFullDirectoryInformation   2

typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONG EndOfFile;
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_DIRECTORY_INFORMATION, *PFILE_DIRECTORY_INFORMATION;

#endif /* _FS_H_ */
