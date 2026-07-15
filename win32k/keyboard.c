/*
 * MinNT - win32k/keyboard.c
 * Keyboard input handling for Win32k.
 *
 * Implements GetKeyboardState, GetKeyState, SetKeyboardState,
 * MapVirtualKey, GetKeyNameText, Keybd_event, GetKeyboardType.
 * Manages per-thread keyboard state arrays and key translation.
 */

#include "precomp.h"

#define VK_MAX_KEYS 256

/* Per-thread keyboard state */
typedef struct _KEYBOARD_STATE {
    UCHAR KeyState[VK_MAX_KEYS];
    BOOL  Initialized;
} KEYBOARD_STATE;

static KEYBOARD_STATE g_KbdState;

NTSTATUS NTAPI KeyboardInit(VOID)
{
    RtlZeroMemory(&g_KbdState, sizeof(g_KbdState));
    g_KbdState.Initialized = TRUE;

    DbgPrint("KEYBOARD: initialized (%d virtual keys)\n", VK_MAX_KEYS);
    return STATUS_SUCCESS;
}

/* UserGetKeyState: get the status of a virtual key */
NTSTATUS NTAPI UserGetKeyState(int vKey, SHORT *pState)
{
    UCHAR state;

    if (!pState) return STATUS_INVALID_PARAMETER;
    if (vKey < 0 || vKey >= VK_MAX_KEYS) {
        *pState = 0;
        return STATUS_SUCCESS;
    }

    state = g_KbdState.KeyState[vKey];

    /* Clear the "was pressed" bit after reading (like real GetKeyState) */
    g_KbdState.KeyState[vKey] &= ~0x80;

    *pState = (SHORT)(state & 0x80) ? -1 : 0;

    /* Include toggle state in low bit for CapsLock, NumLock, ScrollLock */
    if (vKey == VK_CAPITAL || vKey == VK_NUMLOCK || vKey == VK_SCROLL) {
        *pState = (state & 0x01) ? 1 : 0;
    }

    DbgPrint("KEYBOARD: GetKeyState(0x%X) -> %d\n", vKey, *pState);
    return STATUS_SUCCESS;
}

/* UserGetAsyncKeyState: async version (no "was pressed" clearing) */
NTSTATUS NTAPI UserGetAsyncKeyState2(int vKey, SHORT *pState)
{
    if (!pState) return STATUS_INVALID_PARAMETER;
    if (vKey < 0 || vKey >= VK_MAX_KEYS) {
        *pState = 0;
        return STATUS_SUCCESS;
    }

    *pState = (g_KbdState.KeyState[vKey] & 0x80) ? -1 : 0;
    return STATUS_SUCCESS;
}

/* UserGetKeyboardState: copy entire keyboard state array */
NTSTATUS NTAPI UserGetKeyboardState(PUCHAR pKeyState)
{
    if (!pKeyState) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(pKeyState, g_KbdState.KeyState, VK_MAX_KEYS);
    return STATUS_SUCCESS;
}

/* UserSetKeyboardState: set entire keyboard state array */
NTSTATUS NTAPI UserSetKeyboardState(const PUCHAR pKeyState)
{
    if (!pKeyState) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(g_KbdState.KeyState, pKeyState, VK_MAX_KEYS);
    return STATUS_SUCCESS;
}

/* Internal: update key state from hardware event */
VOID KeyboardUpdateKeyState(int vKey, BOOL Down)
{
    if (vKey < 0 || vKey >= VK_MAX_KEYS) return;

    if (Down) {
        g_KbdState.KeyState[vKey] |= 0x80;
        /* Toggle keys flip the low bit */
        if (vKey == VK_CAPITAL || vKey == VK_NUMLOCK || vKey == VK_SCROLL) {
            g_KbdState.KeyState[vKey] ^= 0x01;
        }
    } else {
        g_KbdState.KeyState[vKey] &= ~0x80;
    }
}

