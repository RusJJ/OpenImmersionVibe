#ifndef VIBE_API_H
#define VIBE_API_H



#include <stdint.h>
#include <jni.h>
#include <semaphore.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

// Types
typedef uint8_t VibeUInt8;
typedef int16_t VibeInt16;
typedef int32_t VibeInt32;
typedef int64_t VibeInt64;
typedef VibeInt32 VibeStatus;
typedef uintptr_t VibePointer;

// Macros
#define KILL_LOGS

#define BUILD_DATE_STR __DATE__

#define VERSION_MAJOR ( 5 )
#define VERSION_MINOR ( 0 )
#define VERSION_BUILD ( 0 )
#define VERSION_REVIS ( 11 )
#define VERSION_INT   ( VERSION_REVIS | (VERSION_BUILD << 8) | (VERSION_MINOR << 16) | (VERSION_MAJOR << 24) )

#define STATUS_OK              0
#define STATUS_ALREADY_INITED -1
#define STATUS_WRONG_ARGUMENT -3
#define STATUS_FAILED_GENERIC -4

#ifdef KILL_LOGS
    #define VIBELOGD(...)
#else
    #define VIBELOGD(...)          _android_log_print(ANDROID_LOG_DEBUG, "OpenImmVibe", __VA_ARGS__);
#endif

// Structs
struct ImmVibeMutex
{
    ImmVibeMutex() : name(NULL), mutex(), cond(), released(false) {}

    char* name;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool released;
};

// Externals
#ifdef __cplusplus
extern "C" {
#endif

// Variables
extern jobject g_Activity;
extern jobject g_ContextObj;
extern bool g_bEmulator;
extern JavaVM* g_JavaVM;
extern VibeInt32 g_nTSPVersion;
extern void* g_pBestEffectSet;
extern jobject g_VibratorObject;
extern jmethodID g_VibrateMethod;
extern sem_t mainSemaphore, renderSemaphore;
extern pthread_t g_MainLoopThread;
extern pthread_mutex_t g_VibeDriverUpdateThread, g_VibeDriverVibrateMutex;
extern pthread_cond_t g_VibeDriverUpdateCond;

// ImmDevice
VibeStatus ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle);
VibeStatus ImmVibeCloseDevice(VibeInt32* deviceHandle);
// ImmVibe
VibeInt32 DetectTSPVersion();
void* ImmVibeMainLoopThread(void* data);
VibeStatus ImmVibeInitialize2(int unused, JavaVM* vm, jobject context);
VibeStatus ImmVibeTerminate();
VibeStatus ImmVibeTerminate2();
// ImmEffect
VibeStatus ImmVibeGetEffectState(VibeInt32 deviceHandle, VibeInt32 effect, VibeUInt8* state);
VibeStatus ImmVibeStopPlayingEffect(VibeInt32 deviceHandle, VibeInt32 effect);
VibeStatus ImmVibePlayUHLEffect(VibeInt32 deviceHandle, VibeInt32 effect);
VibeStatus ImmVibeGetIVTEffectIndexFromName(VibeUInt8* ivtData, const char* effectName, VibeUInt8* index);
// ImmVibeEmu
VibeStatus EmuInitialize(VibeInt32 flag);
VibeInt32 EmuGetDeviceCount();
void Emulator_Vibrate(VibeInt32 doVibro);
// ImmOS
VibeInt32 VibeOSCreateMutex(const char* name);
void VibeOSDestroyMutex(VibeInt32 mutexNum);
VibeStatus VibeOSAcquireMutexWaitNoWait(VibeInt32 mutexNum, VibeInt32 time, VibeInt32 wait);
VibeStatus VibeOSAcquireMutexWait(VibeInt32 mutexNum, VibeInt32 time);
VibeStatus VibeOSAcquireMutex(VibeInt32 mutexNum);
VibeInt32 VibeOSCreateMutexAcquired(const char* name);
VibeStatus VibeOSAcquireMutexNoWait(VibeInt32 mutexNum);
void VibeOSReleaseMutex(VibeInt32 mutexNum);
void VibeOSTimerThread();
VibeStatus VibeOSStartTimer();
bool VibeOSIsTimerRunning();
VibeStatus VibeOSStopTimer();
VibeInt32 VibeOSGetTimeMs(__time_t seconds, __suseconds_t subseconds);
// ImmDriver
void VibeDriverLinuxWriteOutputBuffer();

#ifdef __cplusplus
}
#endif



#endif // VIBE_API_H
