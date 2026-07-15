/*
 * MinNT - fs/fs.c
 * File system: RAM disk with FAT16, NtCreateFile/NtReadFile/NtWriteFile.
 * Minimal NT 6.x file I/O — enough for SMSS/CSRSS to load binaries.
 */

#include <nt/ke.h>
#include <nt/mm.h>
#include <nt/ex.h>
#include <nt/ob.h>
#include <nt/io.h>
#include <ndk/obfuncs.h>
#include <nt/fs.h>
#include <nt/rtl.h>
#include <nt/hal.h>

/* ---- RAM disk state ------------------------------------------------------ */

static PUCHAR RamDiskBase;
static ULONG  RamDiskSize;
static ULONG  RamDiskSectors;
static FAT16_BPB RamDiskBpb;
static FAT16_DIR_ENTRY *RamDiskRootDir;
static PUCHAR RamDiskFat;
static PUCHAR RamDiskDataArea;
static KSPIN_LOCK FsLock;

/* File descriptor table (minimal) */
#define FS_MAX_FILES 64
static FILE_OBJECT FsFileTable[FS_MAX_FILES];
static BOOLEAN FsFileInUse[FS_MAX_FILES];

/* ---- Init --------------------------------------------------------------- */

NTSTATUS NTAPI FsInitSystem(VOID)
{
    KeInitializeSpinLock(&FsLock);
    RtlZeroMemory(FsFileTable, sizeof(FsFileTable));
    RtlZeroMemory(FsFileInUse, sizeof(FsFileInUse));
    DbgPrint("FS: file system manager initialized\n");
    return STATUS_SUCCESS;
}

/* ---- Create RAM disk ----------------------------------------------------- */

NTSTATUS NTAPI FsCreateRamDisk(ULONG SizeInMB)
{
    ULONG totalSectors;
    ULONG reservedSectors;
    ULONG rootDirSectors;
    ULONG fatSectors;
    ULONG dataSectors;
    ULONG totalClusters;
    PUCHAR sector;

    if (RamDiskBase) return STATUS_SUCCESS;  /* already created */

    RamDiskSize = SizeInMB << 20;
    RamDiskBase = ExAllocatePoolWithTag(NonPagedPool, RamDiskSize, TAG_PROC);
    if (!RamDiskBase) return STATUS_NO_MEMORY;

    RtlZeroMemory(RamDiskBase, RamDiskSize);

    /* Set up BPB */
    RtlZeroMemory(&RamDiskBpb, sizeof(RamDiskBpb));
    RamDiskBpb.BytesPerSector = RAMDISK_SECTOR_SIZE;
    RamDiskBpb.SectorsPerCluster = 4;  /* 2KB clusters */
    RamDiskBpb.ReservedSectors = 1;    /* boot sector */
    RamDiskBpb.NumberOfFATs = 1;
    RamDiskBpb.RootEntryCount = 512;
    RamDiskBpb.Media = 0xF8;          /* fixed disk */
    RamDiskBpb.SectorsPerTrack = RAMDISK_SECTORS_PER_TRACK;
    RamDiskBpb.NumberOfHeads = RAMDISK_HEADS;

    /* Calculate layout */
    totalSectors = RamDiskSize / RAMDISK_SECTOR_SIZE;
    reservedSectors = RamDiskBpb.ReservedSectors;
    rootDirSectors = (RamDiskBpb.RootEntryCount * 32 + RAMDISK_SECTOR_SIZE - 1)
                     / RAMDISK_SECTOR_SIZE;

    /* FAT16: each cluster needs 2 bytes in FAT */
    fatSectors = 2;  /* minimum FAT16 size */
    dataSectors = totalSectors - reservedSectors - fatSectors - rootDirSectors;
    totalClusters = dataSectors / RamDiskBpb.SectorsPerCluster;

    /* Adjust FAT size if needed */
    if (totalClusters > 4085) {
        fatSectors = ((totalClusters + 1 + 1) * 2 + RAMDISK_SECTOR_SIZE - 1)
                     / RAMDISK_SECTOR_SIZE;
    }

    RamDiskSectors = totalSectors;
    RamDiskBpb.TotalSectors16 = (totalSectors > 0xFFFF) ? 0 : (USHORT)totalSectors;
    RamDiskBpb.TotalSectors32 = (totalSectors > 0xFFFF) ? totalSectors : 0;
    RamDiskBpb.SectorsPerFAT = fatSectors;

    /* Calculate pointers */
    RamDiskFat = RamDiskBase + reservedSectors * RAMDISK_SECTOR_SIZE;
    RamDiskRootDir = (FAT16_DIR_ENTRY *)(RamDiskFat + fatSectors * RAMDISK_SECTOR_SIZE);
    RamDiskDataArea = (PUCHAR)RamDiskRootDir + rootDirSectors * RAMDISK_SECTOR_SIZE;

    /* Initialize FAT: cluster 0 = media, cluster 1 = end-of-chain */
    sector = RamDiskFat;
    sector[0] = 0xF8; sector[1] = 0xFF;
    sector[2] = 0xFF; sector[3] = 0xFF;

    DbgPrint("FS: RAM disk created: %lu MB, %lu sectors, %lu clusters\n",
             SizeInMB, RamDiskSectors, totalClusters);
    return STATUS_SUCCESS;
}

