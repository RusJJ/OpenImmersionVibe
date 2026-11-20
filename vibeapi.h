#ifndef VIBE_API_H
#define VIBE_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Types (fixed-width for arm32 & arm64)
typedef int8_t    VibeInt8;
typedef uint8_t   VibeUInt8;
typedef int16_t   VibeInt16;
typedef uint16_t  VibeUInt16;
typedef int32_t   VibeInt32;
typedef uint32_t  VibeUInt32;
typedef int64_t   VibeInt64;
typedef uint8_t   VibeBool;
typedef uint16_t  VibeWChar;
typedef VibeInt32 VibeStatus;
typedef uintptr_t VibePointer;

// Macros
#define KILL_LOGS

#define VIBE_EXPORT __attribute__((visibility("default")))

#define BUILD_DATE_STR __DATE__

#define VERSION_MAJOR ( 5 )
#define VERSION_MINOR ( 0 )
#define VERSION_BUILD ( 0 )
#define VERSION_REVIS ( 11 )
#define VERSION_INT   ( VERSION_REVIS | (VERSION_BUILD << 8) | (VERSION_MINOR << 16) | (VERSION_MAJOR << 24) )

#define STATUS_OK                       0
#define STATUS_TRUE                     1
#define STATUS_FALSE                    0
#define STATUS_ALREADY_INITED          -1
#define STATUS_FAILED_TO_INIT          -2
#define STATUS_WRONG_ARGUMENT          -3
#define STATUS_FAILED_GENERIC          -4
#define STATUS_INCOMPATIBLE_EFFECT     -5
#define STATUS_INCOMPATIBLE_CAPABILITY -6
#define STATUS_INCOMPATIBLE_PROPERTY   -7
#define STATUS_NEEDS_LICENSE           -8
#define STATUS_NOT_ENOUGH_MEMORY       -9
#define STATUS_SERVICE_NOT_RUNNING    -10
#define STATUS_INSUFFICIENT_PRIORITY  -11
#define STATUS_SERVICE_BUSY           -12
#define STATUS_NOT_SUPPORTED          -13

#define VIBE_MAX_MAGNITUDE             10000
#define VIBE_MIN_MAGNITUDE             0
#define VIBE_TIME_INFINITE             0x7FFFFFFF
#define VIBE_INVALID_HANDLE            -1

#define VIBE_EFFECT_STATE_NOT_PLAYING  0
#define VIBE_EFFECT_STATE_PLAYING      1
#define VIBE_EFFECT_STATE_PAUSED       2

#define VIBE_DEVICESTATE_ATTACHED      0x0001
#define VIBE_DEVICESTATE_BUSY          0x0002

#define VIBE_EFFECT_TYPE_PERIODIC      0
#define VIBE_EFFECT_TYPE_MAGSWEEP      1
#define VIBE_EFFECT_TYPE_TIMELINE      2
#define VIBE_EFFECT_TYPE_STREAMING     3
#define VIBE_EFFECT_TYPE_WAVEFORM      4

#define VIBE_DEVCAPTYPE_DEVICE_CATEGORY        0
#define VIBE_DEVCAPTYPE_MAX_NESTED_REPEATS     1
#define VIBE_DEVCAPTYPE_NUM_ACTUATORS          2
#define VIBE_DEVCAPTYPE_ACTUATOR_TYPE          3
#define VIBE_DEVCAPTYPE_NUM_EFFECT_SLOTS       4
#define VIBE_DEVCAPTYPE_SUPPORTED_STYLES       5
#define VIBE_DEVCAPTYPE_MIN_PERIOD             6
#define VIBE_DEVCAPTYPE_MAX_PERIOD             7
#define VIBE_DEVCAPTYPE_MAX_EFFECT_DURATION    8
#define VIBE_DEVCAPTYPE_SUPPORTED_EFFECTS      9
#define VIBE_DEVCAPTYPE_DEVICE_NAME           10
#define VIBE_DEVCAPTYPE_MAX_ENVELOPE_TIME     11
#define VIBE_DEVCAPTYPE_APIVERSIONNUMBER      12
#define VIBE_DEVCAPTYPE_MAX_IVT_SIZE_TETHERED 13
#define VIBE_DEVCAPTYPE_MAX_IVT_SIZE          14
#define VIBE_DEVCAPTYPE_EDITION_LEVEL         15