/* UserMapVirtualKey: map virtual key to scan code or vice versa */
NTSTATUS NTAPI UserMapVirtualKey(UINT uCode, UINT uMapType, PUINT pResult)
{
    UINT result = 0;

    if (!pResult) return STATUS_INVALID_PARAMETER;

    switch (uMapType) {
        case 0: /* VK to scan code (PS/2 Set 1) */
            if (uCode >= 'A' && uCode <= 'Z') {
                result = uCode - 'A' + 0x1E; /* Set 1: A=0x1E, B=0x2F ... Z=0x2C */
            } else if (uCode >= '0' && uCode <= '9') {
                /* Set 1: 0=0x0B (VK_0), 1=0x02 ... 9=0x0A (VK_9) */
                result = (uCode == '0') ? 0x0B : (uCode - '1' + 0x02);
            } else {
                switch (uCode) {
                    case VK_RETURN:  result = 0x1C; break;
                    case VK_SPACE:   result = 0x39; break;
                    case VK_BACK:    result = 0x0E; break;
                    case VK_TAB:     result = 0x0F; break;
                    case VK_ESCAPE:  result = 0x01; break;
                    case VK_LEFT:    result = 0x4B; break;
                    case VK_UP:      result = 0x48; break;
                    case VK_RIGHT:   result = 0x4D; break;
                    case VK_DOWN:    result = 0x50; break;
                    case VK_F1:      result = 0x3B; break;
                    case VK_F2:      result = 0x3C; break;
                    case VK_F3:      result = 0x3D; break;
                    case VK_F4:      result = 0x3E; break;
                    case VK_F5:      result = 0x3F; break;
                    case VK_F6:      result = 0x40; break;
                    case VK_F7:      result = 0x41; break;
                    case VK_F8:      result = 0x42; break;
                    case VK_F9:      result = 0x43; break;
                    case VK_F10:     result = 0x44; break;
                    case VK_F11:     result = 0x57; break;
                    case VK_F12:     result = 0x58; break;
                    case VK_INSERT:  result = 0x52; break;
                    case VK_DELETE:  result = 0x53; break;
                    case VK_HOME:    result = 0x47; break;
                    case VK_END:     result = 0x4F; break;
                    case VK_PRIOR:   result = 0x49; break;
                    case VK_NEXT:    result = 0x51; break;
                    case VK_SHIFT:   result = 0x2A; break;
                    case VK_CONTROL: result = 0x1D; break;
                    case VK_MENU:    result = 0x38; break;
                    case VK_CAPITAL: result = 0x3A; break;
                    case VK_NUMLOCK: result = 0x45; break;
                    case VK_SCROLL:  result = 0x46; break;
                    default:         result = 0; break;
                }
            }
            break;

        case 1: /* Scan code to VK (PS/2 Set 1) */
            if (uCode >= 0x1E && uCode <= 0x2C) {
                /* A=0x1E, B=0x30, ... Z=0x2C (note: non-contiguous) */
                static const UCHAR scanToVK[] = {
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x00-0x0F */
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,         /* 0x10-0x1D */
                    'A','S','D','F','G','H','J','K','L', /* 0x1E-0x26 (A-J) */
                    0xBA,0xBB,0x0D,                      /* 0x27-0x29 (;:'\n') */
                    0xC0,                                /* 0x2A (LShift) */
                    0xBC,                                /* 0x2B (\\)*/
                    'Z','X','C','V','B','N','M'          /* 0x2C-0x32 (Z-M) */
                };
                if (uCode <= 0x32) {
                    result = scanToVK[uCode - 0x1E];
                }
            } else if (uCode >= 0x02 && uCode <= 0x0A) {
                /* Top-row digits 1-9, 0 at 0x0B */
                result = (uCode == 0x0B) ? '0' : (uCode - 0x02 + '1');
            } else {
                switch (uCode) {
                    case 0x01: result = VK_ESCAPE; break;
                    case 0x0E: result = VK_BACK;   break;
                    case 0x0F: result = VK_TAB;    break;
                    case 0x1C: result = VK_RETURN; break;
                    case 0x39: result = VK_SPACE;  break;
                    case 0x3A: result = VK_CAPITAL;break;
                    case 0x3B: result = VK_F1;     break;
                    case 0x3C: result = VK_F2;     break;
                    case 0x3D: result = VK_F3;     break;
                    case 0x3E: result = VK_F4;     break;
                    case 0x3F: result = VK_F5;     break;
                    case 0x40: result = VK_F6;     break;
                    case 0x41: result = VK_F7;     break;
                    case 0x42: result = VK_F8;     break;
                    case 0x43: result = VK_F9;     break;
                    case 0x44: result = VK_F10;    break;
                    case 0x45: result = VK_NUMLOCK;break;
                    case 0x46: result = VK_SCROLL; break;
                    case 0x47: result = VK_HOME;   break;
                    case 0x48: result = VK_UP;     break;
                    case 0x49: result = VK_PRIOR;  break;
                    case 0x4B: result = VK_LEFT;   break;
                    case 0x4D: result = VK_RIGHT;  break;
                    case 0x4F: result = VK_END;    break;
                    case 0x50: result = VK_DOWN;   break;
                    case 0x51: result = VK_NEXT;   break;
                    case 0x52: result = VK_INSERT; break;
                    case 0x53: result = VK_DELETE; break;
                    default:   result = 0;         break;
                }
            }
            break;

        case 2: /* VK to ASCII */
            if (uCode >= 'A' && uCode <= 'Z') {
                result = uCode;
            } else if (uCode >= '0' && uCode <= '9') {
                result = uCode;
            }
            break;

        case 3: /* ASCII to VK */
            result = uCode;
            break;

        default:
            break;
    }

    *pResult = result;
    DbgPrint("KEYBOARD: MapVirtualKey(%u, %u) -> %u\n", uCode, uMapType, result);
    return STATUS_SUCCESS;
}

