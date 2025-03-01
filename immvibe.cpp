#include "vibeapi.h"

jobject g_Activity = NULL;
jobject g_ContextObj = NULL;
bool g_Emulator = false;
JavaVM* g_JavaVM = NULL;
VibeInt32 g_nTSPVersion = 0;
void* g_pBestEffectSet = NULL; // unused

extern "C" VibeStatus ImmVibeInitialize2(int unused, JavaVM* vm, jobject activity)
{
    return 0;
}

extern "C" VibeStatus ImmVibeTerminate()
{
    return 0;
}

extern "C" VibeStatus ImmVibeTerminate2()
{
    return 0;
}
