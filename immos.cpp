#include "vibeapi.h"

ImmVibeMutex g_mutexes[16];

VibeInt32 VibeOSCreateMutex(const char* name)
{
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
    pthread_mutex_init(&g_mutexes[i].mutex, NULL);
    pthread_cond_init(&g_mutexes[i].cond, NULL);
    return i;
}

void VibeOSDestroyMutex(VibeInt32 mutexNum)
{
    free(g_mutexes[mutexNum].name);
    g_mutexes[mutexNum].name = NULL;
    pthread_mutex_destroy(&g_mutexes[mutexNum].mutex);
    pthread_cond_destroy(&g_mutexes[mutexNum].cond);
}

VibeStatus VibeOSAcquireMutexWaitNoWait(VibeInt32 mutexNum, VibeInt32 time, VibeInt32 wait)
{
    if(mutexNum >= 16 || !g_mutexes[mutexNum].name)
    {
        return STATUS_WRONG_ARGUMENT;
    }

    struct timespec tp;
    clock_gettime(0, &tp);
    tp.tv_sec += time / 1000;
    tp.tv_nsec += time % 1000 * 1000000;

    if(pthread_mutex_lock(&g_mutexes[mutexNum].mutex) != 0)
    {
        return STATUS_FAILED_GENERIC;
    }

    if(!wait && g_mutexes[mutexNum].released)
    {
        pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
        return STATUS_FAILED_GENERIC;
    }

    while(g_mutexes[mutexNum].released)
    {
        if(pthread_cond_timedwait(&g_mutexes[mutexNum].cond, &g_mutexes[mutexNum].mutex, &tp) != 0)
        {
            pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
            return STATUS_FAILED_GENERIC;
        }
    }
    g_mutexes[mutexNum].released = true;
    pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
    return STATUS_OK;
}

VibeStatus VibeOSAcquireMutexWait(VibeInt32 mutexNum, VibeInt32 time)
{
    return VibeOSAcquireMutexWaitNoWait(mutexNum, time, 1);
}

VibeStatus VibeOSAcquireMutex(VibeInt32 mutexNum)
{
    return VibeOSAcquireMutexWait(mutexNum, 1000);
}

VibeInt32 VibeOSCreateMutexAcquired(const char* name)
{
    VibeInt32 mutexNum = VibeOSCreateMutex(name);
    if(mutexNum != -1) VibeOSAcquireMutex(mutexNum);
    return mutexNum;
}

VibeStatus VibeOSAcquireMutexNoWait(VibeInt32 mutexNum)
{
    if(mutexNum >= 16 || !g_mutexes[mutexNum].name)
    {
        return STATUS_WRONG_ARGUMENT;
    }
    return VibeOSAcquireMutexWaitNoWait(mutexNum, 0, 0);
}

void VibeOSReleaseMutex(VibeInt32 mutexNum)
{
    if(!pthread_mutex_lock(&g_mutexes[mutexNum].mutex))
    {
        g_mutexes[mutexNum].released = true;
        pthread_cond_broadcast(&g_mutexes[mutexNum].cond);
        pthread_mutex_unlock(&g_mutexes[mutexNum].mutex);
    }
}

pthread_mutex_t g_MutexLock;
pthread_cond_t mConditionThreadContinue;
bool g_bTimerSet;
void (*g_timerProc)();
void VibeOSTimerThread()
{
    pthread_mutex_lock(&g_MutexLock);
    while(true)
    {
        bool didSetTimer = g_bTimerSet;
        pthread_mutex_unlock(&g_MutexLock);
        if(didSetTimer)
        {
            sleep(0);
            g_timerProc();
            pthread_mutex_lock(&g_MutexLock);
        }
        else
        {
            pthread_mutex_lock(&g_MutexLock);
            pthread_cond_wait(&mConditionThreadContinue, &g_MutexLock);
        }
    }
}

VibeStatus VibeOSStartTimer()
{
    g_bTimerSet = true;
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
    pthread_cond_signal(&mConditionThreadContinue);
    pthread_mutex_unlock(&g_MutexLock);
    return STATUS_OK;
}

VibeInt32 VibeOSGetTimeMs(__time_t seconds, __suseconds_t subseconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = subseconds;
    if(gettimeofday(&tv, 0)) return 0;
    return 1000 * tv.tv_sec + tv.tv_usec / 1000;
}

// IPC part supposed to be below. We dont have an access to low-level hardware.