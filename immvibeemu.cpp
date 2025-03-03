#include "vibeapi.h"

extern "C"
{

VibeInt32 g_hDataMutex;
VibeStatus EmuInitialize(VibeInt32 flag)
{
    if((flag & 0x7FFF0000) > 0x5000000 || flag < 0)
    {
        // huh?
        return STATUS_WRONG_ARGUMENT;
    }

    if(g_nVibeAPIReferenceCount)
    {
        ++g_nVibeAPIReferenceCount;
        return STATUS_OK;
    }

    g_hDataMutex = VibeOSCreateMutex("h");
    if(g_hDataMutex == -1)
    {
        return STATUS_FAILED_GENERIC;
    }

    if(VibeMMInitialize() < STATUS_OK)
    {
        // TODO: Terminate
    }

    // TODO:
  
    return STATUS_OK;
}

VibeInt32 EmuGetDeviceCount()
{
    // The whole logic results in a 1,
    // negative nums if there is an error.
    return 1;
}

void Emulator_Vibrate(VibeInt32 durationMs)
{
    if(durationMs && g_VibratorObject && g_VibrateMethod)
    {
        JNIEnv* env;
        pthread_mutex_lock(&g_VibeDriverVibrateMutex);
        g_JavaVM->AttachCurrentThread(&env, NULL);
        jlong durationLongMs = 25 * durationMs / 127;
        env->CallVoidMethod(g_VibratorObject, g_VibrateMethod, durationLongMs);
        pthread_mutex_unlock(&g_VibeDriverVibrateMutex);
    }
}

VibeStatus EmuOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    *deviceHandle = -1;
    if(!g_nVibeAPIReferenceCount)
    {
        return STATUS_FAILED_TO_INIT;
    }

    // TODO:

    return STATUS_OK;
}

};
