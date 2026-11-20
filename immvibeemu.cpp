#include "vibeapi.h"

#define MAX_GAMEPAD_VIBRATORS    4
#define GAMEPAD_SCAN_INTERVAL_MS 1000

// android.view.InputDevice source masks
#define ANDROID_SOURCE_GAMEPAD   0x00000401
#define ANDROID_SOURCE_JOYSTICK  0x01000010

struct GamepadVibratorEntry
{
    jobject obj;        // Global ref: android.os.VibratorManager (API 31+) or android.os.Vibrator (API < 31)
    bool isManager;
    bool hasAmpControl;
};

static int g_SdkInt = 0;
static bool g_CheckedAmplitudeControl = false;
static bool g_HasAmplitudeControl = false;
static jobject g_MediaVibrationAttrs = NULL;

static GamepadVibratorEntry g_Gamepads[MAX_GAMEPAD_VIBRATORS];
static int g_GamepadCount = 0;
static bool g_GamepadScanInitialized = false;
static VibeInt64 g_LastGamepadScanMs = 0;

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

static jobject GetMediaVibrationAttributesLocked(JNIEnv* env)
{
    if(g_MediaVibrationAttrs) return g_MediaVibrationAttrs;

    jclass vaClass = env->FindClass("android/os/VibrationAttributes");
    if(vaClass)
    {
        jfieldID usageField = env->GetStaticFieldID(vaClass, "USAGE_MEDIA", "I");
        jmethodID createForUsage = env->GetStaticMethodID(vaClass, "createForUsage", "(I)Landroid/os/VibrationAttributes;");
        if(usageField && createForUsage)
        {
            jint usageMedia = env->GetStaticIntField(vaClass, usageField);
            jobject localAttrs = env->CallStaticObjectMethod(vaClass, createForUsage, usageMedia);
            if(localAttrs && !env->ExceptionCheck())
            {
                g_MediaVibrationAttrs = env->NewGlobalRef(localAttrs);
                env->DeleteLocalRef(localAttrs);
            }
        }
        env->DeleteLocalRef(vaClass);
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return g_MediaVibrationAttrs;
}

static VibeInt32 ScaleMagToAmp(VibeInt32 magnitude, bool hasAmpControl)
{
    if(magnitude <= 0) return 0;
    if(!hasAmpControl) return -1; // VibrationEffect.DEFAULT_AMPLITUDE
    VibeInt32 amp = (magnitude * 255) / VIBE_MAX_MAGNITUDE;
    if(amp < 1) amp = 1;
    if(amp > 255) amp = 255;
    return amp;
}

static jobject CreateOneShotEffectLocked(JNIEnv* env, VibeInt64 durationMs, VibeInt32 magnitude, bool hasAmpControl)
{
    jobject effect = NULL;
    jclass veClass = env->FindClass("android/os/VibrationEffect");
    if(veClass)
    {
        jmethodID createOneShot = env->GetStaticMethodID(veClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
        if(createOneShot)
        {
            effect = env->CallStaticObjectMethod(veClass, createOneShot, (jlong)durationMs, (jint)ScaleMagToAmp(magnitude, hasAmpControl));
            if(env->ExceptionCheck())
            {
                env->ExceptionClear();
                if(effect)
                {
                    env->DeleteLocalRef(effect);
                    effect = NULL;
                }
            }
        }
        env->DeleteLocalRef(veClass);
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return effect;
}

static jobject CreateWaveformEffectLocked(JNIEnv* env, const VibeInt64* timingsMs, const VibeInt32* magnitudes, VibeInt32 count, VibeInt32 repeatIndex, bool hasAmpControl)
{
    jobject effect = NULL;
    jclass veClass = env->FindClass("android/os/VibrationEffect");
    if(veClass)
    {
        jmethodID createWaveform = env->GetStaticMethodID(veClass, "createWaveform", "([J[II)Landroid/os/VibrationEffect;");
        if(createWaveform)
        {
            jlong jTimings[VIBRO_MAX_WAVEFORM_STEPS];
            jint jAmps[VIBRO_MAX_WAVEFORM_STEPS];
            for(VibeInt32 i = 0; i < count; ++i)
            {
                jTimings[i] = (jlong)(timingsMs[i] > 0 ? timingsMs[i] : 1);
                jAmps[i] = (jint)ScaleMagToAmp(magnitudes[i], hasAmpControl);
            }

            jlongArray timingArr = env->NewLongArray((jsize)count);
            jintArray ampArr = env->NewIntArray((jsize)count);
            if(timingArr && ampArr)
            {
                env->SetLongArrayRegion(timingArr, 0, (jsize)count, jTimings);
                env->SetIntArrayRegion(ampArr, 0, (jsize)count, jAmps);
                effect = env->CallStaticObjectMethod(veClass, createWaveform, timingArr, ampArr, (jint)repeatIndex);
                if(env->ExceptionCheck())
                {
                    env->ExceptionClear();
                    if(effect)
                    {
                        env->DeleteLocalRef(effect);
                        effect = NULL;
                    }
                }
            }
            if(timingArr) env->DeleteLocalRef(timingArr);
            if(ampArr) env->DeleteLocalRef(ampArr);
        }
        env->DeleteLocalRef(veClass);
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return effect;
}

static bool VibrateWithEffectLocked(JNIEnv* env, jobject vibrator, jclass vibClass, jobject effect, int sdk)
{
    // Android 13+ (API 33+): tag as USAGE_MEDIA so notification mute / DND does not silence game rumble
    if(sdk >= 33)
    {
        jobject attrs = GetMediaVibrationAttributesLocked(env);
        if(attrs)
        {
            jmethodID vibrateWithAttrs = env->GetMethodID(vibClass, "vibrate", "(Landroid/os/VibrationEffect;Landroid/os/VibrationAttributes;)V");
            if(vibrateWithAttrs)
            {
                env->CallVoidMethod(vibrator, vibrateWithAttrs, effect, attrs);
                if(!env->ExceptionCheck()) return true;
            }
            if(env->ExceptionCheck()) env->ExceptionClear();
        }
    }

    // Android 8.0+ (API 26+) or fallback
    jmethodID vibrateEffect = env->GetMethodID(vibClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
    if(vibrateEffect)
    {
        env->CallVoidMethod(vibrator, vibrateEffect, effect);
        if(!env->ExceptionCheck()) return true;
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return false;
}

static bool VibrateManagerWithEffectLocked(JNIEnv* env, jobject vm, jclass vmClass, jobject effect, int sdk)
{
    jclass cvClass = env->FindClass("android/os/CombinedVibration");
    if(!cvClass)
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }

    bool vibrated = false;
    jmethodID createParallel = env->GetStaticMethodID(cvClass, "createParallel", "(Landroid/os/VibrationEffect;)Landroid/os/CombinedVibration;");
    if(createParallel)
    {
        jobject combined = env->CallStaticObjectMethod(cvClass, createParallel, effect);
        if(combined && !env->ExceptionCheck())
        {
            // Android 13+ (API 33+): tag with USAGE_MEDIA VibrationAttributes
            if(sdk >= 33)
            {
                jobject attrs = GetMediaVibrationAttributesLocked(env);
                if(attrs)
                {
                    jmethodID vibrateWithAttrs = env->GetMethodID(vmClass, "vibrate", "(Landroid/os/CombinedVibration;Landroid/os/VibrationAttributes;)V");
                    if(vibrateWithAttrs)
                    {
                        env->CallVoidMethod(vm, vibrateWithAttrs, combined, attrs);
                        if(!env->ExceptionCheck()) vibrated = true;
                    }
                    if(env->ExceptionCheck()) env->ExceptionClear();
                }
            }

            // Android 12+ (API 31+) or fallback
            if(!vibrated)
            {
                jmethodID vibrateCombined = env->GetMethodID(vmClass, "vibrate", "(Landroid/os/CombinedVibration;)V");
                if(vibrateCombined)
                {
                    env->CallVoidMethod(vm, vibrateCombined, combined);
                    if(!env->ExceptionCheck()) vibrated = true;
                }
                if(env->ExceptionCheck()) env->ExceptionClear();
            }

            env->DeleteLocalRef(combined);
        }
    }

    env->DeleteLocalRef(cvClass);
    if(env->ExceptionCheck()) env->ExceptionClear();
    return vibrated;
}

static bool VibrateLegacyOneShotLocked(JNIEnv* env, jobject vibrator, jclass vibClass, VibeInt64 durationMs)
{
    jmethodID vibrateLegacy = env->GetMethodID(vibClass, "vibrate", "(J)V");
    if(vibrateLegacy)
    {
        env->CallVoidMethod(vibrator, vibrateLegacy, (jlong)durationMs);
        if(!env->ExceptionCheck()) return true;
        VIBELOGE("Vibrator.vibrate(long) threw an exception (check android.permission.VIBRATE)");
        env->ExceptionClear();
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return false;
}

static bool VibrateLegacyWaveformLocked(JNIEnv* env, jobject vibrator, jclass vibClass, const VibeInt64* timingsMs, const VibeInt32* magnitudes, VibeInt32 count, VibeInt32 repeatIndex)
{
    bool vibrated = false;
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
                if(!env->ExceptionCheck()) vibrated = true;
                else
                {
                    VIBELOGE("Vibrator.vibrate(long[], int) threw an exception");
                    env->ExceptionClear();
                }
                env->DeleteLocalRef(patArr);
            }
        }
    }
    if(env->ExceptionCheck()) env->ExceptionClear();
    return vibrated;
}

static bool CheckVibratorAmplitudeControlLocked(JNIEnv* env, jobject vibrator, jclass vibClass, int sdk)
{
    if(sdk < 26 || !vibrator || !vibClass) return false;
    bool hasAmp = false;
    jmethodID hasAmpMethod = env->GetMethodID(vibClass, "hasAmplitudeControl", "()Z");
    if(hasAmpMethod)
    {
        hasAmp = (env->CallBooleanMethod(vibrator, hasAmpMethod) == JNI_TRUE);
    }
    if(env->ExceptionCheck())
    {
        env->ExceptionClear();
        hasAmp = false;
    }
    return hasAmp;
}

static void ClearGamepadVibratorsLocked(JNIEnv* env)
{
    if(env)
    {
        for(int i = 0; i < g_GamepadCount; ++i)
        {
            if(g_Gamepads[i].obj) env->DeleteGlobalRef(g_Gamepads[i].obj);
            g_Gamepads[i].obj = NULL;
            g_Gamepads[i].isManager = false;
            g_Gamepads[i].hasAmpControl = false;
        }
    }
    else
    {
        for(int i = 0; i < MAX_GAMEPAD_VIBRATORS; ++i)
        {
            g_Gamepads[i].obj = NULL;
            g_Gamepads[i].isManager = false;
            g_Gamepads[i].hasAmpControl = false;
        }
    }
    g_GamepadCount = 0;
    g_GamepadScanInitialized = false;
    g_LastGamepadScanMs = 0;
}

// Scans InputDevice for connected rumble-capable gamepads (cached for GAMEPAD_SCAN_INTERVAL_MS).
// API 31+: InputDevice.getVibratorManager() (if getVibratorIds().length > 0)
// API 16-30 (or fallback): InputDevice.getVibrator() (if hasVibrator() == true)
static int RefreshGamepadVibratorsLocked(JNIEnv* env, int sdk, bool forceRescan)
{
    if(sdk > 0 && sdk < 16) return 0;

    VibeInt64 nowMs = VibeOSGetTimeMs();
    if(!forceRescan && g_GamepadScanInitialized && (nowMs - g_LastGamepadScanMs) < GAMEPAD_SCAN_INTERVAL_MS)
    {
        return g_GamepadCount;
    }

    ClearGamepadVibratorsLocked(env);
    g_GamepadScanInitialized = true;
    g_LastGamepadScanMs = nowMs;

    jclass inputDevCls = env->FindClass("android/view/InputDevice");
    if(!inputDevCls)
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        return 0;
    }

    jmethodID getDeviceIds = env->GetStaticMethodID(inputDevCls, "getDeviceIds", "()[I");
    jmethodID getDevice = env->GetStaticMethodID(inputDevCls, "getDevice", "(I)Landroid/view/InputDevice;");
    jmethodID getSources = env->GetMethodID(inputDevCls, "getSources", "()I");
    if(!getDeviceIds || !getDevice || !getSources)
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(inputDevCls);
        return 0;
    }

    jintArray idsArr = (jintArray)env->CallStaticObjectMethod(inputDevCls, getDeviceIds);
    if(!idsArr || env->ExceptionCheck())
    {
        if(env->ExceptionCheck()) env->ExceptionClear();
        if(idsArr) env->DeleteLocalRef(idsArr);
        env->DeleteLocalRef(inputDevCls);
        return 0;
    }

    jsize numIds = env->GetArrayLength(idsArr);
    jint* ids = (numIds > 0) ? env->GetIntArrayElements(idsArr, NULL) : NULL;
    if(ids)
    {
        jclass vmClass = (sdk >= 31) ? env->FindClass("android/os/VibratorManager") : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();
        jclass vibClass = env->FindClass("android/os/Vibrator");
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID getVibMgr = (sdk >= 31 && vmClass)
            ? env->GetMethodID(inputDevCls, "getVibratorManager", "()Landroid/os/VibratorManager;")
            : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID getVibIds = (vmClass)
            ? env->GetMethodID(vmClass, "getVibratorIds", "()[I")
            : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID getVibById = (vmClass)
            ? env->GetMethodID(vmClass, "getVibrator", "(I)Landroid/os/Vibrator;")
            : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID getDefVib = (vmClass)
            ? env->GetMethodID(vmClass, "getDefaultVibrator", "()Landroid/os/Vibrator;")
            : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID getVibLegacy = env->GetMethodID(inputDevCls, "getVibrator", "()Landroid/os/Vibrator;");
        if(env->ExceptionCheck()) env->ExceptionClear();

        jmethodID hasVibrator = (vibClass)
            ? env->GetMethodID(vibClass, "hasVibrator", "()Z")
            : NULL;
        if(env->ExceptionCheck()) env->ExceptionClear();

        for(jsize i = 0; i < numIds && g_GamepadCount < MAX_GAMEPAD_VIBRATORS; ++i)
        {
            jobject dev = env->CallStaticObjectMethod(inputDevCls, getDevice, ids[i]);
            if(!dev || env->ExceptionCheck())
            {
                if(env->ExceptionCheck()) env->ExceptionClear();
                if(dev) env->DeleteLocalRef(dev);
                continue;
            }

            jint sources = env->CallIntMethod(dev, getSources);
            if(env->ExceptionCheck())
            {
                env->ExceptionClear();
                env->DeleteLocalRef(dev);
                continue;
            }

            bool isGamepad = ((sources & ANDROID_SOURCE_GAMEPAD) == ANDROID_SOURCE_GAMEPAD) ||
                             ((sources & ANDROID_SOURCE_JOYSTICK) == ANDROID_SOURCE_JOYSTICK);
            if(!isGamepad)
            {
                env->DeleteLocalRef(dev);
                continue;
            }

            bool added = false;

            // Android 12+ (API 31+): InputDevice.getVibratorManager() -> CombinedVibration
            if(sdk >= 31 && getVibMgr && getVibIds)
            {
                jobject vm = env->CallObjectMethod(dev, getVibMgr);
                if(vm && !env->ExceptionCheck())
                {
                    jintArray vibIds = (jintArray)env->CallObjectMethod(vm, getVibIds);
                    if(vibIds && !env->ExceptionCheck())
                    {
                        jsize vibCount = env->GetArrayLength(vibIds);
                        jint firstVibId = 0;
                        if(vibCount > 0)
                        {
                            jint* vIds = env->GetIntArrayElements(vibIds, NULL);
                            if(vIds)
                            {
                                firstVibId = vIds[0];
                                env->ReleaseIntArrayElements(vibIds, vIds, JNI_ABORT);
                            }
                        }
                        env->DeleteLocalRef(vibIds);
                        if(vibCount > 0)
                        {
                            bool hasAmp = true;
                            if(vibClass)
                            {
                                jobject motorVib = NULL;
                                if(getVibById)
                                {
                                    motorVib = env->CallObjectMethod(vm, getVibById, firstVibId);
                                    if(env->ExceptionCheck())
                                    {
                                        env->ExceptionClear();
                                        if(motorVib) { env->DeleteLocalRef(motorVib); motorVib = NULL; }
                                    }
                                }
                                if(!motorVib && getDefVib)
                                {
                                    motorVib = env->CallObjectMethod(vm, getDefVib);
                                    if(env->ExceptionCheck())
                                    {
                                        env->ExceptionClear();
                                        if(motorVib) { env->DeleteLocalRef(motorVib); motorVib = NULL; }
                                    }
                                }
                                if(motorVib)
                                {
                                    hasAmp = CheckVibratorAmplitudeControlLocked(env, motorVib, vibClass, sdk);
                                    env->DeleteLocalRef(motorVib);
                                }
                            }

                            g_Gamepads[g_GamepadCount].obj = env->NewGlobalRef(vm);
                            g_Gamepads[g_GamepadCount].isManager = true;
                            g_Gamepads[g_GamepadCount].hasAmpControl = hasAmp;
                            ++g_GamepadCount;
                            added = true;
                        }
                    }
                    else if(vibIds)
                    {
                        env->DeleteLocalRef(vibIds);
                    }
                    env->DeleteLocalRef(vm);
                }
                if(env->ExceptionCheck()) env->ExceptionClear();
            }

            // Android 4.1 - 11 (API 16 - 30) fallback: InputDevice.getVibrator()
            if(!added && sdk < 31 && getVibLegacy && hasVibrator)
            {
                jobject vib = env->CallObjectMethod(dev, getVibLegacy);
                if(vib && !env->ExceptionCheck())
                {
                    jboolean hasVib = env->CallBooleanMethod(vib, hasVibrator);
                    if(!env->ExceptionCheck() && hasVib == JNI_TRUE)
                    {
                        bool hasAmp = CheckVibratorAmplitudeControlLocked(env, vib, vibClass, sdk);
                        g_Gamepads[g_GamepadCount].obj = env->NewGlobalRef(vib);
                        g_Gamepads[g_GamepadCount].isManager = false;
                        g_Gamepads[g_GamepadCount].hasAmpControl = hasAmp;
                        ++g_GamepadCount;
                    }
                    env->DeleteLocalRef(vib);
                }
                if(env->ExceptionCheck()) env->ExceptionClear();
            }

            env->DeleteLocalRef(dev);
        }

        if(vmClass) env->DeleteLocalRef(vmClass);
        if(vibClass) env->DeleteLocalRef(vibClass);
        env->ReleaseIntArrayElements(idsArr, ids, JNI_ABORT);
    }

    env->DeleteLocalRef(idsArr);
    env->DeleteLocalRef(inputDevCls);
    if(env->ExceptionCheck()) env->ExceptionClear();
    return g_GamepadCount;
}

static bool TryVibrateGamepadsOneShotLocked(JNIEnv* env, VibeInt64 durationMs, VibeInt32 magnitude, int sdk)
{
    if(RefreshGamepadVibratorsLocked(env, sdk, false) <= 0) return false;

    jclass vmClass = (sdk >= 31) ? env->FindClass("android/os/VibratorManager") : NULL;
    if(env->ExceptionCheck()) env->ExceptionClear();
    jclass vibClass = env->FindClass("android/os/Vibrator");
    if(env->ExceptionCheck()) env->ExceptionClear();

    bool anyVibrated = false;
    for(int i = 0; i < g_GamepadCount; ++i)
    {
        GamepadVibratorEntry& gp = g_Gamepads[i];
        if(!gp.obj) continue;

        if(gp.isManager && vmClass)
        {
            jobject effect = CreateOneShotEffectLocked(env, durationMs, magnitude, gp.hasAmpControl);
            if(effect)
            {
                if(VibrateManagerWithEffectLocked(env, gp.obj, vmClass, effect, sdk))
                {
                    anyVibrated = true;
                }
                env->DeleteLocalRef(effect);
            }
        }
        else if(!gp.isManager && vibClass)
        {
            bool ok = false;
            if(sdk >= 26)
            {
                jobject effect = CreateOneShotEffectLocked(env, durationMs, magnitude, gp.hasAmpControl);
                if(effect)
                {
                    ok = VibrateWithEffectLocked(env, gp.obj, vibClass, effect, sdk);
                    env->DeleteLocalRef(effect);
                }
            }
            if(!ok)
            {
                ok = VibrateLegacyOneShotLocked(env, gp.obj, vibClass, durationMs);
            }
            if(ok) anyVibrated = true;
        }
    }

    if(vmClass) env->DeleteLocalRef(vmClass);
    if(vibClass) env->DeleteLocalRef(vibClass);
    if(env->ExceptionCheck()) env->ExceptionClear();

    if(!anyVibrated)
    {
        // Cached gamepad(s) likely disconnected; force rescan next time and fall back to phone
        g_GamepadScanInitialized = false;
    }
    return anyVibrated;
}

static bool TryVibrateGamepadsWaveformLocked(JNIEnv* env, const VibeInt64* timingsMs, const VibeInt32* magnitudes, VibeInt32 count, VibeInt32 repeatIndex, int sdk)
{
    if(RefreshGamepadVibratorsLocked(env, sdk, false) <= 0) return false;

    jclass vmClass = (sdk >= 31) ? env->FindClass("android/os/VibratorManager") : NULL;
    if(env->ExceptionCheck()) env->ExceptionClear();
    jclass vibClass = env->FindClass("android/os/Vibrator");
    if(env->ExceptionCheck()) env->ExceptionClear();

    bool anyVibrated = false;
    for(int i = 0; i < g_GamepadCount; ++i)
    {
        GamepadVibratorEntry& gp = g_Gamepads[i];
        if(!gp.obj) continue;

        if(gp.isManager && vmClass)
        {
            jobject effect = CreateWaveformEffectLocked(env, timingsMs, magnitudes, count, repeatIndex, gp.hasAmpControl);
            if(effect)
            {
                if(VibrateManagerWithEffectLocked(env, gp.obj, vmClass, effect, sdk))
                {
                    anyVibrated = true;
                }
                env->DeleteLocalRef(effect);
            }
        }
        else if(!gp.isManager && vibClass)
        {
            bool ok = false;
            if(sdk >= 26)
            {
                jobject effect = CreateWaveformEffectLocked(env, timingsMs, magnitudes, count, repeatIndex, gp.hasAmpControl);
                if(effect)
                {
                    ok = VibrateWithEffectLocked(env, gp.obj, vibClass, effect, sdk);
                    env->DeleteLocalRef(effect);
                }
            }
            if(!ok)
            {
                ok = VibrateLegacyWaveformLocked(env, gp.obj, vibClass, timingsMs, magnitudes, count, repeatIndex);
            }
            if(ok) anyVibrated = true;
        }
    }

    if(vmClass) env->DeleteLocalRef(vmClass);
    if(vibClass) env->DeleteLocalRef(vibClass);
    if(env->ExceptionCheck()) env->ExceptionClear();

    if(!anyVibrated)
    {
        g_GamepadScanInitialized = false;
    }
    return anyVibrated;
}

static void CancelGamepadVibratorsLocked(JNIEnv* env, int sdk)
{
    if(g_GamepadCount <= 0) return;

    jclass vmClass = (sdk >= 31) ? env->FindClass("android/os/VibratorManager") : NULL;
    if(env->ExceptionCheck()) env->ExceptionClear();
    jclass vibClass = env->FindClass("android/os/Vibrator");
    if(env->ExceptionCheck()) env->ExceptionClear();

    jmethodID vmCancel = vmClass ? env->GetMethodID(vmClass, "cancel", "()V") : NULL;
    if(env->ExceptionCheck()) env->ExceptionClear();
    jmethodID vibCancel = vibClass ? env->GetMethodID(vibClass, "cancel", "()V") : NULL;
    if(env->ExceptionCheck()) env->ExceptionClear();

    for(int i = 0; i < g_GamepadCount; ++i)
    {
        GamepadVibratorEntry& gp = g_Gamepads[i];
        if(!gp.obj) continue;

        if(gp.isManager && vmCancel)
        {
            env->CallVoidMethod(gp.obj, vmCancel);
        }
        else if(!gp.isManager && vibCancel)
        {
            env->CallVoidMethod(gp.obj, vibCancel);
        }
        if(env->ExceptionCheck()) env->ExceptionClear();
    }

    if(vmClass) env->DeleteLocalRef(vmClass);
    if(vibClass) env->DeleteLocalRef(vibClass);
    if(env->ExceptionCheck()) env->ExceptionClear();
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
        g_HasAmplitudeControl = CheckVibratorAmplitudeControlLocked(env, g_VibratorObject, vibClass, sdk);
    }

    // Pre-cache USAGE_MEDIA attributes on API 33+
    if(sdk >= 33) GetMediaVibrationAttributesLocked(env);

    *outVibClass = vibClass;
    return g_VibratorObject;
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
    g_GamepadScanInitialized = false;
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
    ClearGamepadVibratorsLocked(env);
    if(env)
    {
        if(g_UnityPlayerClass) env->DeleteGlobalRef(g_UnityPlayerClass);
        if(g_ContextObj) env->DeleteGlobalRef(g_ContextObj);
        if(g_Activity) env->DeleteGlobalRef(g_Activity);
        if(g_VibratorObject) env->DeleteGlobalRef(g_VibratorObject);
        if(g_MediaVibrationAttrs) env->DeleteGlobalRef(g_MediaVibrationAttrs);
    }
    g_UnityPlayerClass = NULL;
    g_ContextObj = NULL;
    g_Activity = NULL;
    g_VibratorObject = NULL;
    g_MediaVibrationAttrs = NULL;
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
    int sdk = GetAndroidSdkInt(env);

    // 1. Prefer connected rumble-capable gamepad(s)
    if(TryVibrateGamepadsOneShotLocked(env, durationMs, magnitude, sdk))
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    // 2. Automatic fallback to phone's built-in vibrator
    jclass vibClass = NULL;
    jobject vibrator = FetchVibratorLocked(env, &vibClass);
    if(!vibrator || !vibClass)
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    bool vibrated = false;

    // Android 8.0+ (API 26+): VibrationEffect.createOneShot (+ USAGE_MEDIA on API 33+)
    if(sdk >= 26)
    {
        jobject effect = CreateOneShotEffectLocked(env, durationMs, magnitude, g_HasAmplitudeControl);
        if(effect)
        {
            vibrated = VibrateWithEffectLocked(env, vibrator, vibClass, effect, sdk);
            env->DeleteLocalRef(effect);
        }
    }

    // Legacy fallback: Vibrator.vibrate(long)
    if(!vibrated)
    {
        VibrateLegacyOneShotLocked(env, vibrator, vibClass, durationMs);
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
    int sdk = GetAndroidSdkInt(env);

    // 1. Prefer connected rumble-capable gamepad(s)
    if(TryVibrateGamepadsWaveformLocked(env, timingsMs, magnitudes, count, repeatIndex, sdk))
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    // 2. Automatic fallback to phone's built-in vibrator
    jclass vibClass = NULL;
    jobject vibrator = FetchVibratorLocked(env, &vibClass);
    if(!vibrator || !vibClass)
    {
        pthread_mutex_unlock(&g_JniMutex);
        ReleaseJNIEnv(didAttach);
        return;
    }

    bool vibrated = false;

    // Android 8.0+ (API 26+): VibrationEffect.createWaveform (+ USAGE_MEDIA on API 33+)
    if(sdk >= 26)
    {
        jobject effect = CreateWaveformEffectLocked(env, timingsMs, magnitudes, count, repeatIndex, g_HasAmplitudeControl);
        if(effect)
        {
            vibrated = VibrateWithEffectLocked(env, vibrator, vibClass, effect, sdk);
            env->DeleteLocalRef(effect);
        }
    }

    // Legacy fallback: Vibrator.vibrate(long[], int)
    if(!vibrated)
    {
        VibrateLegacyWaveformLocked(env, vibrator, vibClass, timingsMs, magnitudes, count, repeatIndex);
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
    int sdk = GetAndroidSdkInt(env);

    // Cancel any cached gamepad vibrators first
    CancelGamepadVibratorsLocked(env, sdk);

    // Also cancel phone vibrator if already fetched or no gamepad was active
    if(g_VibratorObject || g_GamepadCount == 0)
    {
        jclass vibClass = NULL;
        jobject vibrator = FetchVibratorLocked(env, &vibClass);
        if(vibrator && vibClass)
        {
            jmethodID cancelMethod = env->GetMethodID(vibClass, "cancel", "()V");
            if(cancelMethod) env->CallVoidMethod(vibrator, cancelMethod);
            if(env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(vibClass);
        }
    }

    pthread_mutex_unlock(&g_JniMutex);
    ReleaseJNIEnv(didAttach);
}

};
