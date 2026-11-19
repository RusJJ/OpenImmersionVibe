#include "vibeapi.h"

static VibeInt32 CalcWaveformDurationMs(VibeInt32 dataSize, VibeInt32 sampleRate, VibeInt32 bitDepth)
{
    if(dataSize <= 0 || sampleRate <= 0) return 30;
    VibeInt32 bps = (bitDepth >= 16) ? 2 : 1;
    VibeInt32 samples = dataSize / bps;
    VibeInt32 dur = (VibeInt32)(((VibeInt64)samples * 1000LL) / sampleRate);
    return (dur > 0) ? dur : 15;
}

static VibeInt32 EstimateWaveformMagnitude(const VibeUInt8* data, VibeInt32 dataSize, VibeInt32 fallbackMag)
{
    if(!data || dataSize <= 0) return fallbackMag;
    VibeInt32 peak = 0;
    for(VibeInt32 i = 0; i < dataSize; ++i)
    {
        VibeInt32 a = (VibeInt8)data[i];
        if(a < 0) a = -a;
        if(a > peak) peak = a;
    }
    if(peak == 0) return 0;
    VibeInt32 scaled = (fallbackMag * peak) / 127;
    return (scaled > VIBE_MAX_MAGNITUDE) ? VIBE_MAX_MAGNITUDE : scaled;
}

