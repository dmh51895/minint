/*
 * MinNT - input/tsf.c
 * Text Services Framework / system-wide spellcheck.
 *
 * TSF is the modern Windows text-input architecture that any app can
 * hook into for IME composition, autocomplete, spellchecking, etc.
 * MinNT implements the framework-level hooks: a spellcheck pipeline
 * with a basic dictionary lookup, autocorrect, predictive-text
 * suggestions, and a way for callers to register a composition
 * session.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define TSF_MAX_SESSIONS      32
#define TSF_DICT_MAX          256
#define TSF_WORD_MAX          32
#define TSF_SUGGEST_MAX       8

typedef struct _TSF_SUGGEST {
    CHAR Word[TSF_WORD_MAX];
    ULONG Score;
} TSF_SUGGEST;

typedef struct _TSF_SESSION {
    ULONG Id;
    CHAR Composition[TSF_WORD_MAX];
    CHAR Candidate[TSF_WORD_MAX];
    ULONG CompositionLength;
    BOOLEAN InUse;
    TSF_SUGGEST Suggestions[TSF_SUGGEST_MAX];
    ULONG SuggestCount;
} TSF_SESSION, *PTSF_SESSION;

typedef struct _TSF_DICT_ENTRY {
    CHAR Word[TSF_WORD_MAX];
    BOOLEAN InUse;
} TSF_DICT_ENTRY;

static TSF_SESSION g_Sessions[TSF_MAX_SESSIONS];
static TSF_DICT_ENTRY g_Dict[TSF_DICT_MAX];
static CHAR g_AutocorrectFrom[TSF_WORD_MAX];
static CHAR g_AutocorrectTo[TSF_WORD_MAX];

static BOOLEAN DictHas(const CHAR *word)
{
    for (ULONG i = 0; i < TSF_DICT_MAX; i++) {
        if (!g_Dict[i].InUse) continue;
        BOOLEAN eq = TRUE;
        for (ULONG k = 0; k < TSF_WORD_MAX; k++) {
            if (g_Dict[i].Word[k] != word[k]) { eq = FALSE; break; }
            if (word[k] == 0) break;
        }
        if (eq) return TRUE;
    }
    return FALSE;
}

NTSTATUS NTAPI TsfDictAdd(const CHAR *word)
{
    if (DictHas(word)) return STATUS_SUCCESS;
    for (ULONG i = 0; i < TSF_DICT_MAX; i++) {
        if (!g_Dict[i].InUse) {
            RtlZeroMemory(&g_Dict[i].Word, TSF_WORD_MAX);
            for (ULONG k = 0; k < TSF_WORD_MAX - 1 && word[k]; k++) {
                g_Dict[i].Word[k] = word[k];
            }
            g_Dict[i].InUse = TRUE;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI TsfSetAutocorrect(const CHAR *From, const CHAR *To)
{
    RtlZeroMemory(g_AutocorrectFrom, TSF_WORD_MAX);
    RtlZeroMemory(g_AutocorrectTo, TSF_WORD_MAX);
    for (ULONG k = 0; k < TSF_WORD_MAX - 1 && From[k]; k++) g_AutocorrectFrom[k] = From[k];
    for (ULONG k = 0; k < TSF_WORD_MAX - 1 && To[k]; k++) g_AutocorrectTo[k] = To[k];
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TsfCreateSession(PULONG OutSessionId)
{
    for (ULONG i = 0; i < TSF_MAX_SESSIONS; i++) {
        if (!g_Sessions[i].InUse) {
            RtlZeroMemory(&g_Sessions[i], sizeof(TSF_SESSION));
            g_Sessions[i].InUse = TRUE;
            g_Sessions[i].Id = i + 1;
            if (OutSessionId) *OutSessionId = g_Sessions[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI TsfDestroySession(ULONG SessionId)
{
    if (SessionId == 0 || SessionId > TSF_MAX_SESSIONS) return STATUS_INVALID_PARAMETER;
    if (!g_Sessions[SessionId - 1].InUse) return STATUS_NOT_FOUND;
    RtlZeroMemory(&g_Sessions[SessionId - 1], sizeof(TSF_SESSION));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TsfSetComposition(ULONG SessionId, const CHAR *Text)
{
    if (SessionId == 0 || SessionId > TSF_MAX_SESSIONS) return STATUS_INVALID_PARAMETER;
    PTSF_SESSION s = &g_Sessions[SessionId - 1];
    if (!s->InUse) return STATUS_NOT_FOUND;
    RtlZeroMemory(s->Composition, TSF_WORD_MAX);
    for (ULONG k = 0; k < TSF_WORD_MAX - 1 && Text[k]; k++) s->Composition[k] = Text[k];
    s->CompositionLength = 0;
    while (s->Composition[s->CompositionLength]) s->CompositionLength++;
    return STATUS_SUCCESS;
}

/* Apply autocorrect: if the last completed word matches g_AutocorrectFrom,
 * replace it with g_AutocorrectTo. */
