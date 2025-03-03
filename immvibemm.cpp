#include "vibeapi.h"

extern "C"
{

VibeInt32 g_hMMMutex = -1;
VibeStatus VibeMMInitialize()
{
    g_hMMMutex = VibeOSCreateMutex("g");
    if(g_hMMMutex == -1) return STATUS_FAILED_GENERIC;
    return STATUS_OK;
}

};