#define VIBE_DEVPROPTYPE_LICENSE_KEY           0
#define VIBE_DEVPROPTYPE_PRIORITY              1
#define VIBE_DEVPROPTYPE_DISABLE_EFFECTS       2
#define VIBE_DEVPROPTYPE_STRENGTH              3
#define VIBE_DEVPROPTYPE_MASTERSTRENGTH        4
#define VIBE_DEVPROPTYPE_DEBUG_OUT             5

#define VIBRO_MAX_EFFECT_SLOTS         16
#define VIBRO_MAX_WAVEFORM_STEPS       128

#ifdef KILL_LOGS
    #define VIBELOGD(...)
#else
    #define VIBELOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "OpenImmVibe", __VA_ARGS__)
#endif
#define VIBELOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenImmVibe", __VA_ARGS__)

// Structs
struct ImmVibeMutex
{
    ImmVibeMutex() : name(NULL), mutex(), cond(), locked(false) {}

    char* name;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool locked;
};

struct VibroEffectSlot
{
    bool inUse;
    VibeInt32 handle;
    VibeInt32 state;
    VibeInt32 effectType;
    VibeInt32 durationMs;
    VibeInt32 magnitude;
    VibeInt64 startTimeMs;
    VibeInt32 remainingMs;
};

struct IvtWaveformPlan
{
    VibeInt64 timings[VIBRO_MAX_WAVEFORM_STEPS];
    VibeInt32 amplitudes[VIBRO_MAX_WAVEFORM_STEPS];
    VibeInt32 count;
    VibeInt32 repeatIndex;
    VibeInt32 totalDurationMs;
    VibeInt32 peakMagnitude;
};

struct IvtBasisDef
{
    VibeInt32 durationMs;
    VibeInt32 magnitude;
    VibeInt32 period;
    VibeInt32 styleAndWave;
    VibeInt32 attackTime;
    VibeInt32 attackLevel;
    VibeInt32 fadeTime;
    VibeInt32 fadeLevel;
};

struct VibeEnhancedWaveformBuffer
{
    VibeInt32 format;
    VibeInt32 dataSize;
    VibeUInt8 data[1];
};

// Externals
#ifdef __cplusplus
extern "C" {
#endif

// Variables
extern jobject g_Activity;
extern jobject g_ContextObj;
extern jclass g_UnityPlayerClass;
extern bool g_bEmulator;
extern JavaVM* g_JavaVM;
extern VibeInt32 g_nTSPVersion;
extern VibeInt32 g_nVibeAPIReferenceCount;
extern const VibeUInt8* g_pBestEffectSet;
extern jobject g_VibratorObject;
extern jmethodID g_VibrateMethod;
extern pthread_mutex_t g_VibeDriverVibrateMutex;

extern bool g_bDeviceOpen;
extern bool g_bDisableEffects;
extern VibeInt32 g_nStrength;
extern VibeInt32 g_nMasterStrength;
extern VibeInt32 g_nPriority;
extern VibeInt32 g_nDebugLevel;
extern VibeInt32 g_hDataMutex;
extern VibeInt32 g_nActiveHwHandle;

extern const VibeUInt8 g_EmulatorReferenceIVT[];
extern const size_t g_EmulatorReferenceIVTSize;
extern VIBE_EXPORT const char g_szOEMName[];
extern VIBE_EXPORT const char g_szBuildDate[];

// Internal helpers
const char* VibeStatusToString(VibeStatus status);
VibeInt32 EmuComputeMagnitude(VibeInt32 rawMag);
VibeStatus EmuPlaySimpleEffect(VibeInt32 deviceHandle, VibeInt32 effectType, VibeInt32 durationMs, VibeInt32 magnitude, VibeInt32* effectHandle);
VibeStatus EmuModifyEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 durationMs, VibeInt32 magnitude);
void Emulator_SetJavaVM(JavaVM* vm);
void Emulator_SetContext(JNIEnv* env, jobject context);
void Emulator_CacheVibrator(JNIEnv* env);
void Emulator_ClearJNI(JNIEnv* env);
void Emulator_VibrateWithMagnitude(VibeInt64 durationMs, VibeInt32 magnitude);
void Emulator_VibrateWaveform(const VibeInt64* timingsMs, const VibeInt32* magnitudes, VibeInt32 count, VibeInt32 repeatIndex);
void Emulator_CancelVibrate();

