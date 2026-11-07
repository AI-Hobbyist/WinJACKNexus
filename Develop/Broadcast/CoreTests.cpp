// SPDX-License-Identifier: AGPL-3.0-only
#include <WinJACKNexus/Common/BroadcastAudioStream.hpp>
#include <WinJACKNexus/Common/BroadcastProcessor.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace BC = WinJACKNexus::Common::Broadcast;

int main()
{
    int failures = 0;
    auto check = [&](bool passed, const char *message)
    {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", message);
        failures += !passed;
    };
    constexpr unsigned frames = 17;
    for (const auto layout :
         {BC::Layout::mono, BC::Layout::stereo, BC::Layout::surround51, BC::Layout::surround71})
    {
        BC::Controls controls;
        controls.layout = layout;
        controls.gain = 0.5F;
        const auto channels = BC::channelCount(layout);
        std::array<std::array<float, frames>, 8> input{}, monitor{};
        std::array<const float *, 8> inputs{};
        std::array<float *, 8> outputs{};
        std::array<float, frames * 8> broadcast{};
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            input[channel].fill(float(channel + 1) / 8);
            monitor[channel].fill(-99);
            inputs[channel] = input[channel].data();
            outputs[channel] = monitor[channel].data();
        }
        const auto peak =
            BC::process(controls, false, inputs.data(), outputs.data(), broadcast.data(), frames);
        bool exact = !controls.listen && !controls.solo;
        for (unsigned frame = 0; frame < frames; ++frame)
            for (unsigned channel = 0; channel < channels; ++channel)
                exact &= broadcast[frame * channels + channel] == float(channel + 1) / 16 &&
                         monitor[channel][frame] == 0;
        check(exact && peak == float(channels) / 16, "default Listen off; gain applied once to broadcast");

        controls.listen = true;
        BC::process(controls, false, inputs.data(), outputs.data(), broadcast.data(), frames);
        exact = true;
        for (unsigned channel = 0; channel < channels; ++channel)
            exact &= monitor[channel][0] == float(channel + 1) / 16;
        check(exact, "same-layout monitor receives post-gain audio");

        BC::process(controls, true, inputs.data(), outputs.data(), broadcast.data(), frames);
        check(monitor[0][0] == 0 && broadcast[0] == 0.0625F,
              "another local solo silences monitor without changing broadcast");
        controls.solo = true;
        BC::process(controls, true, inputs.data(), outputs.data(), broadcast.data(), frames);
        check(monitor[0][0] == 0.0625F, "solo track obeys Listen selection");
        controls.listen = false;
        BC::process(controls, true, inputs.data(), outputs.data(), broadcast.data(), frames);
        check(monitor[0][0] == 0, "solo cannot bypass Listen off");

        controls.listen = true;
        controls.mute = true;
        const auto mutedPeak =
            BC::process(controls, true, inputs.data(), outputs.data(), broadcast.data(), frames);
        check(mutedPeak == 0 && monitor[0][0] == 0 && broadcast[0] == 0,
              "M silences broadcast and monitor without removing the stream");

        controls.mute = false;
        input[channels - 1][frames - 1] = -3;
        const auto overload =
            BC::process(controls, true, inputs.data(), outputs.data(), broadcast.data(), frames);
        check(overload == 1.5F, "one aggregate peak includes overload on the last channel");
    }
    BC::Controls controls;
    controls.gain = 0.5F;
    controls.listen = true;
    BC::AudioStream stream(48000, controls);
    std::vector<float> left(6000, 1), right(6000, 0.5F), outLeft(6000), outRight(6000);
    const float *inputs[]{left.data(), right.data()};
    float *outputs[]{outLeft.data(), outRight.data()};
    std::vector<BC::QueuedFrame> queued(6000);
    const auto generation = stream.arm();
    stream.render(controls, false, inputs, outputs, 480);
    const auto count = stream.read(queued);
    check(count == 480 && queued[0].generation == generation && queued[479].position == 479 &&
              queued[0].samples[0] == 0.5F && queued[0].samples[1] == 0.25F && outLeft[0] == 0.5F,
          "queued post-gain audio and local monitor share one source clock and gain application");
    stream.disarm();
    stream.render(controls, false, inputs, outputs, 480);
    check(stream.read(queued) == 0 && outLeft[0] == 0.5F,
          "stopped publishing keeps local monitoring alive without filling a network queue");
    stream.arm();
    stream.render(controls, false, inputs, outputs, 6000);
    const auto buffered = stream.read(queued);
    check(buffered <= 4800 && buffered + stream.droppedFrames() == 6000 && stream.droppedFrames() > 0 &&
              outLeft[5999] == 0.5F && outRight[5999] == 0.25F,
          "full 100ms queue drops publishing frames while every monitor frame is still processed");
    stream.disarm();
    controls.gain = 1;
    stream.render(controls, false, inputs, outputs, 2400);
    bool smooth = outLeft[0] > 0.5F && outLeft[0] < 0.51F && outLeft[2399] > 0.999F;
    for (unsigned index = 1; index < 2400; ++index)
        smooth &= outLeft[index] >= outLeft[index - 1] && outRight[index] == outLeft[index] * 0.5F;
    check(smooth, "gain edit ramps once with a shared envelope across channels");
    const auto nextGeneration = stream.arm();
    stream.render(controls, false, inputs, outputs, 48);
    check(nextGeneration != generation && stream.read(queued) == 48 &&
              queued[0].generation == nextGeneration && queued[0].position == 9360,
          "restarted publishing uses a new generation while the source clock remains continuous");
    return failures ? 1 : 0;
}
