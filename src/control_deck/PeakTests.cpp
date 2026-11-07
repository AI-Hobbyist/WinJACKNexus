// SPDX-License-Identifier: AGPL-3.0-only
#include "NodePaths.hpp"
#include "Service.hpp"
#include <QApplication>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLibrary>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <array>
#include <cmath>
#include <cstdio>

namespace SS = WinJACKNexus::ControlDeck;
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.platformName() != "windows")
        return 2;
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    auto &service = SS::Service::instance();
    const std::array kinds{"peak", "peak_stereo", "peak_5_1", "peak_7_1"};
    const std::array<unsigned, 4> counts{1, 2, 6, 8};
    for (unsigned variant = 0; variant < kinds.size(); ++variant)
    {
        const auto kind = QString::fromLatin1(kinds[variant]);
        const auto count = counts[variant];
        QLibrary library(app.applicationDirPath() + "/nodes/control_deck/" + SS::moduleRelativeFolder(kind) +
                         "/wjn_signal_switch_" + kind + ".dll");
        if (!library.load())
            return 1;
        const auto create =
            reinterpret_cast<WinJACKNexus::Node::INode *(*)(uint32_t)>(library.resolve("wjn_create_node_v4"));
        const auto destroy =
            reinterpret_cast<void (*)(WinJACKNexus::Node::INode *)>(library.resolve("wjn_destroy_node"));
        const auto getApi = reinterpret_cast<const WjnNodeVirtualApiV1 *(*)()>(
            library.resolve("wjn_get_node_virtual_api_v1"));
        if (!create || !destroy || !getApi)
            return 1;
        auto *node = create(WJN_NODE_MODE_VIRT);
        const auto *api = getApi();
        WjnNodePrepareV1 prepare{sizeof(prepare), 64, 48000, nullptr};
        if (!node || !api || !api->prepare(node, &prepare) || api->portCount(node) != count)
            return 1;
        for (unsigned channel = 0; channel < count; ++channel)
        {
            WjnNodePortV1 port{sizeof(port)};
            if (!api->port(node, channel, &port) || port.direction != WJN_NODE_PORT_INPUT)
                return 1;
        }
        std::array<std::array<float, 64>, 8> samples{};
        std::array<float *, 8> pointers{};
        std::array<WjnNodeAudioBufferV1, 8> inputs{};
        for (unsigned channel = 0; channel < count; ++channel)
        {
            samples[channel].fill(float(channel + 1) / 16);
            pointers[channel] = samples[channel].data();
            inputs[channel] = {channel + 1, 1, &pointers[channel]};
        }
        WjnNodeProcessV1 block{};
        block.structSize = sizeof(block);
        block.frames = 64;
        block.audioInputs = inputs.data();
        block.audioInputCount = count;
        api->process(node, &block);
        auto &endpoint = service.endpoints[node];
        if (SS::peakChannelCount(endpoint) != count)
            return 1;
        for (unsigned channel = 0; channel < count; ++channel)
            if (endpoint.peak[channel].load() != float(channel + 1) / 16)
                return 1;
        auto *editor = service.showEditor();
        editor->resize(1100, 800);
        if (!QTest::qWaitForWindowExposed(editor))
            return 1;
        QTimer feed;
        WinJACKNexus::UI::bindRefreshTimer(feed, editor);
        QObject::connect(&feed, &QTimer::timeout, editor, [&] { api->process(node, &block); });
        feed.start();
        for (bool vertical : {false, true})
        {
            endpoint.peakVertical = vertical;
            service.state.cards[endpoint.id].geometry =
                QRect(15, 15, vertical ? int(count) * 85 + 40 : 600, vertical ? 430 : int(count) * 82 + 40);
            api->process(node, &block);
            service.notify();
            auto *card = editor->findChild<QWidget *>("controlDeckButton." + endpoint.id);
            if (!card)
                return 1;
            for (unsigned channel = 0; channel < count; ++channel)
            {
                const auto objectName = channel == 0
                                            ? QStringLiteral("controlDeck.peakMeter")
                                            : QStringLiteral("controlDeck.peakMeter.%1").arg(channel);
                auto *meter = dynamic_cast<WinJACKNexus::UI::FxLevelMeterWidget *>(
                    card->findChild<QWidget *>(objectName));
                const double expected = 20 * std::log10(double(channel + 1) / 16);
                if (!meter || !meter->property("wjn.readoutOutside").toBool() ||
                    std::abs(meter->inputDb() - expected) > 0.001 ||
                    meter->property("wjn.vertical").toBool() != vertical)
                    return 1;
            }
            QTest::qWait(180);
            if (variant == 3 && !editor->screen()
                                     ->grabWindow(editor->winId())
                                     .save(app.applicationDirPath() +
                                           (vertical ? "/../../control-deck-peak-7.1-vertical.png"
                                                     : "/../../control-deck-peak-7.1-horizontal.png")))
                return 1;
        }
        feed.stop();
        api->reset(node);
        for (unsigned channel = 0; channel < count; ++channel)
            if (endpoint.peak[channel].load() != 0)
                return 1;
        api->stop(node);
        destroy(node);
        std::printf(
            "PASS %s: input-only ports, independent levels, horizontal/vertical outside readouts and reset\n",
            kinds[variant]);
    }
    service.shutdownUI();
    std::puts("PASS real Windows meter variants; no JACK server; bounded four editor shows");
    return 0;
}
