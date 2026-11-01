/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/AuxMixerState.hpp>
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>

#include <QApplication>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QScreen>
#include <QThread>
#include <QWindow>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace WinJACKNexus::Common;

namespace
{
bool check(bool value, const char *message)
{
    if (!value)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return value;
}

bool mainChannelPostRegression()
{
    constexpr std::size_t frames = 128;
    constexpr double halfDb = -6.020599913279624;
    const auto near = [](float actual, float expected) {
        return std::abs(actual - expected) < .0001F;
    };
    bool ok = true;
    for (const auto kind : {MixerStripKind::input, MixerStripKind::aux, MixerStripKind::subMix})
    {
        for (const auto layout : {MixerLayout::mono, MixerLayout::stereo, MixerLayout::surround51,
                                  MixerLayout::surround71})
        {
            DigitalMixerState state;
            const auto inputId = state.add(MixerStripKind::input, layout);
            auto sourceId = kind == MixerStripKind::input ? inputId : state.add(kind, layout);
            const auto preId = state.add(MixerStripKind::aux, layout);
            const auto postId =
                kind == MixerStripKind::subMix ? sourceId : state.add(MixerStripKind::aux, layout);
            state.strip(inputId)->mixToMain = false;
            if (sourceId != inputId)
            {
                state.strip(inputId)->auxSendsEnabled = true;
                ok &= check(state.setSend(inputId, {sourceId, true, kind != MixerStripKind::subMix,
                                                    defaultMixerPanner(layout, layout)}),
                            "source bus feed is valid");
            }
            state.strip(sourceId)->mixToMain = true;
            state.strip(sourceId)->auxSendsEnabled = kind != MixerStripKind::subMix;
            state.strip(sourceId)->inputGainDb = -halfDb;
            state.strip(sourceId)->faderPosition = .95 * (12.0 - halfDb) / 84.0;
            state.strip(preId)->mixToMain = false;
            if (kind != MixerStripKind::subMix)
            {
                state.strip(postId)->mixToMain = false;
                ok &= check(state.setSend(sourceId, {preId, true, true,
                                                     defaultMixerPanner(layout, layout)}) &&
                                state.setSend(sourceId, {postId, true, false,
                                                         defaultMixerPanner(layout, layout)}),
                            "Pre and Post routes are valid");
            }

            const auto channels = static_cast<std::size_t>(static_cast<int>(layout));
            std::vector<std::array<float, frames>> inputData(channels);
            std::vector<const float *> inputs;
            for (auto &channel : inputData)
            {
                channel.fill(.25F);
                inputs.push_back(channel.data());
            }
            DigitalMixerProcessor processor;
            ok &= check(processor.configure(state, frames), "Post regression graph configures");
            std::vector<std::array<float, frames>> outputData(processor.outputPortCount());
            std::vector<float *> outputs;
            for (auto &channel : outputData)
                outputs.push_back(channel.data());
            const auto effect = [](void *context, std::uint64_t id, std::uint32_t, float *samples,
                                   std::size_t count) noexcept {
                if (id == *static_cast<const std::uint64_t *>(context))
                    for (std::size_t frame = 0; frame < count; ++frame)
                        samples[frame] *= .5F;
            };
            const auto measure = [&] {
                if (!processor.configure(state, frames))
                    return false;
                processor.setEffectProcessor(&sourceId, effect);
                return processor.process(inputs, outputs, frames) &&
                       processor.process(inputs, outputs, frames);
            };
            const auto sample = [&](std::uint64_t id, std::size_t channel) {
                std::size_t index = 0;
                for (const auto &port : state.ports())
                {
                    if (port.direction != MixerPortDirection::output)
                        continue;
                    if (port.stripId == id && port.channelIndex == channel)
                        return outputData[index].back();
                    ++index;
                }
                return -1000.0F;
            };
            const auto preUnchanged = [&] {
                // Sub Mix exposes its Post output and has no AUX send interface.
                return kind == MixerStripKind::subMix || near(sample(preId, 0), .25F);
            };
            ok &= check(measure(), "baseline audio processes");
            state.strip(sourceId)->mainPanner.points[0].levelDb = halfDb;
            ok &= check(measure() && preUnchanged() && near(sample(postId, 0), .0625F),
                        "MAIN level precedes fader and Post; Pre remains after gain and FX");
            // Isolate channel zero to detect accidental double application in MAIN.
            for (std::size_t channel = 1; channel < channels; ++channel)
                inputData[channel].fill(0.0F);
            state.strip(sourceId)->mainPanner.points[0].levelDb = 0.0;
            ok &= check(measure(), "isolated baseline processes");
            const auto isolatedMain = sample(state.main().id, 0);
            state.strip(sourceId)->mainPanner.points[0].levelDb = halfDb;
            ok &= check(measure() && near(sample(state.main().id, 0), isolatedMain * .5F),
                        "MAIN channel level applies exactly once to Master");
            state.strip(sourceId)->mixToMain = false;
            ok &= check(measure() && near(sample(postId, 0), .0625F) &&
                            near(sample(state.main().id, 0), 0.0F),
                        "MIX off excludes Master without changing AUX Post");
            state.strip(sourceId)->mainPanner.points[0].mute = true;
            ok &= check(measure() && near(sample(postId, 0), 0.0F) && preUnchanged(),
                        "MAIN channel Mute closes Post while preserving Pre");
            state.strip(sourceId)->mainPanner.points[0].mute = false;
            if (channels > 1)
            {
                inputData[1].fill(.25F);
                state.strip(sourceId)->mainPanner.points[1].solo = true;
                ok &= check(measure() && near(sample(postId, 0), 0.0F) &&
                                near(sample(postId, 1), .125F) && preUnchanged(),
                            "MAIN channel Solo filters Post while preserving Pre");
                state.strip(sourceId)->mainPanner.points[1].solo = false;
            }
            if (kind != MixerStripKind::subMix && layout == MixerLayout::stereo)
            {
                state.strip(sourceId)->mainPanner.classicPan = 1.0;
                ok &= check(measure() && near(sample(postId, 0), .0625F),
                            "ordinary MAIN Pan remains independent of AUX Post");
            }
        }
    }
    return ok;
}

bool testClassicPanChannelDrag()
{
    using namespace WinJACKNexus;
    bool passed = check(QApplication::platformName() == QStringLiteral("windows"),
                        "classic Pan regression requires the Windows Qt backend");
    if (!passed)
        return false;
    QApplication::setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    for (const bool subMix : {false, true})
    {
        auto state = std::make_shared<Common::AuxMixerState>();
        UI::AuxSettingsWindow window(state);
        window.setSubMixMode(subMix);
        const auto source = QStringLiteral("classic-pan-drag");
        window.setSource(source, QStringLiteral("AUX / SubMix 分控"), 0,
                         Common::AuxChannelLayout::stereo);
        window.setChainEnabled(true);
        window.resize(1360, 813);
        window.show();
        for (int frame = 0; frame < 20; ++frame)
        {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        passed &=
            check(window.isVisible() && window.windowHandle() && window.windowHandle()->isExposed(),
                  "test window is visible and exposed");
        auto *bus = state->busForSource(source, 1);
        if (!check(bus && bus->panMode == Common::AuxPanMode::classic,
                   "stereo route starts in classic Pan"))
            return false;
        const auto drag = [&] {
            const QPointF from(1062, 132), to(1062, 112);
            QMouseEvent press(QEvent::MouseButtonPress, from, window.mapToGlobal(from.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &press);
            QMouseEvent move(QEvent::MouseMove, to, window.mapToGlobal(to.toPoint()), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, to, window.mapToGlobal(to.toPoint()),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&window, &release);
            QCoreApplication::processEvents();
        };
        bus->sendsLinked = false;
        drag();
        passed &= check(std::abs(bus->sends[0].levelDb - 4.0) < .001 &&
                            std::abs(bus->sends[1].levelDb) < .001,
                        "classic Pan independent drag changes only its own channel");
        bus->sendsLinked = true;
        drag();
        passed &= check(std::abs(bus->sends[0].levelDb - 8.0) < .001 &&
                            std::abs(bus->sends[1].levelDb - 4.0) < .001 && bus->classicPan == 0.0,
                        "classic Pan linked drag updates both channels without changing Pan");
        const auto image = window.screen()->grabWindow(window.winId());
        const auto screenshot = subMix ? QStringLiteral("build/release/submix-classic-1.0.1.png")
                                       : QStringLiteral("build/release/aux-classic-1.0.1.png");
        passed &= check(image.save(screenshot), "visible window screenshot saved");
        window.close();
        QCoreApplication::processEvents();
    }
    if (passed)
        std::puts("PASS: AUX/SubMix classic Pan real-window drag; window shows=2");
    return passed;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    const std::string_view mode(argv[1]);
    if (mode == "audio")
    {
        const bool passed = mainChannelPostRegression();
        if (passed)
            std::puts(
                "PASS: MAIN channel Pre/Post routing across Input/AUX/SubMix and 1/2/6/8 channels");
        return passed ? 0 : 1;
    }
    if (mode == "gui")
    {
        QApplication application(argc, argv);
        return testClassicPanChannelDrag() ? 0 : 1;
    }
    return 2;
}
