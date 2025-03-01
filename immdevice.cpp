#include "vibeapi.h"

extern "C" VibeStatus ImmVibe ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    return STATUS_OK;
}

extern "C" VibeStatus ImmVibe ImmVibeOpenDevice(VibeInt32* deviceHandle)
{
    return STATUS_OK;
}
