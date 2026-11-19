#include "vibeapi.h"

extern "C"
{

jobject g_Activity = NULL;
jobject g_ContextObj = NULL;
jclass g_UnityPlayerClass = NULL;
bool g_bEmulator = false;
JavaVM* g_JavaVM = NULL;
VibeInt32 g_nTSPVersion = 0;
VibeInt32 g_nVibeAPIReferenceCount = 0;
const VibeUInt8* g_pBestEffectSet = g_EmulatorReferenceIVT;

jobject g_VibratorObject = NULL;
jmethodID g_VibrateMethod = NULL;
pthread_mutex_t g_VibeDriverVibrateMutex = PTHREAD_MUTEX_INITIALIZER;

bool g_bDeviceOpen = false;
bool g_bDisableEffects = false;
VibeInt32 g_nStrength = VIBE_MAX_MAGNITUDE;
VibeInt32 g_nMasterStrength = VIBE_MAX_MAGNITUDE;
VibeInt32 g_nPriority = 0;
VibeInt32 g_nDebugLevel = 0;

extern VIBE_EXPORT const char g_szOEMName[] __attribute__((used)) = "OpenImmersionVibe by RusJJ";
extern VIBE_EXPORT const char g_szBuildDate[] __attribute__((used)) = "OpenImmersionVibe Build: " BUILD_DATE_STR " (RusJJ)";

const char* VibeStatusToString(VibeStatus status)
{
    switch(status)
    {
        case STATUS_OK:                       return "VIBE_S_SUCCESS";
        case STATUS_ALREADY_INITED:           return "VIBE_E_ALREADY_INITIALIZED";
        case STATUS_FAILED_TO_INIT:           return "VIBE_E_NOT_INITIALIZED";
        case STATUS_WRONG_ARGUMENT:           return "VIBE_E_INVALID_ARGUMENT";
        case STATUS_FAILED_GENERIC:           return "VIBE_E_FAIL";
        case STATUS_INCOMPATIBLE_EFFECT:      return "VIBE_E_INCOMPATIBLE_EFFECT_TYPE";
        case STATUS_INCOMPATIBLE_CAPABILITY:  return "VIBE_E_INCOMPATIBLE_CAPABILITY_TYPE";
        case STATUS_INCOMPATIBLE_PROPERTY:    return "VIBE_E_INCOMPATIBLE_PROPERTY_TYPE";
        case STATUS_NEEDS_LICENSE:            return "VIBE_E_DEVICE_NEEDS_LICENSE";
        case STATUS_NOT_ENOUGH_MEMORY:        return "VIBE_E_NOT_ENOUGH_MEMORY";
        case STATUS_SERVICE_NOT_RUNNING:      return "VIBE_E_SERVICE_NOT_RUNNING";
        case STATUS_INSUFFICIENT_PRIORITY:    return "VIBE_E_INSUFFICIENT_PRIORITY";
        case STATUS_SERVICE_BUSY:             return "VIBE_E_SERVICE_BUSY";
        case STATUS_NOT_SUPPORTED:            return "VIBE_E_NOT_SUPPORTED";
        default:                              return "Unknown Error";
    }
}

VibeInt32 DetectTSPVersion()
{
    // Modern Android uses Vibrator / VibratorManager instead of immvibed daemon.
    return 0;
}

VIBE_EXPORT VibeStatus ImmVibeInitialize(VibeInt32 apiVersion)
{
    (void)apiVersion;
    VibeInt32 tspDetected = DetectTSPVersion();
    if(tspDetected == 0)
    {
        g_bEmulator = true;
        g_nTSPVersion = 0;
        // Original libImmEmulatorJ.so passes 0x02000000 to EmuInitialize regardless of caller apiVersion
        return EmuInitialize(0x02000000);
    }
    return STATUS_FAILED_GENERIC;
}

VIBE_EXPORT VibeStatus ImmVibeInitialize2(VibeInt32 apiVersion, JavaVM* vm, jobject context)
{
    (void)apiVersion;
    if(vm)
    {
        Emulator_SetJavaVM(vm);
    }
    else
    {
        vm = g_JavaVM;
    }

    if(vm && context)
    {
        JNIEnv* env = NULL;
        bool didAttach = false;
        jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if(res == JNI_EDETACHED)
        {
            if(vm->AttachCurrentThread(&env, NULL) == 0) didAttach = true;
        }
        if(env)
        {
            Emulator_SetContext(env, context);
            Emulator_CacheVibrator(env);
        }
        if(didAttach) vm->DetachCurrentThread();
    }

    VibeStatus status = ImmVibeInitialize(0x02000000);
    VIBELOGD("CUHL::ImmVibeInitialize: found emulator version (%d)", g_nTSPVersion);
    return status;
}

VIBE_EXPORT VibeStatus ImmVibeTerminate2()
{
    if(g_bEmulator) return EmuTerminate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeTerminate()
{
    return ImmVibeTerminate2();
}

VIBE_EXPORT VibeStatus CUHLInitializeFromWatchdog(VibeInt32 apiVersion)
{
    return ImmVibeInitialize(apiVersion);
}

VIBE_EXPORT VibeStatus ImmVibeSetDebugLevel(VibeInt32 debugLevel)
{
    g_nDebugLevel = debugLevel;
    return STATUS_OK;
}

VIBE_EXPORT void ImmVibeJ_DEBUG(const char* fmt, ...)
{
    (void)fmt;
}

// Low-level SPI exports
VIBE_EXPORT VibeInt32 ImmVibeSPI_Device_GetNum()
{
    return 1;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 actuatorIndex, char* devName, int size)
{
    (void)actuatorIndex;
    if(!devName || size <= 0) return STATUS_WRONG_ARGUMENT;
    snprintf(devName, (size_t)size, "%s", "TS Emulator");
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_Initialize()
{
    return ImmVibeSPI_ForceOut_AmpDisable(0);
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_Terminate()
{
    return ImmVibeSPI_ForceOut_AmpDisable(0);
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 actuatorIndex)
{
    (void)actuatorIndex;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 actuatorIndex)
{
    (void)actuatorIndex;
    Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 actuatorIndex, VibeUInt16 bitDepth, VibeUInt16 bufferSizeInBytes, VibeInt8* forceOutputBuffer)
{
    (void)actuatorIndex;
    (void)bitDepth;
    if(!forceOutputBuffer || bufferSizeInBytes == 0) return STATUS_OK;

    VibeInt32 peak = 0;
    for(VibeUInt16 i = 0; i < bufferSizeInBytes; ++i)
    {
        VibeInt32 v = forceOutputBuffer[i];
        if(v < 0) v = -v;
        if(v > peak) peak = v;
    }
    if(peak > 0) Emulator_Vibrate(peak);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 actuatorIndex, VibeUInt16 freqParamId, VibeUInt32 freqParamValue)
{
    (void)actuatorIndex;
    (void)freqParamId;
    (void)freqParamValue;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_IVTFile_Save(const VibeUInt8* ivtData, const char* pathName)
{
    if(!ivtData || !pathName) return STATUS_WRONG_ARGUMENT;
    VibeInt32 sz = ImmVibeGetIVTSize(ivtData);
    if(sz <= 0) return STATUS_WRONG_ARGUMENT;

    FILE* fp = fopen(pathName, "wb");
    if(!fp) return STATUS_FAILED_GENERIC;
    size_t written = fwrite(ivtData, 1, (size_t)sz, fp);
    fclose(fp);
    return (written == (size_t)sz) ? STATUS_OK : STATUS_FAILED_GENERIC;
}

VIBE_EXPORT VibeStatus ImmVibeSPI_IVTFile_Delete(const char* pathName)
{
    if(!pathName) return STATUS_WRONG_ARGUMENT;
    return (remove(pathName) == 0) ? STATUS_OK : STATUS_FAILED_GENERIC;
}

VIBE_EXPORT void* ImmVibeSPI_Diag_BufPktAlloc(VibeInt32 size)
{
    return (size > 0) ? calloc(1, (size_t)size) : NULL;
}

};
