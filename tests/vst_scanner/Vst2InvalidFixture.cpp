#include "pluginterfaces/vst2.x/aeffectx.h"

extern "C" __declspec(dllexport) AEffect* VSTPluginMain(audioMasterCallback)
{
    static AEffect invalidEffect {};
    return &invalidEffect;
}