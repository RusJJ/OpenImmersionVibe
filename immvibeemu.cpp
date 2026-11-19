#include "vibeapi.h"

static int g_SdkInt = 0;
static bool g_CheckedAmplitudeControl = false;
static bool g_HasAmplitudeControl = false;
static pthread_mutex_t g_JniMutex = PTHREAD_MUTEX_INITIALIZER;

static JNIEnv* AcquireJNIEnv(bool* didAttach)
{
    *didAttach = false;
    if(!g_JavaVM) return NULL;

    JNIEnv* env = NULL;
    jint res = g_JavaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    if(res == JNI_EDETACHED)
    {
        if(g_JavaVM->AttachCurrentThread(&env, NULL) == 0)
        {
            *didAttach = true;
        }
        else
        {
            return NULL;
        }
    }
    else if(res != JNI_OK)
    {
        return NULL;
    }
    return env;
}

static void ReleaseJNIEnv(bool didAttach)
{
    if(didAttach && g_JavaVM)
    {
        g_JavaVM->DetachCurrentThread();
    }
}

static int GetAndroidSdkInt(JNIEnv* env)
{
    if(g_SdkInt > 0) return g_SdkInt;

    jclass buildVerCls = env->FindClass("android/os/Build$VERSION");
    if(buildVerCls)
    {
        jfieldID sdkField = env->GetStaticFieldID(buildVerCls, "SDK_INT", "I");
        if(sdkField) g_SdkInt = (int)env->GetStaticIntField(buildVerCls, sdkField);
        env->DeleteLocalRef(buildVerCls);
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return g_SdkInt;
}

static jobject ResolveContextLocked(JNIEnv* env)
{
    if(g_ContextObj) return g_ContextObj;
    if(g_Activity) return g_Activity;

    if(g_UnityPlayerClass)
    {
        jfieldID fid = env->GetStaticFieldID(g_UnityPlayerClass, "currentActivity", "Landroid/app/Activity;");
        if(fid)
        {
            jobject act = env->GetStaticObjectField(g_UnityPlayerClass, fid);
            if(act)
            {
                g_Activity = env->NewGlobalRef(act);
                env->DeleteLocalRef(act);
                return g_Activity;
            }
        }
        if(env->ExceptionCheck()) env->ExceptionClear();
    }
    return NULL;
}

static jobject FetchVibratorLocked(JNIEnv* env, jclass* outVibClass)
{
    jclass vibClass = env->FindClass("android/os/Vibrator");
    if(!vibClass)
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        *outVibClass = NULL;
        return NULL;
    }

    // Fast path: reuse cached global Vibrator reference (mirrors original libImmEmulatorJ.so)
    if(g_VibratorObject)
    {
        *outVibClass = vibClass;
        return g_VibratorObject;
    }

    jobject ctx = ResolveContextLocked(env);
    if(!ctx)
    {
        VIBELOGE("No Android Context available (g_ContextObj and UnityPlayer.currentActivity are NULL)");
        env->DeleteLocalRef(vibClass);
        *outVibClass = NULL;
        return NULL;
    }

    jclass ctxClass = env->FindClass("android/content/Context");
    if(!ctxClass)
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(vibClass);
        *outVibClass = NULL;
        return NULL;
    }

    jobject localVib = NULL;
    jmethodID getSysSvcMethod = env->GetMethodID(ctxClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    int sdk = GetAndroidSdkInt(env);

    // Android 12+ (API 31+): VibratorManager.getDefaultVibrator()
    if(sdk >= 31 && getSysSvcMethod)
    {
        jfieldID vmSvcField = env->GetStaticFieldID(ctxClass, "VIBRATOR_MANAGER_SERVICE", "Ljava/lang/String;");
        jclass vmClass = env->FindClass("android/os/VibratorManager");
        if(vmSvcField && vmClass)
        {
            jobject vmSvcName = env->GetStaticObjectField(ctxClass, vmSvcField);
            if(vmSvcName)
            {
                jobject vm = env->CallObjectMethod(ctx, getSysSvcMethod, vmSvcName);
                env->DeleteLocalRef(vmSvcName);
                if(vm && !env->ExceptionCheck())
                {
                    jmethodID getDefVib = env->GetMethodID(vmClass, "getDefaultVibrator", "()Landroid/os/Vibrator;");
                    if(getDefVib) localVib = env->CallObjectMethod(vm, getDefVib);
                    env->DeleteLocalRef(vm);
                }
            }
        }
        if(vmClass) env->DeleteLocalRef(vmClass);
        if(env->ExceptionCheck())
        {
            env->ExceptionClear();
            if(localVib)
            {
                env->DeleteLocalRef(localVib);
                localVib = NULL;
            }
        }
    }

    // Standard / fallback: Context.VIBRATOR_SERVICE
    if(!localVib && getSysSvcMethod)
    {
        jfieldID vibSvcField = env->GetStaticFieldID(ctxClass, "VIBRATOR_SERVICE", "Ljava/lang/String;");
        if(vibSvcField)
        {
            jobject vibSvcName = env->GetStaticObjectField(ctxClass, vibSvcField);
            if(vibSvcName)
            {
                localVib = env->CallObjectMethod(ctx, getSysSvcMethod, vibSvcName);
                env->DeleteLocalRef(vibSvcName);
            }
        }
    }

    env->DeleteLocalRef(ctxClass);

    if(env->ExceptionCheck())
    {
        VIBELOGE("Exception while retrieving Android Vibrator service");
        env->ExceptionClear();
        if(localVib)
        {
            env->DeleteLocalRef(localVib);
            localVib = NULL;
        }
    }

    if(!localVib)
    {
        VIBELOGE("getSystemService returned NULL for Vibrator");
        env->DeleteLocalRef(vibClass);
        *outVibClass = NULL;
        return NULL;
    }

    g_VibratorObject = env->NewGlobalRef(localVib);
    env->DeleteLocalRef(localVib);

    // Check Vibrator.hasAmplitudeControl() once on API 26+
    if(!g_CheckedAmplitudeControl && sdk >= 26)
    {
        g_CheckedAmplitudeControl = true;
        jmethodID hasAmpMethod = env->GetMethodID(vibClass, "hasAmplitudeControl", "()Z");
        if(hasAmpMethod)
        {
            g_HasAmplitudeControl = (env->CallBooleanMethod(g_VibratorObject, hasAmpMethod) == JNI_TRUE);
        }
        if(env->ExceptionCheck())
        {
            env->ExceptionClear();
            g_HasAmplitudeControl = false;
        }
    }

    *outVibClass = vibClass;
    return g_VibratorObject;
}

