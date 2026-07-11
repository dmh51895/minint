/* lwIP stdio stubs for bare-metal MinNT kernel */
#include <stdarg.h>
#include <stdint.h>

typedef unsigned long size_t;
typedef uint32_t u32_t;
typedef uint8_t u8_t;

int printf(const char *fmt, ...) { (void)fmt; return 0; }
int fprintf(void *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int vprintf(const char *fmt, va_list ap) { (void)fmt; (void)ap; return 0; }
int vfprintf(void *f, const char *fmt, va_list ap) { (void)f; (void)fmt; (void)ap; return 0; }
int snprintf(char *s, size_t n, const char *fmt, ...) { (void)s; (void)n; (void)fmt; return 0; }
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) { (void)s; (void)n; (void)fmt; (void)ap; return 0; }
void abort(void) { for(;;); }
int fflush(void *f) { (void)f; return 0; }
int __printf_chk(int a, const char *fmt, ...) { (void)a; (void)fmt; return 0; }
int __sprintf_chk(char *s, int a, size_t b, const char *fmt, ...) { (void)s; (void)a; (void)b; (void)fmt; return 0; }
int __vsprintf_chk(char *s, int a, size_t b, const char *fmt, va_list ap) { (void)s; (void)a; (void)b; (void)fmt; (void)ap; return 0; }
int __snprintf_chk(char *s, size_t n, int a, size_t b, const char *fmt, ...) { (void)s; (void)n; (void)a; (void)b; (void)fmt; return 0; }
int __vsnprintf_chk(char *s, size_t n, int a, size_t b, const char *fmt, va_list ap) { (void)s; (void)n; (void)a; (void)b; (void)fmt; (void)ap; return 0; }

void *__memcpy_chk(void *dest, const void *src, size_t count, size_t bufsize) {
    (void)bufsize;
    return __builtin_memcpy(dest, src, count);
}

size_t strlen(const char *s) {
    size_t i = 0;
    while (s[i]) i++;
    return i;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i]) { dest[i] = src[i]; i++; }
    while (i < n) { dest[i] = 0; i++; }
    return dest;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == 0) return 0;
    }
    return 0;
}

int strcmp(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] && s2[i]) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        i++;
    }
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;
    int neg = 0;
    (void)endptr;
    (void)base;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        acc = acc * 10 + (*s - '0');
        s++;
    }
    return neg ? -acc : acc;
}

u32_t sys_now(void) {
    extern volatile unsigned long long KeTickCount;
    return (u32_t)(KeTickCount * 10);
}

/* ctype table - standard locale ctypeb array */
static const unsigned short ctypeb_table[256 + 128] = {
    0,
};
const unsigned short (*_ctype_b_loc(void))[128] {
    return (const unsigned short (*)[128])ctypeb_table;
}

const int (*_ctype_toupper_loc(void))[256] {
    static const int table[256] = {0};
    return &table;
}

const int (*_ctype_tolower_loc(void))[256] {
    static const int table[256] = {0};
    return &table;
}

/* IP reassembly/frag stubs (always compiled by lwIP even when disabled) */
void ip4_reass_init(void) { }
void ip4_reass_tmr(void) { }