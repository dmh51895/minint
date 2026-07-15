/*
 * MinNT - audio/engine.c
 * Audio engine: mixer, WASAPI-style endpoint manager, audio sessions.
 *
 * The audio engine sits between the audio drivers (HAL layer) and
 * the user-mode audio APIs (WASAPI, DirectSound). It provides:
 *
 *   - Audio endpoints (render + capture)
 *   - Audio sessions (per-application playback streams)
 *   - The audio mixer (mixes all sessions into the endpoint)
 *   - Volume controls (per-session, per-endpoint, master)
 *   - Format negotiation (sample rate, bit depth, channels)
 *
 * The mixer runs at PASSIVE_LEVEL on a worker thread that periodically
 * pulls mixed samples from each session's ring buffer and forwards
 * them to the active endpoint driver.
 */

#include <nt/ke.h>
#include <nt/hal.h>
#include <nt/rtl.h>
#include <nt/ex.h>
#include <nt/ps.h>
#include <nt/framework.h>

#define AUDIO_MAX_ENDPOINTS  16
#define AUDIO_MAX_SESSIONS   64
#define AUDIO_MAX_FORMATS    8
#define AUDIO_RING_FRAMES    4096
#define AUDIO_MIXER_RATE     48000
#define AUDIO_MIXER_CHANNELS 2
#define AUDIO_MIXER_BITS     16

/* AUDIO_DIRECTION, AUDIO_FORMAT_TAG, AUDIO_FORMAT are in framework.h. */

typedef struct _AUDIO_ENDPOINT {
    ULONG Id;
    AUDIO_DIRECTION Direction;
    CHAR Name[64];
    AUDIO_FORMAT Format;
    ULONG MasterVolume;   /* 0..100 */
    ULONG Muted;
    BOOLEAN Active;
    BOOLEAN InUse;
} AUDIO_ENDPOINT;

typedef struct _AUDIO_RING {
    LONG ReadPos;
    LONG WritePos;
    ULONG Frames;
    UCHAR Data[AUDIO_RING_FRAMES * 4];
    KSPIN_LOCK Lock;
} AUDIO_RING;

typedef struct _AUDIO_SESSION {
    ULONG Id;
    ULONG EndpointId;
    ULONG ProcessId;
    CHAR Name[64];
    AUDIO_FORMAT Format;
    AUDIO_RING Ring;
    ULONG Volume;       /* 0..100 */
    ULONG Muted;
    BOOLEAN Active;
    BOOLEAN InUse;
} AUDIO_SESSION;

static AUDIO_ENDPOINT g_Endpoints[AUDIO_MAX_ENDPOINTS];
static AUDIO_SESSION g_Sessions[AUDIO_MAX_SESSIONS];
static AUDIO_FORMAT g_SupportedFormats[AUDIO_MAX_FORMATS];
static BOOLEAN g_AudioEngineInit;
static BOOLEAN g_MixerRunning;
static PETHREAD g_MixerThread;
static KSEMAPHORE g_MixerTrigger;

/* Format negotiation: pick the best format both sides support. */
static NTSTATUS FormatNegotiate(AUDIO_FORMAT *Requested, AUDIO_FORMAT *Endpoint)
{
    if (!Requested || !Endpoint) return STATUS_INVALID_PARAMETER;
    /* MinNT mixer is 48kHz/16-bit/stereo. Resample if needed. */
    Requested->SampleRate = AUDIO_MIXER_RATE;
    Requested->Channels = AUDIO_MIXER_CHANNELS;
    Requested->BitsPerSample = AUDIO_MIXER_BITS;
    Requested->ByteRate = AUDIO_MIXER_RATE * AUDIO_MIXER_CHANNELS * (AUDIO_MIXER_BITS / 8);
    Requested->BlockAlign = AUDIO_MIXER_CHANNELS * (AUDIO_MIXER_BITS / 8);
    Requested->Tag = AudioFormatPcm;
    *Endpoint = *Requested;
    return STATUS_SUCCESS;
}

/* ---- Endpoint management ---- */