// ImmIVT internal helpers
VibeStatus IvtGetEffectNameUtf16(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeWChar* outBuf, size_t outChars);
VibeStatus IvtGetEffectNameAscii(const VibeUInt8* ivtData, VibeInt32 effectIndex, char* outBuf, size_t outBufLen);
VibeStatus IvtGetEffectIndexFromNameU(const VibeUInt8* ivtData, const VibeWChar* effectNameU, size_t nameLenChars, VibeInt32* index);
VibeStatus IvtGetBasisDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, IvtBasisDef* outDef);
VibeStatus IvtBuildWaveformPlan(const VibeUInt8* ivtData, VibeInt32 effectIndex, IvtWaveformPlan* outPlan);

// ImmVibe
VibeInt32 DetectTSPVersion();
VIBE_EXPORT VibeStatus ImmVibeInitialize(VibeInt32 apiVersion);
VIBE_EXPORT VibeStatus ImmVibeInitialize2(VibeInt32 apiVersion, JavaVM* vm, jobject context);
VIBE_EXPORT VibeStatus ImmVibeTerminate();
VIBE_EXPORT VibeStatus ImmVibeTerminate2();
VIBE_EXPORT VibeStatus CUHLInitializeFromWatchdog(VibeInt32 apiVersion);
VIBE_EXPORT VibeStatus ImmVibeSetDebugLevel(VibeInt32 debugLevel);
VIBE_EXPORT void ImmVibeJ_DEBUG(const char* fmt, ...);

// ImmDevice
VIBE_EXPORT VibeInt32 ImmVibeGetDeviceCount();
VIBE_EXPORT VibeStatus ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle);
VIBE_EXPORT VibeStatus ImmVibeOpenCompositeDevice(const VibeInt32* deviceIndices, VibeInt32 numDevices, VibeInt32* deviceHandle);
VIBE_EXPORT VibeStatus ImmVibeCloseDevice(VibeInt32 deviceHandle);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceState(VibeInt32 deviceHandle, VibeInt32* deviceState);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceActuatorIndices(VibeInt32 deviceIndex, VibeInt32* actuatorIndices, VibeInt32* numActuators);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityBool(VibeInt32 deviceIndex, VibeInt32 capType, VibeBool* capValue);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityInt32(VibeInt32 deviceIndex, VibeInt32 capType, VibeInt32* capValue);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityString(VibeInt32 deviceIndex, VibeInt32 capType, VibeInt32 size, char* capValue);
VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyBool(VibeInt32 deviceHandle, VibeInt32 propType, VibeBool* propValue);
VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyBool(VibeInt32 deviceHandle, VibeInt32 propType, VibeBool propValue);
VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyInt32(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32* propValue);
VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyInt32(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32 propValue);
VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyString(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32 size, char* propValue);
VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyString(VibeInt32 deviceHandle, VibeInt32 propType, const char* propValue);
VIBE_EXPORT VibeStatus ImmVibeGetDeviceKernelParameter(VibeInt32 deviceHandle, VibeInt32 paramId, VibeInt32* paramValue);
VIBE_EXPORT VibeStatus CUHLSetDeviceKernelParameter(VibeInt32 deviceHandle, VibeInt32 paramId, VibeInt32 paramValue);