/* UserGetKeyNameText: get the name for a scan code */
NTSTATUS NTAPI UserKeyNameText(LONG lParam, PWCHAR pName, int cchName)
{
    UINT scanCode = (lParam >> 16) & 0xFF;

    if (!pName || cchName <= 0) return STATUS_INVALID_PARAMETER;

    /* Simplified key name generation */
    if (scanCode >= 30 && scanCode <= 55) {
        /* Letter keys */
        if (cchName >= 2) {
            pName[0] = (WCHAR)(scanCode - 30 + 'A');
            pName[1] = 0;
        }
    } else if (scanCode == 0x1C) {
        if (cchName >= 7) {
            RtlCopyMemory(pName, L"Enter", 6 * sizeof(WCHAR));
            pName[5] = 0;
        }
    } else if (scanCode == 0x39) {
        if (cchName >= 6) {
            RtlCopyMemory(pName, L"Space", 6 * sizeof(WCHAR));
            pName[5] = 0;
        }
    } else if (scanCode == 0x0E) {
        if (cchName >= 5) {
            RtlCopyMemory(pName, L"Back", 5 * sizeof(WCHAR));
            pName[4] = 0;
        }
    } else if (scanCode == 0x01) {
        if (cchName >= 7) {
            RtlCopyMemory(pName, L"Escape", 7 * sizeof(WCHAR));
            pName[6] = 0;
        }
    } else {
        if (cchName >= 5) {
            RtlCopyMemory(pName, L"Key?", 5 * sizeof(WCHAR));
            pName[4] = 0;
        }
    }

    return STATUS_SUCCESS;
}

/* UserToUnicode: convert virtual key to unicode character
 *
 * wFlags bits:
 *   0x01 = TM_MENU    - Alt/menu key is down (prefix ESC for menu mode)
 *   0x02 = TM_KEYPREV - previous key state was down (used to suppress
 *                       autorepeat characters)
 *   0x04 = TM_TRANS   - key transition state (key-up) - produces no char
 */
