#include "vibeapi.h"

extern "C"
{

static VibeInt32 g_hMMMutex = -1;

VibeStatus VibeMMInitialize()
{
    g_hMMMutex = VibeOSCreateMutex("g");
    if(g_hMMMutex < 0) return STATUS_FAILED_GENERIC;
    return STATUS_OK;
}

void VibeMMTerminate()
{
    if(g_hMMMutex >= 0)
    {
        VibeOSDestroyMutex(g_hMMMutex);
        g_hMMMutex = -1;
    }
}

};
