#include <nt/hal.h>
#include <nt/ke.h>

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

static UCHAR KbdBuffer[256];
static volatile ULONG KbdHead, KbdTail;
static BOOLEAN KbdShift, KbdCaps, KbdCtrl, KbdAlt;
static BOOLEAN KbdExtended;
static volatile BOOLEAN KbdSasDetected;

static const CHAR ScancodeToAscii[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0, 0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-', 0,0,0,'+', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const CHAR ScancodeToAsciiShift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0, 0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    '-', 0,0,0,'+', 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const CHAR ExtendedScancodeToAscii[256] = {
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, '\r', 0,  0,  0,   /* 0x00-0x0F: Enter, etc. */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x10-0x1F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x20-0x2F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x30-0x3F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x40-0x4F: Arrow keys at 0x48,0x4B-0x4D */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x50-0x5F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x60-0x6F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x70-0x7F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x80-0x8F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0x90-0x9F */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0xA0-0xAF */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0xB0-0xBF */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0xC0-0xCF */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0xD0-0xDF */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0,   /* 0xE0-0xEF */
    0,    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,  0    /* 0xF0-0xFF */
};

static VOID NTAPI KiKeyboardInterrupt(PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UCHAR status = READ_PORT_UCHAR(KBD_STATUS_PORT);
    if (!(status & 1)) { HalEndOfInterrupt(1); return; }
    UCHAR scancode = READ_PORT_UCHAR(KBD_DATA_PORT);

    if (scancode == 0xE0) {
        KbdExtended = TRUE;
        HalEndOfInterrupt(1);
        return;
    }

    BOOLEAN extended = KbdExtended;
    KbdExtended = FALSE;
    static BOOL KbdCapsDown = FALSE;

    if (scancode == 0x2A || scancode == 0x36)
        KbdShift = TRUE;
    else if (scancode == 0xAA || scancode == 0xB6)
        KbdShift = FALSE;
    else if (scancode == 0x3A) {
        /* Toggle CapsLock only on make (press), not repeat */
        if (!KbdCapsDown) {
            KbdCaps = !KbdCaps;
            KbdCapsDown = TRUE;
        }
    }
    else if (scancode == 0xAA) {  /* CapsLock break code */
        KbdCapsDown = FALSE;
    }
    else if (scancode == 0x1D)
        KbdCtrl = TRUE;
    else if (scancode == 0x9D)
        KbdCtrl = FALSE;
    else if (scancode == 0x38)
        KbdAlt = TRUE;
    else if (scancode == 0xB8)
        KbdAlt = FALSE;

    /* SAS detection: Ctrl+Alt+Del (extended 0xE0 0x76) */
    if (KbdCtrl && KbdAlt && extended && scancode == 0x76) {
        KbdSasDetected = TRUE;
        DbgPrint("KBD: SAS (Ctrl+Alt+Del) detected!\n");
        HalEndOfInterrupt(1);
        return;
    }

    /* Ctrl+Alt+Del key release (all three released) */
    if (!KbdCtrl && !KbdAlt && !KbdShift && (scancode == 0x76 || scancode == 0x12 || scancode == 0x59)) {
        /* Don't clear SAS flag until it's consumed */
    }
    /* If the consumer drained the flag, clear it. */
    if (!KbdCtrl && !KbdAlt && !KbdShift) {
        KbdSasDetected = FALSE;
    }

    if (!(scancode & 0x80)) {
        CHAR c;
        if (extended) {
            c = ExtendedScancodeToAscii[scancode];
        } else {
            c = KbdShift ? ScancodeToAsciiShift[scancode]
                         : ScancodeToAscii[scancode];
            if (KbdCaps && c >= 'a' && c <= 'z')
                c -= 32;
            else if (KbdCaps && c >= 'A' && c <= 'Z')
                c += 32;
        }
        if (c) {
            ULONG next = (KbdHead + 1) & 0xFF;
            if (next != KbdTail) {
                KbdBuffer[KbdHead] = (UCHAR)c;
                KbdHead = next;
            }
        }
    }
    HalEndOfInterrupt(1);
}

NTSTATUS NTAPI HalpKbdInit(VOID)
{
    KbdHead = KbdTail = 0;
    KbdShift = KbdCaps = KbdCtrl = KbdAlt = FALSE;
    KbdExtended = FALSE;
    KeConnectInterrupt(PIC_IRQ_BASE + 1, KiKeyboardInterrupt);
    HalEnableSystemInterrupt(1);
    DbgPrint("KBD: PS/2 keyboard initialized (IRQ1)\n");
    return STATUS_SUCCESS;
}

CHAR NTAPI HalpKbdGetChar(VOID)
{
    if (KbdHead == KbdTail) return 0;
    CHAR c = (CHAR)KbdBuffer[KbdTail];
    KbdTail = (KbdTail + 1) & 0xFF;
    return c;
}

BOOLEAN NTAPI HalpKbdHasKey(VOID)
{
    return KbdHead != KbdTail;
}

BOOLEAN NTAPI HalpKbdWasSasDetected(VOID)
{
    return KbdSasDetected;
}

VOID NTAPI HalpKbdClearSas(VOID)
{
    KbdSasDetected = FALSE;
}
