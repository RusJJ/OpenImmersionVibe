#include "vibeapi.h"

static jfieldID g_nInitializedFieldID = NULL;
static jfieldID g_nDeviceFieldID = NULL;

static void ThrowByName(JNIEnv* env, const char* name, const char* msg)
{
    jclass cls = env->FindClass(name);
    if(cls)
    {
        env->ThrowNew(cls, msg ? msg : "ImmVibe error");
        env->DeleteLocalRef(cls);
    }
}

static void ThrowIfError(JNIEnv* env, VibeStatus status)
{
    if(status < STATUS_OK)
    {
        ThrowByName(env, "java/lang/RuntimeException", VibeStatusToString(status));
    }
}

static void CacheImmVibeFields(JNIEnv* env, jobject thiz)
{
    if(!thiz) return;
    if(g_nInitializedFieldID && g_nDeviceFieldID) return;

    jclass cls = env->GetObjectClass(thiz);
    if(!cls) return;

    if(!g_nInitializedFieldID)
    {
        g_nInitializedFieldID = env->GetFieldID(cls, "m_bInitialized", "Z");
        if(env->ExceptionCheck()) env->ExceptionClear();
    }
    if(!g_nDeviceFieldID)
    {
        g_nDeviceFieldID = env->GetFieldID(cls, "m_hDevice", "I");
        if(env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
}

static VibeInt32 GetObjDeviceHandle(JNIEnv* env, jobject thiz)
{
    CacheImmVibeFields(env, thiz);
    if(thiz && g_nDeviceFieldID) return (VibeInt32)env->GetIntField(thiz, g_nDeviceFieldID);
    return 1;
}

static VibeEnhancedWaveformBuffer* PackEnhancedBuf(JNIEnv* env, jbyteArray data, jint format)
{
    if(!data) return NULL;
    jsize len = env->GetArrayLength(data);
    if(len < 0) return NULL;

    VibeEnhancedWaveformBuffer* buf = (VibeEnhancedWaveformBuffer*)malloc(8u + (size_t)len);
    if(!buf) return NULL;
    buf->format = (VibeInt32)format;
    buf->dataSize = (VibeInt32)len;

    jbyte* bytes = env->GetByteArrayElements(data, NULL);
    if(bytes && len > 0) memcpy(buf->data, bytes, (size_t)len);
    if(bytes) env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    return buf;
}

static jbyteArray MakeBuiltInIVTByteArray(JNIEnv* env)
{
    jbyteArray arr = env->NewByteArray((jsize)g_EmulatorReferenceIVTSize);
    if(arr)
    {
        env->SetByteArrayRegion(arr, 0, (jsize)g_EmulatorReferenceIVTSize, (const jbyte*)g_EmulatorReferenceIVT);
    }
    return arr;
}

extern "C"
{

// JNI lifecycle
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    Emulator_SetJavaVM(vm);
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved)
{
    (void)reserved;
    JNIEnv* env = NULL;
    if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) Emulator_ClearJNI(env);
    else Emulator_ClearJNI(NULL);
}

// ImmVibe lifecycle
JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_Initialize(JNIEnv* env, jobject thiz)
{
    VibeStatus status = ImmVibeInitialize(VERSION_INT);
    CacheImmVibeFields(env, thiz);
    if(status >= STATUS_OK && thiz && g_nInitializedFieldID) env->SetBooleanField(thiz, g_nInitializedFieldID, JNI_TRUE);
    ThrowIfError(env, status);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_Initialize2(JNIEnv* env, jobject thiz, jobject context)
{
    if(context)
    {
        Emulator_SetContext(env, context);
        Emulator_CacheVibrator(env);
    }
    VibeStatus status = ImmVibeInitialize(VERSION_INT);
    CacheImmVibeFields(env, thiz);
    if(status >= STATUS_OK && thiz && g_nInitializedFieldID) env->SetBooleanField(thiz, g_nInitializedFieldID, JNI_TRUE);
    ThrowIfError(env, status);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_Terminate(JNIEnv* env, jobject thiz)
{
    ImmVibeTerminate();
    CacheImmVibeFields(env, thiz);
    if(thiz && g_nInitializedFieldID) env->SetBooleanField(thiz, g_nInitializedFieldID, JNI_FALSE);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_Terminate2(JNIEnv* env, jobject thiz)
{
    (void)env; (void)thiz;
    ImmVibeTerminate();
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetTSVersion(JNIEnv* env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)VERSION_INT;
}

JNIEXPORT jboolean JNICALL Java_com_immersion_uhl_internal_ImmVibe_IsEmulator(JNIEnv* env, jobject thiz)
{
    (void)env; (void)thiz;
    return JNI_TRUE;
}

// Device & properties
JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceCount(JNIEnv* env, jobject thiz)
{
    (void)env; (void)thiz;
    return (jint)ImmVibeGetDeviceCount();
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceState(JNIEnv* env, jobject thiz, jint deviceIndex)
{
    (void)deviceIndex;
    VibeInt32 state = VIBE_DEVICESTATE_ATTACHED;
    ThrowIfError(env, ImmVibeGetDeviceState(GetObjDeviceHandle(env, thiz), &state));
    return (jint)state;
}

JNIEXPORT jboolean JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceCapabilityBool(JNIEnv* env, jobject thiz, jint deviceIndex, jint capType)
{
    (void)thiz;
    VibeBool val = STATUS_FALSE;
    ThrowIfError(env, ImmVibeGetDeviceCapabilityBool((VibeInt32)deviceIndex, (VibeInt32)capType, &val));
    return val ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceCapabilityInt32(JNIEnv* env, jobject thiz, jint deviceIndex, jint capType)
{
    (void)thiz;
    VibeInt32 val = 0;
    ThrowIfError(env, ImmVibeGetDeviceCapabilityInt32((VibeInt32)deviceIndex, (VibeInt32)capType, &val));
    return (jint)val;
}

JNIEXPORT jstring JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceCapabilityString(JNIEnv* env, jobject thiz, jint deviceIndex, jint capType)
{
    (void)thiz;
    char buf[128];
    ThrowIfError(env, ImmVibeGetDeviceCapabilityString((VibeInt32)deviceIndex, (VibeInt32)capType, (VibeInt32)sizeof(buf), buf));
    return env->NewStringUTF(buf);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_OpenDevice(JNIEnv* env, jobject thiz, jint deviceIndex, jstring licenseKey)
{
    (void)licenseKey;
    VibeInt32 hDevice = VIBE_INVALID_HANDLE;
    VibeStatus status = ImmVibeOpenDevice((VibeInt32)deviceIndex, &hDevice);
    CacheImmVibeFields(env, thiz);
    if(status == STATUS_OK && thiz && g_nDeviceFieldID) env->SetIntField(thiz, g_nDeviceFieldID, (jint)hDevice);
    ThrowIfError(env, status);
    return (jint)hDevice;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_OpenDevice2(JNIEnv* env, jobject thiz, jint deviceIndex)
{
    (void)thiz;
    VibeInt32 hDevice = VIBE_INVALID_HANDLE;
    ThrowIfError(env, ImmVibeOpenDevice((VibeInt32)deviceIndex, &hDevice));
    return (jint)hDevice;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_OpenCompositeDevice(JNIEnv* env, jobject thiz, jintArray deviceIndices, jint numDevices, jstring licenseKey)
{
    (void)deviceIndices; (void)numDevices;
    return Java_com_immersion_uhl_internal_ImmVibe_OpenDevice(env, thiz, 0, licenseKey);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_OpenCompositeDevice2(JNIEnv* env, jobject thiz, jintArray deviceIndices, jint numDevices)
{
    (void)deviceIndices; (void)numDevices;
    return Java_com_immersion_uhl_internal_ImmVibe_OpenDevice2(env, thiz, 0);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_CloseDevice(JNIEnv* env, jobject thiz, jint hDevice)
{
    VibeStatus status = ImmVibeCloseDevice((VibeInt32)hDevice);
    CacheImmVibeFields(env, thiz);
    if(thiz && g_nDeviceFieldID) env->SetIntField(thiz, g_nDeviceFieldID, (jint)VIBE_INVALID_HANDLE);
    ThrowIfError(env, status);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_CloseDevice2(JNIEnv* env, jobject thiz, jint hDevice)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeCloseDevice((VibeInt32)hDevice));
}

JNIEXPORT jboolean JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDevicePropertyBool(JNIEnv* env, jobject thiz, jint hDevice, jint propType)
{
    (void)thiz;
    VibeBool val = STATUS_FALSE;
    ThrowIfError(env, ImmVibeGetDevicePropertyBool((VibeInt32)hDevice, (VibeInt32)propType, &val));
    return val ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_SetDevicePropertyBool(JNIEnv* env, jobject thiz, jint hDevice, jint propType, jboolean val)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeSetDevicePropertyBool((VibeInt32)hDevice, (VibeInt32)propType, (val == JNI_TRUE) ? STATUS_TRUE : STATUS_FALSE));
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDevicePropertyInt32(JNIEnv* env, jobject thiz, jint hDevice, jint propType)
{
    (void)thiz;
    VibeInt32 val = 0;
    ThrowIfError(env, ImmVibeGetDevicePropertyInt32((VibeInt32)hDevice, (VibeInt32)propType, &val));
    return (jint)val;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_SetDevicePropertyInt32(JNIEnv* env, jobject thiz, jint hDevice, jint propType, jint val)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeSetDevicePropertyInt32((VibeInt32)hDevice, (VibeInt32)propType, (VibeInt32)val));
}

JNIEXPORT jstring JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDevicePropertyString(JNIEnv* env, jobject thiz, jint hDevice, jint propType)
{
    (void)thiz;
    char buf[128];
    ThrowIfError(env, ImmVibeGetDevicePropertyString((VibeInt32)hDevice, (VibeInt32)propType, (VibeInt32)sizeof(buf), buf));
    return env->NewStringUTF(buf);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_SetDevicePropertyString(JNIEnv* env, jobject thiz, jint hDevice, jint propType, jstring val)
{
    (void)thiz;
    const char* str = val ? env->GetStringUTFChars(val, NULL) : NULL;
    VibeStatus status = ImmVibeSetDevicePropertyString((VibeInt32)hDevice, (VibeInt32)propType, str);
    if(str) env->ReleaseStringUTFChars(val, str);
    ThrowIfError(env, status);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetDeviceKernelParameter(JNIEnv* env, jobject thiz, jint hDevice, jint paramId)
{
    (void)thiz;
    VibeInt32 val = 0;
    ThrowIfError(env, ImmVibeGetDeviceKernelParameter((VibeInt32)hDevice, (VibeInt32)paramId, &val));
    return (jint)val;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_SetDeviceKernelParameter(JNIEnv* env, jobject thiz, jint hDevice, jint paramId, jint val)
{
    (void)thiz;
    ThrowIfError(env, CUHLSetDeviceKernelParameter((VibeInt32)hDevice, (VibeInt32)paramId, (VibeInt32)val));
}

// Effect playback & control
JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayMagSweepEffect(JNIEnv* env, jobject thiz, jint hDevice, jint duration, jint magnitude, jint style, jint attackTime, jint attackLevel, jint fadeTime, jint fadeLevel)
{
    (void)thiz;
    VibeInt32 hEffect = 0;
    ThrowIfError(env, ImmVibePlayMagSweepEffect((VibeInt32)hDevice, (VibeInt32)duration, (VibeInt32)magnitude, (VibeInt32)style, (VibeInt32)attackTime, (VibeInt32)attackLevel, (VibeInt32)fadeTime, (VibeInt32)fadeLevel, &hEffect));
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayPeriodicEffect(JNIEnv* env, jobject thiz, jint hDevice, jint duration, jint magnitude, jint period, jint styleAndWaveType, jint attackTime, jint attackLevel, jint fadeTime, jint fadeLevel)
{
    (void)thiz;
    VibeInt32 hEffect = 0;
    ThrowIfError(env, ImmVibePlayPeriodicEffect((VibeInt32)hDevice, (VibeInt32)duration, (VibeInt32)magnitude, (VibeInt32)period, (VibeInt32)styleAndWaveType, (VibeInt32)attackTime, (VibeInt32)attackLevel, (VibeInt32)fadeTime, (VibeInt32)fadeLevel, &hEffect));
    return (jint)hEffect;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_ModifyPlayingMagSweepEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jint duration, jint magnitude, jint style, jint attackTime, jint attackLevel, jint fadeTime, jint fadeLevel)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeModifyPlayingMagSweepEffect((VibeInt32)hDevice, (VibeInt32)hEffect, (VibeInt32)duration, (VibeInt32)magnitude, (VibeInt32)style, (VibeInt32)attackTime, (VibeInt32)attackLevel, (VibeInt32)fadeTime, (VibeInt32)fadeLevel));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_ModifyPlayingPeriodicEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jint duration, jint magnitude, jint period, jint styleAndWaveType, jint attackTime, jint attackLevel, jint fadeTime, jint fadeLevel)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeModifyPlayingPeriodicEffect((VibeInt32)hDevice, (VibeInt32)hEffect, (VibeInt32)duration, (VibeInt32)magnitude, (VibeInt32)period, (VibeInt32)styleAndWaveType, (VibeInt32)attackTime, (VibeInt32)attackLevel, (VibeInt32)fadeTime, (VibeInt32)fadeLevel));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_ModifyPlayingInterpolatedEffectInterpolant(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jint interpolant)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeModifyPlayingInterpolatedEffectInterpolant((VibeInt32)hDevice, (VibeInt32)hEffect, (VibeInt32)interpolant));
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayUHLEffect(JNIEnv* env, jobject thiz, jint hDevice, jint effectIndex)
{
    (void)thiz;
    VibeInt32 hEffect = 0;
    ThrowIfError(env, ImmVibePlayUHLEffect((VibeInt32)hDevice, (VibeInt32)effectIndex, &hEffect));
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetUHLEffectDuration(JNIEnv* env, jobject thiz, jint effectIndex)
{
    (void)thiz;
    VibeInt32 dur = 0;
    ThrowIfError(env, ImmVibeGetIVTEffectDuration(g_EmulatorReferenceIVT, (VibeInt32)effectIndex, &dur));
    return (jint)dur;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayIVTEffect(JNIEnv* env, jobject thiz, jint hDevice, jbyteArray ivt, jint effectIndex)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 hEffect = 0;
    VibeStatus status = ImmVibePlayIVTEffect((VibeInt32)hDevice, (const VibeUInt8*)bytes, (VibeInt32)effectIndex, &hEffect);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayIVTEffectRepeat(JNIEnv* env, jobject thiz, jint hDevice, jbyteArray ivt, jint effectIndex, jbyte repeat)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 hEffect = 0;
    VibeStatus status = ImmVibePlayIVTEffectRepeat((VibeInt32)hDevice, (const VibeUInt8*)bytes, (VibeInt32)effectIndex, (VibeUInt8)repeat, &hEffect);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayIVTInterpolatedEffect(JNIEnv* env, jobject thiz, jint hDevice, jbyteArray ivt, jint effectIndex, jint interpolant)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 hEffect = 0;
    VibeStatus status = ImmVibePlayIVTInterpolatedEffect((VibeInt32)hDevice, (const VibeUInt8*)bytes, (VibeInt32)effectIndex, (VibeInt32)interpolant, &hEffect);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayBuiltInEffectRepeat(JNIEnv* env, jobject thiz, jint hDevice, jint effectIndex, jbyte repeat)
{
    (void)thiz;
    VibeInt32 hEffect = 0;
    ThrowIfError(env, ImmVibePlayIVTEffectRepeat((VibeInt32)hDevice, g_EmulatorReferenceIVT, (VibeInt32)effectIndex, (VibeUInt8)repeat, &hEffect));
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayWaveformEffect(JNIEnv* env, jobject thiz, jint hDevice, jbyteArray data, jint dataSize, jint sampleRate, jint bitDepth, jint magnitude)
{
    (void)thiz;
    jbyte* bytes = data ? env->GetByteArrayElements(data, NULL) : NULL;
    VibeInt32 hEffect = 0;
    VibeStatus status = ImmVibePlayWaveformEffect((VibeInt32)hDevice, (const VibeUInt8*)bytes, (VibeInt32)dataSize, (VibeInt32)sampleRate, (VibeInt32)bitDepth, (VibeInt32)magnitude, &hEffect);
    if(bytes) env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_AppendWaveformEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jbyteArray data, jint dataSize, jint sampleRate, jint bitDepth, jint magnitude)
{
    (void)thiz;
    jbyte* bytes = data ? env->GetByteArrayElements(data, NULL) : NULL;
    VibeInt32 hOut = (VibeInt32)hEffect;
    VibeStatus status = ImmVibeAppendWaveformEffect((VibeInt32)hDevice, (VibeInt32)hEffect, (const VibeUInt8*)bytes, (VibeInt32)dataSize, (VibeInt32)sampleRate, (VibeInt32)bitDepth, (VibeInt32)magnitude, &hOut);
    if(bytes) env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)hOut;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayEnhancedWaveformEffect(JNIEnv* env, jobject thiz, jint hDevice, jbyteArray data, jint sampleRate, jint bitDepth, jint magnitude, jint format)
{
    (void)thiz;
    VibeEnhancedWaveformBuffer* buf = PackEnhancedBuf(env, data, format);
    if(!buf)
    {
        ThrowIfError(env, STATUS_WRONG_ARGUMENT);
        return 0;
    }
    VibeInt32 hEffect = 0;
    VibeStatus status = ImmVibePlayEnhancedWaveformEffect((VibeInt32)hDevice, (VibeInt32)sampleRate, (VibeInt32)bitDepth, (VibeInt32)magnitude, buf, &hEffect);
    free(buf);
    ThrowIfError(env, status);
    return (jint)hEffect;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_AppendEnhancedWaveformEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jbyteArray data, jint sampleRate, jint bitDepth, jint magnitude, jint format)
{
    (void)thiz;
    VibeEnhancedWaveformBuffer* buf = PackEnhancedBuf(env, data, format);
    if(!buf)
    {
        ThrowIfError(env, STATUS_WRONG_ARGUMENT);
        return 0;
    }
    VibeInt32 hOut = (VibeInt32)hEffect;
    VibeStatus status = ImmVibeAppendEnhancedWaveformEffect((VibeInt32)hDevice, (VibeInt32)hEffect, (VibeInt32)sampleRate, (VibeInt32)bitDepth, (VibeInt32)magnitude, buf, &hOut);
    free(buf);
    ThrowIfError(env, status);
    return (jint)hOut;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_ReplaceEnhancedWaveformEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jbyteArray data, jint sampleRate, jint bitDepth, jint magnitude, jint format)
{
    (void)thiz;
    VibeEnhancedWaveformBuffer* buf = PackEnhancedBuf(env, data, format);
    if(!buf)
    {
        ThrowIfError(env, STATUS_WRONG_ARGUMENT);
        return 0;
    }
    VibeInt32 hOut = (VibeInt32)hEffect;
    VibeStatus status = ImmVibeReplaceEnhancedWaveformEffect((VibeInt32)hDevice, (VibeInt32)hEffect, (VibeInt32)sampleRate, (VibeInt32)bitDepth, (VibeInt32)magnitude, buf, &hOut);
    free(buf);
    ThrowIfError(env, status);
    return (jint)hOut;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_StopPlayingEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeStopPlayingEffect((VibeInt32)hDevice, (VibeInt32)hEffect));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_StopAllPlayingEffects(JNIEnv* env, jobject thiz, jint hDevice)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeStopAllPlayingEffects((VibeInt32)hDevice));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_PausePlayingEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    ThrowIfError(env, ImmVibePausePlayingEffect((VibeInt32)hDevice, (VibeInt32)hEffect));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_ResumePausedEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeResumePausedEffect((VibeInt32)hDevice, (VibeInt32)hEffect));
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetEffectState(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    VibeInt32 state = VIBE_EFFECT_STATE_NOT_PLAYING;
    ThrowIfError(env, ImmVibeGetEffectState((VibeInt32)hDevice, (VibeInt32)hEffect, &state));
    return (jint)state;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetEffectRemainingDuration(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    VibeInt32 rem = 0;
    ThrowIfError(env, ImmVibeGetEffectRemainingDuration((VibeInt32)hDevice, (VibeInt32)hEffect, &rem));
    return (jint)rem;
}

// Streaming
JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_CreateStreamingEffect(JNIEnv* env, jobject thiz, jint hDevice)
{
    (void)thiz;
    VibeInt32 hEffect = 0;
    ThrowIfError(env, ImmVibeCreateStreamingEffect((VibeInt32)hDevice, &hEffect));
    return (jint)hEffect;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_DestroyStreamingEffect(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect)
{
    (void)thiz;
    ThrowIfError(env, ImmVibeDestroyStreamingEffect((VibeInt32)hDevice, (VibeInt32)hEffect));
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayStreamingSample(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jbyteArray buffer, jint size)
{
    (void)thiz;
    jbyte* bytes = buffer ? env->GetByteArrayElements(buffer, NULL) : NULL;
    VibeStatus status = ImmVibePlayStreamingSample((VibeInt32)hDevice, (VibeInt32)hEffect, (const VibeUInt8*)bytes, (VibeInt32)size);
    if(bytes) env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    ThrowIfError(env, status);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_PlayStreamingSampleWithOffset(JNIEnv* env, jobject thiz, jint hDevice, jint hEffect, jbyteArray buffer, jint size, jint offsetMs)
{
    (void)thiz;
    jbyte* bytes = buffer ? env->GetByteArrayElements(buffer, NULL) : NULL;
    VibeStatus status = ImmVibePlayStreamingSampleWithOffset((VibeInt32)hDevice, (VibeInt32)hEffect, (const VibeUInt8*)bytes, (VibeInt32)size, (VibeInt32)offsetMs);
    if(bytes) env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);
    ThrowIfError(env, status);
}

// IVTBuffer & Built-in effects
JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetBuiltInEffects(JNIEnv* env, jobject thiz)
{
    (void)thiz;
    return MakeBuiltInIVTByteArray(env);
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_IVTBuffer_GetBuiltInEffects(JNIEnv* env, jobject thiz)
{
    (void)thiz;
    return MakeBuiltInIVTByteArray(env);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTEffectCount(JNIEnv* env, jobject thiz, jbyteArray ivt)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 cnt = ImmVibeGetIVTEffectCount((const VibeUInt8*)bytes);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, cnt);
    return (jint)cnt;
}

JNIEXPORT jstring JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTEffectName(JNIEnv* env, jobject thiz, jbyteArray ivt, jint effectIndex)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeWChar wbuf[128];
    VibeStatus status = ImmVibeGetIVTEffectNameU((const VibeUInt8*)bytes, (VibeInt32)effectIndex, 128, wbuf);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return env->NewString((const jchar*)wbuf, (jsize)ImmVibeGetStringLengthU(wbuf));
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTEffectIndexFromName(JNIEnv* env, jobject thiz, jbyteArray ivt, jstring name)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    const jchar* chars = name ? env->GetStringChars(name, NULL) : NULL;
    jsize len = name ? env->GetStringLength(name) : 0;

    VibeInt32 idx = -1;
    VibeStatus status = IvtGetEffectIndexFromNameU(bytes ? (const VibeUInt8*)bytes : g_EmulatorReferenceIVT, (const VibeWChar*)chars, (size_t)len, &idx);
    if(chars) env->ReleaseStringChars(name, chars);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)idx;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTEffectType(JNIEnv* env, jobject thiz, jbyteArray ivt, jint effectIndex)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 t = ImmVibeGetIVTEffectType((const VibeUInt8*)bytes, (VibeInt32)effectIndex);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, t);
    return (jint)t;
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTEffectDuration(JNIEnv* env, jobject thiz, jbyteArray ivt, jint effectIndex)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 dur = 0;
    VibeStatus status = ImmVibeGetIVTEffectDuration((const VibeUInt8*)bytes, (VibeInt32)effectIndex, &dur);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    return (jint)dur;
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTMagSweepEffectDefinition(JNIEnv* env, jobject thiz, jbyteArray ivt, jint effectIndex, jintArray duration, jintArray magnitude, jintArray style, jintArray attackTime, jintArray attackLevel, jintArray fadeTime, jintArray fadeLevel)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 dur = 0, mag = 0, stW = 0, atkT = 0, atkL = 0, fadeT = 0, fadeL = 0;
    VibeStatus status = ImmVibeGetIVTMagSweepEffectDefinition((const VibeUInt8*)bytes, (VibeInt32)effectIndex, &dur, &mag, &stW, &atkT, &atkL, &fadeT, &fadeL);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    if(status >= STATUS_OK)
    {
        jint v;
        if(duration)    { v = (jint)dur;   env->SetIntArrayRegion(duration, 0, 1, &v); }
        if(magnitude)   { v = (jint)mag;   env->SetIntArrayRegion(magnitude, 0, 1, &v); }
        if(style)       { v = (jint)stW;   env->SetIntArrayRegion(style, 0, 1, &v); }
        if(attackTime)  { v = (jint)atkT;  env->SetIntArrayRegion(attackTime, 0, 1, &v); }
        if(attackLevel) { v = (jint)atkL;  env->SetIntArrayRegion(attackLevel, 0, 1, &v); }
        if(fadeTime)    { v = (jint)fadeT; env->SetIntArrayRegion(fadeTime, 0, 1, &v); }
        if(fadeLevel)   { v = (jint)fadeL; env->SetIntArrayRegion(fadeLevel, 0, 1, &v); }
    }
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTPeriodicEffectDefinition(JNIEnv* env, jobject thiz, jbyteArray ivt, jint effectIndex, jintArray duration, jintArray magnitude, jintArray period, jintArray styleAndWaveType, jintArray attackTime, jintArray attackLevel, jintArray fadeTime, jintArray fadeLevel)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 dur = 0, mag = 0, per = 0, stW = 0, atkT = 0, atkL = 0, fadeT = 0, fadeL = 0;
    VibeStatus status = ImmVibeGetIVTPeriodicEffectDefinition((const VibeUInt8*)bytes, (VibeInt32)effectIndex, &dur, &mag, &per, &stW, &atkT, &atkL, &fadeT, &fadeL);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
    if(status >= STATUS_OK)
    {
        jint v;
        if(duration)         { v = (jint)dur;   env->SetIntArrayRegion(duration, 0, 1, &v); }
        if(magnitude)        { v = (jint)mag;   env->SetIntArrayRegion(magnitude, 0, 1, &v); }
        if(period)           { v = (jint)per;   env->SetIntArrayRegion(period, 0, 1, &v); }
        if(styleAndWaveType) { v = (jint)stW;   env->SetIntArrayRegion(styleAndWaveType, 0, 1, &v); }
        if(attackTime)       { v = (jint)atkT;  env->SetIntArrayRegion(attackTime, 0, 1, &v); }
        if(attackLevel)      { v = (jint)atkL;  env->SetIntArrayRegion(attackLevel, 0, 1, &v); }
        if(fadeTime)         { v = (jint)fadeT; env->SetIntArrayRegion(fadeTime, 0, 1, &v); }
        if(fadeLevel)        { v = (jint)fadeL; env->SetIntArrayRegion(fadeLevel, 0, 1, &v); }
    }
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_SaveIVTFile(JNIEnv* env, jobject thiz, jbyteArray ivt, jstring path)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    const char* p = path ? env->GetStringUTFChars(path, NULL) : NULL;
    VibeStatus status = ImmVibeSaveIVTFile((const VibeUInt8*)bytes, p);
    if(p) env->ReleaseStringUTFChars(path, p);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    ThrowIfError(env, status);
}

JNIEXPORT void JNICALL Java_com_immersion_uhl_internal_ImmVibe_DeleteIVTFile(JNIEnv* env, jobject thiz, jstring path)
{
    (void)thiz;
    const char* p = path ? env->GetStringUTFChars(path, NULL) : NULL;
    VibeStatus status = ImmVibeDeleteIVTFile(p);
    if(p) env->ReleaseStringUTFChars(path, p);
    ThrowIfError(env, status);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_internal_ImmVibe_GetIVTSize(JNIEnv* env, jobject thiz, jbyteArray ivt)
{
    (void)thiz;
    jbyte* bytes = ivt ? env->GetByteArrayElements(ivt, NULL) : NULL;
    VibeInt32 sz = ImmVibeGetIVTSize((const VibeUInt8*)bytes);
    if(bytes) env->ReleaseByteArrayElements(ivt, bytes, JNI_ABORT);
    return (jint)(sz > 0 ? sz : 0);
}

JNIEXPORT jint JNICALL Java_com_immersion_uhl_IVTBuffer_GetIVTSize2(JNIEnv* env, jobject thiz, jbyteArray ivt)
{
    return Java_com_immersion_uhl_internal_ImmVibe_GetIVTSize(env, thiz, ivt);
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_IVTBuffer_InitializeIVTBuffer(JNIEnv* env, jobject thiz, jint bufferSize)
{
    (void)thiz;
    jsize sz = (bufferSize >= 8) ? (jsize)bufferSize : 8;
    jbyteArray arr = env->NewByteArray(sz);
    if(arr)
    {
        VibeUInt8 hdr[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
        env->SetByteArrayRegion(arr, 0, 8, (const jbyte*)hdr);
    }
    return arr;
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_internal_ImmVibe_InsertIVTElement(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jintArray element)
{
    (void)env; (void)thiz; (void)timelineIdx; (void)element;
    return ivt;
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_IVTBuffer_InsertIVTElement2(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jintArray element, jbyteArray data)
{
    (void)env; (void)thiz; (void)timelineIdx; (void)element; (void)data;
    return ivt;
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_internal_ImmVibe_RemoveIVTElement(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jint elemIdx)
{
    (void)env; (void)thiz; (void)timelineIdx; (void)elemIdx;
    return ivt;
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_IVTBuffer_RemoveIVTElement2(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jint elemIdx)
{
    (void)env; (void)thiz; (void)timelineIdx; (void)elemIdx;
    return ivt;
}

JNIEXPORT jintArray JNICALL Java_com_immersion_uhl_internal_ImmVibe_ReadIVTElement(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jint elemIdx)
{
    (void)thiz; (void)ivt; (void)timelineIdx; (void)elemIdx;
    return env->NewIntArray(0);
}

JNIEXPORT jintArray JNICALL Java_com_immersion_uhl_IVTBuffer_ReadIVTElement2(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jint elemIdx)
{
    (void)thiz; (void)ivt; (void)timelineIdx; (void)elemIdx;
    return env->NewIntArray(0);
}

JNIEXPORT jbyteArray JNICALL Java_com_immersion_uhl_IVTBuffer_ReadIVTElementData(JNIEnv* env, jobject thiz, jbyteArray ivt, jint timelineIdx, jint elemIdx)
{
    (void)thiz; (void)ivt; (void)timelineIdx; (void)elemIdx;
    return env->NewByteArray(0);
}

};
