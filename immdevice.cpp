#include "vibeapi.h"

extern "C"
{

VibeStatus ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    return STATUS_OK;
}

VibeStatus ImmVibeCloseDevice(VibeInt32* deviceHandle)
{
    return STATUS_OK;
}

};