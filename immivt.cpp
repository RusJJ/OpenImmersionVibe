#include "vibeapi.h"

static VibeUInt16 ReadU16LE(const VibeUInt8* p)
{
    return (VibeUInt16)((VibeUInt16)p[0] | ((VibeUInt16)p[1] << 8));
}

static VibeUInt16 ReadU16BE(const VibeUInt8* p)
{
    return (VibeUInt16)(((VibeUInt16)p[0] << 8) | (VibeUInt16)p[1]);
}

static VibeInt32 GetIvtHeaderSize(const VibeUInt8* ivtData)
{
    if(!ivtData) return 0;
    if(ivtData[0] == 1 || ivtData[0] == 2) return 8;
    if(ivtData[0] == 3) return 9;
    return 0;
}

static bool ParseIvtLayout(const VibeUInt8* ivtData, VibeInt32* outHdr, VibeInt32* outNumEff, VibeInt32* outStorSz, VibeInt32* outNameSz)
{
    VibeInt32 hdr = GetIvtHeaderSize(ivtData);
    if(hdr == 0) return false;

    VibeInt32 numEff = (VibeInt32)ReadU16LE(ivtData + 2);
    VibeInt32 storSz = 0;
    VibeInt32 nameSz = 0;
    if(ivtData[0] == 3)
    {
        storSz = (VibeInt32)((VibeUInt32)ivtData[4] | ((VibeUInt32)ivtData[5] << 8) | ((VibeUInt32)ivtData[6] << 16));
        nameSz = (VibeInt32)ReadU16LE(ivtData + 7);
    }
    else
    {
        storSz = (VibeInt32)ReadU16LE(ivtData + 4);
        nameSz = (VibeInt32)ReadU16LE(ivtData + 6);
    }
    if(numEff <= 0 || storSz < numEff * 2) return false;

    if(outHdr) *outHdr = hdr;
    if(outNumEff) *outNumEff = numEff;
    if(outStorSz) *outStorSz = storSz;
    if(outNameSz) *outNameSz = nameSz;
    return true;
}

static const VibeUInt8* GetEffectPacket(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* outLen)
{
    VibeInt32 hdr = 0, numEff = 0, storSz = 0;
    if(!ParseIvtLayout(ivtData, &hdr, &numEff, &storSz, NULL)) return NULL;
    if(effectIndex < 0 || effectIndex >= numEff) return NULL;

    const VibeUInt8* offTable = ivtData + hdr;
    VibeInt32 dataBase = hdr + numEff * 2;
    VibeInt32 storEnd = hdr + storSz;

    VibeInt32 pktStart = dataBase + (VibeInt32)ReadU16LE(offTable + effectIndex * 2);
    if(pktStart >= storEnd) return NULL;

    VibeInt32 pktEnd = storEnd;
    if(effectIndex + 1 < numEff)
    {
        VibeInt32 nextStart = dataBase + (VibeInt32)ReadU16LE(offTable + (effectIndex + 1) * 2);
        if(nextStart > pktStart && nextStart <= storEnd) pktEnd = nextStart;
    }

    if(outLen) *outLen = pktEnd - pktStart;
    return ivtData + pktStart;
}