static VibeInt32 ScaleMagToAmp(VibeInt32 magnitude)
{
    if(magnitude <= 0) return 0;
    if(!g_HasAmplitudeControl) return -1; // VibrationEffect.DEFAULT_AMPLITUDE
    VibeInt32 amp = (magnitude * 255) / VIBE_MAX_MAGNITUDE;
    if(amp < 1) amp = 1;
    if(amp > 255) amp = 255;
    return amp;
}

extern "C"
{

VibeInt32 g_hDataMutex = -1;

void Emulator_SetJavaVM(JavaVM* vm)
{
    pthread_mutex_lock(&g_JniMutex);
    g_JavaVM = vm;
    if(vm)
    {
        JNIEnv* env = NULL;
        if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env)
        {
            jclass unityCls = env->FindClass("com/unity3d/player/UnityPlayer");
            if(unityCls)
            {
                if(g_UnityPlayerClass) env->DeleteGlobalRef(g_UnityPlayerClass);
                g_UnityPlayerClass = (jclass)env->NewGlobalRef(unityCls);
                env->DeleteLocalRef(unityCls);
            }
            else
            {
                env->ExceptionClear();
            }
            GetAndroidSdkInt(env);
        }
    }
    pthread_mutex_unlock(&g_JniMutex);
}

void Emulator_SetContext(JNIEnv* env, jobject context)
{
    if(!env) return;
    pthread_mutex_lock(&g_JniMutex);
    if(g_ContextObj)
    {
        env->DeleteGlobalRef(g_ContextObj);
        g_ContextObj = NULL;
    }
    if(g_VibratorObject)
    {
        env->DeleteGlobalRef(g_VibratorObject);
        g_VibratorObject = NULL;
    }
    g_CheckedAmplitudeControl = false;
    g_HasAmplitudeControl = false;
    if(context)
    {
        g_ContextObj = env->NewGlobalRef(context);
    }
    pthread_mutex_unlock(&g_JniMutex);
}

