#include "pluginterfaces/vst2.x/aeffectx.h"

#include <cstdint>
#include <cstring>

namespace {

enum class FixtureKind : std::uintptr_t {
    shell,
    childOne,
    childTwo
};

constexpr VstIntPtr childOneId = 0x46495841;
constexpr VstIntPtr childTwoId = 0x46495842;

int shellPluginIndex = 0;

FixtureKind kindOf(const AEffect* effect)
{
    return static_cast<FixtureKind>(reinterpret_cast<std::uintptr_t>(effect->user));
}

void writeString(void* destination, int maximumLength, const char* value)
{
    if (destination == nullptr || maximumLength <= 0)
        return;
    std::strncpy(static_cast<char*>(destination), value, static_cast<std::size_t>(maximumLength - 1));
    static_cast<char*>(destination)[maximumLength - 1] = '\0';
}

const char* productName(FixtureKind kind)
{
    switch (kind) {
    case FixtureKind::childOne:
        return "Fixture Child One";
    case FixtureKind::childTwo:
        return "Fixture Child Two";
    case FixtureKind::shell:
    default:
        return "Fixture Shell";
    }
}

VstIntPtr VSTCALLBACK fixtureDispatcher(AEffect* effect,
                                        VstInt32 opcode,
                                        VstInt32,
                                        VstIntPtr,
                                        void* pointer,
                                        float)
{
    const auto kind = kindOf(effect);
    switch (opcode) {
    case effOpen:
        if (kind == FixtureKind::shell)
            shellPluginIndex = 0;
        return 1;
    case effClose:
        return 1;
    case effGetVendorString:
        writeString(pointer, kVstMaxVendorStrLen + 1, "Fixture Vendor");
        return 1;
    case effGetProductString:
        writeString(pointer, kVstMaxProductStrLen + 1, productName(kind));
        return 1;
    case effGetPlugCategory:
        return kind == FixtureKind::shell ? kPlugCategShell
                                          : kind == FixtureKind::childTwo ? kPlugCategSynth
                                                                          : kPlugCategEffect;
    case effGetNumMidiInputChannels:
        return kind == FixtureKind::childTwo ? 1 : 0;
    case effGetNumMidiOutputChannels:
        return kind == FixtureKind::childOne ? 1 : 0;
    case effShellGetNextPlugin:
        if (kind != FixtureKind::shell || pointer == nullptr)
            return 0;
        if (shellPluginIndex == 0) {
            writeString(pointer, kVstMaxProductStrLen + 1, "Fixture Child One");
            ++shellPluginIndex;
            return childOneId;
        }
        if (shellPluginIndex == 1) {
            writeString(pointer, kVstMaxProductStrLen + 1, "Fixture Child Two");
            ++shellPluginIndex;
            return childTwoId;
        }
        return 0;
    default:
        return 0;
    }
}

AEffect shellEffect;
AEffect childOneEffect;
AEffect childTwoEffect;

void initializeEffect(AEffect& effect, FixtureKind kind, VstInt32 inputs, VstInt32 outputs, VstInt32 flags)
{
    std::memset(&effect, 0, sizeof(effect));
    effect.magic = kEffectMagic;
    effect.dispatcher = fixtureDispatcher;
    effect.numInputs = inputs;
    effect.numOutputs = outputs;
    effect.flags = flags;
    effect.uniqueID = static_cast<VstInt32>(kind == FixtureKind::childOne ? childOneId : childTwoId);
    effect.version = 1000;
    effect.user = reinterpret_cast<void*>(static_cast<std::uintptr_t>(kind));
}

}

extern "C" __declspec(dllexport) AEffect* VSTPluginMain(audioMasterCallback audioMaster)
{
    const auto requestedId = audioMaster == nullptr
        ? VstIntPtr(0)
        : audioMaster(nullptr, audioMasterCurrentId, 0, 0, nullptr, 0.0F);
    if (requestedId == childOneId) {
        initializeEffect(childOneEffect, FixtureKind::childOne, 2, 2, 0);
        return &childOneEffect;
    }
    if (requestedId == childTwoId) {
        initializeEffect(childTwoEffect, FixtureKind::childTwo, 0, 2, 0);
        return &childTwoEffect;
    }
    initializeEffect(shellEffect, FixtureKind::shell, 0, 0, 0);
    return &shellEffect;
}
