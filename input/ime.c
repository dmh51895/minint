/*
 * MinNT - input/ime.c
 * Input Method Editor (IME) framework.
 *
 * Provides IME composition infrastructure for non-Latin text input.
 * The IME sits between the keyboard and the application, intercepting
 * keystrokes and converting them to composition characters.
 *
 * For MinNT we implement a minimal framework:
 *   - IME registration (multiple IMEs supported)
 *   - Composition state machine (empty, composing, committed)
 *   - Composition string buffer
 *   - Candidate list (a list of possible matches)
 *   - Default English IME (passthrough)
 *   - Default Japanese-style IME (hiragana composition)
 */

#include <nt/ke.h>
#include <nt/rtl.h>

#define MAX_IMES 4
#define MAX_COMPOSITION_LEN 32
#define MAX_CANDIDATES 16

typedef enum _IME_STATE {
    IME_STATE_IDLE,           /* no composition active */
    IME_STATE_COMPOSING,      /* user is entering characters */
    IME_STATE_CANDIDATES,     /* showing candidate list */
    IME_STATE_COMMITTED       /* composition complete */
} IME_STATE;

typedef enum _IME_LANGUAGE {
    IME_LANG_ENGLISH = 0,
    IME_LANG_JAPANESE,
    IME_LANG_CHINESE_SIMP,
    IME_LANG_KOREAN
} IME_LANGUAGE;

typedef struct _IME {
    IME_LANGUAGE Language;
    WCHAR Name[32];
    BOOLEAN Active;
    BOOLEAN InUse;
} IME, *PIME;

typedef struct _IME_CONTEXT {
    IME_STATE State;
    WCHAR Composition[MAX_COMPOSITION_LEN];
    ULONG CompositionLen;
    ULONG CaretPos;
    WCHAR Candidates[MAX_CANDIDATES][MAX_COMPOSITION_LEN];
    ULONG CandidateCount;
    ULONG SelectedCandidate;
    PIME ActiveIME;
    BOOLEAN InUse;
} IME_CONTEXT, *PIME_CONTEXT;

static IME g_IMEs[MAX_IMES];
static IME_CONTEXT g_IMEContext;
static KSPIN_LOCK g_ImeLock;

/* Convert ASCII keystrokes to hiragana characters (simple romaji-to-kana). */
static const WCHAR *g_RomajiToKana[] = {
    L"a", L"i", L"u", L"e", L"o",
    L"ka", L"ki", L"ku", L"ke", L"ko",
    L"sa", L"shi", L"su", L"se", L"so",
    L"ta", L"chi", L"tsu", L"te", L"to",
    L"na", L"ni", L"nu", L"ne", L"no",
    L"ha", L"hi", L"fu", L"he", L"ho",
    L"ma", L"mi", L"mu", L"me", L"mo",
    L"ya", L"yu", L"yo",
    L"ra", L"ri", L"ru", L"re", L"ro",
    L"wa", L"wo", L"n",
    NULL
};

static VOID ToHiragana(const WCHAR *romaji, WCHAR *out)
{
    ULONG j = 0;
    while (*romaji && j < MAX_COMPOSITION_LEN - 1) {
        ULONG i = 0;
        BOOLEAN found = FALSE;
        while (g_RomajiToKana[i]) {
            ULONG k = 0;
            BOOLEAN match = TRUE;
            while (g_RomajiToKana[i][k] && romaji[k]) {
                if (g_RomajiToKana[i][k] != romaji[k]) { match = FALSE; break; }
                k++;
            }
            if (match && g_RomajiToKana[i][k] == 0 && romaji[k] == 0) {
                /* Full match - copy kana to output */
                const WCHAR *kana = g_RomajiToKana[i];
                while (*kana && j < MAX_COMPOSITION_LEN - 1) out[j++] = *kana++;
                romaji += k;
                found = TRUE;
                break;
            } else if (match && g_RomajiToKana[i][k] == 0 && romaji[k] != 0) {
                /* Partial match - use the longest so far */
                const WCHAR *kana = g_RomajiToKana[i];
                while (*kana && j < MAX_COMPOSITION_LEN - 1) out[j++] = *kana++;
                romaji += k;
                found = TRUE;
                break;
            }
            i++;
        }
        if (!found) {
            /* Unknown character - pass through */
            out[j++] = *romaji++;
        }
    }
    out[j] = 0;
}