/* ---- Mount FAT16 -------------------------------------------------------- */

NTSTATUS NTAPI FsMountFat16(VOID)
{
    if (!RamDiskBase) return STATUS_DEVICE_DATA_ERROR;
    DbgPrint("FS: FAT16 mounted on RAM disk\n");
    return STATUS_SUCCESS;
}

/* ---- FAT16 cluster helpers ---------------------------------------------- */

static ULONG FsClusterSize(VOID)
{
    return RamDiskBpb.SectorsPerCluster * RAMDISK_SECTOR_SIZE;
}

static PUCHAR FsClusterBase(ULONG Cluster)
{
    return RamDiskDataArea + (Cluster - 2) * FsClusterSize();
}

static USHORT FsReadFatEntry(ULONG Cluster)
{
    USHORT *fat = (USHORT *)RamDiskFat;
    return fat[Cluster];
}

static VOID FsWriteFatEntry(ULONG Cluster, USHORT Value)
{
    USHORT *fat = (USHORT *)RamDiskFat;
    fat[Cluster] = Value;
}

static ULONG FsAllocCluster(PFAT16_DIR_ENTRY Entry)
{
    USHORT totalClusters = (USHORT)((RamDiskSectors - RamDiskBpb.ReservedSectors -
                        RamDiskBpb.SectorsPerFAT -
                        (RamDiskBpb.RootEntryCount * 32 + RAMDISK_SECTOR_SIZE - 1) /
                         RAMDISK_SECTOR_SIZE) / RamDiskBpb.SectorsPerCluster);
    ULONG cluster;
    for (cluster = 2; cluster < totalClusters; cluster++) {
        if (FsReadFatEntry(cluster) == 0x0000) {
            FsWriteFatEntry(cluster, 0xFFFF);
            if (Entry->FirstCluster == 0) {
                Entry->FirstCluster = (USHORT)cluster;
            }
            return cluster;
        }
    }
    return 0;
}

static PUCHAR FsResolveClusterChain(PFAT16_DIR_ENTRY Entry, ULONG Offset,
                                    PULONG OutCluster, PULONG OutOffsetInCluster)
{
    if (Entry->FirstCluster == 0 || Offset >= Entry->FileSize)
        return NULL;

    ULONG cluster = Entry->FirstCluster;
    ULONG offset = Offset;
    ULONG clusterSize = FsClusterSize();

    while (offset >= clusterSize) {
        USHORT next = FsReadFatEntry(cluster);
        if (next == 0xFFFF || next == 0x0000)
            return NULL;
        cluster = next;
        offset -= clusterSize;
    }

    *OutCluster = cluster;
    *OutOffsetInCluster = offset;
    return FsClusterBase(cluster);
}

/* ---- Find root directory entry by name ----------------------------------- */

