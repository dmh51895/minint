/*
 * MinNT - rtl/rtl.c
 */

#include <nt/rtl.h>

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dd = d;
    while (n--) *dd++ = (unsigned char)c;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) {
        while (n--) *dd++ = *ss++;
    } else {
        dd += n; ss += n;
        while (n--) *--dd = *--ss;
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

PVOID RtlCopyMemory(PVOID Dst, const VOID *Src, SIZE_T Length)
{
    return memcpy(Dst, Src, Length);
}

PVOID RtlZeroMemory(PVOID Dst, SIZE_T Length)
{
    return memset(Dst, 0, Length);
}

PVOID RtlFillMemory(PVOID Dst, SIZE_T Length, UCHAR Fill)
{
    return memset(Dst, Fill, Length);
}

/*
 * RtlCompareMemory - compares two blocks of memory and returns the number
 * of bytes that are equal. Per the NT contract this routine never faults
 * on invalid memory (it touches each byte through the supplied pointers
 * only within the declared length, and the caller is expected to have
 * already probed the addresses).
 */
SIZE_T NTAPI RtlCompareMemory(const VOID *Source1, const VOID *Source2,
                              SIZE_T Length)
{
    SIZE_T i = 0;
    const UCHAR *s1 = (const UCHAR *)Source1;
    const UCHAR *s2 = (const UCHAR *)Source2;
    while (i < Length) {
        if (s1[i] != s2[i]) return i;
        i++;
    }
    return Length;
}

SIZE_T RtlStringLength(const CHAR *S)
{
    SIZE_T n = 0;
    while (S[n]) n++;
    return n;
}

VOID NTAPI RtlInitUnicodeString(PUNICODE_STRING Dst, PCWSTR Src)
{
    SIZE_T n = 0;
    if (Src) while (Src[n]) n++;
    Dst->Buffer = (PWSTR)Src;
    Dst->Length = (USHORT)(n * sizeof(WCHAR));
    Dst->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
}

static WCHAR RtlpUpcase(WCHAR c)
{
    return (c >= L'a' && c <= L'z') ? (WCHAR)(c - 32) : c;
}

BOOLEAN NTAPI RtlEqualUnicodeString(PCUNICODE_STRING A, PCUNICODE_STRING B,
                                    BOOLEAN CaseInsensitive)
{
    USHORT i, n;
    if (A->Length != B->Length) return FALSE;
    n = A->Length / sizeof(WCHAR);
    for (i = 0; i < n; i++) {
        WCHAR x = A->Buffer[i], y = B->Buffer[i];
        if (CaseInsensitive) { x = RtlpUpcase(x); y = RtlpUpcase(y); }
        if (x != y) return FALSE;
    }
    return TRUE;
}

size_t wcslen(const wchar_t *s)
{
    size_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = NULL;
    if (s) {
        while (*s) {
            if (*s == c) last = s;
            s++;
        }
    }
    return (wchar_t *)last;
}
