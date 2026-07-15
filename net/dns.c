/*
 * MinNT - net/dns.c
 * DNS resolver + cache.
 *
 * Implements a simple stub resolver with a TTL-bounded cache. Real
 * DNS query/response parsing is stubbed (we synthesize a fake
 * response so apps that link against gethostbyname still work), but
 * the cache mechanics are real.
 *
 * The cache is a fixed-size hash table keyed by the question name
 * (case-insensitive). Each entry tracks its TTL; expired entries are
 * evicted lazily on lookup.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define DNS_MAX_ENTRIES  128
#define DNS_MAX_NAME     128
#define DNS_CACHE_BUCKETS 32

typedef struct _DNS_ENTRY {
    CHAR Name[DNS_MAX_NAME];
    ULONG Ttl;
    LARGE_INTEGER InsertTime;
    ULONG Ip;
    BOOLEAN InUse;
} DNS_ENTRY;

static DNS_ENTRY g_Cache[DNS_MAX_ENTRIES];
static BOOLEAN g_DnsRunning;

static ULONG DnsHash(const CHAR *Name)
{
    ULONG h = 5381;
    for (ULONG i = 0; Name[i]; i++) {
        CHAR c = Name[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        h = h * 33 + (ULONG)(UCHAR)c;
    }
    return h % DNS_CACHE_BUCKETS;
}

static BOOLEAN DnsNameMatch(const CHAR *a, const CHAR *b)
{
    while (*a && *b) {
        CHAR ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return FALSE;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

NTSTATUS NTAPI DnsInit(VOID)
{
    RtlZeroMemory(g_Cache, sizeof(g_Cache));
    g_DnsRunning = TRUE;
    /* Seed a few common entries. */
    DnsRegister("localhost", 0x7F000001, 3600);
    DnsRegister("minnt.local", 0xC0A80164, 3600);
    DbgPrint("DNS: resolver initialized (cache size=%u)\n", DNS_MAX_ENTRIES);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DnsShutdown(VOID)
{
    g_DnsRunning = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI DnsRegister(const CHAR *Name, ULONG Ip, ULONG Ttl)
{
    if (!Name || g_DnsRunning == FALSE) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < DNS_MAX_ENTRIES; i++) {
        if (!g_Cache[i].InUse) {
            RtlZeroMemory(&g_Cache[i], sizeof(DNS_ENTRY));
            g_Cache[i].InUse = TRUE;
            for (ULONG k = 0; k < DNS_MAX_NAME - 1 && Name[k]; k++) g_Cache[i].Name[k] = Name[k];
            g_Cache[i].Ip = Ip;
            g_Cache[i].Ttl = Ttl;
            KeQuerySystemTime(&g_Cache[i].InsertTime);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI DnsRemove(const CHAR *Name)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < DNS_MAX_ENTRIES; i++) {
        if (g_Cache[i].InUse && DnsNameMatch(g_Cache[i].Name, Name)) {
            RtlZeroMemory(&g_Cache[i], sizeof(DNS_ENTRY));
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

/* Lookup an entry by name. Evicts expired entries. */
NTSTATUS NTAPI DnsResolve(const CHAR *Name, PULONG OutIp)
{
    if (!Name || !OutIp) return STATUS_INVALID_PARAMETER;
    /* Synthesize local answers without going to the network. */
    if (DnsNameMatch(Name, "localhost")) { *OutIp = 0x7F000001; return STATUS_SUCCESS; }
    /* Walk the cache. */
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    for (ULONG i = 0; i < DNS_MAX_ENTRIES; i++) {
        if (!g_Cache[i].InUse) continue;
        if (!DnsNameMatch(g_Cache[i].Name, Name)) continue;
        /* Check TTL (in seconds). */
        ULONG ageSec = (ULONG)((now.QuadPart - g_Cache[i].InsertTime.QuadPart) / 10000000);
        if (ageSec >= g_Cache[i].Ttl) {
            RtlZeroMemory(&g_Cache[i], sizeof(DNS_ENTRY));
            continue;
        }
        *OutIp = g_Cache[i].Ip;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

ULONG NTAPI DnsCacheSize(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < DNS_MAX_ENTRIES; i++) if (g_Cache[i].InUse) n++;
    return n;
}

NTSTATUS NTAPI DnsFlushCache(VOID)
{
    RtlZeroMemory(g_Cache, sizeof(g_Cache));
    return STATUS_SUCCESS;
}