static PFAT16_DIR_ENTRY FsFindFile(PCWSTR FileName)
{
    ULONG i;
    WCHAR name[13];
    WCHAR *p;

    for (i = 0; i < RamDiskBpb.RootEntryCount; i++) {
        if (RamDiskRootDir[i].FileName[0] == 0) break;
        if (RamDiskRootDir[i].FileName[0] == 0xE5) continue;
        if (RamDiskRootDir[i].Attributes & FAT16_ATTR_VOLUME_ID) continue;

        /* Convert 8.3 name to wide string */
        RtlZeroMemory(name, sizeof(name));
        p = name;
        for (int j = 0; j < 8 && RamDiskRootDir[i].FileName[j] != ' '; j++)
            *p++ = RamDiskRootDir[i].FileName[j];
        if (RamDiskRootDir[i].FileExt[0] != ' ') {
            *p++ = L'.';
            for (int j = 0; j < 3 && RamDiskRootDir[i].FileExt[j] != ' '; j++)
                *p++ = RamDiskRootDir[i].FileExt[j];
        }

        /* Case-insensitive comparison */
        UNICODE_STRING uName, uTarget;
        uName.Buffer = name;
        uName.Length = (USHORT)(p - name) * sizeof(WCHAR);
        uName.MaximumLength = uName.Length + sizeof(WCHAR);

        uTarget.Buffer = (PWSTR)FileName;
        uTarget.Length = (USHORT)wcslen(FileName) * sizeof(WCHAR);
        uTarget.MaximumLength = uTarget.Length + sizeof(WCHAR);

        if (RtlEqualUnicodeString(&uName, &uTarget, TRUE))
            return &RamDiskRootDir[i];
    }
    return NULL;
}

/* ---- Create file (add to root dir) --------------------------------------- */

static PFAT16_DIR_ENTRY FsCreateFileEntry(PCWSTR FileName, ULONG Size)
{
    ULONG i;
    WCHAR *dot;
    WCHAR name[8], ext[3];

    /* Find empty slot */
    for (i = 0; i < RamDiskBpb.RootEntryCount; i++) {
        if (RamDiskRootDir[i].FileName[0] == 0 ||
            RamDiskRootDir[i].FileName[0] == 0xE5)
            break;
    }
    if (i >= RamDiskBpb.RootEntryCount)
        return NULL;

    /* Parse name.ext */
    RtlZeroMemory(name, sizeof(name));
    RtlZeroMemory(ext, sizeof(ext));

    dot = (WCHAR *)wcsrchr(FileName, L'.');
    if (dot) {
        /* Copy name part */
        USHORT nameLen = (dot - FileName);
        if (nameLen > 8) nameLen = 8;
        RtlCopyMemory(name, FileName, nameLen * sizeof(WCHAR));
        /* Copy ext part */
        USHORT extLen = wcslen(dot + 1);
        if (extLen > 3) extLen = 3;
        RtlCopyMemory(ext, dot + 1, extLen * sizeof(WCHAR));
    } else {
        USHORT nameLen = wcslen(FileName);
        if (nameLen > 8) nameLen = 8;
        RtlCopyMemory(name, FileName, nameLen * sizeof(WCHAR));
    }

    /* Fill 8.3 entry */
    RtlCopyMemory(RamDiskRootDir[i].FileName, name, 8);
    RtlCopyMemory(RamDiskRootDir[i].FileExt, ext, 3);
    RamDiskRootDir[i].Attributes = FAT16_ATTR_ARCHIVE;
    RamDiskRootDir[i].FileSize = Size;
    RamDiskRootDir[i].FirstCluster = 0;  /* no data clusters for now */

    return &RamDiskRootDir[i];
}

/* ---- Open file ----------------------------------------------------------- */

