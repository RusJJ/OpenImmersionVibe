#include "vibeapi.h"

extern "C"
{

jobject g_Activity = NULL;
jobject g_ContextObj = NULL;
bool g_bEmulator = false;
JavaVM* g_JavaVM = NULL;
VibeInt32 g_nTSPVersion = 0;
void* g_pBestEffectSet = NULL; // unused

jobject g_VibratorObject;
jmethodID g_VibrateMethod;

sem_t mainSemaphore, renderSemaphore;

VibeInt32 DetectTSPVersion()
{
    // Im not sure if immvibed daemon exists and if it's really useful right now?
    // I didnt find /tspreq and /tsprsp on my phone, dunno
    return 0;
}

void* ImmVibeMainLoopThread(void* data)
{
    JNIEnv* env;
    g_JavaVM->AttachCurrentThread((void**)&env, NULL);
    
    jclass vibratorCls = env->FindClass("android/os/Vibrator");
    jclass contextCls = env->FindClass("android/context/Context");
    jmethodID sysServiceMethod = env->GetMethodID(contextCls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jfieldID vibratorSrvField = env->GetStaticFieldID(contextCls, "VIBRATOR_SERVICE", "Ljava/lang/String;");
    jstring vibratorFieldStr = env->GetStaticObjectField(contextCls, vibratorSrvField);
    jobject localVibrateObject = env->CallObjectMethod(g_ContextObj, sysServiceMethod, vibratorSrvField);

    g_VibratorObject = env->NewGlobalRef(localVibrateObject);
    g_VibrateMethod = env->GetMethodID(vibratorCls, "vibrate", "(J)V");

    sem_wait(&mainSemaphore);
    sem_post(&renderSemaphore);
    
    while(true)
    {
        // TODO:
    }
    return (void*)STATUS_OK;
}

VibeStatus ImmVibeInitialize2(int unused, JavaVM* vm, jobject activity)
{
    g_bEmulator = false;
    g_JavaVM = vm;

    JNIEnv* env;
    vm->AttachCurrentThread((void**)&env, NULL);
    g_ContextObj = env->NewGlobalRef(activity);
    if(!activity) return STATUS_FAILED_GENERIC;

    sem_init(&mainSemaphore, 0, 1);
    sem_init(&renderSemaphore, 0, 1);
    //GetApplicationPath(); // For analytics, lets just get rid of this

    VibeStatus finalInitStatus = STATUS_OK;
    VibeInt32 tspDetected = DetectTSPVersion();
    if(tspDetected == 0)
    {
        g_bEmulator = true;
        g_nTSPVersion = 0;
    }
    else
    {
        // Not required.
    }

    if(g_bEmulator)
    {
        // TODO:
    }
    else
    {
        // Not required.
    }
    return finalInitStatus;
}

VibeStatus ImmVibeTerminate()
{
    return STATUS_OK;
}

VibeStatus ImmVibeTerminate2()
{
    return STATUS_OK;
}

};
