#include "vibeapi.h"

extern "C"
{

VibeStatus EmuInitialize(VibeInt32 flag)
{
    if((flag & 0x7FFF0000) > 0x5000000 || flag < 0)
    {
        // huh?
        return -3;
    }

    // TODO:
  
    return STATUS_OK;
}

};