static bool DecodeBasisPacket(const VibeUInt8* pkt, VibeInt32 pktLen, IvtBasisDef* outDef)
{
    if(!pkt || pktLen < 8 || !outDef) return false;
    memset(outDef, 0, sizeof(*outDef));

    const VibeUInt8* p = pkt;
    VibeInt32 len = pktLen;

    // 8-byte envelope header (0x30)
    if(p[0] == 0x30 && len >= 16)
    {
        outDef->attackTime = (VibeInt32)ReadU16LE(p + 1);
        outDef->fadeTime = (VibeInt32)ReadU16LE(p + 3);
        outDef->attackLevel = ((VibeInt32)(p[5] & 0x7F) * VIBE_MAX_MAGNITUDE) / 127;
        outDef->fadeLevel = ((VibeInt32)(p[6] & 0x7F) * VIBE_MAX_MAGNITUDE) / 127;
        p += 8;
        len -= 8;
    }
    if(len < 8) return false;

    VibeUInt32 durRaw = ((((VibeUInt32)(p[2] & 0x0F) << 8) | (VibeUInt32)p[1]) << 4) | ((VibeUInt32)p[7] >> 4);
    VibeInt32 durMs = (durRaw == 0xFFFFu) ? VIBE_TIME_INFINITE : (VibeInt32)durRaw;
    if(durMs == 0) durMs = 15;

    VibeInt32 mag = ((VibeInt32)(p[4] & 0x7F) * VIBE_MAX_MAGNITUDE) / 127;
    if(mag > VIBE_MAX_MAGNITUDE) mag = VIBE_MAX_MAGNITUDE;

    outDef->durationMs = durMs;
    outDef->magnitude = mag;
    outDef->period = (VibeInt32)(((VibeUInt32)p[6] << 4) | ((VibeUInt32)p[5] >> 4));
    outDef->styleAndWave = (VibeInt32)(p[5] & 0x0F);
    return true;
}

struct TimelinePulse
{
    VibeInt32 startMs;
    VibeInt32 durationMs;
    VibeInt32 magnitude;
};

static void AppendStep(IvtWaveformPlan* plan, VibeInt64 durationMs, VibeInt32 magnitude)
{
    if(durationMs <= 0) return;
    if(plan->count > 0 && plan->amplitudes[plan->count - 1] == magnitude)
    {
        plan->timings[plan->count - 1] += durationMs;
        return;
    }
    if(plan->count >= VIBRO_MAX_WAVEFORM_STEPS) return;
    plan->timings[plan->count] = durationMs;
    plan->amplitudes[plan->count] = magnitude;
    plan->count++;
}

