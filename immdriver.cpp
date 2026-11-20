#include "vibeapi.h"

static VibroEffectSlot g_Slots[VIBRO_MAX_EFFECT_SLOTS];
static VibeInt32 g_nNextEffectId = 1;

static void RefreshSlot(VibroEffectSlot* slot, VibeInt64 nowMs)
{
    if(!slot->inUse || slot->state != VIBE_EFFECT_STATE_PLAYING) return;
    if(slot->durationMs == VIBE_TIME_INFINITE) return;

    if(nowMs - slot->startTimeMs >= slot->durationMs)
    {
        slot->state = VIBE_EFFECT_STATE_NOT_PLAYING;
        slot->remainingMs = 0;
        slot->inUse = false;
        if(g_nActiveHwHandle == slot->handle)
        {
            g_nActiveHwHandle = VIBE_INVALID_HANDLE;
        }
    }
}

extern "C"
{

VibeInt32 g_nActiveHwHandle = VIBE_INVALID_HANDLE;

void VibeDriverResetSlots()
{
    memset(g_Slots, 0, sizeof(g_Slots));
    g_nActiveHwHandle = VIBE_INVALID_HANDLE;
}

bool VibeDriverAnySlotPlaying()
{
    VibeInt64 now = VibeOSGetTimeMs();
    for(int i = 0; i < VIBRO_MAX_EFFECT_SLOTS; ++i)
    {
        RefreshSlot(&g_Slots[i], now);
        if(g_Slots[i].inUse && g_Slots[i].state == VIBE_EFFECT_STATE_PLAYING) return true;
    }
    return false;
}

VibroEffectSlot* VibeDriverAllocSlot()
{
    VibeInt64 now = VibeOSGetTimeMs();
    for(int i = 0; i < VIBRO_MAX_EFFECT_SLOTS; ++i)
    {
        RefreshSlot(&g_Slots[i], now);
        if(!g_Slots[i].inUse) return &g_Slots[i];
    }

    int oldest = 0;
    for(int i = 1; i < VIBRO_MAX_EFFECT_SLOTS; ++i)
    {
        if(g_Slots[i].startTimeMs < g_Slots[oldest].startTimeMs) oldest = i;
    }
    return &g_Slots[oldest];
}

VibroEffectSlot* VibeDriverFindSlot(VibeInt32 effectHandle)
{
    VibeInt64 now = VibeOSGetTimeMs();
    for(int i = 0; i < VIBRO_MAX_EFFECT_SLOTS; ++i)
    {
        RefreshSlot(&g_Slots[i], now);
        if(g_Slots[i].inUse && g_Slots[i].handle == effectHandle) return &g_Slots[i];
    }
    return NULL;
}

VibeInt32 VibeDriverNextEffectHandle()
{
    VibeInt32 handle = g_nNextEffectId++;
    if(g_nNextEffectId <= 0) g_nNextEffectId = 1;
    return handle;
}

void VibeDriverLinuxWriteOutputBuffer()
{
    // Android Vibrator API handles hardware output directly.
}

bool VibeDriverUpdate()
{
    return VibeDriverAnySlotPlaying();
}

};