NTSTATUS NTAPI ImeInit(VOID)
{
    RtlZeroMemory(g_IMEs, sizeof(g_IMEs));
    RtlZeroMemory(&g_IMEContext, sizeof(g_IMEContext));
    KeInitializeSpinLock(&g_ImeLock);

    /* Register default IMEs */
    {
        IME *e = &g_IMEs[0];
        e->Language = IME_LANG_ENGLISH;
        RtlCopyMemory(e->Name, L"English (US)", 24);
        e->Name[23] = 0;
        e->Active = TRUE;
        e->InUse = TRUE;
    }
    {
        IME *e = &g_IMEs[1];
        e->Language = IME_LANG_JAPANESE;
        RtlCopyMemory(e->Name, L"Japanese", 16);
        e->Name[15] = 0;
        e->Active = FALSE;
        e->InUse = TRUE;
    }
    {
        IME *e = &g_IMEs[2];
        e->Language = IME_LANG_CHINESE_SIMP;
        RtlCopyMemory(e->Name, L"Chinese (Simplified)", 40);
        e->Name[39] = 0;
        e->Active = FALSE;
        e->InUse = TRUE;
    }
    {
        IME *e = &g_IMEs[3];
        e->Language = IME_LANG_KOREAN;
        RtlCopyMemory(e->Name, L"Korean", 13);
        e->Name[12] = 0;
        e->Active = FALSE;
        e->InUse = TRUE;
    }

    g_IMEContext.State = IME_STATE_IDLE;
    g_IMEContext.CompositionLen = 0;
    g_IMEContext.CaretPos = 0;
    g_IMEContext.CandidateCount = 0;
    g_IMEContext.SelectedCandidate = 0;
    g_IMEContext.InUse = TRUE;
    g_IMEContext.ActiveIME = &g_IMEs[0];

    DbgPrint("IME: framework initialized (%d IMEs registered)\n", 4);
    return STATUS_SUCCESS;
}