extern "C"
{

VibeStatus IvtGetEffectNameUtf16(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeWChar* outBuf, size_t outChars)
{
    if(!outBuf || outChars == 0) return STATUS_WRONG_ARGUMENT;
    outBuf[0] = 0;

    VibeInt32 hdr = 0, numEff = 0, storSz = 0, nameSz = 0;
    if(!ParseIvtLayout(ivtData, &hdr, &numEff, &storSz, &nameSz)) return STATUS_WRONG_ARGUMENT;
    if(effectIndex < 0 || effectIndex >= numEff) return STATUS_WRONG_ARGUMENT;
    if(nameSz < numEff * 2) return STATUS_FAILED_GENERIC;

    VibeInt32 nameStart = hdr + storSz;
    VibeInt32 nameEnd = nameStart + nameSz;
    VibeInt32 nameData = nameStart + numEff * 2;

    VibeInt32 pos = nameData + (VibeInt32)ReadU16LE(ivtData + nameStart + effectIndex * 2);
    if(pos >= nameEnd) return STATUS_FAILED_GENERIC;

    size_t outPos = 0;
    while(pos + 1 < nameEnd && outPos + 1 < outChars)
    {
        VibeWChar ch = ReadU16LE(ivtData + pos);
        pos += 2;
        if(ch == 0) break;
        outBuf[outPos++] = ch;
    }
    outBuf[outPos] = 0;
    return STATUS_OK;
}

VibeStatus IvtGetEffectNameAscii(const VibeUInt8* ivtData, VibeInt32 effectIndex, char* outBuf, size_t outBufLen)
{
    if(!outBuf || outBufLen == 0) return STATUS_WRONG_ARGUMENT;
    VibeWChar wbuf[128];
    size_t cap = (outBufLen < 128) ? outBufLen : 128;
    VibeStatus st = IvtGetEffectNameUtf16(ivtData, effectIndex, wbuf, cap);
    if(st < STATUS_OK)
    {
        outBuf[0] = '\0';
        return st;
    }

    size_t i = 0;
    for(; i + 1 < outBufLen && i < cap && wbuf[i] != 0; ++i)
    {
        outBuf[i] = (wbuf[i] < 128) ? (char)wbuf[i] : '?';
    }
    outBuf[i] = '\0';
    return STATUS_OK;
}

VibeStatus IvtGetEffectIndexFromNameU(const VibeUInt8* ivtData, const VibeWChar* effectNameU, size_t nameLenChars, VibeInt32* index)
{
    if(!ivtData || !effectNameU || !index) return STATUS_WRONG_ARGUMENT;
    VibeInt32 numEff = ImmVibeGetIVTEffectCount(ivtData);
    if(numEff < 0) return numEff;

    VibeWChar wbuf[128];
    for(VibeInt32 i = 0; i < numEff; ++i)
    {
        if(IvtGetEffectNameUtf16(ivtData, i, wbuf, 128) == STATUS_OK)
        {
            size_t len = 0;
            while(len < 128 && wbuf[len] != 0) ++len;
            if(len == nameLenChars && memcmp(wbuf, effectNameU, len * sizeof(VibeWChar)) == 0)
            {
                *index = i;
                return STATUS_OK;
            }
        }
    }
    return STATUS_FAILED_GENERIC;
}

VibeStatus IvtGetBasisDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, IvtBasisDef* outDef)
{
    if(!ivtData || !outDef) return STATUS_WRONG_ARGUMENT;
    VibeInt32 pktLen = 0;
    const VibeUInt8* pkt = GetEffectPacket(ivtData, effectIndex, &pktLen);
    if(!pkt || pktLen <= 0) return STATUS_WRONG_ARGUMENT;

    if(pkt[0] != 0xF1 && pkt[0] != 0xF2)
    {
        return DecodeBasisPacket(pkt, pktLen, outDef) ? STATUS_OK : STATUS_FAILED_GENERIC;
    }

    IvtWaveformPlan plan;
    VibeStatus st = IvtBuildWaveformPlan(ivtData, effectIndex, &plan);
    if(st < STATUS_OK) return st;
    memset(outDef, 0, sizeof(*outDef));
    outDef->durationMs = plan.totalDurationMs;
    outDef->magnitude = plan.peakMagnitude;
    return STATUS_OK;
}

