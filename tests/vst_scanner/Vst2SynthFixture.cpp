#include "pluginterfaces/vst2.x/aeffectx.h"

namespace {

AEffect synthEffect;

void VSTCALLBACK synthProcessReplacing(AEffect*, float**, float** outputs, VstInt32 frames)
{
    for (VstInt32 channel = 0; channel < 2; ++channel) {
        if (outputs[channel] == nullptr)
            continue;
        for (VstInt32 frame = 0; frame < frames; ++frame)
            outputs[channel][frame] = 0.0F;
    }
}

void writeString(void* destination, int maximumLength, const char* value)
{
    if (destination == nullptr || maximumLength <= 0)
        return;
    auto* output = static_cast<char*>(destination);
    int index = 0;
    for (; index + 1 < maximumLength && value[index] != '\0'; ++index)
        output[index] = value[index];
    output[index] = '\0';
}

VstIntPtr VSTCALLBACK synthDispatcher(AEffect*,
                                      VstInt32 opcode,
                                      VstInt32,
                                      VstIntPtr,
                                      void* pointer,
                                      float)
{
    switch (opcode) {
    case effOpen:
    case effClose:
        return 1;
    case effGetVendorString:
        writeString(pointer, kVstMaxVendorStrLen + 1, "Fixture Vendor");
        return 1;
    case effGetPlugCategory:
        return kPlugCategSynth;
    case effGetNumMidiInputChannels:
        return 1;
    default:
        return 0;
    }
}

}

extern "C" __declspec(dllexport) AEffect* VSTPluginMain(audioMasterCallback)
{
    synthEffect = {};
    synthEffect.magic = kEffectMagic;
    synthEffect.dispatcher = synthDispatcher;
    synthEffect.processReplacing = synthProcessReplacing;
    synthEffect.numOutputs = 2;
    synthEffect.version = 1000;
    return &synthEffect;
}