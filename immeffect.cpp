#include "vibeapi.h"

extern "C"
{

VibeStatus ImmVibeGetEffectState(VibeInt32 deviceHandle, VibeInt32 effect, VibeUInt8* state)
{
    return STATUS_OK;
}

VibeStatus ImmVibeStopPlayingEffect(VibeInt32 deviceHandle, VibeInt32 effect)
{
    return STATUS_OK;
}

VibeStatus ImmVibePlayUHLEffect(VibeInt32 deviceHandle, VibeInt32 effect)
{
    return STATUS_OK;
}

VibeStatus ImmVibeGetIVTEffectIndexFromName(VibeUInt8* ivtData, const char* effectName, VibeUInt8* index)
{
    // Not used in a project i develop.
    // Not gonna add this!
    return STATUS_OK;
}

};