NTSTATUS NTAPI UserToUnicode(UINT wVirtKey, UINT wScanCode, const PUCHAR lpKeyState,
                              PWCHAR pwszBuff, int cchBuff, UINT wFlags)
{
    UINT vk;
    WCHAR ch = 0;
    BOOL fShift, fCaps;
    BOOL fMenu = (wFlags & 0x01) != 0;
    BOOL fPrevDown = (wFlags & 0x02) != 0;
    BOOL fTransition = (wFlags & 0x04) != 0;
    int pos = 0;

    if (!pwszBuff || cchBuff <= 0) return STATUS_INVALID_PARAMETER;

    vk = wVirtKey;
    fShift = (lpKeyState && (lpKeyState[VK_SHIFT] & 0x80)) ? TRUE : FALSE;
    fCaps  = (lpKeyState && (lpKeyState[VK_CAPITAL] & 0x01)) ? TRUE : FALSE;

    /* Key-up transitions never produce characters. */
    if (fTransition) {
        pwszBuff[0] = 0;
        return STATUS_SUCCESS;
    }

    /* Autorepeat: if the previous state was down (key is held), skip the
     * character to emulate NT's behavior where only the initial key-down
     * generates WM_CHAR. */
    if (fPrevDown) {
        pwszBuff[0] = 0;
        return STATUS_SUCCESS;
    }

    /* Simple VK to Unicode conversion */
    if (vk >= 'A' && vk <= 'Z') {
        /* CapsLock (fCaps) XOR Shift selects uppercase */
        ch = (fCaps ^ fShift) ? (WCHAR)vk : (WCHAR)(vk + 32);
    } else if (vk >= '0' && vk <= '9') {
        if (fShift) {
            /* Shifted numbers -> symbols */
            static const WCHAR shiftDigits[] = L")!@#$%^&*(";
            ch = shiftDigits[vk - '0'];
        } else {
            ch = (WCHAR)vk;
        }
    } else if (vk == VK_SPACE) {
        ch = L' ';
    } else if (vk == VK_RETURN) {
        ch = L'\r'; /* CR; LF appended below */
    } else if (vk == VK_TAB) {
        ch = L'\t';
    } else if (vk == VK_BACK) {
        ch = L'\b';
    } else if (vk == VK_ESCAPE) {
        ch = L'\x1B';
    } else {
        /* Fallback: scan-code-based translation for OEM keys whose VK
         * does not map directly. Scan code sits in WM_CHAR lParam bits
         * 16-23 when the caller posts the message. */
        switch (wScanCode) {
            case 0x0C: ch = fShift ? L'_' : L'-';  break;
            case 0x0D: ch = fShift ? L'+' : L'=';  break;
            case 0x1A: ch = fShift ? L'{' : L'[';  break;
            case 0x1B: ch = fShift ? L'}' : L']';  break;
            case 0x27: ch = fShift ? L':' : L';';  break;
            case 0x28: ch = fShift ? L'"' : L'\''; break;
            case 0x29: ch = fShift ? L'~' : L'`';  break;
            case 0x2B: ch = fShift ? L'|' : L'\\'; break;
            case 0x33: ch = fShift ? L'<' : L',';  break;
            case 0x34: ch = fShift ? L'>' : L'.';  break;
            case 0x35: ch = fShift ? L'?' : L'/';  break;
            default:   ch = 0;                     break;
        }
    }

    /* Menu (Alt) prefix: ESC before the character enables menu navigation */
    if (fMenu && ch && ch != L'\x1B' && pos < cchBuff - 1) {
        pwszBuff[pos++] = L'\x1B';
    }

    if (ch && pos < cchBuff - 1) {
        pwszBuff[pos++] = ch;
    }

    /* RETURN produces CR + LF */
    if (vk == VK_RETURN && pos < cchBuff - 1) {
        pwszBuff[pos++] = L'\n';
    }

    pwszBuff[pos] = 0;
    return STATUS_SUCCESS;
}

