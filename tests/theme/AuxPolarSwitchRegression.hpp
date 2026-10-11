/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <QApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <QVariant>

inline void verifyAuxPolarSwitches(void (*check)(bool, const char *))
{
    using namespace WinJACKNexus;
    Common::DigitalMixerState model;
    const auto source = model.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    const auto target = model.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    model.strip(source)->auxSendsEnabled = true;
    check(model.setSend(source, {target, true, true,
                                 Common::defaultMixerPanner(Common::MixerLayout::stereo,
                                                            Common::MixerLayout::stereo)}),
          "create AUX polar regression route");
    std::array<float, 960> audio;
    audio.fill(.25F);
    std::array<const float *, 2> inputs{audio.data(), audio.data()};
    std::array<std::array<float, 960>, 6> outputAudio{};
    std::array<float *, 6> outputs;
    for (std::size_t c = 0; c < outputs.size(); ++c)
        outputs[c] = outputAudio[c].data();
    Common::DigitalMixerProcessor processor;
    Common::MixerPolarSnapshot snapshot;
    check(processor.configure(model, 960) && processor.process(inputs, outputs, 960) &&
              processor.readPolar(source, target, snapshot),
          "enabled AUX supplies real polar data");
    for (bool master : {true, false})
    {
        model.strip(source)->auxSendsEnabled = master;
        model.strip(source)->sends[0].enabled = !master;
        Common::DigitalMixerProcessor disabled;
        check(disabled.configure(model, 960), "configure disabled AUX switches");
        disabled.adoptPolarHistory(processor);
        check(disabled.process(inputs, outputs, 960) &&
                  !disabled.readPolar(source, target, snapshot),
              "disabled AUX route suppresses both polar measurements");
        model.strip(source)->auxSendsEnabled = true;
        model.strip(source)->sends[0].enabled = true;
        Common::DigitalMixerProcessor enabled;
        check(enabled.configure(model, 960), "configure reopened AUX route");
        enabled.adoptPolarHistory(disabled);
        check(!enabled.readPolar(source, target, snapshot),
              "reopening AUX discards stale disabled history");
        check(enabled.process(inputs, outputs, 960) && enabled.readPolar(source, target, snapshot),
              "reopening AUX resumes fresh polar measurements");
        processor = std::move(enabled);
    }
    auto state = std::make_shared<Common::AuxMixerState>();
    UI::AuxSettingsWindow window(state);
    const auto key = QStringLiteral("polar-switch-regression");
    window.setSource(key, key);
    auto *bus = state->busForSource(key, window.selectedBusId());
    check(bus != nullptr, "select AUX view regression target");
    if (!bus)
        return;
    bus->enabled = true;
    window.setChainEnabled(true);
    int reads = 0;
    window.setPolarReadCallback([&](std::uint32_t, Common::MixerPolarSnapshot &result) {
        result = {};
        result.sampleCount = 960;
        result.sampleRate = 48000;
        result.firstSample = static_cast<std::uint64_t>(reads++) * 960;
        result.endSample = result.firstSample + 960;
        result.sourceLayout = result.targetLayout = Common::MixerLayout::stereo;
        result.inputSumSquares[0] = result.outputSumSquares[0] = 60.0;
        return true;
    });
    const auto frame = [] {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    };
    frame();
    check(reads > 0 && window.property("auxPolarDataValid").toBool(),
          "enabled AUX view shows polar data");
    QMouseEvent disableTarget(QEvent::MouseButtonPress, QPointF(48, 98), QPointF(48, 98),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &disableTarget);
    check(!bus->enabled, "click turns off the selected AUX send");
    const auto beforeOff = reads;
    check(!window.property("auxPolarDataValid").toBool(),
          "target-off clears both curves immediately");
    frame();
    check(reads == beforeOff && !window.property("auxPolarDataValid").toBool(),
          "disabled selected AUX never reads live source samples");
    bus->enabled = true;
    window.acceptExternalState();
    frame();
    check(reads > beforeOff && window.property("auxPolarDataValid").toBool(),
          "target-on restores live curves");
    window.setChainEnabled(false);
    const auto beforeMasterOff = reads;
    check(!window.property("auxPolarDataValid").toBool(),
          "master-off clears both curves immediately");
    frame();
    check(reads == beforeMasterOff, "disabled AUX master never reads live source samples");
}
