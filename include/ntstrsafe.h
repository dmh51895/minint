/*
 * MinNT - ntstrsafe.h stub
 * Safe string functions - provides basic implementations
 */

#ifndef _NTSTRSAFE_H_
#define _NTSTRSAFE_H_

#include <nt/ntdef.h>

/* Safe string length */
NTSTATUS NTAPI RtlStringCbLengthW(PCWSTR psz, SIZE_T cbMax, SIZE_T *pcbLength);
NTSTATUS NTAPI RtlStringCbLengthA(PCSTR psz, SIZE_T cbMax, SIZE_T *pcbLength);

/* Safe string copy */
NTSTATUS NTAPI RtlStringCbCopyW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszSrc);
NTSTATUS NTAPI RtlStringCbCopyA(PSTR pszDest, SIZE_T cbDest, PCSTR pszSrc);

/* Safe string copy with count */
NTSTATUS NTAPI RtlStringCchCopyW(PWSTR pszDest, size_t cchDest, PCWSTR pszSrc);
NTSTATUS NTAPI RtlStringCchCopyA(PSTR pszDest, size_t cchDest, PCSTR pszSrc);

/* Safe string cat */
NTSTATUS NTAPI RtlStringCbCatW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszSrc);
NTSTATUS NTAPI RtlStringCbCatA(PSTR pszDest, SIZE_T cbDest, PCSTR pszSrc);

/* Safe string cat with count */
NTSTATUS NTAPI RtlStringCchCatW(PWSTR pszDest, size_t cchDest, PCWSTR pszSrc);
NTSTATUS NTAPI RtlStringCchCatA(PSTR pszDest, size_t cchDest, PCSTR pszSrc);

/* Safe string printf */
NTSTATUS RtlStringCbPrintfW(PWSTR pszDest, SIZE_T cbDest, PCWSTR pszFormat, ...);
NTSTATUS RtlStringCbPrintfA(PSTR pszDest, SIZE_T cbDest, PCSTR pszFormat, ...);

/* Safe string printf with count */
NTSTATUS RtlStringCchPrintfW(PWSTR pszDest, size_t cchDest, PCWSTR pszFormat, ...);
NTSTATUS RtlStringCchPrintfA(PSTR pszDest, size_t cchDest, PCSTR pszFormat, ...);

/* Unsafe macros for compatibility */
#define RtlStringCbCatA(dest, destSize, src) RtlStringCbCatA(dest, destSize, src)
#define RtlStringCbCatW(dest, destSize, src) RtlStringCbCatW(dest, destSize, src)
#define RtlStringCchCatA(dest, destSize, src) RtlStringCchCatA(dest, destSize, src)
#define RtlStringCchCatW(dest, destSize, src) RtlStringCchCatW(dest, destSize, src)

#endif /* _NTSTRSAFE_H_ */