/* UserKeyboardType: get keyboard type */
NTSTATUS NTAPI UserKeyboardType(UINT TypeIndex, PUINT pType)
{
    if (!pType) return STATUS_INVALID_PARAMETER;

    switch (TypeIndex) {
        case 0: /* Keyboard type */
            *pType = 4;  /* Enhanced 101/102-key */
            break;
        case 1: /* Keyboard subtype */
            *pType = 0;  /* Standard subtype */
            break;
        case 2: /* Number of function keys */
            *pType = 12; /* F1-F12 */
            break;
        default:
            *pType = 0;
            break;
    }

    return STATUS_SUCCESS;
}

/* UserSetKeyboardLed: set keyboard LED indicators.
 *
 * Programs the i8042 keyboard controller to update the LED state.
 * Sequence:
 *   1. Wait for input buffer empty (port 0x64 bit 1 = 0)
 *   2. Write 0xED (SET LEDS command) to port 0x60
 *   3. Wait for ACK (0xFA) from controller
 *   4. Write LED byte (bit 0 = Scroll, bit 1 = Num, bit 2 = Caps)
 */
NTSTATUS NTAPI UserSetKeyboardLed(UINT LedFlags)
{
    UCHAR ledByte;
    ULONG timeout;

    /* Translate Win32 LED flags to PS/2 LED byte.
     * Win32: bit 0 = Scroll Lock, bit 1 = Num Lock, bit 2 = Caps Lock
     * PS/2: same mapping. */
    ledByte = (UCHAR)(LedFlags & 0x07);

    /* Step 1: Wait for input buffer empty. */
    timeout = 100000;
    while ((READ_PORT_UCHAR(0x64) & 0x02) && --timeout) {
        KeStallExecutionProcessor(1);
    }
    if (timeout == 0) return STATUS_DEVICE_BUSY;

    /* Step 2: Send SET LEDS command. */
    WRITE_PORT_UCHAR(0x60, 0xED);

    /* Step 3: Wait for ACK. */
    timeout = 100000;
    while ((READ_PORT_UCHAR(0x64) & 0x01) == 0 && --timeout) {
        KeStallExecutionProcessor(1);
    }
    if (timeout == 0) return STATUS_IO_TIMEOUT;

    if (READ_PORT_UCHAR(0x60) != 0xFA) {
        DbgPrint("KEYBOARD: SetKeyboardLed got non-ACK response\n");
        return STATUS_IO_DEVICE_ERROR;
    }

    /* Step 4: Send LED byte. */
    timeout = 100000;
    while ((READ_PORT_UCHAR(0x64) & 0x02) && --timeout) {
        KeStallExecutionProcessor(1);
    }
    if (timeout == 0) return STATUS_DEVICE_BUSY;

    WRITE_PORT_UCHAR(0x60, ledByte);

    /* Wait for ACK. */
    timeout = 100000;
    while ((READ_PORT_UCHAR(0x64) & 0x01) == 0 && --timeout) {
        KeStallExecutionProcessor(1);
    }
    if (timeout == 0) return STATUS_IO_TIMEOUT;

    DbgPrint("KEYBOARD: SetKeyboardLed flags=0x%X -> PS/2 byte 0x%X\n",
             LedFlags, ledByte);
    return STATUS_SUCCESS;
}

/* UserEnableKeyboardLayout: enable a keyboard layout */
NTSTATUS NTAPI UserEnableKeyboardLayout(UINT LayoutId, PUINT pPrevLayout)
{
    if (pPrevLayout) *pPrevLayout = 0x409; /* US English */

    DbgPrint("KEYBOARD: EnableKeyboardLayout(0x%X)\n", LayoutId);
    return STATUS_SUCCESS;
}