VibeStatus IvtBuildWaveformPlan(const VibeUInt8* ivtData, VibeInt32 effectIndex, IvtWaveformPlan* outPlan)
{
    if(!ivtData || !outPlan) return STATUS_WRONG_ARGUMENT;
    memset(outPlan, 0, sizeof(*outPlan));
    outPlan->repeatIndex = -1;

    VibeInt32 pktLen = 0;
    const VibeUInt8* pkt = GetEffectPacket(ivtData, effectIndex, &pktLen);
    if(!pkt || pktLen <= 0) return STATUS_WRONG_ARGUMENT;

    // Direct basis effect
    if(pkt[0] != 0xF1 && pkt[0] != 0xF2)
    {
        IvtBasisDef def;
        if(!DecodeBasisPacket(pkt, pktLen, &def))
        {
            def.durationMs = 40;
            def.magnitude = VIBE_MAX_MAGNITUDE;
        }
        outPlan->peakMagnitude = def.magnitude;
        if(def.durationMs == VIBE_TIME_INFINITE)
        {
            AppendStep(outPlan, 1000, def.magnitude);
            outPlan->repeatIndex = 0;
            outPlan->totalDurationMs = VIBE_TIME_INFINITE;
        }
        else
        {
            AppendStep(outPlan, def.durationMs, def.magnitude);
            outPlan->totalDurationMs = def.durationMs;
        }
        return STATUS_OK;
    }

    // Timeline effect
    TimelinePulse pulses[32];
    VibeInt32 numPulses = 0;
    bool hasRepeat = false;
    VibeInt32 repeatStartMs = 0, repeatEndMs = 0, repeatCount = 0;

    VibeInt32 j = 0;
    while(j < pktLen)
    {
        VibeUInt8 op = pkt[j++];
        if(op == 0xFF) break;

        if(op == 0xF1)
        {
            VibeInt32 basisId = -1, timeOffMs = 0, durOverride = -1, magOverride = -1;
            while(j < pktLen && pkt[j] != 0xF1 && pkt[j] != 0xF2 && pkt[j] != 0xFF)
            {
                VibeUInt8 sub = pkt[j++];
                if(sub == 0xE0 && j < pktLen) basisId = pkt[j++];
                else if(sub == 0xE2 && j + 1 < pktLen) { timeOffMs = (VibeInt32)ReadU16BE(pkt + j); j += 2; }
                else if(sub == 0xD0 && j + 1 < pktLen) { durOverride = (VibeInt32)ReadU16BE(pkt + j); j += 2; }
                else if(sub == 0xD1 && j < pktLen) magOverride = ((VibeInt32)(pkt[j++] & 0x7F) * VIBE_MAX_MAGNITUDE) / 127;
                else if(sub == 0xD2 && j + 1 < pktLen) j += 2;
                else if(sub == 0xE1 && j < pktLen) j += 1;
                else break;
            }

            IvtBasisDef bdef;
            bdef.durationMs = 40;
            bdef.magnitude = VIBE_MAX_MAGNITUDE;
            if(basisId >= 0)
            {
                VibeInt32 bLen = 0;
                const VibeUInt8* bPkt = GetEffectPacket(ivtData, basisId, &bLen);
                if(bPkt) DecodeBasisPacket(bPkt, bLen, &bdef);
            }
            if(durOverride > 0) bdef.durationMs = durOverride;
            if(magOverride >= 0) bdef.magnitude = magOverride;

            if(numPulses < 32)
            {
                pulses[numPulses].startMs = timeOffMs;
                pulses[numPulses].durationMs = bdef.durationMs;
                pulses[numPulses].magnitude = bdef.magnitude;
                numPulses++;
            }
        }
        else if(op == 0xF2)
        {
            VibeInt32 rStart = 0, rEnd = 0, rCnt = 0;
            int e2Seen = 0;
            while(j < pktLen && pkt[j] != 0xF1 && pkt[j] != 0xF2 && pkt[j] != 0xFF)
            {
                VibeUInt8 sub = pkt[j++];
                if(sub == 0xE2 && j + 1 < pktLen)
                {
                    VibeInt32 v = (VibeInt32)ReadU16BE(pkt + j);
                    j += 2;
                    if(e2Seen == 0) rStart = v; else rEnd = v;
                    e2Seen++;
                }
                else if(sub == 0xE5 && j < pktLen) rCnt = (VibeInt32)pkt[j++];
                else if(sub == 0xD0 && j + 1 < pktLen) j += 2;
                else if(sub == 0xE1 && j < pktLen) j += 1;
                else break;
            }
            if(!hasRepeat || (rEnd - rStart) > (repeatEndMs - repeatStartMs))
            {
                hasRepeat = true;
                repeatStartMs = rStart;
                repeatEndMs = rEnd;
                repeatCount = rCnt;
            }
        }
        else
        {
            break;
        }
    }

    if(numPulses == 0)
    {
        AppendStep(outPlan, 30, VIBE_MAX_MAGNITUDE);
        outPlan->totalDurationMs = 30;
        outPlan->peakMagnitude = VIBE_MAX_MAGNITUDE;
        return STATUS_OK;
    }

    for(VibeInt32 a = 1; a < numPulses; ++a)
    {
        TimelinePulse key = pulses[a];
        VibeInt32 b = a - 1;
        while(b >= 0 && pulses[b].startMs > key.startMs)
        {
            pulses[b + 1] = pulses[b];
            --b;
        }
        pulses[b + 1] = key;
    }

    VibeInt32 cursorMs = 0;
    bool isInfinite = false;

    for(VibeInt32 i = 0; i < numPulses; ++i)
    {
        VibeInt32 pStart = pulses[i].startMs;
        VibeInt32 pDur = pulses[i].durationMs;
        VibeInt32 pMag = pulses[i].magnitude;
        if(pMag > outPlan->peakMagnitude) outPlan->peakMagnitude = pMag;

        if(pStart > cursorMs)
        {
            AppendStep(outPlan, pStart - cursorMs, 0);
            cursorMs = pStart;
        }
        if(pDur == VIBE_TIME_INFINITE)
        {
            AppendStep(outPlan, 1000, pMag);
            outPlan->repeatIndex = (outPlan->count > 0) ? (outPlan->count - 1) : 0;
            isInfinite = true;
            break;
        }
        else
        {
            VibeInt32 pEnd = pStart + pDur;
            if(pEnd > cursorMs)
            {
                AppendStep(outPlan, pEnd - cursorMs, pMag);
                cursorMs = pEnd;
            }
        }
    }

    if(!isInfinite && hasRepeat && repeatEndMs > repeatStartMs)
    {
        if(repeatEndMs > cursorMs)
        {
            AppendStep(outPlan, repeatEndMs - cursorMs, 0);
            cursorMs = repeatEndMs;
        }
        if(repeatCount == 0xFF)
        {
            outPlan->repeatIndex = 0;
            isInfinite = true;
        }
        else if(repeatCount > 0)
        {
            VibeInt32 baseSteps = outPlan->count;
            VibeInt32 reps = (repeatCount > 8) ? 8 : repeatCount;
            for(VibeInt32 r = 0; r < reps && outPlan->count < VIBRO_MAX_WAVEFORM_STEPS; ++r)
            {
                for(VibeInt32 s = 0; s < baseSteps && outPlan->count < VIBRO_MAX_WAVEFORM_STEPS; ++s)
                {
                    AppendStep(outPlan, outPlan->timings[s], outPlan->amplitudes[s]);
                    cursorMs += (VibeInt32)outPlan->timings[s];
                }
            }
        }
    }

    outPlan->totalDurationMs = isInfinite ? VIBE_TIME_INFINITE : cursorMs;
    return STATUS_OK;
}

