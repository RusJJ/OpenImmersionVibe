#include "vibeapi.h"

static ImmVibeMutex g_mutexes[16];
static pthread_mutex_t g_MutexLock = PTHREAD_MUTEX_INITIALIZER;
static bool g_bTimerSet = false;

extern "C"
{

VibeInt32 VibeOSCreateMutex(const char* name)
{
    if(!name) return STATUS_WRONG_ARGUMENT;

    int i;
    for(i = 0; i < 16; ++i)
    {
        if(g_mutexes[i].name)
        {
            if(!strcmp(g_mutexes[i].name, name))
            {
                return i;
            }
        }
        else
        {
            break;
        }
    }
    if(i >= 16) return STATUS_ALREADY_INITED;

    g_mutexes[i].name = strdup(name);
    g_mutexes[i].locked = false;
    pthread_mutex_init(&g_mutexes[i].mutex, NULL);
    pthread_cond_init(&g_mutexes[i].cond, NULL);
    return i;
}

void VibeOSDestroyMutex(VibeInt32 mutexNum)
{
    if(mutexNum < 0 || mutexNum >= 16 || !g_mutexes[mutexNum].name) return;

    free(g_mutexes[mutexNum].name);
    g_mutexes[mutexNum].name = NULL;
    g_mutexes[mutexNum].locked = false;
    pthread_mutex_destroy(&g_mutexes[mutexNum].mutex);
    pthread_cond_destroy(&g_mutexes[mutexNum].cond);
}

VibeStatus VibeOSAcquireMutexWaitNoWait(VibeInt32 mutexNum, VibeInt32 timeMs, VibeInt32 wait)
{
    if(mutexNum < 0 || mutexNum >= 16 || !g_mutexes[mutexNum].name)
    {
        return STATUS_WRONG_ARGUMENT;
    }

    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    tp.tv_sec += timeMs / 1000;
    tp.tv_nsec += (long)(timeMs % 1000) * 1000000L;
    if(tp.tv_nsec >= 1000000000L)
    {
        tp.tv_sec += 1;
        tp.tv_nsec -= 1000000000L;
    }

    if(pthread_mutex_lock(&g_mutexes[mutexNum].mutex) != 0)
    {
        return STATUS_FAILED_GENERIC;
    }

    if(!wait && g_mutexes[mutexNum].locked)
    {
        pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
        return STATUS_FAILED_GENERIC;
    }

    while(g_mutexes[mutexNum].locked)
    {
        if(pthread_cond_timedwait(&g_mutexes[mutexNum].cond, &g_mutexes[mutexNum].mutex, &tp) != 0)
        {
            pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
            return STATUS_FAILED_GENERIC;
        }
    }
    g_mutexes[mutexNum].locked = true;
    pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
    return STATUS_OK;
}

VibeStatus VibeOSAcquireMutexWait(VibeInt32 mutexNum, VibeInt32 timeMs)
{
    return VibeOSAcquireMutexWaitNoWait(mutexNum, timeMs, 1);
}

VibeStatus VibeOSAcquireMutex(VibeInt32 mutexNum)
{
    return VibeOSAcquireMutexWait(mutexNum, 1000);
}

VibeInt32 VibeOSCreateMutexAcquired(const char* name)
{
    VibeInt32 mutexNum = VibeOSCreateMutex(name);
    if(mutexNum >= 0) VibeOSAcquireMutex(mutexNum);
    return mutexNum;
}

VibeStatus VibeOSAcquireMutexNoWait(VibeInt32 mutexNum)
{
    if(mutexNum < 0 || mutexNum >= 16 || !g_mutexes[mutexNum].name)
    {
        return STATUS_WRONG_ARGUMENT;
    }
    return VibeOSAcquireMutexWaitNoWait(mutexNum, 0, 0);
}

void VibeOSReleaseMutex(VibeInt32 mutexNum)
{
    if(mutexNum < 0 || mutexNum >= 16 || !g_mutexes[mutexNum].name) return;

    if(!pthread_mutex_lock(&g_mutexes[mutexNum].mutex))
    {
        g_mutexes[mutexNum].locked = false;
        pthread_cond_broadcast(&g_mutexes[mutexNum].cond);
        pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
    }
}

VibeStatus VibeOSStartTimer()
{
    pthread_mutex_lock(&g_MutexLock);
    g_bTimerSet = true;
    pthread_mutex_unlock(&g_MutexLock);
    return STATUS_OK;
}

bool VibeOSIsTimerRunning()
{
    pthread_mutex_lock(&g_MutexLock);
    bool didSetTimer = g_bTimerSet;
    pthread_mutex_unlock(&g_MutexLock);
    return didSetTimer;
}

VibeStatus VibeOSStopTimer()
{
    pthread_mutex_lock(&g_MutexLock);
    g_bTimerSet = false;
    pthread_mutex_unlock(&g_MutexLock);
    return STATUS_OK;
}

VibeInt64 VibeOSGetTimeMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (VibeInt64)ts.tv_sec * 1000LL + (VibeInt64)(ts.tv_nsec / 1000000L);
}

};
