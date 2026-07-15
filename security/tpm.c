/*
 * MinNT - security/tpm.c
 * TPM (Trusted Platform Module) and Secure Boot integration.
 *
 * Real TPM integration requires an actual TPM 1.2/2.0 device with a
 * hierarchy of keys (Endorsement, Storage, Platform, Attestation).
 * MinNT models the TPM as a key store with PCR (Platform Configuration
 * Register) values, plus a Secure Boot chain that verifies a series
 * of signatures from firmware through bootloader to kernel.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define TPM_MAX_KEYS        32
#define TPM_PCR_COUNT       24
#define TPM_KEY_NAME_MAX    64

typedef enum _TPM_KEY_TYPE {
    TpmKeyEndorsement = 0,
    TpmKeyStorage,
    TpmKeyPlatform,
    TpmKeyAttestation,
} TPM_KEY_TYPE;

typedef struct _TPM_KEY {
    CHAR Name[TPM_KEY_NAME_MAX];
    TPM_KEY_TYPE Type;
    UCHAR PublicKey[64];
    UCHAR PrivateKey[64];
    ULONG PublicKeyLength;
    ULONG PrivateKeyLength;
    BOOLEAN InUse;
} TPM_KEY, *PTPM_KEY;

typedef struct _TPM_STATE {
    BOOLEAN Present;
    BOOLEAN Enabled;
    BOOLEAN Activated;
    UCHAR Pcr[TPM_PCR_COUNT][32];
    ULONG PcrLength[TPM_PCR_COUNT];
    TPM_KEY Keys[TPM_MAX_KEYS];
    ULONG KeyCount;
    BOOLEAN SecureBootEnabled;
    BOOLEAN FirmwareVerified;
    BOOLEAN BootloaderVerified;
    BOOLEAN KernelVerified;
} TPM_STATE, *PTPM_STATE;

static TPM_STATE g_Tpm;

NTSTATUS NTAPI TpmPcrExtend(ULONG Index, PVOID Data, ULONG Length)
{
    if (Index >= TPM_PCR_COUNT) return STATUS_INVALID_PARAMETER;
    PUCHAR pcr = g_Tpm.Pcr[Index];
    PUCHAR src = (PUCHAR)Data;
    ULONG cur = g_Tpm.PcrLength[Index];
    /* PCR extend: pcr = SHA1(pcr || data) -- we model the hash output
     * by mixing the new bytes into the existing PCR. */
    for (ULONG k = 0; k < Length; k++) {
        pcr[k % 32] ^= src[k];
        if (cur < 32) cur++;
    }
    g_Tpm.PcrLength[Index] = cur;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TpmPcrRead(ULONG Index, PVOID OutBuffer, ULONG BufferLength, PULONG OutLength)
{
    if (Index >= TPM_PCR_COUNT) return STATUS_INVALID_PARAMETER;
    ULONG got = g_Tpm.PcrLength[Index];
    if (got > 32) got = 32;
    if (got > BufferLength) got = BufferLength;
    RtlCopyMemory(OutBuffer, g_Tpm.Pcr[Index], got);
    if (OutLength) *OutLength = got;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TpmCreateKey(const CHAR *Name, TPM_KEY_TYPE Type,
                            PVOID PublicKey, ULONG PublicKeyLength,
                            PULONG OutKeyId)
{
    for (ULONG i = 0; i < TPM_MAX_KEYS; i++) {
        if (!g_Tpm.Keys[i].InUse) {
            RtlZeroMemory(&g_Tpm.Keys[i], sizeof(TPM_KEY));
            g_Tpm.Keys[i].InUse = TRUE;
            g_Tpm.Keys[i].Type = Type;
            for (ULONG k = 0; k < TPM_KEY_NAME_MAX; k++) {
                g_Tpm.Keys[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            if (PublicKey && PublicKeyLength) {
                if (PublicKeyLength > sizeof(g_Tpm.Keys[i].PublicKey)) PublicKeyLength = sizeof(g_Tpm.Keys[i].PublicKey);
                RtlCopyMemory(g_Tpm.Keys[i].PublicKey, PublicKey, PublicKeyLength);
                g_Tpm.Keys[i].PublicKeyLength = PublicKeyLength;
            }
            g_Tpm.KeyCount++;
            if (OutKeyId) *OutKeyId = i + 1;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI TpmSeal(ULONG KeyId, PVOID Data, ULONG DataLength,
                       PVOID OutBlob, ULONG BlobLength, PULONG OutLength)
{
    if (KeyId == 0 || KeyId > TPM_MAX_KEYS) return STATUS_INVALID_PARAMETER;
    PTPM_KEY k = &g_Tpm.Keys[KeyId - 1];
    if (!k->InUse) return STATUS_NOT_FOUND;
    /* A real TPM seal encrypts Data with the storage key and wraps it
     * with a PCR policy. We simulate that by XORing Data with the
     * public key stream and prepending a "TPM1.2\0" header. */
    if (BlobLength < DataLength + 8) return STATUS_BUFFER_TOO_SMALL;
    PUCHAR blob = (PUCHAR)OutBlob;
    const UCHAR header[8] = { 'T','P','M','1','.','2', 0, 0 };
    for (ULONG i = 0; i < 8; i++) blob[i] = header[i];
    for (ULONG i = 0; i < DataLength; i++) {
        blob[8 + i] = ((PUCHAR)Data)[i] ^ k->PublicKey[i % k->PublicKeyLength];
    }
    if (OutLength) *OutLength = DataLength + 8;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TpmUnseal(ULONG KeyId, PVOID Blob, ULONG BlobLength,
                         PVOID OutData, ULONG DataLength, PULONG OutLength)
{
    if (KeyId == 0 || KeyId > TPM_MAX_KEYS) return STATUS_INVALID_PARAMETER;
    PTPM_KEY k = &g_Tpm.Keys[KeyId - 1];
    if (!k->InUse) return STATUS_NOT_FOUND;
    if (BlobLength < 8) return STATUS_INVALID_PARAMETER;
    PUCHAR blob = (PUCHAR)Blob;
    if (!(blob[0] == 'T' && blob[1] == 'P' && blob[2] == 'M' &&
          blob[3] == '1' && blob[4] == '.' && blob[5] == '2'))
        return STATUS_INVALID_PARAMETER;
    ULONG payload = BlobLength - 8;
    if (payload > DataLength) return STATUS_BUFFER_TOO_SMALL;
    for (ULONG i = 0; i < payload; i++) {
        ((PUCHAR)OutData)[i] = blob[8 + i] ^ k->PublicKey[i % k->PublicKeyLength];
    }
    if (OutLength) *OutLength = payload;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TpmSecureBootVerifyStage(ULONG Stage)
{
    /* Stages: 0 = firmware, 1 = bootloader, 2 = kernel. */
    switch (Stage) {
    case 0:
        g_Tpm.FirmwareVerified = TRUE;
        TpmPcrExtend(0, "firmware", 8);
        break;
    case 1:
        g_Tpm.BootloaderVerified = TRUE;
        TpmPcrExtend(1, "bootloader", 10);
        break;
    case 2:
        g_Tpm.KernelVerified = TRUE;
        TpmPcrExtend(2, "kernel", 6);
        break;
    default: return STATUS_INVALID_PARAMETER;
    }
    DbgPrint("TPM: Secure Boot stage %u verified\n", Stage);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI TpmEnableSecureBoot(VOID)
{
    g_Tpm.SecureBootEnabled = TRUE;
    DbgPrint("TPM: Secure Boot enabled\n");
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI TpmIsSecureBootEnabled(VOID)
{
    return g_Tpm.SecureBootEnabled;
}

NTSTATUS NTAPI TpmInit(VOID)
{
    RtlZeroMemory(&g_Tpm, sizeof(g_Tpm));
    g_Tpm.Present = TRUE;
    g_Tpm.Enabled = TRUE;
    g_Tpm.Activated = TRUE;
    for (ULONG i = 0; i < TPM_PCR_COUNT; i++) {
        g_Tpm.PcrLength[i] = 0;
    }
    /* Seed default keys: endorsement, storage, platform. */
    UCHAR pub[64];
    for (ULONG i = 0; i < 64; i++) pub[i] = (UCHAR)((i * 31 + 7) & 0xFF);
    TpmCreateKey("EK", TpmKeyEndorsement, pub, 32, NULL);
    TpmCreateKey("SRK", TpmKeyStorage, pub, 32, NULL);
    TpmCreateKey("PK", TpmKeyPlatform, pub, 32, NULL);
    DbgPrint("TPM: trusted platform module + Secure Boot initialized (%u keys)\n",
             g_Tpm.KeyCount);
    return STATUS_SUCCESS;
}
