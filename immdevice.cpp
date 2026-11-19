#include "vibeapi.h"

extern "C"
{

VIBE_EXPORT VibeInt32 ImmVibeGetDeviceCount()
{
    if(g_bEmulator) return EmuGetDeviceCount();
    return 1;
}

VIBE_EXPORT VibeStatus ImmVibeOpenDevice(VibeInt32 deviceIndex, VibeInt32* deviceHandle)
{
    if(g_bEmulator) return EmuOpenDevice(deviceIndex, deviceHandle);
    return STATUS_FAILED_TO_INIT;
}

VIBE_EXPORT VibeStatus ImmVibeOpenCompositeDevice(const VibeInt32* deviceIndices, VibeInt32 numDevices, VibeInt32* deviceHandle)
{
    (void)deviceIndices;
    (void)numDevices;
    return ImmVibeOpenDevice(0, deviceHandle);
}

VIBE_EXPORT VibeStatus ImmVibeCloseDevice(VibeInt32 deviceHandle)
{
    if(g_bEmulator) return EmuCloseDevice(deviceHandle);
    return STATUS_FAILED_TO_INIT;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceState(VibeInt32 deviceHandle, VibeInt32* deviceState)
{
    (void)deviceHandle;
    if(g_nVibeAPIReferenceCount <= 0) return STATUS_FAILED_TO_INIT;
    if(!deviceState) return STATUS_WRONG_ARGUMENT;

    pthread_mutex_lock(&g_VibeDriverVibrateMutex);
    VibeInt32 s = VIBE_DEVICESTATE_ATTACHED;
    if(VibeDriverAnySlotPlaying()) s |= VIBE_DEVICESTATE_BUSY;
    *deviceState = s;
    pthread_mutex_unlock(&g_VibeDriverVibrateMutex);
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceActuatorIndices(VibeInt32 deviceIndex, VibeInt32* actuatorIndices, VibeInt32* numActuators)
{
    (void)deviceIndex;
    if(actuatorIndices) actuatorIndices[0] = 0;
    if(numActuators) *numActuators = 1;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityBool(VibeInt32 deviceIndex, VibeInt32 capType, VibeBool* capValue)
{
    (void)deviceIndex;
    (void)capType;
    if(!capValue) return STATUS_WRONG_ARGUMENT;
    *capValue = STATUS_TRUE;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityInt32(VibeInt32 deviceIndex, VibeInt32 capType, VibeInt32* capValue)
{
    (void)deviceIndex;
    if(!capValue) return STATUS_WRONG_ARGUMENT;

    switch(capType)
    {
        case VIBE_DEVCAPTYPE_DEVICE_CATEGORY:        *capValue = 1; break;
        case VIBE_DEVCAPTYPE_MAX_NESTED_REPEATS:     *capValue = 3; break;
        case VIBE_DEVCAPTYPE_NUM_ACTUATORS:          *capValue = 1; break;
        case VIBE_DEVCAPTYPE_ACTUATOR_TYPE:          *capValue = 0; break;
        case VIBE_DEVCAPTYPE_NUM_EFFECT_SLOTS:       *capValue = VIBRO_MAX_EFFECT_SLOTS; break;
        case VIBE_DEVCAPTYPE_SUPPORTED_STYLES:       *capValue = 0x07; break;
        case VIBE_DEVCAPTYPE_MIN_PERIOD:             *capValue = 5; break;
        case VIBE_DEVCAPTYPE_MAX_PERIOD:             *capValue = 1000; break;
        case VIBE_DEVCAPTYPE_MAX_EFFECT_DURATION:    *capValue = 10000; break;
        case VIBE_DEVCAPTYPE_SUPPORTED_EFFECTS:      *capValue = 0x1F; break;
        case VIBE_DEVCAPTYPE_MAX_ENVELOPE_TIME:      *capValue = 10000; break;
        case VIBE_DEVCAPTYPE_APIVERSIONNUMBER:       *capValue = VERSION_INT; break;
        case VIBE_DEVCAPTYPE_MAX_IVT_SIZE_TETHERED:  *capValue = 65536; break;
        case VIBE_DEVCAPTYPE_MAX_IVT_SIZE:           *capValue = 65536; break;
        case VIBE_DEVCAPTYPE_EDITION_LEVEL:          *capValue = 3000; break;
        default:                                     *capValue = 0; break;
    }
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceCapabilityString(VibeInt32 deviceIndex, VibeInt32 capType, VibeInt32 size, char* capValue)
{
    (void)deviceIndex;
    (void)capType;
    if(!capValue || size <= 0) return STATUS_WRONG_ARGUMENT;
    snprintf(capValue, (size_t)size, "%s", "TS Emulator");
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyBool(VibeInt32 deviceHandle, VibeInt32 propType, VibeBool* propValue)
{
    (void)deviceHandle;
    if(!propValue) return STATUS_WRONG_ARGUMENT;
    *propValue = (propType == VIBE_DEVPROPTYPE_DISABLE_EFFECTS && g_bDisableEffects) ? STATUS_TRUE : STATUS_FALSE;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyBool(VibeInt32 deviceHandle, VibeInt32 propType, VibeBool propValue)
{
    (void)deviceHandle;
    if(propType == VIBE_DEVPROPTYPE_DISABLE_EFFECTS)
    {
        g_bDisableEffects = (propValue != 0);
        if(g_bDisableEffects) Emulator_CancelVibrate();
    }
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyInt32(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32* propValue)
{
    (void)deviceHandle;
    if(!propValue) return STATUS_WRONG_ARGUMENT;

    switch(propType)
    {
        case VIBE_DEVPROPTYPE_PRIORITY:       *propValue = g_nPriority; break;
        case VIBE_DEVPROPTYPE_STRENGTH:       *propValue = g_nStrength; break;
        case VIBE_DEVPROPTYPE_MASTERSTRENGTH: *propValue = g_nMasterStrength; break;
        default:                              *propValue = 0; break;
    }
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyInt32(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32 propValue)
{
    (void)deviceHandle;
    switch(propType)
    {
        case VIBE_DEVPROPTYPE_PRIORITY:
            g_nPriority = propValue;
            break;
        case VIBE_DEVPROPTYPE_STRENGTH:
            if(propValue < 0) propValue = 0;
            if(propValue > VIBE_MAX_MAGNITUDE) propValue = VIBE_MAX_MAGNITUDE;
            g_nStrength = propValue;
            break;
        case VIBE_DEVPROPTYPE_MASTERSTRENGTH:
            if(propValue < 0) propValue = 0;
            if(propValue > VIBE_MAX_MAGNITUDE) propValue = VIBE_MAX_MAGNITUDE;
            g_nMasterStrength = propValue;
            break;
        default:
            break;
    }
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDevicePropertyString(VibeInt32 deviceHandle, VibeInt32 propType, VibeInt32 size, char* propValue)
{
    (void)deviceHandle;
    (void)propType;
    if(!propValue || size <= 0) return STATUS_WRONG_ARGUMENT;
    propValue[0] = '\0';
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeSetDevicePropertyString(VibeInt32 deviceHandle, VibeInt32 propType, const char* propValue)
{
    (void)deviceHandle;
    (void)propType;
    (void)propValue;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus ImmVibeGetDeviceKernelParameter(VibeInt32 deviceHandle, VibeInt32 paramId, VibeInt32* paramValue)
{
    (void)deviceHandle;
    (void)paramId;
    if(paramValue) *paramValue = 0;
    return STATUS_OK;
}

VIBE_EXPORT VibeStatus CUHLSetDeviceKernelParameter(VibeInt32 deviceHandle, VibeInt32 paramId, VibeInt32 paramValue)
{
    (void)deviceHandle;
    (void)paramId;
    (void)paramValue;
    return STATUS_OK;
}

};