/* Register a new IME. */
NTSTATUS NTAPI ImeRegister(IME_LANGUAGE Language, const WCHAR *Name)
{
    ULONG i;
    KIRQL irql;
    if (!Name) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    for (i = 0; i < MAX_IMES; i++) {
        if (!g_IMEs[i].InUse) {
            RtlZeroMemory(&g_IMEs[i], sizeof(IME));
            g_IMEs[i].Language = Language;
            {
                ULONG j = 0;
                while (Name[j] && j < 31) g_IMEs[i].Name[j] = Name[j], j++;
                g_IMEs[i].Name[j] = 0;
            }
            g_IMEs[i].InUse = TRUE;
            KeReleaseSpinLock(&g_ImeLock, &irql);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return STATUS_INSUFFICIENT_RESOURCES;
}

/* Activate an IME by index. */
NTSTATUS NTAPI ImeActivate(ULONG Index)
{
    KIRQL irql;
    if (Index >= MAX_IMES || !g_IMEs[Index].InUse) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    {
        ULONG i;
        for (i = 0; i < MAX_IMES; i++) g_IMEs[i].Active = (i == Index);
        g_IMEContext.ActiveIME = &g_IMEs[Index];
        g_IMEContext.State = IME_STATE_IDLE;
        g_IMEContext.CompositionLen = 0;
        g_IMEContext.CaretPos = 0;
        g_IMEContext.CandidateCount = 0;
    }
    KeReleaseSpinLock(&g_ImeLock, &irql);
    DbgPrint("IME: activated '%ws'\n", g_IMEs[Index].Name);
    return STATUS_SUCCESS;
}

/* Process a keystroke through the active IME. */
NTSTATUS NTAPI ImeProcessKey(WCHAR Key)
{
    KIRQL irql;
    if (!g_IMEContext.ActiveIME) return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&g_ImeLock, &irql);

    /* English IME: just pass through to composition. */
    if (g_IMEContext.ActiveIME->Language == IME_LANG_ENGLISH) {
        if (Key >= 0x20 && Key < 0x7F) {
            if (g_IMEContext.CompositionLen < MAX_COMPOSITION_LEN - 1) {
                g_IMEContext.Composition[g_IMEContext.CompositionLen++] = Key;
                g_IMEContext.Composition[g_IMEContext.CompositionLen] = 0;
                g_IMEContext.State = IME_STATE_COMPOSING;
            }
        }
        KeReleaseSpinLock(&g_ImeLock, &irql);
        return STATUS_SUCCESS;
    }

    /* Japanese IME: convert romaji to hiragana. */
    if (g_IMEContext.ActiveIME->Language == IME_LANG_JAPANESE) {
        if (Key >= 'a' && Key <= 'z') {
            /* Append to current romaji buffer (reuse composition as buffer) */
            if (g_IMEContext.CompositionLen < MAX_COMPOSITION_LEN - 1) {
                g_IMEContext.Composition[g_IMEContext.CompositionLen++] = Key;
                g_IMEContext.Composition[g_IMEContext.CompositionLen] = 0;
            }
            g_IMEContext.State = IME_STATE_COMPOSING;
        } else if (Key == 0x0D /* Enter */) {
            /* Commit composition */
            g_IMEContext.State = IME_STATE_COMMITTED;
        } else if (Key == 0x08 /* Backspace */) {
            if (g_IMEContext.CompositionLen > 0) {
                g_IMEContext.Composition[--g_IMEContext.CompositionLen] = 0;
            }
        } else if (Key == 0x20 /* Space */) {
            /* Convert current buffer to hiragana */
            WCHAR hiragana[MAX_COMPOSITION_LEN];
            ToHiragana(g_IMEContext.Composition, hiragana);
            RtlCopyMemory(g_IMEContext.Composition, hiragana,
                          sizeof(WCHAR) * (RtlWStringLength(hiragana) + 1));
            g_IMEContext.CompositionLen = RtlWStringLength(g_IMEContext.Composition);
        } else if (Key == 0x09 /* Tab */) {
            /* Generate candidates (simple: variations of the composition) */
            g_IMEContext.CandidateCount = 0;
            if (g_IMEContext.CompositionLen > 0) {
                ULONG i;
                for (i = 0; i < 4 && g_IMEContext.CandidateCount < MAX_CANDIDATES; i++) {
                    RtlCopyMemory(g_IMEContext.Candidates[g_IMEContext.CandidateCount],
                                  g_IMEContext.Composition,
                                  sizeof(WCHAR) * (g_IMEContext.CompositionLen + 1));
                    g_IMEContext.CandidateCount++;
                }
            }
            g_IMEContext.SelectedCandidate = 0;
            g_IMEContext.State = IME_STATE_CANDIDATES;
        }
    }

    KeReleaseSpinLock(&g_ImeLock, &irql);
    return STATUS_SUCCESS;
}

/* Commit the current composition. */
NTSTATUS NTAPI ImeCommit(PWCHAR OutBuffer, ULONG BufferLen)
{
    KIRQL irql;
    ULONG i;
    if (!OutBuffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    if (g_IMEContext.State == IME_STATE_CANDIDATES &&
        g_IMEContext.SelectedCandidate < g_IMEContext.CandidateCount) {
        RtlCopyMemory(OutBuffer, g_IMEContext.Candidates[g_IMEContext.SelectedCandidate],
                      sizeof(WCHAR) * MAX_COMPOSITION_LEN);
    } else {
        RtlCopyMemory(OutBuffer, g_IMEContext.Composition,
                      sizeof(WCHAR) * MAX_COMPOSITION_LEN);
    }
    /* Reset state */
    g_IMEContext.State = IME_STATE_IDLE;
    g_IMEContext.CompositionLen = 0;
    g_IMEContext.CaretPos = 0;
    g_IMEContext.CandidateCount = 0;
    (void)i;
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return STATUS_SUCCESS;
}

/* Get the current composition string. */
NTSTATUS NTAPI ImeGetComposition(PWCHAR OutBuffer, ULONG BufferLen,
                                  PULONG pCompositionLen, PULONG pCaretPos)
{
    KIRQL irql;
    if (!OutBuffer || BufferLen == 0) return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    {
        ULONG len = g_IMEContext.CompositionLen;
        if (len >= BufferLen) len = BufferLen - 1;
        for (ULONG i = 0; i < len; i++) OutBuffer[i] = g_IMEContext.Composition[i];
        OutBuffer[len] = 0;
        if (pCompositionLen) *pCompositionLen = g_IMEContext.CompositionLen;
        if (pCaretPos) *pCaretPos = g_IMEContext.CaretPos;
    }
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return STATUS_SUCCESS;
}

/* Get the candidate list. */
ULONG NTAPI ImeGetCandidates(ULONG MaxCount, PCHAR *pCandidates,
                             PULONG pSelected)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    for (i = 0; i < g_IMEContext.CandidateCount && n < MaxCount; i++) {
        ULONG j = 0;
        while (g_IMEContext.Candidates[i][j] && j < MAX_COMPOSITION_LEN - 1) {
            ((WCHAR *)pCandidates[n])[j] = g_IMEContext.Candidates[i][j];
            j++;
        }
        ((WCHAR *)pCandidates[n])[j] = 0;
        n++;
    }
    if (pSelected) *pSelected = g_IMEContext.SelectedCandidate;
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return n;
}

/* Select next/prev candidate. */
NTSTATUS NTAPI ImeSelectCandidate(LONG Delta)
{
    KIRQL irql;
    LONG newSel;
    if (g_IMEContext.CandidateCount == 0) return STATUS_NO_MORE_ENTRIES;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    newSel = (LONG)g_IMEContext.SelectedCandidate + Delta;
    if (newSel < 0) newSel = (LONG)g_IMEContext.CandidateCount - 1;
    if (newSel >= (LONG)g_IMEContext.CandidateCount) newSel = 0;
    g_IMEContext.SelectedCandidate = (ULONG)newSel;
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return STATUS_SUCCESS;
}

/* Enumerate registered IMEs. */
ULONG NTAPI ImeEnum(ULONG MaxCount, PCHAR *pNames, PULONG pLanguages)
{
    ULONG i, n = 0;
    KIRQL irql;
    KeAcquireSpinLock(&g_ImeLock, &irql);
    for (i = 0; i < MAX_IMES && n < MaxCount; i++) {
        if (g_IMEs[i].InUse) {
            ULONG j = 0;
            while (g_IMEs[i].Name[j] && j < 31) pNames[n][j] = (CHAR)g_IMEs[i].Name[j], j++;
            pNames[n][j] = 0;
            if (pLanguages) pLanguages[n] = (ULONG)g_IMEs[i].Language;
            n++;
        }
    }
    KeReleaseSpinLock(&g_ImeLock, &irql);
    return n;
}

/* Get current IME state. */
ULONG NTAPI ImeGetState(VOID)
{
    return (ULONG)g_IMEContext.State;
}