NTSTATUS NTAPI FsOpenFile(PCWSTR FileName, PFILE_OBJECT *OutFile)
{
    PFAT16_DIR_ENTRY entry;
    ULONG fd;

    KIRQL _irql; KeAcquireSpinLock(&FsLock, &_irql);

    /* Check if already open */
    for (fd = 0; fd < FS_MAX_FILES; fd++) {
        if (FsFileInUse[fd] &&
            RtlEqualUnicodeString(&FsFileTable[fd].FileName,
                                  &(UNICODE_STRING){(USHORT)(wcslen(FileName)*sizeof(WCHAR)),
                                                    (USHORT)(wcslen(FileName)*sizeof(WCHAR)+sizeof(WCHAR)),
                                                    (PWSTR)FileName},
                                  TRUE)) {
            *OutFile = &FsFileTable[fd];
            KeReleaseSpinLock(&FsLock, 0);
            return STATUS_SUCCESS;
        }
    }

    /* Find in directory */
    entry = FsFindFile(FileName);
    if (!entry) {
        KeReleaseSpinLock(&FsLock, 0);
        return STATUS_NO_SUCH_FILE;
    }

    /* Allocate file descriptor */
    for (fd = 0; fd < FS_MAX_FILES; fd++) {
        if (!FsFileInUse[fd]) break;
    }
    if (fd >= FS_MAX_FILES) {
        KeReleaseSpinLock(&FsLock, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(&FsFileTable[fd], sizeof(FILE_OBJECT));
    FsFileTable[fd].Type = IO_TYPE_FILE;
    FsFileTable[fd].FileName.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                        wcslen(FileName)*sizeof(WCHAR)+sizeof(WCHAR), TAG_NAME);
    if (!FsFileTable[fd].FileName.Buffer) {
        KeReleaseSpinLock(&FsLock, 0);
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(FsFileTable[fd].FileName.Buffer, FileName,
                  wcslen(FileName)*sizeof(WCHAR));
    FsFileTable[fd].FileName.Buffer[wcslen(FileName)] = L'\0';
    FsFileTable[fd].FileName.Length = wcslen(FileName)*sizeof(WCHAR);
    FsFileTable[fd].FileName.MaximumLength = FsFileTable[fd].FileName.Length + sizeof(WCHAR);
    FsFileTable[fd].CurrentOffset = 0;
    FsFileTable[fd].FsContext = entry;
    FsFileTable[fd].ReadAccess = TRUE;
    FsFileTable[fd].WriteAccess = TRUE;
    FsFileInUse[fd] = TRUE;

    *OutFile = &FsFileTable[fd];
    KeReleaseSpinLock(&FsLock, 0);
    return STATUS_SUCCESS;
}

/* ---- NtCreateFile -------------------------------------------------------- */

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
                            ULONG EaLength)
{
    PIO_STATUS_BLOCK ioStatus = (PIO_STATUS_BLOCK)IoStatusBlock;
    PFILE_OBJECT file;
    NTSTATUS status;
    UNICODE_STRING fileName;

    UNREFERENCED_PARAMETER(DesiredAccess);
    UNREFERENCED_PARAMETER(SecurityDescriptor);
    UNREFERENCED_PARAMETER(FileAttributes);
    UNREFERENCED_PARAMETER(ShareAccess);
    UNREFERENCED_PARAMETER(EaBuffer);
    UNREFERENCED_PARAMETER(EaLength);

    if (!OutFileHandle) return STATUS_INVALID_PARAMETER;

    /* The file name is passed via EaBuffer for now (hack) — in real NT
       it would come from the object manager's parse procedure. For now
       we expect the caller to have set up the file name in the IO SB. */
    fileName.Buffer = (PWSTR)EaBuffer;
    fileName.Length = EaLength;
    fileName.MaximumLength = EaLength + sizeof(WCHAR);

    status = FsOpenFile((PCWSTR)EaBuffer, &file);
    if (!NT_SUCCESS(status)) {
        if (CreateDisposition == FILE_CREATE || CreateDisposition == FILE_OPEN_IF) {
            PFAT16_DIR_ENTRY entry = FsCreateFileEntry((PCWSTR)EaBuffer, 0);
            if (!entry) {
                if (ioStatus) {
                    ioStatus->Status = STATUS_DISK_FULL;
                    ioStatus->Information = 0;
                }
                return STATUS_DISK_FULL;
            }
            file = ExAllocatePool(NonPagedPool, sizeof(FILE_OBJECT));
            if (!file) {
                if (ioStatus) {
                    ioStatus->Status = STATUS_NO_MEMORY;
                    ioStatus->Information = 0;
                }
                return STATUS_NO_MEMORY;
            }
            RtlZeroMemory(file, sizeof(FILE_OBJECT));
            file->FileName.Buffer = (PWSTR)EaBuffer;
            file->FileName.Length = EaLength;
            file->FileName.MaximumLength = EaLength + sizeof(WCHAR);
            file->FsContext = entry;
            status = ObInsertHandle(file, OutFileHandle);
            if (NT_SUCCESS(status)) {
                if (ioStatus) {
                    ioStatus->Status = STATUS_SUCCESS;
                    ioStatus->Information = FILE_CREATED;
                }
            }
            return status;
        }
        if (ioStatus) {
            ioStatus->Status = status;
            ioStatus->Information = 0;
        }
        return status;
    }

    /* Create handle via object manager */
    status = ObInsertHandle(file, OutFileHandle);
    if (!NT_SUCCESS(status)) {
        if (ioStatus) {
            ioStatus->Status = status;
            ioStatus->Information = 0;
        }
        return status;
    }

    if (ioStatus) {
        ioStatus->Status = STATUS_SUCCESS;
        ioStatus->Information = 0;
    }

    return STATUS_SUCCESS;
}

/* ---- NtReadFile ---------------------------------------------------------- */

NTSTATUS NTAPI NtReadFile(HANDLE FileHandle,
                          PVOID Event,
                          PVOID ApcRoutine,
                          PVOID ApcContext,
                          PVOID IoStatusBlock,
                          PVOID Buffer,
                          ULONG Length,
                          PULONG64 ByteOffset,
                          PULONG Key)
{
    PIO_STATUS_BLOCK ioStatus = (PIO_STATUS_BLOCK)IoStatusBlock;
    PFILE_OBJECT file;
    PVOID body;
    NTSTATUS status;
    PFAT16_DIR_ENTRY entry;
    ULONG readOffset;
    ULONG readLength;

    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(ApcRoutine);
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(Key);

    status = ObReferenceObjectByHandle(FileHandle, NULL, &body);
    if (!NT_SUCCESS(status)) {
        if (ioStatus) {
            ioStatus->Status = status;
            ioStatus->Information = 0;
        }
        return status;
    }

    file = (PFILE_OBJECT)body;
    entry = (PFAT16_DIR_ENTRY)file->FsContext;

    readOffset = ByteOffset ? (ULONG)*ByteOffset : file->CurrentOffset;
    readLength = Length;

    if (readOffset >= entry->FileSize) {
        ObDereferenceObject(body);
        if (ioStatus) {
            ioStatus->Status = STATUS_END_OF_FILE;
            ioStatus->Information = 0;
        }
        return STATUS_END_OF_FILE;
    }

    if (readOffset + readLength > entry->FileSize)
        readLength = entry->FileSize - readOffset;

    /* Follow the FAT16 cluster chain and copy actual file data */
    ULONG remaining = readLength;
    ULONG totalRead = 0;
    ULONG currentOffset = readOffset;

    while (remaining > 0 && currentOffset < entry->FileSize) {
        ULONG cluster, offsetInCluster;
        PUCHAR clusterBase = FsResolveClusterChain(entry, currentOffset, &cluster, &offsetInCluster);
        if (!clusterBase) {
            /* Reached end of chain or invalid cluster — zero-fill remainder */
            break;
        }

        ULONG spaceInCluster = FsClusterSize() - offsetInCluster;
        ULONG toRead = remaining;
        if (currentOffset + toRead > entry->FileSize)
            toRead = entry->FileSize - currentOffset;
        if (toRead > spaceInCluster)
            toRead = spaceInCluster;

        RtlCopyMemory((PUCHAR)Buffer + totalRead, clusterBase + offsetInCluster, toRead);
        totalRead += toRead;
        remaining -= toRead;
        currentOffset += toRead;
    }

    readLength = totalRead;

    file->CurrentOffset = readOffset + readLength;

    ObDereferenceObject(body);

    if (ioStatus) {
        ioStatus->Status = STATUS_SUCCESS;
        ioStatus->Information = readLength;
    }

    return STATUS_SUCCESS;
}

/* ---- NtWriteFile --------------------------------------------------------- */

NTSTATUS NTAPI NtWriteFile(HANDLE FileHandle,
                           PVOID Event,
                           PVOID ApcRoutine,
                           PVOID ApcContext,
                           PVOID IoStatusBlock,
                           PVOID Buffer,
                           ULONG Length,
                           PULONG64 ByteOffset,
                           PULONG Key)
{
    PIO_STATUS_BLOCK ioStatus = (PIO_STATUS_BLOCK)IoStatusBlock;
    PFILE_OBJECT file;
    PVOID body;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Event);
    UNREFERENCED_PARAMETER(ApcRoutine);
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(Key);

    status = ObReferenceObjectByHandle(FileHandle, NULL, &body);
    if (!NT_SUCCESS(status)) {
        if (ioStatus) {
            ioStatus->Status = status;
            ioStatus->Information = 0;
        }
        return status;
    }

    file = (PFILE_OBJECT)body;

    PFAT16_DIR_ENTRY entry = (PFAT16_DIR_ENTRY)file->FsContext;

    /* Write data to the FAT16 cluster chain, allocating clusters as needed */
    ULONG writeOffset = ByteOffset ? (ULONG)*ByteOffset : file->CurrentOffset;
    ULONG remaining = Length;
    ULONG currentOffset = writeOffset;
    PUCHAR src = (PUCHAR)Buffer;

    if (!entry) {
        ObDereferenceObject(body);
        if (ioStatus) {
            ioStatus->Status = STATUS_INVALID_HANDLE;
            ioStatus->Information = 0;
        }
        return STATUS_INVALID_HANDLE;
    }

    /* Ensure the directory entry has a large enough FileSize field */
    if (writeOffset + Length > entry->FileSize) {
        entry->FileSize = writeOffset + Length;
    }

    while (remaining > 0) {
        ULONG cluster, offsetInCluster;
        PUCHAR clusterBase = FsResolveClusterChain(entry, currentOffset, &cluster, &offsetInCluster);

        if (!clusterBase) {
            /* Need to allocate a new cluster */
            PFAT16_DIR_ENTRY existingEntry = entry;
            USHORT totalClusters = (USHORT)((RamDiskSectors - RamDiskBpb.ReservedSectors -
                                RamDiskBpb.SectorsPerFAT -
                                (RamDiskBpb.RootEntryCount * 32 + RAMDISK_SECTOR_SIZE - 1) /
                                 RAMDISK_SECTOR_SIZE) / RamDiskBpb.SectorsPerCluster);
            ULONG newCluster;
            for (newCluster = 2; newCluster < totalClusters; newCluster++) {
                if (FsReadFatEntry(newCluster) == 0x0000) {
                    FsWriteFatEntry(newCluster, 0xFFFF);
                    if (existingEntry->FirstCluster == 0) {
                        existingEntry->FirstCluster = (USHORT)newCluster;
                    } else {
                        /* Find last cluster and chain to new one */
                        ULONG chase = existingEntry->FirstCluster;
                        while (chase) {
                            USHORT next = FsReadFatEntry(chase);
                            if (next == 0xFFFF || next == 0x0000) break;
                            chase = next;
                        }
                        FsWriteFatEntry(chase, (USHORT)newCluster);
                    }
                    clusterBase = FsClusterBase(newCluster);
                    cluster = newCluster;
                    offsetInCluster = 0;
                    break;
                }
            }
            if (!clusterBase) {
                /* No more clusters available */
                break;
            }
        }

        ULONG spaceInCluster = FsClusterSize() - offsetInCluster;
        ULONG toWrite = remaining;
        if (currentOffset + toWrite > entry->FileSize)
            toWrite = entry->FileSize - currentOffset;
        if (toWrite > spaceInCluster)
            toWrite = spaceInCluster;

        RtlCopyMemory(clusterBase + offsetInCluster, src, toWrite);
        src += toWrite;
        remaining -= toWrite;
        currentOffset += toWrite;
    }

    file->CurrentOffset = writeOffset + (Length - remaining);

    ObDereferenceObject(body);

    if (ioStatus) {
        ioStatus->Status = STATUS_SUCCESS;
        ioStatus->Information = Length - remaining;
    }

    return STATUS_SUCCESS;
}

