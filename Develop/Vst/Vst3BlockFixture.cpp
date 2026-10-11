// Offline VST3 block/state fixture. SPDX-License-Identifier: AGPL-3.0-only
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
class BlockFixture final : public SingleComponentEffect
{
  public:
    static FUnknown *create(void *)
    {
        return static_cast<IComponent *>(new BlockFixture);
    }
    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        const auto result = SingleComponentEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI"));
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        return stream ? stream->write(&gain_, sizeof(gain_)) : kInvalidArgument;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        return stream ? stream->read(&gain_, sizeof(gain_)) : kInvalidArgument;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API getEditorState(IBStream *stream) override
    {
        return getState(stream);
    }
    tresult PLUGIN_API setEditorState(IBStream *stream) override
    {
        return setState(stream);
    }
    tresult PLUGIN_API process(ProcessData &data) override
    {
        if (!data.numOutputs || !data.outputs[0].channelBuffers32)
            return kResultOk;
        auto **outputs = data.outputs[0].channelBuffers32;
        auto **inputs = data.numInputs ? data.inputs[0].channelBuffers32 : nullptr;
        const auto position =
            data.processContext ? uint64(data.processContext->projectTimeSamples) : 0;
        for (int32 frame = 0; frame < data.numSamples; ++frame)
        {
            outputs[0][frame] = (inputs && inputs[0] ? inputs[0][frame] : 0) * gain_ +
                                float(processSetup.sampleRate / 1000000);
            outputs[1][frame] = float(position % 1000 + frame);
        }
        if (data.inputEvents)
        {
            for (int32 index = 0; index < data.inputEvents->getEventCount(); ++index)
            {
                Event event{};
                if (data.inputEvents->getEvent(index, event) == kResultOk &&
                    event.sampleOffset >= 0 && event.sampleOffset < data.numSamples)
                    outputs[1][event.sampleOffset] = float(1000 + event.sampleOffset);
            }
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

  private:
    float gain_ = 0.5F;
};
}
BEGIN_FACTORY_DEF("WinJACKNexus fixture", "", "")
DEF_CLASS2(INLINE_UID(0x12345678, 0x11223344, 0x55667788, 0x1234abcd), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "S5 Block Fixture", 0, "Fx", "1.0", kVstVersionString,
           BlockFixture::create)
END_FACTORY
extern "C" __declspec(dllexport) bool InitDll()
{
    return true;
}
extern "C" __declspec(dllexport) bool ExitDll()
{
    return true;
}
