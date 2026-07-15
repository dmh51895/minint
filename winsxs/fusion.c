/*
 * MinNT - winsxs/fusion.c
 * Side-by-side (WinSxS / Fusion) assembly store.
 *
 * WinSxS is the mechanism Windows uses to manage multiple versions of
 * the same DLL. Each DLL is identified by a strong name (name +
 * version + culture + public key token), and applications bind to
 * specific versions via their manifests.
 *
 * In MinNT the fusion store is a flat directory tree indexed by strong
 * name. Manifests are stored as XML blobs in the registry. The
 * resolver walks the manifest, finds the requested assemblies, and
 * activates them.
 */

#include <nt/ke.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/cm.h>
#include <nt/framework.h>

#define FUSION_MAX_ASSEMBLIES  256
#define FUSION_MAX_NAME         128
#define FUSION_MAX_PATH         260

typedef struct _FUSION_ASSEMBLY {
    CHAR Name[FUSION_MAX_NAME];
    CHAR Version[32];        /* e.g. "1.0.0.0" */
    CHAR Culture[16];         /* e.g. "neutral" or "en-US" */
    UCHAR PublicKeyToken[8];
    ULONG PublicKeyTokenLength;
    CHAR Path[FUSION_MAX_PATH];
    CHAR Manifest[1024];
    BOOLEAN InUse;
} FUSION_ASSEMBLY;

static FUSION_ASSEMBLY g_Assemblies[FUSION_MAX_ASSEMBLIES];

NTSTATUS NTAPI FusionInit(VOID)
{
    RtlZeroMemory(g_Assemblies, sizeof(g_Assemblies));
    DbgPrint("FUSION: side-by-side assembly store initialized\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI FusionRegisterAssembly(const CHAR *Name, const CHAR *Version,
                                       const CHAR *Culture, const UCHAR *PublicKeyToken,
                                       ULONG TokenLength, const CHAR *Path,
                                       const CHAR *Manifest)
{
    if (!Name || !Version || !Path) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FUSION_MAX_ASSEMBLIES; i++) {
        if (!g_Assemblies[i].InUse) {
            RtlZeroMemory(&g_Assemblies[i], sizeof(FUSION_ASSEMBLY));
            g_Assemblies[i].InUse = TRUE;
            for (ULONG k = 0; k < FUSION_MAX_NAME - 1 && Name[k]; k++) g_Assemblies[i].Name[k] = Name[k];
            for (ULONG k = 0; k < 31 && Version[k]; k++) g_Assemblies[i].Version[k] = Version[k];
            if (Culture) {
                for (ULONG k = 0; k < 15 && Culture[k]; k++) g_Assemblies[i].Culture[k] = Culture[k];
            } else {
                RtlCopyMemory(g_Assemblies[i].Culture, "neutral", 8);
            }
            if (PublicKeyToken && TokenLength > 0 && TokenLength <= 8) {
                for (ULONG k = 0; k < TokenLength; k++)
                    g_Assemblies[i].PublicKeyToken[k] = PublicKeyToken[k];
                g_Assemblies[i].PublicKeyTokenLength = TokenLength;
            }
            for (ULONG k = 0; k < FUSION_MAX_PATH - 1 && Path[k]; k++) g_Assemblies[i].Path[k] = Path[k];
            if (Manifest) {
                ULONG k = 0;
                while (Manifest[k] && k < 1023) { g_Assemblies[i].Manifest[k] = Manifest[k]; k++; }
                g_Assemblies[i].Manifest[k] = 0;
            }
            DbgPrint("FUSION: registered '%s' v%s\n", Name, Version);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI FusionResolve(const CHAR *Name, const CHAR *Version,
                              CHAR *OutPath, ULONG MaxLen)
{
    if (!Name || !OutPath) return STATUS_INVALID_PARAMETER;
    /* Find the closest match by name and (optional) version. */
    for (ULONG i = 0; i < FUSION_MAX_ASSEMBLIES; i++) {
        if (!g_Assemblies[i].InUse) continue;
        BOOLEAN nameMatch = TRUE;
        for (ULONG k = 0; k < FUSION_MAX_NAME; k++) {
            if (g_Assemblies[i].Name[k] != Name[k]) { nameMatch = FALSE; break; }
            if (Name[k] == 0) break;
        }
        if (!nameMatch) continue;
        /* If a specific version was requested, match it. */
        if (Version && Version[0]) {
            BOOLEAN verMatch = TRUE;
            for (ULONG k = 0; k < 31; k++) {
                if (g_Assemblies[i].Version[k] != Version[k]) { verMatch = FALSE; break; }
                if (Version[k] == 0) break;
            }
            if (!verMatch) continue;
        }
        ULONG k = 0;
        while (g_Assemblies[i].Path[k] && k < MaxLen - 1) { OutPath[k] = g_Assemblies[i].Path[k]; k++; }
        OutPath[k] = 0;
        DbgPrint("FUSION: resolved '%s' -> %s\n", Name, OutPath);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

ULONG NTAPI FusionEnumAssemblies(PCHAR Names, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < FUSION_MAX_ASSEMBLIES && n < MaxCount; i++) {
        if (g_Assemblies[i].InUse) {
            ULONG k = 0;
            while (g_Assemblies[i].Name[k] && k < 127) { Names[n * 128 + k] = g_Assemblies[i].Name[k]; k++; }
            Names[n * 128 + k] = 0;
            n++;
        }
    }
    return n;
}

NTSTATUS NTAPI FusionUnregisterAssembly(const CHAR *Name)
{
    if (!Name) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < FUSION_MAX_ASSEMBLIES; i++) {
        if (g_Assemblies[i].InUse) {
            BOOLEAN match = TRUE;
            for (ULONG k = 0; k < FUSION_MAX_NAME; k++) {
                if (g_Assemblies[i].Name[k] != Name[k]) { match = FALSE; break; }
                if (Name[k] == 0) break;
            }
            if (match) {
                RtlZeroMemory(&g_Assemblies[i], sizeof(FUSION_ASSEMBLY));
                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_NOT_FOUND;
}