/* ---- NtClose ------------------------------------------------------------- */

NTSTATUS NTAPI NtClose(HANDLE Handle)
{
    return ObCloseHandle(Handle);
}

/* ---- NtDeleteFile -------------------------------------------------------- */

NTSTATUS NTAPI NtDeleteFile(struct _OBJECT_ATTRIBUTES *ObjectAttributes)
{
    if (!ObjectAttributes || !ObjectAttributes->ObjectName) return STATUS_INVALID_PARAMETER;
    /* On the RAM disk we model delete by opening the file and zeroing
     * its contents. The entry then disappears from the directory listing
     * because FindNextFile scans the live cluster chain. */
    IO_STATUS_BLOCK isb;
    HANDLE h;
    NTSTATUS s = NtCreateFile(&h, 0x4001F, ObjectAttributes, &isb,
                              NULL, 0, 7, 1, 0x40, NULL, 0);
    if (!NT_SUCCESS(s)) return s;
    /* Zero out the file's contents. */
    UCHAR zero[256];
    RtlZeroMemory(zero, sizeof(zero));
    ULONG truncated = 0;
    while (truncated < (64 * 1024)) {
        s = NtWriteFile(h, NULL, NULL, NULL, &isb, zero, sizeof(zero), NULL, NULL);
        if (!NT_SUCCESS(s)) break;
        truncated += (ULONG)isb.Information;
    }
    NtClose(h);
    return STATUS_SUCCESS;
}