VIBE_EXPORT VibeInt32 ImmVibeGetIVTEffectCount(const VibeUInt8* ivtData)
{
    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    VibeInt32 numEff = 0;
    if(!ParseIvtLayout(ivt, NULL, &numEff, NULL, NULL)) return STATUS_WRONG_ARGUMENT;
    return numEff;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectName(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 size, char* effectName)
{
    if(!effectName || size <= 0) return STATUS_WRONG_ARGUMENT;
    return IvtGetEffectNameAscii(ivtData ? ivtData : g_EmulatorReferenceIVT, effectIndex, effectName, (size_t)size);
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectNameU(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 size, VibeWChar* effectName)
{
    if(!effectName || size <= 0) return STATUS_WRONG_ARGUMENT;
    return IvtGetEffectNameUtf16(ivtData ? ivtData : g_EmulatorReferenceIVT, effectIndex, effectName, (size_t)size);
}

VIBE_EXPORT VibeInt32 ImmVibeGetStringLengthU(const VibeWChar* str)
{
    if(!str) return 0;
    VibeInt32 len = 0;
    while(str[len] != 0) ++len;
    return len;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectIndexFromName(const VibeUInt8* ivtData, const char* effectName, VibeInt32* index)
{
    if(!effectName || !index) return STATUS_WRONG_ARGUMENT;
    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    VibeInt32 numEff = ImmVibeGetIVTEffectCount(ivt);
    if(numEff < 0) return numEff;

    char nameBuf[128];
    for(VibeInt32 i = 0; i < numEff; ++i)
    {
        if(IvtGetEffectNameAscii(ivt, i, nameBuf, sizeof(nameBuf)) == STATUS_OK)
        {
            if(strcmp(nameBuf, effectName) == 0)
            {
                *index = i;
                return STATUS_OK;
            }
        }
    }
    return STATUS_FAILED_GENERIC;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectIndexFromNameU(const VibeUInt8* ivtData, const VibeWChar* effectName, VibeInt32* index)
{
    if(!effectName || !index) return STATUS_WRONG_ARGUMENT;
    size_t len = (size_t)ImmVibeGetStringLengthU(effectName);
    return IvtGetEffectIndexFromNameU(ivtData ? ivtData : g_EmulatorReferenceIVT, effectName, len, index);
}

VIBE_EXPORT VibeInt32 ImmVibeGetIVTEffectType(const VibeUInt8* ivtData, VibeInt32 effectIndex)
{
    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    VibeInt32 pktLen = 0;
    const VibeUInt8* pkt = GetEffectPacket(ivt, effectIndex, &pktLen);
    if(!pkt || pktLen <= 0) return STATUS_WRONG_ARGUMENT;

    VibeUInt8 b0 = pkt[0];
    if(b0 == 0xF1 || b0 == 0xF2) return VIBE_EFFECT_TYPE_TIMELINE;
    if(b0 == 0x30 && pktLen >= 9) b0 = pkt[8];
    if((b0 & 0xF0) == 0x20) return VIBE_EFFECT_TYPE_PERIODIC;
    if((b0 & 0xF0) == 0x10) return VIBE_EFFECT_TYPE_MAGSWEEP;
    if((b0 & 0xF0) == 0x40) return VIBE_EFFECT_TYPE_WAVEFORM;
    return VIBE_EFFECT_TYPE_PERIODIC;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectDuration(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration)
{
    if(!duration) return STATUS_WRONG_ARGUMENT;
    IvtWaveformPlan plan;
    VibeStatus st = IvtBuildWaveformPlan(ivtData ? ivtData : g_EmulatorReferenceIVT, effectIndex, &plan);
    if(st < STATUS_OK) return st;
    *duration = plan.totalDurationMs;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTInterpolatedEffectDuration(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 interpolant, VibeInt32* duration)
{
    (void)interpolant;
    return ImmVibeGetIVTEffectDuration(ivtData, effectIndex, duration);
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTMagSweepEffectDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration, VibeInt32* magnitude, VibeInt32* style, VibeInt32* attackTime, VibeInt32* attackLevel, VibeInt32* fadeTime, VibeInt32* fadeLevel)
{
    IvtBasisDef def;
    VibeStatus st = IvtGetBasisDefinition(ivtData ? ivtData : g_EmulatorReferenceIVT, effectIndex, &def);
    if(st < STATUS_OK) return st;
    if(duration) *duration = def.durationMs;
    if(magnitude) *magnitude = def.magnitude;
    if(style) *style = def.styleAndWave;
    if(attackTime) *attackTime = def.attackTime;
    if(attackLevel) *attackLevel = def.attackLevel;
    if(fadeTime) *fadeTime = def.fadeTime;
    if(fadeLevel) *fadeLevel = def.fadeLevel;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetIVTPeriodicEffectDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration, VibeInt32* magnitude, VibeInt32* period, VibeInt32* styleAndWaveType, VibeInt32* attackTime, VibeInt32* attackLevel, VibeInt32* fadeTime, VibeInt32* fadeLevel)
{
    IvtBasisDef def;
    VibeStatus st = IvtGetBasisDefinition(ivtData ? ivtData : g_EmulatorReferenceIVT, effectIndex, &def);
    if(st < STATUS_OK) return st;
    if(duration) *duration = def.durationMs;
    if(magnitude) *magnitude = def.magnitude;
    if(period) *period = def.period;
    if(styleAndWaveType) *styleAndWaveType = def.styleAndWave;
    if(attackTime) *attackTime = def.attackTime;
    if(attackLevel) *attackLevel = def.attackLevel;
    if(fadeTime) *fadeTime = def.fadeTime;
    if(fadeLevel) *fadeLevel = def.fadeLevel;
    return STATUS_OK;
}

VIBE_EXPORT VibeInt32 ImmVibeGetIVTSize(const VibeUInt8* ivtData)
{
    const VibeUInt8* ivt = ivtData ? ivtData : g_EmulatorReferenceIVT;
    VibeInt32 hdr = 0, storSz = 0, nameSz = 0;
    if(!ParseIvtLayout(ivt, &hdr, NULL, &storSz, &nameSz)) return STATUS_WRONG_ARGUMENT;
    return hdr + storSz + nameSz;
}

VIBE_EXPORT VibeStatus ImmVibeInitializeIVTBuffer(VibeUInt8* ivtData, VibeInt32 bufferSize)
{
    if(!ivtData || bufferSize < 8) return STATUS_WRONG_ARGUMENT;
    memset(ivtData, 0, (size_t)bufferSize);
    ivtData[0] = 1;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element)
{
    (void)ivtData; (void)bufferSize; (void)timelineIndex; (void)element;
    return STATUS_OK;
}
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement2(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element)
{
    return ImmVibeInsertIVTElement(ivtData, bufferSize, timelineIndex, element);
}
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement3(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element)
{
    return ImmVibeInsertIVTElement(ivtData, bufferSize, timelineIndex, element);
}
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement4(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element)
{
    return ImmVibeInsertIVTElement(ivtData, bufferSize, timelineIndex, element);
}

VIBE_EXPORT VibeStatus ImmVibeReadIVTElement(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element)
{
    (void)ivtData; (void)bufferSize; (void)timelineIndex; (void)elementIndex; (void)element;
    return STATUS_NOT_SUPPORTED;
}
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement2(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element)
{
    return ImmVibeReadIVTElement(ivtData, bufferSize, timelineIndex, elementIndex, element);
}
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement3(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element)
{
    return ImmVibeReadIVTElement(ivtData, bufferSize, timelineIndex, elementIndex, element);
}
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement4(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element)
{
    return ImmVibeReadIVTElement(ivtData, bufferSize, timelineIndex, elementIndex, element);
}

VIBE_EXPORT VibeStatus ImmVibeRemoveIVTElement(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex)
{
    (void)ivtData; (void)bufferSize; (void)timelineIndex; (void)elementIndex;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSaveIVTFile(const VibeUInt8* ivtData, const char* pathName)
{
    return ImmVibeSPI_IVTFile_Save(ivtData, pathName);
}

VIBE_EXPORT VibeStatus ImmVibeDeleteIVTFile(const char* pathName)
{
    return ImmVibeSPI_IVTFile_Delete(pathName);
}

VIBE_EXPORT VibeStatus ImmVibeDownloadIVTData(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 size)
{
    (void)deviceHandle; (void)ivtData; (void)size;
    return (g_nVibeAPIReferenceCount > 0) ? STATUS_OK : STATUS_FAILED_TO_INIT;
}

VIBE_EXPORT VibeStatus ImmVibeLoadIVTFromXIVT(VibeInt32 deviceHandle, VibeInt32 xivtSize, const VibeUInt8* xivtData, VibeUInt8** outIvt)
{
    (void)deviceHandle; (void)xivtSize; (void)xivtData;
    if(!outIvt) return STATUS_WRONG_ARGUMENT;
    VibeUInt8* copy = (VibeUInt8*)malloc(g_EmulatorReferenceIVTSize);
    if(!copy) return STATUS_NOT_ENOUGH_MEMORY;
    memcpy(copy, g_EmulatorReferenceIVT, g_EmulatorReferenceIVTSize);
    *outIvt = copy;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeUnloadIVTFromXIVT(VibeUInt8* ivtData)
{
    free(ivtData);
    return STATUS_OK;
}

};