void Emulator_CacheVibrator(JNIEnv* env)
{
    if(!env) return;
    pthread_mutex_lock(&g_JniMutex);
    jclass vibClass = NULL;
    FetchVibratorLocked(env, &vibClass);
    if(vibClass) env->DeleteLocalRef(vibClass);
    pthread_mutex_unlock(&g_JniMutex);
}

void Emulator_ClearJNI(JNIEnv* env)
{
    pthread_mutex_lock(&g_JniMutex);
    if(env)
    {
        if(g_UnityPlayerClass) env->DeleteGlobalRef(g_UnityPlayerClass);
        if(g_ContextObj) env->DeleteGlobalRef(g_ContextObj);
        if(g_Activity) env->DeleteGlobalRef(g_Activity);
        if(g_VibratorObject) env->DeleteGlobalRef(g_VibratorObject);
    }
    g_UnityPlayerClass = NULL;
    g_ContextObj = NULL;
    g_Activity = NULL;
    g_VibratorObject = NULL;
    g_VibrateMethod = NULL;
    g_JavaVM = NULL;
    g_SdkInt = 0;
    g_CheckedAmplitudeControl = false;
    g_HasAmplitudeControl = false;
    pthread_mutex_unlock(&g_JniMutex);
}

VibeStatus EmuInitialize(VibeInt32 flag)
{
    (void)flag;
    if(g_nVibeAPIReferenceCount > 0)
    {
        ++g_nVibeAPIReferenceCount;
        return STATUS_OK;
    }

    g_hDataMutex = VibeOSCreateMutex("h");
    if(g_hDataMutex < 0)
    {
        return STATUS_FAILED_GENERIC;
    }

    if(VibeMMInitialize() < STATUS_OK)
    {
        VibeOSDestroyMutex(g_hDataMutex);
        g_hDataMutex = -1;
        return STATUS_FAILED_GENERIC;
    }

    g_bDisableEffects = false;
    g_nStrength = VIBE_MAX_MAGNITUDE;
    g_nMasterStrength = VIBE_MAX_MAGNITUDE;
    g_pBestEffectSet = g_EmulatorReferenceIVT;
    VibeDriverResetSlots();
    g_nVibeAPIReferenceCount = 1;
    return STATUS_OK;
}

VibeStatus EmuTerminate()
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_OK;

    --g_nVibeAPIReferenceCount;
    if(g_nVibeAPIReferenceCount == 0)
    {
        VibeDriverResetSlots();
        g_bDeviceOpen = false;
        VibeOSStopTimer();
        Emulator_CancelVibrate();
        VibeMMTerminate();
        if(g_hDataMutex >= 0)
        {
            VibeOSDestroyMutex(g_hDataMutex);
            g_hDataMutex = -1;
        }
    }
    return STATUS_OK;
}

VibeInt32 EmuGetDeviceCount()
{
    // The whole logic results in a 1.
    return 1;
}

VibeStatus EmuOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    if(!deviceHandle || deviceIndex < 0) return STATUS_WRONG_ARGUMENT;

    *deviceHandle = VIBE_INVALID_HANDLE;
    if(g_nVibeAPIReferenceCount <= 0)
    {
        return STATUS_FAILED_TO_INIT;
    }

    g_bDeviceOpen = true;
    *deviceHandle = 1;
    return STATUS_OK;
}

VibeStatus EmuCloseDevice(VibeInt32 deviceHandle)
{
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(deviceHandle != 1) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibeDriverResetSlots();
    g_bDeviceOpen = false;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);

    Emulator_CancelVibrate();
    return STATUS_OK;
}

VIBE_EXPORT void Emulator_Vibrate(VibeInt32 force)
{
    if(force <= 0) return;
    VibeInt64 durationMs = ((VibeInt64)force * 25LL) / 127LL;
    if(durationMs <= 0) durationMs = 1;
    VibeInt32 mag = (force * VIBE_MAX_MAGNITUDE) / 127;
    if(mag > VIBE_MAX_MAGNITUDE) mag = VIBE_MAX_MAGNITUDE;
    Emulator_VibrateWithMagnitude(durationMs, mag);
}

