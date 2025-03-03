#include "vibeapi.h"

extern "C"
{

VibeStatus ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    if(g_bEmulator) return EmuOpenDevice(deviceIndex, deviceHandle);
    return STATUS_FAILED_TO_INIT;
}

VibeStatus ImmVibeCloseDevice(VibeInt32* deviceHandle)
{
    return STATUS_OK;
}

};