NTSTATUS NTAPI TsfApplyAutocorrect(PCHAR Text, ULONG MaxLen)
{
    if (!Text || !g_AutocorrectFrom[0]) return STATUS_SUCCESS;
    ULONG len = 0;
    while (Text[len]) len++;
    if (len == 0) return STATUS_SUCCESS;
    /* Find the last word. */
    LONG wordEnd = (LONG)len - 1;
    while (wordEnd >= 0 && Text[wordEnd] == ' ') wordEnd--;
    LONG wordStart = wordEnd;
    while (wordStart >= 0 && Text[wordStart] != ' ') wordStart--;
    wordStart++;
    LONG wlen = wordEnd - wordStart + 1;
    if (wlen <= 0) return STATUS_SUCCESS;
    /* Compare. */
    BOOLEAN match = TRUE;
    for (LONG k = 0; g_AutocorrectFrom[k] && k < wlen; k++) {
        if (g_AutocorrectFrom[k] != Text[wordStart + k]) { match = FALSE; break; }
    }
    if (!match || g_AutocorrectFrom[wlen] != 0) return STATUS_SUCCESS;
    /* Replace. */
    ULONG repLen = 0;
    while (g_AutocorrectTo[repLen]) repLen++;
    if (repLen > (ULONG)wlen) {
        if (len + (repLen - (ULONG)wlen) >= MaxLen) return STATUS_BUFFER_TOO_SMALL;
        for (LONG k = (LONG)len - 1; k > wordEnd; k--) Text[k + (LONG)repLen - wlen] = Text[k];
    } else if (repLen < (ULONG)wlen) {
        for (LONG k = wordEnd + 1; k < (LONG)len; k++) Text[k + (LONG)repLen - wlen] = Text[k];
    }
    for (ULONG k = 0; k < repLen; k++) Text[wordStart + k] = g_AutocorrectTo[k];
    Text[len + repLen - wlen] = 0;
    return STATUS_SUCCESS;
}

/* Generate suggestions: any dictionary word starting with the prefix. */
NTSTATUS NTAPI TsfSuggest(ULONG SessionId, PCHAR OutBuffer, ULONG MaxLen, PULONG OutCount)
{
    if (SessionId == 0 || SessionId > TSF_MAX_SESSIONS || !OutCount) return STATUS_INVALID_PARAMETER;
    PTSF_SESSION s = &g_Sessions[SessionId - 1];
    if (!s->InUse) return STATUS_NOT_FOUND;
    ULONG outCount = 0;
    ULONG prefixLen = s->CompositionLength;
    for (ULONG i = 0; i < TSF_DICT_MAX && outCount < TSF_SUGGEST_MAX; i++) {
        if (!g_Dict[i].InUse) continue;
        BOOLEAN match = TRUE;
        for (ULONG k = 0; k < prefixLen && g_Dict[i].Word[k]; k++) {
            if (g_Dict[i].Word[k] != s->Composition[k]) { match = FALSE; break; }
        }
        if (match && g_Dict[i].Word[prefixLen] != 0) {
            if (outCount < TSF_SUGGEST_MAX) {
                for (ULONG k = 0; k < TSF_WORD_MAX && g_Dict[i].Word[k]; k++) {
                    s->Suggestions[outCount].Word[k] = g_Dict[i].Word[k];
                }
                s->Suggestions[outCount].Score = 100;
                outCount++;
            }
        }
    }
    s->SuggestCount = outCount;
    /* Serialize suggestions into a space-separated buffer. */
    if (OutBuffer) {
        ULONG pos = 0;
        for (ULONG i = 0; i < outCount; i++) {
            ULONG wlen = 0;
            while (s->Suggestions[i].Word[wlen]) wlen++;
            if (pos + wlen + 1 >= MaxLen) break;
            for (ULONG k = 0; k < wlen; k++) OutBuffer[pos++] = s->Suggestions[i].Word[k];
            if (i + 1 < outCount && pos + 1 < MaxLen) OutBuffer[pos++] = ' ';
        }
        if (pos < MaxLen) OutBuffer[pos] = 0;
    }
    *OutCount = outCount;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TsfSpellCheck(const CHAR *Word, PBOOLEAN OutCorrect)
{
    if (!Word || !OutCorrect) return STATUS_INVALID_PARAMETER;
    *OutCorrect = DictHas(Word);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TsfInit(VOID)
{
    RtlZeroMemory(g_Sessions, sizeof(g_Sessions));
    RtlZeroMemory(g_Dict, sizeof(g_Dict));
    RtlZeroMemory(g_AutocorrectFrom, TSF_WORD_MAX);
    RtlZeroMemory(g_AutocorrectTo, TSF_WORD_MAX);
    /* Seed a small dictionary. */
    const CHAR *seed[] = {
        "the", "be", "to", "of", "and", "a", "in", "that", "have", "I",
        "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
        "this", "but", "his", "by", "from", "they", "we", "say", "her",
        "she", "or", "an", "will", "my", "one", "all", "would", "there",
        "their", "what", "hello", "world", "minnt", "computer", "system",
        "kernel", "module", "memory", "process", "thread", "driver",
        "device", "program", "executable", "library", "header",
        NULL
    };
    for (ULONG i = 0; seed[i]; i++) TsfDictAdd(seed[i]);
    /* Default autocorrect: teh -> the */
    TsfSetAutocorrect("teh", "the");
    TsfSetAutocorrect("recieve", "receive");
    TsfSetAutocorrect("occured", "occurred");
    DbgPrint("TSF: text services framework + spellcheck initialized (%u dict entries)\n",
             TSF_DICT_MAX);
    return STATUS_SUCCESS;
}
