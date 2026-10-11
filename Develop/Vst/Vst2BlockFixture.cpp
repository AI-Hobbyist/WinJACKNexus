// Offline VST2 block/state fixture. SPDX-License-Identifier: AGPL-3.0-only
#include "pluginterfaces/vst2.x/aeffectx.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace
{
AEffect effect{};
audioMasterCallback host{};
float gain = 0.5F;
double rate = 48000;
std::array<int, 1024> offsets{};
int events{};
VstIntPtr VSTCALLBACK dispatch(AEffect *, VstInt32 opcode, VstInt32, VstIntPtr value, void *pointer,
                               float option)
{
    switch (opcode)
    {
    case effOpen:
    case effClose:
    case effMainsChanged:
        return 1;
    case effGetPlugCategory:
        return kPlugCategEffect;
    case effGetNumMidiInputChannels:
        return 1;
    case effSetSampleRate:
        rate = option;
        return 1;
    case effGetChunk:
        *static_cast<void **>(pointer) = &gain;
        return sizeof(gain);
    case effSetChunk:
        if (value == sizeof(gain))
            std::memcpy(&gain, pointer, sizeof(gain));
        return 1;
    case effProcessEvents: {
        auto *block = static_cast<VstEvents *>(pointer);
        events = 0;
        for (int index = 0; index < block->numEvents && events < int(offsets.size()); ++index)
            if (block->events[index]->type == kVstMidiType)
                offsets[events++] = block->events[index]->deltaFrames;
        return 1;
    }
    default:
        return 0;
    }
}
void VSTCALLBACK process(AEffect *, float **inputs, float **outputs, VstInt32 frames)
{
    const auto *time =
        reinterpret_cast<const VstTimeInfo *>(host(&effect, audioMasterGetTime, 0, 0, nullptr, 0));
    const auto position = time ? uint64_t(time->samplePos) : 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        outputs[0][frame] = inputs[0][frame] * gain + float(rate / 1000000);
        outputs[1][frame] = float(position % 1000 + frame);
    }
    for (int index = 0; index < events; ++index)
        if (offsets[index] >= 0 && offsets[index] < frames)
            outputs[1][offsets[index]] = float(1000 + offsets[index]);
    events = 0;
}
}
extern "C" __declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback callback)
{
    host = callback;
    gain = 0.5F;
    effect = {};
    effect.magic = kEffectMagic;
    effect.dispatcher = dispatch;
    effect.processReplacing = process;
    effect.numInputs = 2;
    effect.numOutputs = 2;
    effect.flags = effFlagsCanReplacing | effFlagsProgramChunks;
    effect.uniqueID = 0x574a5335;
    return &effect;
}