// ImmEffect
VIBE_EXPORT VibeStatus ImmVibePlayUHLEffect(VibeInt32 deviceHandle, VibeInt32 effectIndex, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayIVTEffect(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayIVTEffectRepeat(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeUInt8 repeat, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayIVTInterpolatedEffect(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 interpolant, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayMagSweepEffect(VibeInt32 deviceHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 style, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayPeriodicEffect(VibeInt32 deviceHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 period, VibeInt32 styleAndWaveType, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayWaveformEffect(VibeInt32 deviceHandle, const VibeUInt8* data, VibeInt32 dataSize, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibeAppendWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* data, VibeInt32 dataSize, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, VibeInt32* outEffectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibeAppendEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* outEffectHandle);
VIBE_EXPORT VibeStatus ImmVibeReplaceEnhancedWaveformEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 sampleRate, VibeInt32 bitDepth, VibeInt32 magnitude, const void* enhancedData, VibeInt32* outEffectHandle);
VIBE_EXPORT VibeStatus ImmVibeModifyPlayingMagSweepEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 style, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel);
VIBE_EXPORT VibeStatus ImmVibeModifyPlayingPeriodicEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 duration, VibeInt32 magnitude, VibeInt32 period, VibeInt32 styleAndWaveType, VibeInt32 attackTime, VibeInt32 attackLevel, VibeInt32 fadeTime, VibeInt32 fadeLevel);
VIBE_EXPORT VibeStatus ImmVibeModifyPlayingInterpolatedEffectInterpolant(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32 interpolant);
VIBE_EXPORT VibeStatus ImmVibeStopPlayingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle);
VIBE_EXPORT VibeStatus ImmVibeStopAllPlayingEffects(VibeInt32 deviceHandle);
VIBE_EXPORT VibeStatus ImmVibePausePlayingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle);
VIBE_EXPORT VibeStatus ImmVibeResumePausedEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle);
VIBE_EXPORT VibeStatus ImmVibeGetEffectState(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32* state);
VIBE_EXPORT VibeStatus ImmVibeGetEffectRemainingDuration(VibeInt32 deviceHandle, VibeInt32 effectHandle, VibeInt32* remainingMs);
VIBE_EXPORT VibeStatus ImmVibeCreateStreamingEffect(VibeInt32 deviceHandle, VibeInt32* effectHandle);
VIBE_EXPORT VibeStatus ImmVibeDestroyStreamingEffect(VibeInt32 deviceHandle, VibeInt32 effectHandle);
VIBE_EXPORT VibeStatus ImmVibePlayStreamingSample(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* sample, VibeInt32 size);
VIBE_EXPORT VibeStatus ImmVibePlayStreamingSampleWithOffset(VibeInt32 deviceHandle, VibeInt32 effectHandle, const VibeUInt8* sample, VibeInt32 size, VibeInt32 offsetTime);

// ImmIVT
VIBE_EXPORT VibeInt32 ImmVibeGetIVTEffectCount(const VibeUInt8* ivtData);
VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectName(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 size, char* effectName);
VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectNameU(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 size, VibeWChar* effectName);
VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectIndexFromName(const VibeUInt8* ivtData, const char* effectName, VibeInt32* index);
VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectIndexFromNameU(const VibeUInt8* ivtData, const VibeWChar* effectName, VibeInt32* index);
VIBE_EXPORT VibeInt32 ImmVibeGetIVTEffectType(const VibeUInt8* ivtData, VibeInt32 effectIndex);
VIBE_EXPORT VibeStatus ImmVibeGetIVTEffectDuration(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration);
VIBE_EXPORT VibeStatus ImmVibeGetIVTInterpolatedEffectDuration(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32 interpolant, VibeInt32* duration);
VIBE_EXPORT VibeStatus ImmVibeGetIVTMagSweepEffectDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration, VibeInt32* magnitude, VibeInt32* style, VibeInt32* attackTime, VibeInt32* attackLevel, VibeInt32* fadeTime, VibeInt32* fadeLevel);
VIBE_EXPORT VibeStatus ImmVibeGetIVTPeriodicEffectDefinition(const VibeUInt8* ivtData, VibeInt32 effectIndex, VibeInt32* duration, VibeInt32* magnitude, VibeInt32* period, VibeInt32* styleAndWaveType, VibeInt32* attackTime, VibeInt32* attackLevel, VibeInt32* fadeTime, VibeInt32* fadeLevel);
VIBE_EXPORT VibeInt32 ImmVibeGetIVTSize(const VibeUInt8* ivtData);
VIBE_EXPORT VibeInt32 ImmVibeGetStringLengthU(const VibeWChar* str);
VIBE_EXPORT VibeStatus ImmVibeInitializeIVTBuffer(VibeUInt8* ivtData, VibeInt32 bufferSize);
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element);
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement2(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element);
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement3(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element);
VIBE_EXPORT VibeStatus ImmVibeInsertIVTElement4(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, const void* element);
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element);
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement2(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element);
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement3(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element);
VIBE_EXPORT VibeStatus ImmVibeReadIVTElement4(const VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex, void* element);
VIBE_EXPORT VibeStatus ImmVibeRemoveIVTElement(VibeUInt8* ivtData, VibeInt32 bufferSize, VibeInt32 timelineIndex, VibeInt32 elementIndex);
VIBE_EXPORT VibeStatus ImmVibeSaveIVTFile(const VibeUInt8* ivtData, const char* pathName);
VIBE_EXPORT VibeStatus ImmVibeDeleteIVTFile(const char* pathName);
VIBE_EXPORT VibeStatus ImmVibeDownloadIVTData(VibeInt32 deviceHandle, const VibeUInt8* ivtData, VibeInt32 size);
VIBE_EXPORT VibeStatus ImmVibeLoadIVTFromXIVT(VibeInt32 deviceHandle, VibeInt32 xivtSize, const VibeUInt8* xivtData, VibeUInt8** outIvt);
VIBE_EXPORT VibeStatus ImmVibeUnloadIVTFromXIVT(VibeUInt8* ivtData);