void Emulator_VibrateWithMagnitude(VibeInt64 durationMs, VibeInt32 magnitude)
{
    if(durationMs <= 0 || magnitude <= 0) return;
    if(durationMs > 10000) durationMs = 10000;

    bool didAttach = false;
    JNIEnv* env = AcquireJNIEnv(&didAttach);
    if(!env)
    {
        VIBELOGE("Emulator_VibrateWithMagnitude: g_JavaVM is NULL or thread attach failed");
        return;
    }

    pthread_mutex_lock(&g_JniMutex);
    jclass vibClass = NULL;
    jobject vibrator = FetchVibratorLocked(env, &vibClass);
    if(!vibrator || !vibClass)
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    bool vibrated = false;
    int sdk = GetAndroidSdkInt(env);

    // Android 8.0+ (API 26+): VibrationEffect.createOneShot
    if(sdk >= 26)
    {
        jclass veClass = env->FindClass("android/os/VibrationEffect");
        if(veClass)
        {
            jmethodID createOneShot = env->GetStaticMethodID(veClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
            jmethodID vibrateEffect = env->GetMethodID(vibClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
            if(createOneShot && vibrateEffect)
            {
                jobject effect = env->CallStaticObjectMethod(veClass, createOneShot, (jlong)durationMs, (jint)ScaleMagToAmp(magnitude));
                if(effect && !env->ExceptionCheck())
                {
                    env->CallVoidMethod(vibrator, vibrateEffect, effect);
                    if(!env->ExceptionCheck()) vibrated = true;
                    env->DeleteLocalRef(effect);
                }
            }
            env->DeleteLocalRef(veClass);
        }
        if(env->ExceptionCheck()) env->ExceptionClear();
    }

    // Legacy fallback: Vibrator.vibrate(long)
    if(!vibrated)
    {
        jmethodID vibrateLegacy = env->GetMethodID(vibClass, "vibrate", "(J)V");
        if(vibrateLegacy)
        {
            env->CallVoidMethod(vibrator, vibrateLegacy, (jlong)durationMs);
            if(env->ExceptionCheck())
            {
                VIBELOGE("Vibrator.vibrate(long) threw an exception (check android.permission.VIBRATE)");
                env->ExceptionClear();
            }
        }
    }

    env->DeleteLocalRef(vibClass);
    pthread_mutex_unlock(&g_JniMutex);
    ReleaseJNIEnv(didAttach);
}

void Emulator_VibrateWaveform(const VibeInt64* timingsMs, const VibeInt32* magnitudes, VibeInt32 count, VibeInt32 repeatIndex)
{
    if(!timingsMs || !magnitudes || count <= 0) return;

    if(count == 1 && repeatIndex < 0)
    {
        Emulator_VibrateWithMagnitude(timingsMs[0], magnitudes[0]);
        return;
    }

    if(count > VIBRO_MAX_WAVEFORM_STEPS) count = VIBRO_MAX_WAVEFORM_STEPS;

    bool didAttach = false;
    JNIEnv* env = AcquireJNIEnv(&didAttach);
    if(!env)
    {
        VIBELOGE("Emulator_VibrateWaveform: g_JavaVM is NULL or thread attach failed");
        return;
    }

    pthread_mutex_lock(&g_JniMutex);
    jclass vibClass = NULL;
    jobject vibrator = FetchVibratorLocked(env, &vibClass);
    if(!vibrator || !vibClass)
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    bool vibrated = false;
    int sdk = GetAndroidSdkInt(env);

    // Android 8.0+ (API 26+): VibrationEffect.createWaveform
    if(sdk >= 26)
    {
        jclass veClass = env->FindClass("android/os/VibrationEffect");
        if(veClass)
        {
            jmethodID createWaveform = env->GetStaticMethodID(veClass, "createWaveform", "([J[II)Landroid/os/VibrationEffect;");
            jmethodID vibrateEffect = env->GetMethodID(vibClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
            if(createWaveform && vibrateEffect)
            {
                jlong jTimings[VIBRO_MAX_WAVEFORM_STEPS];
                jint jAmps[VIBRO_MAX_WAVEFORM_STEPS];
                for(VibeInt32 i = 0; i < count; ++i)
                {
                    jTimings[i] = (jlong)(timingsMs[i] > 0 ? timingsMs[i] : 1);
                    jAmps[i] = (jint)ScaleMagToAmp(magnitudes[i]);
                }

                jlongArray timingArr = env->NewLongArray((jsize)count);
                jintArray ampArr = env->NewIntArray((jsize)count);
                if(timingArr && ampArr)
                {
                    env->SetLongArrayRegion(timingArr, 0, (jsize)count, jTimings);
                    env->SetIntArrayRegion(ampArr, 0, (jsize)count, jAmps);
                    jobject effect = env->CallStaticObjectMethod(veClass, createWaveform, timingArr, ampArr, (jint)repeatIndex);
                    if(effect && !env->ExceptionCheck())
                    {
                        env->CallVoidMethod(vibrator, vibrateEffect, effect);
                        if(!env->ExceptionCheck()) vibrated = true;
                        env->DeleteLocalRef(effect);
                    }
                }
                if(timingArr) env->DeleteLocalRef(timingArr);
                if(ampArr) env->DeleteLocalRef(ampArr);
            }
            env->DeleteLocalRef(veClass);
        }
        if(env->ExceptionCheck()) env->ExceptionClear();
    }

    // Legacy fallback: Vibrator.vibrate(long[], int)
    if(!vibrated)
    {
        jmethodID vibratePat = env->GetMethodID(vibClass, "vibrate", "([JI)V");
        if(vibratePat)
        {
            jlong pattern[VIBRO_MAX_WAVEFORM_STEPS * 2];
            VibeInt32 pLen = 0;
            bool expectOff = true;
            for(VibeInt32 i = 0; i < count && pLen + 2 < VIBRO_MAX_WAVEFORM_STEPS * 2; ++i)
            {
                bool isOn = (magnitudes[i] > 0);
                if(expectOff && isOn)
                {
                    pattern[pLen++] = 0;
                    pattern[pLen++] = (jlong)timingsMs[i];
                }
                else if(expectOff && !isOn)
                {
                    pattern[pLen++] = (jlong)timingsMs[i];
                    expectOff = false;
                }
                else if(!expectOff && isOn)
                {
                    pattern[pLen++] = (jlong)timingsMs[i];
                    expectOff = true;
                }
                else if(pLen > 0)
                {
                    pattern[pLen - 1] += (jlong)timingsMs[i];
                }
            }
            if(pLen > 0)
            {
                jlongArray patArr = env->NewLongArray((jsize)pLen);
                if(patArr)
                {
                    env->SetLongArrayRegion(patArr, 0, (jsize)pLen, pattern);
                    env->CallVoidMethod(vibrator, vibratePat, patArr, (jint)(repeatIndex >= 0 ? 0 : -1));
                    env->DeleteLocalRef(patArr);
                }
            }
        }
        if(env->ExceptionCheck())
        {
            VIBELOGE("Vibrator.vibrate(long[], int) threw an exception");
            env->ExceptionClear();
        }
    }

    env->DeleteLocalRef(vibClass);
    pthread_mutex_unlock(&g_JniMutex);
    ReleaseJNIEnv(didAttach);
}

void Emulator_CancelVibrate()
{
    bool didAttach = false;
    JNIEnv* env = AcquireJNIEnv(&didAttach);
    if(!env) return;

    pthread_mutex_lock(&g_JniMutex);
    jclass vibClass = NULL;
    jobject vibrator = FetchVibratorLocked(env, &vibClass);
    if(vibrator && vibClass)
    {
        jmethodID cancelMethod = env->GetMethodID(vibClass, "cancel", "()V");
        if(cancelMethod) env->CallVoidMethod(vibrator, cancelMethod);
        if(env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(vibClass);
    }
    pthread_mutex_unlock(&g_JniMutex);

    ReleaseJNIEnv(didAttach);
}

};