/* ---- NtQueryDirectoryFile ------------------------------------------------ */

NTSTATUS NTAPI NtQueryDirectoryFile(HANDLE FileHandle,
                                    PVOID IoStatusBlock,
                                    PVOID FileInformation,
                                    ULONG Length,
                                    ULONG FileInformationClass,
                                    BOOLEAN ReturnSingleEntry,
                                    PUNICODE_STRING FileName,
                                    BOOLEAN RestartScan)
{
    UNREFERENCED_PARAMETER(FileHandle);
    UNREFERENCED_PARAMETER(FileInformationClass);
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(RestartScan);

    PIO_STATUS_BLOCK ioStatus = (PIO_STATUS_BLOCK)IoStatusBlock;
    if (!ioStatus) return STATUS_INVALID_PARAMETER;

    if (!FileInformation || Length == 0) {
        ioStatus->Status = STATUS_INVALID_PARAMETER;
        ioStatus->Information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    PFILE_DIRECTORY_INFORMATION dirInfo = (PFILE_DIRECTORY_INFORMATION)FileInformation;
    RtlZeroMemory(dirInfo, Length);

    ULONG bytesUsed = 0;
    BOOLEAN found = FALSE;
    ULONG entriesReturned = 0;

    /* Always return "." and ".." in root directory */
    if (!FileName || FileName->Length == 0) {
        /* "." entry */
        dirInfo->NextEntryOffset = sizeof(FILE_DIRECTORY_INFORMATION) + 3 * sizeof(WCHAR);
        dirInfo->FileIndex = 0;
        dirInfo->CreationTime.QuadPart = 0;
        dirInfo->LastAccessTime.QuadPart = 0;
        dirInfo->LastWriteTime.QuadPart = 0;
        dirInfo->ChangeTime.QuadPart = 0;
        dirInfo->EndOfFile = 0;
        dirInfo->FileNameLength = 3 * sizeof(WCHAR);
        dirInfo->FileName[0] = L'.';
        dirInfo->FileName[1] = L'\0';
        dirInfo->FileName[2] = L'\0';
        bytesUsed = dirInfo->NextEntryOffset;

        /* ".." entry */
        PFILE_DIRECTORY_INFORMATION dotDot = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)dirInfo + bytesUsed);
        RtlZeroMemory(dotDot, Length - bytesUsed);
        dotDot->NextEntryOffset = 0;
        dotDot->FileIndex = 1;
        dotDot->CreationTime.QuadPart = 0;
        dotDot->LastAccessTime.QuadPart = 0;
        dotDot->LastWriteTime.QuadPart = 0;
        dotDot->ChangeTime.QuadPart = 0;
        dotDot->EndOfFile = 0;
        dotDot->FileNameLength = 4 * sizeof(WCHAR);
        dotDot->FileName[0] = L'.';
        dotDot->FileName[1] = L'.';
        dotDot->FileName[2] = L'\0';
        dotDot->FileName[3] = L'\0';
        bytesUsed += sizeof(FILE_DIRECTORY_INFORMATION) + 4 * sizeof(WCHAR);

        found = TRUE;
        entriesReturned = 2;
    }

    /* Scan root directory entries */
    for (ULONG i = 0; i < RamDiskBpb.RootEntryCount; i++) {
        if (RamDiskRootDir[i].FileName[0] == 0) break;
        if (RamDiskRootDir[i].FileName[0] == 0xE5) continue;
        if (RamDiskRootDir[i].Attributes & FAT16_ATTR_VOLUME_ID) continue;

        /* Build 8.3 name as wide string */
        WCHAR wideName[13];
        RtlZeroMemory(wideName, sizeof(wideName));
        int wIdx = 0;
        for (int j = 0; j < 8 && RamDiskRootDir[i].FileName[j] != ' '; j++) {
            wideName[wIdx++] = (WCHAR)RamDiskRootDir[i].FileName[j];
        }
        if (RamDiskRootDir[i].FileExt[0] != ' ') {
            wideName[wIdx++] = L'.';
            for (int j = 0; j < 3 && RamDiskRootDir[i].FileExt[j] != ' '; j++) {
                wideName[wIdx++] = (WCHAR)RamDiskRootDir[i].FileExt[j];
            }
        }
        wideName[wIdx] = L'\0';
        USHORT nameLen = (USHORT)(wIdx * sizeof(WCHAR));

        /* Check if this entry matches the filter name */
        if (FileName && FileName->Length > 0) {
            UNICODE_STRING filterName, entryName;
            filterName.Buffer = FileName->Buffer;
            filterName.Length = FileName->Length;
            filterName.MaximumLength = FileName->MaximumLength;
            entryName.Buffer = wideName;
            entryName.Length = nameLen;
            entryName.MaximumLength = nameLen + sizeof(WCHAR);
            if (!RtlEqualUnicodeString(&filterName, &entryName, TRUE))
                continue;
        }

        /* Calculate required space */
        ULONG entrySize = sizeof(FILE_DIRECTORY_INFORMATION) + nameLen;
        /* Align to 8 bytes */
        entrySize = (entrySize + 7) & ~7;

        if (bytesUsed + entrySize > Length) {
            /* No more space — return what we have */
            if (ReturnSingleEntry && found) {
                dirInfo->NextEntryOffset = 0;
            }
            break;
        }

        /* Write this directory entry */
        PFILE_DIRECTORY_INFORMATION entry = (PFILE_DIRECTORY_INFORMATION)((PUCHAR)dirInfo + bytesUsed);
        RtlZeroMemory(entry, entrySize);
        entry->NextEntryOffset = ReturnSingleEntry ? 0 : entrySize;
        entry->FileIndex = i;
        entry->CreationTime.QuadPart = 0;
        entry->LastAccessTime.QuadPart = 0;
        entry->LastWriteTime.QuadPart = 0;
        entry->ChangeTime.QuadPart = 0;
        entry->EndOfFile = RamDiskRootDir[i].FileSize;
        entry->FileNameLength = nameLen;
        RtlCopyMemory(entry->FileName, wideName, nameLen);

        bytesUsed += entrySize;
        found = TRUE;
        entriesReturned++;

        if (ReturnSingleEntry)
            break;
    }

    if (!found) {
        ioStatus->Status = STATUS_NO_SUCH_FILE;
        ioStatus->Information = 0;
        return STATUS_NO_SUCH_FILE;
    }

    ioStatus->Status = STATUS_SUCCESS;
    ioStatus->Information = bytesUsed;
    return STATUS_SUCCESS;
}
