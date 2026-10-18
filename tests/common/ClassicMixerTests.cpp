// SPDX-License-Identifier: AGPL-3.0-only
#include <QJsonArray>
#include <QJsonDocument>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>

namespace Mixer = WinJACKNexus::Common::ClassicMixer;
namespace
{
int checks = 0;
void check(bool value, const char *message)
{
    ++checks;
    if (!value)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void near(double value, double expected, const char *message, double tolerance = 1e-5)
{
    if (std::abs(value - expected) > tolerance)
        std::cerr << "actual=" << value << " expected=" << expected << '\n';
    check(std::isfinite(value) && std::abs(value - expected) <= tolerance, message);
}
struct Fixture
{
    static constexpr int frames = 1024;
    Mixer::Processor processor;
    std::array<std::array<float, frames>, Mixer::maximumAudioInputs> input{};
    std::array<std::array<float, frames>, Mixer::outputChannels> output{};
    std::array<const float *, Mixer::maximumAudioInputs> inputs{};
    std::array<float *, Mixer::outputChannels> outputs{};
    Fixture()
    {
        for (int index = 0; index < inputs.size(); ++index)
            inputs[index] = input[index].data();
        for (int index = 0; index < outputs.size(); ++index)
            outputs[index] = output[index].data();
    }
    void run(const Mixer::State &state)
    {
        check(processor.prepare(48000, frames, Mixer::configuration(state, 48000)), "prepare");
        processor.process(inputs.data(), outputs.data(), frames);
    }
    double response(Mixer::State state, double frequency, bool master = false)
    {
        for (int index = 0; index < frames; ++index)
            input[0][index] = static_cast<float>(
                0.1 * std::sin(2 * std::numbers::pi * frequency * index / 48000));
        state.inputs[0].pan = -1;
        check(processor.prepare(48000, frames, Mixer::configuration(state, 48000)), "EQ prepare");
        double inputEnergy = 0, outputEnergy = 0;
        for (int block = 0; block < 32; ++block)
        {
            // Continuous phase, avoiding discontinuities at the block boundary.
            for (int index = 0; index < frames; ++index)
                input[0][index] =
                    static_cast<float>(0.1 * std::sin(2 * std::numbers::pi * frequency *
                                                      (block * frames + index) / 48000));
            processor.process(inputs.data(), outputs.data(), frames);
            if (block > 16)
            {
                for (int index = 0; index < frames; ++index)
                {
                    inputEnergy += input[0][index] * input[0][index];
                    outputEnergy += output[0][index] * output[0][index];
                }
            }
        }
        return 10 * std::log10(outputEnergy / inputEnergy);
    }
};
} // namespace

int main()
{
    for (const int layout : {8, 16, 24})
    {
        auto state = Mixer::State::create(layout);
        check(state.validate(), "valid fixed layout");
        check(state.audioInputCount() == layout, "audio count");
        check(state.inputs.size() == (layout == 8 ? 7 : layout == 16 ? 12 : 20), "strip count");
        check(Mixer::ports(state).size() == layout + 8, "port count");
        state.inputs[0].name = QString::fromUtf8("人声 보컬 ボーカル");
        state.inputs[0].gain = 3;
        state.inputs[0].eq = {true, 1, -2, 3, 2400};
        state.inputs[0].sends = {-8, 2};
        state.inputs[0].pre = {true, false};
        state.inputs[0].enabled = {true, true};
        state.inputs[0].listen = Mixer::Listen::afl;
        check(state.savePreset(QString::fromUtf8("现场 라이브")), "save embedded preset");
        const auto encoded = QJsonDocument(state.toJson()).toJson().toBase64();
        Mixer::State restored;
        check(Mixer::State::fromJson(
                  QJsonDocument::fromJson(QByteArray::fromBase64(encoded)).object(), restored),
              "outer Base64 round trip");
        check(restored.toJson() == state.toJson(), "all state preserved");
        restored.inputs[0].gain = -3;
        Mixer::State applied;
        check(restored.applyPreset(restored.selectedPreset, applied), "embedded preset apply");
        near(applied.inputs[0].gain, 3, "preset parameter");
        check(applied.presets.size() == 1 &&
                  !QJsonDocument::fromJson(applied.presets[0].payload).object().contains("presets"),
              "nonrecursive payload");
        const auto before = restored.toJson();
        auto invalid = before;
        invalid.insert("masterFader", "bad");
        check(!Mixer::State::fromJson(invalid, restored), "reject wrong type");
        check(restored.toJson() == before, "invalid state preserves destination");
        invalid = before;
        invalid.insert("layoutId", 16.5);
        check(!Mixer::State::fromJson(invalid, restored), "reject fractional layout");
        invalid = before;
        auto presets = invalid.value("presets").toArray();
        auto preset = presets[0].toObject();
        preset.insert("payloadBase64", "#broken#");
        presets[0] = preset;
        invalid.insert("presets", presets);
        check(!Mixer::State::fromJson(invalid, restored), "reject malformed Base64");
        const auto ports = Mixer::ports(state);
        state.inputs[0].name = "Renamed";
        check(Mixer::ports(state)[0].id == ports[0].id &&
                  Mixer::ports(state)[0].name != ports[0].name,
              "rename stable ID");
    }
    auto state = Mixer::State::create(8);
    auto &channel = state.inputs[0];
    Fixture fixture;
    fixture.input[0].fill(1);
    fixture.run(state);
    near(fixture.output[0][100], std::numbers::sqrt2 / 2, "center left");
    near(fixture.output[1][100], std::numbers::sqrt2 / 2, "center right");
    near(fixture.output[6][100], fixture.output[0][100], "monitor follows master");
    channel.pan = -1;
    channel.enabled = {true, true};
    channel.sends = {0, 0};
    channel.pre = {true, false};
    channel.fader = -6;
    fixture.run(state);
    near(fixture.output[0][100], Mixer::gainForDb(-6), "fader");
    near(fixture.output[2][100], 1, "pre AUX after pan");
    near(fixture.output[3][100], 0, "pre AUX hard pan right");
    near(fixture.output[4][100], Mixer::gainForDb(-6), "post AUX follows fader");
    near(fixture.processor.levels().post[0][0], Mixer::gainForDb(-6), "mono meter equivalent");
    channel.pan = 1;
    fixture.run(state);
    near(fixture.output[2][100], 0, "pre AUX hard right left silence");
    near(fixture.output[3][100], 1, "pre AUX hard right");
    near(fixture.processor.levels().post[0][0], Mixer::gainForDb(-6),
         "mono meter no false silence");
    channel.mix = false;
    fixture.run(state);
    near(fixture.output[1][100], 0, "mix off");
    near(fixture.output[3][100], 1, "mix does not cut AUX");
    channel.mute = true;
    channel.listen = Mixer::Listen::afl;
    channel.gain = 6;
    channel.eq = {true, 12, 12, -12, 500};
    fixture.run(state);
    near(fixture.output[3][100], 0, "mute cuts pre AUX");
    near(fixture.output[6][100], Mixer::gainForDb(6) / std::numbers::sqrt2,
         "AFL gain after raw before EQ/pan/fader/mute");
    channel.listen = Mixer::Listen::pfl;
    fixture.run(state);
    near(fixture.output[6][100], 1 / std::numbers::sqrt2, "PFL before gain");
    state.masterMute = true;
    fixture.run(state);
    near(fixture.output[6][100], 1 / std::numbers::sqrt2, "master mute keeps PFL");
    state = Mixer::State::create(8);
    state.inputs[0].enabled[0] = true;
    state.inputs[0].sends[0] = 0;
    state.inputs[1].solo = true;
    fixture.run(state);
    near(fixture.output[0][100], 0, "solo excludes other master inputs");
    check(fixture.output[2][100] > 0, "solo does not cut AUX");
    state.inputs[0].solo = true;
    state.inputs[0].mix = false;
    fixture.run(state);
    near(fixture.output[0][100], 0, "solo cannot enable mix");
    state = Mixer::State::create(8);
    fixture.input[0].fill(0);
    fixture.input[6].fill(0.25f);
    fixture.input[7].fill(0.75f);
    fixture.run(state);
    near(fixture.output[0][100], 0.25, "stereo independent left");
    near(fixture.output[1][100], 0.75, "stereo independent right");
    state.inputs.back().pan = -1;
    fixture.run(state);
    near(fixture.output[0][100], 0.25, "balance preserves left");
    near(fixture.output[1][100], 0, "balance cuts right");
    state.inputs.back().fader = Mixer::silenceDb;
    fixture.run(state);
    near(fixture.output[0][100], 0, "true zero fader");
    near(Mixer::gainForDb(Mixer::faderDb(1)), 0, "bottom zero mapping");
    fixture.input[6].fill(0);
    fixture.input[7].fill(0);
    state = Mixer::State::create(8);
    state.inputs[0].eq = {true, 0, 6, 0, 1000};
    near(fixture.response(state, 1000), 6, "mid boost", 0.03);
    state.inputs[0].eq.frequency = 4000;
    check(fixture.response(state, 1000) < 1, "mid frequency moves response");
    near(fixture.response(state, 4000), 6, "moved mid center", 0.03);
    state.inputs[0].eq = {true, 6, 0, 0, 1000};
    check(fixture.response(state, 20) > 5.9, "low shelf");
    state.inputs[0].eq = {true, 0, 0, 6, 1000};
    check(fixture.response(state, 18000) > 5.8, "high shelf");
    state.inputs[0].eq.enabled = false;
    near(fixture.response(state, 1000), 0, "EQ bypass unity", 0.001);
    state.masterEq = {true, 0, -6, 0, 1000};
    near(fixture.response(state, 1000), -6, "master EQ", 0.03);
    for (const double rate : {8000, 44100, 96000, 192000})
    {
        check(fixture.processor.prepare(rate, Fixture::frames, Mixer::configuration(state, rate)),
              "sample rate prepare");
        fixture.processor.process(nullptr, fixture.outputs.data(), Fixture::frames);
        near(fixture.output[0][100], 0, "missing input silence");
    }
    check(!fixture.processor.prepare(0, 512, {}), "invalid sample rate");
    state = Mixer::State::create(8);
    state.inputs.back().name = "Master";
    check(!state.validate(), "reject reserved stereo port collision");
    state = Mixer::State::create(8);
    state.inputs[0].pan = -1;
    fixture.input[0].fill(1);
    fixture.run(state);
    state.inputs[0].fader = Mixer::silenceDb;
    fixture.processor.setConfiguration(Mixer::configuration(state, 48000));
    fixture.processor.process(fixture.inputs.data(), fixture.outputs.data(), Fixture::frames);
    check(fixture.output[0][0] > 0.9 && fixture.output[0][1000] < 0.02,
          "gain transitions are smoothed");
    fixture.processor.reset();
    fixture.processor.process(fixture.inputs.data(), fixture.outputs.data(), Fixture::frames);
    near(fixture.output[0][100], 0, "reset retains target parameters");
    state.inputs[0].fader = 0;
    state.inputs[0].enabled[0] = true;
    state.inputs[0].sends[0] = 0;
    state.auxMaster[0] = -6;
    state.masterMute = true;
    fixture.run(state);
    near(fixture.output[2][100], Mixer::gainForDb(-6), "AUX master independent of master mute");
    near(fixture.output[0][100], 0, "master mute final output");
    state.masterMute = false;
    state.inputs[0].listen = Mixer::Listen::pfl;
    state.inputs[1].listen = Mixer::Listen::pfl;
    fixture.input[1].fill(1);
    fixture.run(state);
    near(fixture.output[6][100], std::numbers::sqrt2, "multiple listen sources sum");
    state.monitorFader = -6;
    fixture.run(state);
    near(fixture.output[6][100], std::numbers::sqrt2 * Mixer::gainForDb(-6), "monitor fader");
    near(fixture.output[0][100], 1 + 1 / std::numbers::sqrt2,
         "monitor fader does not alter master");
    auto incompatible = Mixer::State::create(16);
    incompatible.presets = state.presets;
    check(state.savePreset("Eight"), "save for layout mismatch");
    incompatible.presets = state.presets;
    Mixer::State unchanged = incompatible;
    check(!incompatible.applyPreset(state.selectedPreset, unchanged),
          "cross-layout preset rejected");
    std::cout << "PASS " << checks << " offline Classic Mixer checks\n";
}
