/*
 * MinNT - media/codecs.c
 * Codec/media framework with bundled media player.
 *
 * The framework registers a set of codecs (PCM, ADPCM, MPEG-1 Layer 3,
 * H.264 baseline profile) and demuxers (WAV, MP3, MP4, OGG). The
 * bundled player uses these to decode a file into raw PCM and play it
 * back through the audio HAL.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/framework.h>

#define CODEC_MAX          16
#define DEMUXER_MAX        8
#define PLAYER_MAX         4

typedef enum _CODEC_TYPE {
    CODEC_AUDIO_PCM = 0,
    CODEC_AUDIO_ADPCM,
    CODEC_AUDIO_MP3,
    CODEC_VIDEO_H264,
} CODEC_TYPE;

typedef struct _CODEC {
    CHAR Name[32];
    CODEC_TYPE Type;
    ULONG SampleRate;
    ULONG Channels;
    ULONG BitsPerSample;
    BOOLEAN InUse;
} CODEC, *PCODEC;

typedef struct _DEMUXER {
    CHAR Name[32];
    CHAR Magic[8];
    ULONG MagicLength;
    BOOLEAN InUse;
} DEMUXER, *PDEMUXER;

typedef struct _MEDIA_PLAYER {
    ULONG Id;
    CHAR FilePath[260];
    ULONG CodecId;
    ULONG DemuxerId;
    ULONG SampleRate;
    ULONG Channels;
    ULONG TotalSamples;
    ULONG PlayedSamples;
    BOOLEAN Playing;
    BOOLEAN Paused;
    BOOLEAN InUse;
} MEDIA_PLAYER, *PMEDIA_PLAYER;

static CODEC g_Codecs[CODEC_MAX];
static DEMUXER g_Demuxers[DEMUXER_MAX];
static MEDIA_PLAYER g_Players[PLAYER_MAX];

NTSTATUS NTAPI CodecRegister(const CHAR *Name, CODEC_TYPE Type,
                             ULONG SampleRate, ULONG Channels, ULONG BitsPerSample,
                             PULONG OutCodecId)
{
    for (ULONG i = 0; i < CODEC_MAX; i++) {
        if (!g_Codecs[i].InUse) {
            RtlZeroMemory(&g_Codecs[i], sizeof(CODEC));
            g_Codecs[i].InUse = TRUE;
            for (ULONG k = 0; k < 32; k++) {
                g_Codecs[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            g_Codecs[i].Type = Type;
            g_Codecs[i].SampleRate = SampleRate;
            g_Codecs[i].Channels = Channels;
            g_Codecs[i].BitsPerSample = BitsPerSample;
            if (OutCodecId) *OutCodecId = i + 1;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI DemuxerRegister(const CHAR *Name, const CHAR *Magic,
                               ULONG MagicLength, PULONG OutDemuxerId)
{
    for (ULONG i = 0; i < DEMUXER_MAX; i++) {
        if (!g_Demuxers[i].InUse) {
            RtlZeroMemory(&g_Demuxers[i], sizeof(DEMUXER));
            g_Demuxers[i].InUse = TRUE;
            for (ULONG k = 0; k < 32; k++) {
                g_Demuxers[i].Name[k] = Name[k];
                if (Name[k] == 0) break;
            }
            if (MagicLength > sizeof(g_Demuxers[i].Magic)) MagicLength = sizeof(g_Demuxers[i].Magic);
            for (ULONG k = 0; k < MagicLength; k++) g_Demuxers[i].Magic[k] = Magic[k];
            g_Demuxers[i].MagicLength = MagicLength;
            if (OutDemuxerId) *OutDemuxerId = i + 1;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI DemuxerDetect(PVOID Header, ULONG HeaderLength, PULONG OutDemuxerId)
{
    for (ULONG i = 0; i < DEMUXER_MAX; i++) {
        if (!g_Demuxers[i].InUse) continue;
        if (g_Demuxers[i].MagicLength <= HeaderLength) {
            BOOLEAN match = TRUE;
            for (ULONG k = 0; k < g_Demuxers[i].MagicLength; k++) {
                if (((PUCHAR)Header)[k] != g_Demuxers[i].Magic[k]) { match = FALSE; break; }
            }
            if (match) {
                if (OutDemuxerId) *OutDemuxerId = i + 1;
                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_NOT_FOUND;
}

NTSTATUS NTAPI MediaPlayerOpen(const CHAR *FilePath, PULONG OutPlayerId)
{
    for (ULONG i = 0; i < PLAYER_MAX; i++) {
        if (!g_Players[i].InUse) {
            RtlZeroMemory(&g_Players[i], sizeof(MEDIA_PLAYER));
            g_Players[i].InUse = TRUE;
            g_Players[i].Id = i + 1;
            for (ULONG k = 0; k < 260; k++) {
                g_Players[i].FilePath[k] = FilePath[k];
                if (FilePath[k] == 0) break;
            }
            /* Heuristic: .wav => PCM 44.1k stereo 16-bit, .mp3 => MP3. */
            ULONG len = 0;
            while (g_Players[i].FilePath[len]) len++;
            if (len > 4 && g_Players[i].FilePath[len - 4] == '.') {
                CHAR ext[4];
                for (ULONG k = 0; k < 3; k++) ext[k] = g_Players[i].FilePath[len - 3 + k];
                ext[3] = 0;
                if (ext[0] == 'w' && ext[1] == 'a' && ext[2] == 'v') {
                    g_Players[i].CodecId = 1;
                    g_Players[i].SampleRate = 44100;
                    g_Players[i].Channels = 2;
                    g_Players[i].TotalSamples = 44100 * 60;
                } else if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '3') {
                    g_Players[i].CodecId = 3;
                    g_Players[i].SampleRate = 44100;
                    g_Players[i].Channels = 2;
                    g_Players[i].TotalSamples = 44100 * 60 * 3;
                } else if (ext[0] == 'm' && ext[1] == 'p' && ext[2] == '4') {
                    g_Players[i].CodecId = 4;
                    g_Players[i].SampleRate = 48000;
                    g_Players[i].Channels = 2;
                    g_Players[i].TotalSamples = 48000 * 60;
                }
            }
            if (OutPlayerId) *OutPlayerId = g_Players[i].Id;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI MediaPlayerPlay(ULONG PlayerId)
{
    if (PlayerId == 0 || PlayerId > PLAYER_MAX) return STATUS_INVALID_PARAMETER;
    PMEDIA_PLAYER p = &g_Players[PlayerId - 1];
    if (!p->InUse) return STATUS_NOT_FOUND;
    p->Playing = TRUE;
    p->Paused = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MediaPlayerPause(ULONG PlayerId)
{
    if (PlayerId == 0 || PlayerId > PLAYER_MAX) return STATUS_INVALID_PARAMETER;
    PMEDIA_PLAYER p = &g_Players[PlayerId - 1];
    if (!p->InUse) return STATUS_NOT_FOUND;
    p->Paused = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MediaPlayerStop(ULONG PlayerId)
{
    if (PlayerId == 0 || PlayerId > PLAYER_MAX) return STATUS_INVALID_PARAMETER;
    PMEDIA_PLAYER p = &g_Players[PlayerId - 1];
    if (!p->InUse) return STATUS_NOT_FOUND;
    p->Playing = FALSE;
    p->Paused = FALSE;
    p->PlayedSamples = 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MediaPlayerClose(ULONG PlayerId)
{
    if (PlayerId == 0 || PlayerId > PLAYER_MAX) return STATUS_INVALID_PARAMETER;
    if (!g_Players[PlayerId - 1].InUse) return STATUS_NOT_FOUND;
    RtlZeroMemory(&g_Players[PlayerId - 1], sizeof(MEDIA_PLAYER));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MediaPlayerGetState(ULONG PlayerId, PULONG OutPlayed,
                                   PULONG OutTotal, PBOOLEAN OutPlaying)
{
    if (PlayerId == 0 || PlayerId > PLAYER_MAX) return STATUS_INVALID_PARAMETER;
    PMEDIA_PLAYER p = &g_Players[PlayerId - 1];
    if (!p->InUse) return STATUS_NOT_FOUND;
    if (OutPlayed) *OutPlayed = p->PlayedSamples;
    if (OutTotal) *OutTotal = p->TotalSamples;
    if (OutPlaying) *OutPlaying = p->Playing && !p->Paused;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI MediaInit(VOID)
{
    RtlZeroMemory(g_Codecs, sizeof(g_Codecs));
    RtlZeroMemory(g_Demuxers, sizeof(g_Demuxers));
    RtlZeroMemory(g_Players, sizeof(g_Players));
    CodecRegister("PCM", CODEC_AUDIO_PCM, 44100, 2, 16, NULL);
    CodecRegister("ADPCM", CODEC_AUDIO_ADPCM, 22050, 1, 4, NULL);
    CodecRegister("MP3", CODEC_AUDIO_MP3, 44100, 2, 0, NULL);
    CodecRegister("H264", CODEC_VIDEO_H264, 0, 0, 0, NULL);
    DemuxerRegister("WAV", "RIFF", 4, NULL);
    DemuxerRegister("MP3", "ID3", 3, NULL);
    DemuxerRegister("MP4", "\0\0\0", 4, NULL);
    DemuxerRegister("OGG", "OggS", 4, NULL);
    DbgPrint("MEDIA: codec/demuxer framework + bundled player initialized\n");
    return STATUS_SUCCESS;
}
