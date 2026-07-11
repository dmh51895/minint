#include <nt/hal.h>

#define MB2_TAG_TYPE_FRAMEBUFFER 8

typedef struct _MB2_TAG_HEADER {
    ULONG Type;
    ULONG Size;
} MB2_TAG_HEADER;

typedef struct __attribute__((packed)) _MB2_FB_TAG {
    ULONG Type;
    ULONG Size;
    ULONG64 Addr;
    ULONG Pitch;
    ULONG Width;
    ULONG Height;
    UCHAR Bpp;
    UCHAR FbType;
    USHORT Reserved;
} MB2_FB_TAG;

NTSTATUS NTAPI HalpParseMb2Framebuffer(PVOID Mb2Info, PMB2_FRAMEBUFFER_INFO Out)
{
    if (!Mb2Info || !Out) return STATUS_INVALID_PARAMETER;
    Out->Valid = FALSE;

    ULONG TotalSize = *(ULONG *)Mb2Info;
    UCHAR *Ptr = (UCHAR *)Mb2Info + 8;
    UCHAR *End = (UCHAR *)Mb2Info + TotalSize;

    while (Ptr < End) {
        MB2_TAG_HEADER *Tag = (MB2_TAG_HEADER *)Ptr;
        if (Tag->Type == 0 && Tag->Size == 0) break;
        if (Tag->Type == MB2_TAG_TYPE_FRAMEBUFFER) {
            MB2_FB_TAG *FbTag = (MB2_FB_TAG *)Ptr;
            Out->Address = (PVOID)(ULONG_PTR)FbTag->Addr;
            Out->Width = FbTag->Width;
            Out->Height = FbTag->Height;
            Out->Pitch = FbTag->Pitch;
            Out->Bpp = FbTag->Bpp;
            Out->Valid = TRUE;
            DbgPrint("MB2: framebuffer %ux%u bpp=%u pitch=%u type=%u at %p\n",
                     FbTag->Width, FbTag->Height, (ULONG)FbTag->Bpp, FbTag->Pitch, (ULONG)FbTag->FbType, Out->Address);
            return STATUS_SUCCESS;
        }
        ULONG AlignedSize = (Tag->Size + 7) & ~7;
        Ptr += AlignedSize;
    }

    DbgPrint("MB2: no framebuffer tag found\n");
    return STATUS_UNSUCCESSFUL;
}