// ImmVibeEmu
VibeStatus EmuInitialize(VibeInt32 flag);
VibeStatus EmuTerminate();
VibeInt32 EmuGetDeviceCount();
VibeStatus EmuOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle);
VibeStatus EmuCloseDevice(VibeInt32 deviceHandle);
VIBE_EXPORT void Emulator_Vibrate(VibeInt32 force);

// ImmSPI
VIBE_EXPORT VibeInt32 ImmVibeSPI_Device_GetNum();
VIBE_EXPORT VibeStatus ImmVibeSPI_Device_GetName(VibeUInt8 actuatorIndex, char* devName, int size);
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_Initialize();
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_Terminate();
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_AmpEnable(VibeUInt8 actuatorIndex);
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_AmpDisable(VibeUInt8 actuatorIndex);
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_SetSamples(VibeUInt8 actuatorIndex, VibeUInt16 bitDepth, VibeUInt16 bufferSizeInBytes, VibeInt8* forceOutputBuffer);
VIBE_EXPORT VibeStatus ImmVibeSPI_ForceOut_SetFrequency(VibeUInt8 actuatorIndex, VibeUInt16 freqParamId, VibeUInt32 freqParamValue);
VIBE_EXPORT VibeStatus ImmVibeSPI_IVTFile_Save(const VibeUInt8* ivtData, const char* pathName);
VIBE_EXPORT VibeStatus ImmVibeSPI_IVTFile_Delete(const char* pathName);
VIBE_EXPORT void* ImmVibeSPI_Diag_BufPktAlloc(VibeInt32 size);

// ImmOS
VibeInt32 VibeOSCreateMutex(const char* name);
void VibeOSDestroyMutex(VibeInt32 mutexNum);
VibeStatus VibeOSAcquireMutexWaitNoWait(VibeInt32 mutexNum, VibeInt32 time, VibeInt32 wait);
VibeStatus VibeOSAcquireMutexWait(VibeInt32 mutexNum, VibeInt32 time);
VibeStatus VibeOSAcquireMutex(VibeInt32 mutexNum);
VibeInt32 VibeOSCreateMutexAcquired(const char* name);
VibeStatus VibeOSAcquireMutexNoWait(VibeInt32 mutexNum);
void VibeOSReleaseMutex(VibeInt32 mutexNum);
VibeStatus VibeOSStartTimer();
bool VibeOSIsTimerRunning();
VibeStatus VibeOSStopTimer();
VibeInt64 VibeOSGetTimeMs();

// ImmDriver
void VibeDriverResetSlots();
bool VibeDriverAnySlotPlaying();
VibroEffectSlot* VibeDriverAllocSlot();
VibroEffectSlot* VibeDriverFindSlot(VibeInt32 effectHandle);
VibeInt32 VibeDriverNextEffectHandle();
void VibeDriverLinuxWriteOutputBuffer();
bool VibeDriverUpdate();

// ImmVibeMM
VibeStatus VibeMMInitialize();
void VibeMMTerminate();

#ifdef __cplusplus
}
#endif

#endif // VIBE_API_H