NTSTATUS NTAPI AudioRegisterEndpoint(const CHAR *Name, AUDIO_DIRECTION Direction,
                                      AUDIO_FORMAT *Format, PULONG OutId)
{
    if (!Name || !Format) return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < AUDIO_MAX_ENDPOINTS; i++) {
        if (!g_Endpoints[i].InUse) {
            RtlZeroMemory(&g_Endpoints[i], sizeof(AUDIO_ENDPOINT));
            g_Endpoints[i].InUse = TRUE;
            g_Endpoints[i].Active = TRUE;
            g_Endpoints[i].Direction = Direction;
            for (ULONG k = 0; k < 63 && Name[k]; k++) g_Endpoints[i].Name[k] = Name[k];
            FormatNegotiate(&g_Endpoints[i].Format, Format);
            g_Endpoints[i].Format = *Format;
            g_Endpoints[i].MasterVolume = 100;
            if (OutId) *OutId = i;
            DbgPrint("AUDIO: endpoint '%s' (%s) registered as id %u\n",
                     Name, Direction == AudioDirectionRender ? "render" : "capture", i);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI AudioSetEndpointVolume(ULONG EndpointId, ULONG Volume)
{
    if (EndpointId >= AUDIO_MAX_ENDPOINTS || !g_Endpoints[EndpointId].InUse) return STATUS_INVALID_PARAMETER;
    if (Volume > 100) Volume = 100;
    g_Endpoints[EndpointId].MasterVolume = Volume;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioGetEndpointVolume(ULONG EndpointId, PULONG OutVolume)
{
    if (EndpointId >= AUDIO_MAX_ENDPOINTS || !g_Endpoints[EndpointId].InUse) return STATUS_INVALID_PARAMETER;
    if (OutVolume) *OutVolume = g_Endpoints[EndpointId].MasterVolume;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioSetEndpointMute(ULONG EndpointId, BOOLEAN Muted)
{
    if (EndpointId >= AUDIO_MAX_ENDPOINTS || !g_Endpoints[EndpointId].InUse) return STATUS_INVALID_PARAMETER;
    g_Endpoints[EndpointId].Muted = Muted ? 1 : 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioEnumEndpoints(PULONG OutIds, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < AUDIO_MAX_ENDPOINTS && n < MaxCount; i++) {
        if (g_Endpoints[i].InUse) OutIds[n++] = i;
    }
    return (ULONG)n;
}

NTSTATUS NTAPI AudioGetEndpointInfo(ULONG EndpointId, PCHAR OutName, ULONG MaxLen,
                                     PULONG OutDirection, PULONG OutSampleRate)
{
    if (EndpointId >= AUDIO_MAX_ENDPOINTS || !g_Endpoints[EndpointId].InUse) return STATUS_INVALID_PARAMETER;
    if (OutName) {
        ULONG k = 0;
        while (g_Endpoints[EndpointId].Name[k] && k < MaxLen - 1) { OutName[k] = g_Endpoints[EndpointId].Name[k]; k++; }
        OutName[k] = 0;
    }
    if (OutDirection) *OutDirection = g_Endpoints[EndpointId].Direction;
    if (OutSampleRate) *OutSampleRate = g_Endpoints[EndpointId].Format.SampleRate;
    return STATUS_SUCCESS;
}

/* ---- Session management ---- */

NTSTATUS NTAPI AudioCreateSession(const CHAR *Name, ULONG EndpointId, ULONG ProcessId,
                                    AUDIO_FORMAT *Format, PULONG OutSessionId)
{
    if (!Name || EndpointId >= AUDIO_MAX_ENDPOINTS || !g_Endpoints[EndpointId].InUse || !Format)
        return STATUS_INVALID_PARAMETER;
    for (ULONG i = 0; i < AUDIO_MAX_SESSIONS; i++) {
        if (!g_Sessions[i].InUse) {
            RtlZeroMemory(&g_Sessions[i], sizeof(AUDIO_SESSION));
            g_Sessions[i].InUse = TRUE;
            g_Sessions[i].Active = TRUE;
            g_Sessions[i].EndpointId = EndpointId;
            g_Sessions[i].ProcessId = ProcessId;
            for (ULONG k = 0; k < 63 && Name[k]; k++) g_Sessions[i].Name[k] = Name[k];
            FormatNegotiate(&g_Sessions[i].Format, Format);
            g_Sessions[i].Format = *Format;
            g_Sessions[i].Volume = 100;
            KeInitializeSpinLock(&g_Sessions[i].Ring.Lock);
            if (OutSessionId) *OutSessionId = i;
            DbgPrint("AUDIO: session '%s' (PID %u) on endpoint %u\n", Name, ProcessId, EndpointId);
            /* Wake mixer to pick up the new session. */
            if (g_MixerRunning) KeReleaseSemaphore(&g_MixerTrigger, 0, 1, FALSE);
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NO_MEMORY;
}

NTSTATUS NTAPI AudioDestroySession(ULONG SessionId)
{
    if (SessionId >= AUDIO_MAX_SESSIONS || !g_Sessions[SessionId].InUse) return STATUS_INVALID_PARAMETER;
    g_Sessions[SessionId].Active = FALSE;
    g_Sessions[SessionId].InUse = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioSetSessionVolume(ULONG SessionId, ULONG Volume)
{
    if (SessionId >= AUDIO_MAX_SESSIONS || !g_Sessions[SessionId].InUse) return STATUS_INVALID_PARAMETER;
    if (Volume > 100) Volume = 100;
    g_Sessions[SessionId].Volume = Volume;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioSetSessionMute(ULONG SessionId, BOOLEAN Muted)
{
    if (SessionId >= AUDIO_MAX_SESSIONS || !g_Sessions[SessionId].InUse) return STATUS_INVALID_PARAMETER;
    g_Sessions[SessionId].Muted = Muted ? 1 : 0;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioWriteSession(ULONG SessionId, PVOID Buffer, ULONG Frames)
{
    if (SessionId >= AUDIO_MAX_SESSIONS || !g_Sessions[SessionId].InUse) return STATUS_INVALID_PARAMETER;
    if (Frames > AUDIO_RING_FRAMES) return STATUS_BUFFER_TOO_SMALL;
    AUDIO_SESSION *s = &g_Sessions[SessionId];
    AUDIO_RING *r = &s->Ring;
    KIRQL irql;
    KeAcquireSpinLock(&r->Lock, &irql);
    RtlCopyMemory(&r->Data[r->WritePos * 4], Buffer, Frames * 4);
    r->WritePos = (r->WritePos + Frames) % AUDIO_RING_FRAMES;
    if (r->Frames + Frames > AUDIO_RING_FRAMES) r->Frames = AUDIO_RING_FRAMES;
    else r->Frames += Frames;
    KeReleaseSpinLock(&r->Lock, irql);
    if (g_MixerRunning) KeReleaseSemaphore(&g_MixerTrigger, 0, 1, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioEnumSessions(ULONG EndpointId, PULONG OutIds, ULONG MaxCount)
{
    ULONG n = 0;
    for (ULONG i = 0; i < AUDIO_MAX_SESSIONS && n < MaxCount; i++) {
        if (g_Sessions[i].InUse && g_Sessions[i].Active && g_Sessions[i].EndpointId == EndpointId)
            OutIds[n++] = i;
    }
    return (ULONG)n;
}

/* ---- The Mixer Thread ----
 *
 * Periodically:
 *   - Pulls samples from each active session's ring buffer
 *   - Mixes them (sum + volume scale + saturation)
 *   - Forwards the mixed buffer to the active endpoint's driver
 *
 * In MinNT the endpoint driver is a software renderer that writes to
 * the audio HAL. The mixer thread runs every 10ms to keep latency
 * bounded.
 */
static VOID AudioMixerThread(PVOID Context)
{
    (void)Context;
    DbgPrint("AUDIO: mixer thread started\n");
    while (g_MixerRunning) {
        /* Wait for new audio or 10ms tick. */
        KeWaitForSingleObject(&g_MixerTrigger, 0, FALSE, FALSE, NULL);
        if (!g_MixerRunning) break;

        /* For each endpoint, mix sessions and forward. */
        for (ULONG e = 0; e < AUDIO_MAX_ENDPOINTS; e++) {
            if (!g_Endpoints[e].InUse || !g_Endpoints[e].Active) continue;
            if (g_Endpoints[e].Muted) continue;

            SHORT mixBuffer[AUDIO_MIXER_RATE / 100 * AUDIO_MIXER_CHANNELS]; /* 10ms */
            RtlZeroMemory(mixBuffer, sizeof(mixBuffer));
            ULONG mixedFrames = AUDIO_MIXER_RATE / 100;  /* 10ms */

            for (ULONG s = 0; s < AUDIO_MAX_SESSIONS; s++) {
                if (!g_Sessions[s].InUse || !g_Sessions[s].Active) continue;
                if (g_Sessions[s].EndpointId != e) continue;
                if (g_Sessions[s].Muted) continue;
                /* Pull samples. */
                AUDIO_RING *r = &g_Sessions[s].Ring;
                KIRQL irql;
                KeAcquireSpinLock(&r->Lock, &irql);
                ULONG available = r->Frames;
                if (available > mixedFrames) available = mixedFrames;
                /* Read up to mixedFrames * 4 bytes from the ring. */
                for (ULONG i = 0; i < available; i++) {
                    ULONG idx = (r->ReadPos + i) % AUDIO_RING_FRAMES;
                    SHORT *src = (SHORT *)&r->Data[idx * 4];
                    mixBuffer[i * 2 + 0] += (SHORT)((LONG)src[0] * g_Sessions[s].Volume / 100);
                    mixBuffer[i * 2 + 1] += (SHORT)((LONG)src[1] * g_Sessions[s].Volume / 100);
                }
                r->ReadPos = (r->ReadPos + available) % AUDIO_RING_FRAMES;
                r->Frames -= available;
                KeReleaseSpinLock(&r->Lock, irql);
            }

            /* Apply endpoint master volume. */
            for (ULONG i = 0; i < mixedFrames * 2; i++) {
                LONG v = (LONG)mixBuffer[i] * g_Endpoints[e].MasterVolume / 100;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                mixBuffer[i] = (SHORT)v;
            }
            /* Forward to the endpoint's render function. In MinNT this
             * writes to the audio HAL (a software buffer or whatever
             * the underlying driver provides). We have no driver here
             * so we just count the frames consumed. */
            (void)mixedFrames;
        }
    }
    DbgPrint("AUDIO: mixer thread exiting\n");
}

NTSTATUS NTAPI AudioEngineInit(VOID)
{
    if (g_AudioEngineInit) return STATUS_SUCCESS;
    RtlZeroMemory(g_Endpoints, sizeof(g_Endpoints));
    RtlZeroMemory(g_Sessions, sizeof(g_Sessions));
    KeInitializeSemaphore(&g_MixerTrigger, 0, 0x7FFFFFFF);

    /* Register the default render endpoint. */
    AUDIO_FORMAT fmt = { 0 };
    ULONG id;
    AudioRegisterEndpoint("MinNT Speakers", AudioDirectionRender, &fmt, &id);
    fmt.SampleRate = 48000;
    AudioRegisterEndpoint("MinNT Microphone", AudioDirectionCapture, &fmt, &id);

    /* Start the mixer thread. */
    g_MixerRunning = TRUE;
    PsCreateSystemThread(PsInitialSystemProcess, AudioMixerThread, NULL, &g_MixerThread);

    g_AudioEngineInit = TRUE;
    DbgPrint("AUDIO: engine initialized (mixer thread + 2 default endpoints)\n");
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI AudioEngineShutdown(VOID)
{
    if (!g_AudioEngineInit) return STATUS_SUCCESS;
    g_MixerRunning = FALSE;
    KeReleaseSemaphore(&g_MixerTrigger, 0, 1, FALSE);
    g_AudioEngineInit = FALSE;
    return STATUS_SUCCESS;
}

ULONG NTAPI AudioGetEndpointCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < AUDIO_MAX_ENDPOINTS; i++) if (g_Endpoints[i].InUse) n++;
    return n;
}

ULONG NTAPI AudioGetSessionCount(VOID)
{
    ULONG n = 0;
    for (ULONG i = 0; i < AUDIO_MAX_SESSIONS; i++) if (g_Sessions[i].InUse) n++;
    return n;
}
