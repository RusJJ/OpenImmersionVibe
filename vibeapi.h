#ifndef VIBE_API_H
#define VIBE_API_H



#include <stdint.h>
#include <jni.h>
#include <semaphore.h>

// Types
typedef uint8_t VibeUInt8;
typedef int16_t VibeInt16;
typedef int32_t VibeInt32;
typedef int64_t VibeInt64;
typedef VibeInt32 VibeStatus;
typedef uintptr_t VibePointer;

// Macros
#define BUILD_DATE_STR __DATE__

#define VERSION_MAJOR ( 5 )
#define VERSION_MINOR ( 0 )
#define VERSION_BUILD ( 0 )
#define VERSION_REVIS ( 11 )
#define VERSION_INT   ( VERSION_REVIS | (VERSION_BUILD << 8) | (VERSION_MINOR << 16) | (VERSION_MAJOR << 24) )

#define STATUS_OK 0
#define STATUS_ALREADY_INITED -1
#define STATUS_WRONG_ARGUMENT -3
#define STATUS_FAILED_GENERIC -4



#endif // VIBE_API_H