extern "C"
{

VibeInt32 EmuComputeMagnitude(VibeInt32 rawMag)
{
    if(g_bDisableEffects || rawMag <= 0) return 0;
    if(rawMag > VIBE_MAX_MAGNITUDE) rawMag = VIBE_MAX_MAGNITUDE;

    VibeInt64 scaled = ((VibeInt64)rawMag * g_nStrength) / VIBE_MAX_MAGNITUDE;
    scaled = (scaled * g_nMasterStrength) / VIBE_MAX_MAGNITUDE;
    if(scaled < 0) scaled = 0;
    if(scaled > VIBE_MAX_MAGNITUDE) scaled = VIBE_MAX_MAGNITUDE;
    return (VibeInt32)scaled;
}

VibeStatus EmuPlaySimpleEffect(VibeInt32 deviceHandle, VibeInt32 effectType, VibeInt32 durationMs, VibeInt32 magnitude, VibeInt32* effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;
    if(durationMs < 0) return STATUS_WRONG_ARGUMENT;

    VibeInt32 effMag = EmuComputeMagnitude(magnitude);

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverAllocSlot();
    VibeInt32 handle = VibeDriverNextEffectHandle();
    slot->inUse = (durationMs > 0);
    slot->handle = handle;
    slot->effectType = effectType;
    slot->durationMs = durationMs;
    slot->magnitude = magnitude;
    slot->startTimeMs = VibeOSGetTimeMs();
    slot->remainingMs = durationMs;
    slot->state = (durationMs > 0 && effMag > 0) ? VIBE_EFFECT_STATE_PLAYING : VIBE_EFFECT_STATE_NOT_PLAYING;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(durationMs > 0 && effMag > 0)
    {
        if(durationMs == VIBE_TIME_INFINITE)
        {
            VibeInt64 t[1] = { 1000 };
            VibeInt32 m[1] = { effMag };
            Emulator_VibrateWaveform(t, m, 1, 0);
        }
        else
        {
            Emulator_VibrateWithMagnitude((VibeInt64)durationMs, effMag);
        }
    }

    if(effectHandle) *effectHandle = handle;
    return STATUS_OK;
}

VibeStatus EmuModifyEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 durationMs, VibeInt32 magnitude)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    VibeInt32 effMag = EmuComputeMagnitude(magnitude);
    bool shouldVibrate = false;
    bool found = false;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    if(slot)
    {
        found = true;
        slot->durationMs = durationMs;
        slot->magnitude = magnitude;
        slot->startTimeMs = VibeOSGetTimeMs();
        slot->remainingMs = durationMs;
        if(durationMs > 0 && effMag > 0)
        {
            slot->state = VIBE_EFFECT_STATE_PLAYING;
            shouldVibrate = true;
        }
        else
        {
            slot->state = VIBE_EFFECT_STATE_NOT_PLAYING;
            slot->inUse = false;
        }
    }
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(shouldVibrate) Emulator_VibrateWithMagnitude((VibeInt64)durationMs, effMag);
    else if(found) Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibePlayIVTEffect(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    IvtWaveformPlan plan;
    VibeStatus st = IvtBuildWaveformPlan(ivt, effectIndex, &plan);
    if(st < STATUS_OK) return st;

    bool anyNonZero = false;
    for(VibeInt32 i = 0; i < plan.count; ++i)
    {
        plan.amplitudes[i] = EmuComputeMagnitude(plan.amplitudes[i]);
        if(plan.amplitudes[i] > 0) anyNonZero = true;
    }

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverAllocSlot();
    VibeInt32 handle = VibeDriverNextEffectHandle();
    slot->inUse = (plan.totalDurationMs > 0);
    slot->handle = handle;
    slot->effectType = ImmVibeGetIVTEffectType(ivt, effectIndex);
    slot->durationMs = plan.totalDurationMs;
    slot->magnitude = plan.peakMagnitude;
    slot->startTimeMs = VibeOSGetTimeMs();
    slot->remainingMs = plan.totalDurationMs;
    slot->state = (plan.count > 0 && anyNonZero) ? VIBE_EFFECT_STATE_PLAYING : VIBE_EFFECT_STATE_NOT_PLAYING;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(plan.count > 0 && anyNonZero)
    {
        Emulator_VibrateWaveform(plan.timings, plan.amplitudes, plan.count, plan.repeatIndex);
    }

    if(effectHandle) *effectHandle = handle;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibePlayIVTEffectRepeat(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeUInt8 repeat, VibeInt32* effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;
    if(repeat == 0) return ImmVibePlayIVTEffect(deviceHandle, ivtData, effectIndex, effectHandle);

    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    IvtWaveformPlan plan;
    VibeStatus st = IvtBuildWaveformPlan(ivt, effectIndex, &plan);
    if(st < STATUS_OK) return st;

    if(repeat == 0xFF)
    {
        plan.repeatIndex = 0;
        plan.totalDurationMs = VIBE_TIME_INFINITE;
    }
    else if(plan.totalDurationMs != VIBE_TIME_INFINITE)
    {
        VibeInt32 baseCount = plan.count;
        VibeInt32 extra = (repeat > 8) ? 8 : (VibeInt32)repeat;
        for(VibeInt32 r = 0; r < extra && plan.count < VIBRO_MAX_WAVEFORM_STEPS; ++r)
        {
            for(VibeInt32 s = 0; s < baseCount && plan.count < VIBRO_MAX_WAVEFORM_STEPS; ++s)
            {
                plan.timings[plan.count] = plan.timings[s];
                plan.amplitudes[plan.count] = plan.amplitudes[s];
                plan.totalDurationMs += (VibeInt32)plan.timings[s];
                plan.count++;
            }
        }
    }

    bool anyNonZero = false;
    for(VibeInt32 i = 0; i < plan.count; ++i)
    {
        plan.amplitudes[i] = EmuComputeMagnitude(plan.amplitudes[i]);
        if(plan.amplitudes[i] > 0) anyNonZero = true;
    }

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverAllocSlot();
    VibeInt32 handle = VibeDriverNextEffectHandle();
    slot->inUse = (plan.totalDurationMs > 0);
    slot->handle = handle;
    slot->effectType = ImmVibeGetIVTEffectType(ivt, effectIndex);
    slot->durationMs = plan.totalDurationMs;
    slot->magnitude = plan.peakMagnitude;
    slot->startTimeMs = VibeOSGetTimeMs();
    slot->remainingMs = plan.totalDurationMs;
    slot->state = (plan.count > 0 && anyNonZero) ? VIBE_EFFECT_STATE_PLAYING : VIBE_EFFECT_STATE_NOT_PLAYING;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(plan.count > 0 && anyNonZero)
    {
        Emulator_VibrateWaveform(plan.timings, plan.amplitudes, plan.count, plan.repeatIndex);
    }

    if(effectHandle) *effectHandle = handle;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibePlayIVTInterpolatedEffect(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 interpolant, VibeInt32* effectHandle)
{
    (void)interpolant;
    return ImmVibePlayIVTEffect(deviceHandle, ivtData, effectIndex, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibePlayUHLEffect(VibeInt32 deviceHandle, VibeInt32 effectIndex, VibeInt32* effectHandle)
{
    return ImmVibePlayIVTEffect(deviceHandle, g_EmulatorReferenceIVT, effectIndex, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibePlayMagSweepEffect(VibeInt32 deviceHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 style, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel, VibeInt32* effectHandle)
{
    (void)style; (void)attackTime; (void)attackLevel; (void)fadeTime; (void)fadeLevel;
    return EmuPlaySimpleEffect(deviceHandle, VIBE_EFFECT_TYPE_MAGSWEEP, duration, magnitude, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibePlayPeriodicEffect(VibeInt32 deviceHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 period, VibeInt32 styleAndWaveType, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel, VibeInt32* effectHandle)
{
    (void)period; (void)styleAndWaveType; (void)attackTime; (void)attackLevel; (void)fadeTime; (void)fadeLevel;
    return EmuPlaySimpleEffect(deviceHandle, VIBE_EFFECT_TYPE_PERIODIC, duration, magnitude, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibePlayWaveformEffect(VibeInt32 deviceHandle, const VibeUInt8* data, VibeInt32 dataSize, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, VibeInt32* effectHandle)
{
    VibeInt32 dur = CalcWaveformDurationMs(dataSize, sampleRate, bitDepth);
    VibeInt32 mag = EstimateWaveformMagnitude(data, dataSize, magnitude > 0 ? magnitude : VIBE_MAX_MAGNITUDE);
    return EmuPlaySimpleEffect(deviceHandle, VIBE_EFFECT_TYPE_WAVEFORM, dur, mag, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibeAppendWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* data, VibeInt32 dataSize, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, VibeInt32* outEffectHandle)
{
    VibeInt32 dur = CalcWaveformDurationMs(dataSize, sampleRate, bitDepth);
    VibeInt32 mag = EstimateWaveformMagnitude(data, dataSize, magnitude > 0 ? magnitude : VIBE_MAX_MAGNITUDE);
    VibeStatus st = EmuModifyEffect(deviceHandle, effectHandle, dur, mag);
    if(outEffectHandle) *outEffectHandle = effectHandle;
    return st;
}

VIBE_EXPORT VibeStatus ImmVibePlayEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* effectHandle)
{
    const VibeEnhancedWaveformBuffer* buf = (const VibeEnhancedWaveformBuffer*)enhancedData;
    const VibeUInt8* payload = buf ? buf->data : NULL;
    VibeInt32 dataSize = buf ? buf->dataSize : 0;
    return ImmVibePlayWaveformEffect(deviceHandle, payload, dataSize, sampleRate, bitDepth, magnitude, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibeAppendEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* outEffectHandle)
{
    const VibeEnhancedWaveformBuffer* buf = (const VibeEnhancedWaveformBuffer*)enhancedData;
    const VibeUInt8* payload = buf ? buf->data : NULL;
    VibeInt32 dataSize = buf ? buf->dataSize : 0;
    return ImmVibeAppendWaveformEffect(deviceHandle, effectHandle, payload, dataSize, sampleRate, bitDepth, magnitude, outEffectHandle);
}

VIBE_EXPORT VibeStatus ImmVibeReplaceEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* outEffectHandle)
{
    return ImmVibeAppendEnhancedWaveformEffect(deviceHandle, effectHandle, sampleRate, bitDepth, magnitude, enhancedData, outEffectHandle);
}

VIBE_EXPORT VibeStatus ImmVibeModifyPlayingMagSweepEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 style, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel)
{
    (void)style; (void)attackTime; (void)attackLevel; (void)fadeTime; (void)fadeLevel;
    return EmuModifyEffect(deviceHandle, effectHandle, duration, magnitude);
}

VIBE_EXPORT VibeStatus ImmVibeModifyPlayingPeriodicEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 period, VibeInt32 styleAndWaveType, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel)
{
    (void)period; (void)styleAndWaveType; (void)attackTime; (void)attackLevel; (void)fadeTime; (void)fadeLevel;
    return EmuModifyEffect(deviceHandle, effectHandle, duration, magnitude);
}

VIBE_EXPORT VibeStatus ImmVibeModifyPlayingInterpolatedEffectInterpolant(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 interpolant)
{
    (void)deviceHandle; (void)effectHandle; (void)interpolant;
    return (g_nVibeAPIReferenceCount > 0) ? STATUS_OK : STATUS_FAILED_TO_INIT;
}

VIBE_EXPORT VibeStatus ImmVibeStopPlayingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    if(slot)
    {
        slot->state = VIBE_EFFECT_STATE_NOT_PLAYING;
        slot->remainingMs = 0;
        slot->inUse = false;
    }
    bool anyPlaying = VibeDriverAnySlotPlaying();
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(!anyPlaying) Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeStopAllPlayingEffects(VibeInt32 deviceHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibeDriverResetSlots();
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibePausePlayingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    bool shouldCancel = false;
    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    if(slot && slot->state == VIBE_EFFECT_STATE_PLAYING)
    {
        VibeInt64 elapsed = VibeOSGetTimeMs() - slot->startTimeMs;
        VibeInt32 rem = (slot->durationMs == VIBE_TIME_INFINITE) ? VIBE_TIME_INFINITE : (VibeInt32)(slot->durationMs - elapsed);
        slot->remainingMs = (rem > 0) ? rem : 0;
        slot->state = VIBE_EFFECT_STATE_PAUSED;
        shouldCancel = !VibeDriverAnySlotPlaying();
    }
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(shouldCancel) Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeResumePausedEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    VibeInt32 dur = 0;
    VibeInt32 mag = 0;
    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    if(slot && slot->state == VIBE_EFFECT_STATE_PAUSED && slot->remainingMs > 0)
    {
        slot->durationMs = slot->remainingMs;
        slot->startTimeMs = VibeOSGetTimeMs();
        slot->state = VIBE_EFFECT_STATE_PLAYING;
        dur = slot->durationMs;
        mag = EmuComputeMagnitude(slot->magnitude);
    }
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    if(dur > 0 && mag > 0) Emulator_VibrateWithMagnitude((VibeInt64)dur, mag);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetEffectState(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32* state)
{
    (void)deviceHandle;
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!state) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    *state = slot ? slot->state : VIBE_EFFECT_STATE_NOT_PLAYING;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetEffectRemainingDuration(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32* remainingMs)
{
    (void)deviceHandle;
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!remainingMs) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibroEffectSlot* slot = VibeDriverFindSlot(effectHandle);
    if(!slot)
    {
        *remainingMs = 0;
    }
    else if(slot->state == VIBE_EFFECT_STATE_PAUSED)
    {
        *remainingMs = slot->remainingMs;
    }
    else if(slot->state == VIBE_EFFECT_STATE_PLAYING)
    {
        if(slot->durationMs == VIBE_TIME_INFINITE)
        {
            *remainingMs = VIBE_TIME_INFINITE;
        }
        else
        {
            VibeInt64 elapsed = VibeOSGetTimeMs() - slot->startTimeMs;
            VibeInt32 rem = (VibeInt32)(slot->durationMs - elapsed);
            *remainingMs = (rem > 0) ? rem : 0;
        }
    }
    else
    {
        *remainingMs = 0;
    }
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeCreateStreamingEffect(VibeInt32 deviceHandle, VibeInt32* effectHandle)
{
    return EmuPlaySimpleEffect(deviceHandle, VIBE_EFFECT_TYPE_STREAMING, 0, 0, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibeDestroyStreamingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle)
{
    return ImmVibeStopPlayingEffect(deviceHandle, effectHandle);
}

VIBE_EXPORT VibeStatus ImmVibePlayStreamingSample(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* sample, VibeInt32 size)
{
    (void)effectHandle;
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!g_bDeviceOpen || deviceHandle != 1) return STATUS_WRONG_ARGUMENT;
    if(!sample || size <= 0) return STATUS_OK;

    VibeInt32 dur = size * 5;
    VibeInt32 mag = EstimateWaveformMagnitude(sample, size, VIBE_MAX_MAGNITUDE);
    VibeInt32 effMag = EmuComputeMagnitude(mag);
    if(effMag > 0) Emulator_VibrateWithMagnitude((VibeInt64)dur, effMag);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibePlayStreamingSampleWithOffset(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* sample, VibeInt32 size, VibeInt32 offsetTime)
{
    (void)offsetTime;
    return ImmVibePlayStreamingSample(deviceHandle, effectHandle, sample, size);
}

};
