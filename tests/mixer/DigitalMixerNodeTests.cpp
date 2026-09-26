/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/INode.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/NativeFx/graphic_eq/Processor.hpp>
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
bool check(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

template<class Function>
Function symbol(HMODULE library, const char* name)
{
    return reinterpret_cast<Function>(GetProcAddress(library, name));
}

struct SignalProbe final {
    WinJACKNexus::Audio::JackPort* source {};
    WinJACKNexus::Audio::JackPort* sink {};
    std::atomic<float> peak { 0.0F };
};

int probeProcess(jack_nframes_t frames, void* context) noexcept
{
    auto* probe = static_cast<SignalProbe*>(context);
    if (auto* output = probe->source->audioBuffer(frames))
        std::fill_n(output, frames, .25F);
    if (auto* input = probe->sink->audioBuffer(frames)) {
        float peak = 0.0F;
        for (jack_nframes_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::abs(input[i]));
        probe->peak.store(peak, std::memory_order_release);
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir isolatedSettings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedSettings.path());
    const auto path = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("nodes/mixer/mod_digital_mixer.dll"));
    const auto library = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (!check(library != nullptr && QFileInfo::exists(path), "Digital Mixer DLL exists and loads")) return 1;
    using Version = const char* (*)();
    using Count = std::uint32_t (*)();
    using Metadata = const char* (*)(std::uint32_t);
    using Create = WinJACKNexus::Node::INode* (*)(std::uint32_t);
    using Destroy = void (*)(WinJACKNexus::Node::INode*);
    using GetJson = const char* (*)(WinJACKNexus::Node::INode*);
    using SetJson = int (*)(WinJACKNexus::Node::INode*, const char*);
    using ClientId = const char* (*)(WinJACKNexus::Node::INode*);
    using FailRegistration = void (*)(WinJACKNexus::Node::INode*, int);
    const auto version = symbol<Version>(library, "wjn_get_node_api_version");
    const auto count = symbol<Count>(library, "wjn_get_node_count");
    const auto name = symbol<Metadata>(library, "wjn_get_node_name");
    const auto category = symbol<Metadata>(library, "wjn_get_node_category");
    const auto create = symbol<Create>(library, "wjn_create_node");
    const auto destroy = symbol<Destroy>(library, "wjn_destroy_node");
    const auto getJson = symbol<GetJson>(library, "wjn_get_node_settings_json");
    const auto setJson = symbol<SetJson>(library, "wjn_set_node_settings_json");
    const auto clientId = symbol<ClientId>(library, "wjn_get_node_client_id");
    const auto failRegistration = symbol<FailRegistration>(library, "wjn_digital_mixer_fail_registration_after");
    bool ok = check(version && count && name && category && create && destroy && getJson && setJson && clientId,
                    "all node API exports exist");
    if (!ok) { FreeLibrary(library); return 1; }
    ok &= check(std::strcmp(version(), "2") == 0 && count() == 1
                && std::strcmp(name(0), "Digital Mixer") == 0
                && std::strcmp(category(0), "Digital Mixer") == 0,
                "scan metadata identifies one Digital Mixer without instantiation");
    auto* first = create(0);
    ok &= check(first && !create(0) && !create(1), "factory enforces single instance");
    if (first) {
        ok &= check(first->init(), "node initializes with JACK service");
        WinJACKNexus::Audio::JackClient observer;
        ok &= check(observer.open({"MixerNodeTestProbe", false, false, nullptr}), "JACK observer opens");
        auto& api = WinJACKNexus::Audio::JackApi::instance();
        const auto exists = [&observer, &api](const char* shortName, int expectedFlag) {
            const QByteArray fullName = QByteArray("DigitalMixer:") + shortName;
            auto* port = api.portByName(observer.nativeHandle(), fullName.constData());
            return port && (api.portFlags(port) & expectedFlag) != 0;
        };
        ok &= check(clientId(first) && std::strcmp(clientId(first), "DigitalMixer") == 0
                    && exists("MAIN_L", JackPortIsOutput) && exists("MAIN_R", JackPortIsOutput),
                    "single client owns permanent MAIN output ports");
        const QByteArray original(getJson(first));
        ok &= check(QJsonDocument::fromJson(original).isObject(), "node owns serializable state");
        ok &= check(setJson(first, "{") == 0 && QByteArray(getJson(first)) == original,
                    "invalid restore leaves node state unchanged");
        ok &= check(setJson(first, original.constData()) == 1, "valid restore succeeds");
        WinJACKNexus::Common::DigitalMixerState topology;
        ok &= check(topology.loadJson(original), "test can inspect node topology");
        const auto ch = topology.add(WinJACKNexus::Common::MixerStripKind::input,
                                     WinJACKNexus::Common::MixerLayout::stereo);
        const auto aux = topology.add(WinJACKNexus::Common::MixerStripKind::aux,
                                      WinJACKNexus::Common::MixerLayout::mono);
        constexpr double unityFader = .95 * 12.0 / 84.0;
        topology.strip(ch)->faderPosition = unityFader;
        topology.strip(topology.main().id)->faderPosition = unityFader;
        const auto next = topology.toJson();
        ok &= check(setJson(first, next.constData()) == 1
                    && exists("CH1_L", JackPortIsInput) && exists("CH1_R", JackPortIsInput)
                    && exists("AUX1_M", JackPortIsOutput), "state transaction creates CH and AUX ports");
        WinJACKNexus::Audio::JackPort testOutput, testInput;
        SignalProbe signalProbe { &testOutput, &testInput };
        ok &= check(testOutput.registerPort(observer, "TestOut", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput)
                    && testInput.registerPort(observer, "TestIn", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput)
                    && observer.setProcessCallback(&probeProcess, &signalProbe) == 0
                    && observer.activate(), "signal fixture activates");
        const auto connected = testOutput.name()
            && api.connect(observer.nativeHandle(), testOutput.name(), "DigitalMixer:CH1_L") == 0;
        ok &= check(connected && testOutput.visitConnections(nullptr, nullptr) == 1,
                    "fixture connects to existing CH port");
        ok &= check(testInput.name()
                    && api.connect(observer.nativeHandle(), "DigitalMixer:MAIN_L", testInput.name()) == 0,
                    "fixture connects MAIN output to signal monitor");
        bool signalPassed = false;
        for (int attempt = 0; attempt < 100 && !signalPassed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            signalPassed = std::abs(signalProbe.peak.load(std::memory_order_acquire) - .25F) < .02F;
        }
        ok &= check(signalPassed, "JACK process mixes real CH samples into MAIN output");
        const auto waitUi = [](int milliseconds) {
            QEventLoop loop;
            QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const auto liveWindow = first->createWindow(nullptr);
        auto* mixerWidget = liveWindow ? QWidget::find(reinterpret_cast<WId>(liveWindow)) : nullptr;
        if (auto* menu = mixerWidget ? mixerWidget->findChild<QMenuBar*>(QStringLiteral("mixerMenuBar")) : nullptr)
            menu->actions().back()->trigger();
        waitUi(220);
        auto* mainPage = mixerWidget ? mixerWidget->findChild<QWidget*>(QStringLiteral("selMainPage")) : nullptr;
        bool independentMeters = false;
        if (mainPage) {
            QImage frame(mainPage->size(), QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            mainPage->render(&frame);
            const auto scale = std::min(mainPage->width() / 1394.0, mainPage->height() / 813.0);
            const auto right = 313.0 * scale
                + (mainPage->width() - 313.0 * scale - 1064.0 * scale) / 2.0;
            const auto offset = right - 313.0 * scale;
            const auto top = (mainPage->height() - 813.0 * scale) / 2.0;
            const auto green = [&](int row) {
                int count = 0;
                const auto y = qRound(top + (100 + row * 86 + 31) * scale);
                for (int x = 1170; x < 1230; ++x) {
                    const auto color = frame.pixelColor(qRound(offset + x * scale), y);
                    count += color.green() > color.red() * 1.5
                        && color.green() > color.blue() * 1.2;
                }
                return count;
            };
            const auto leftGreen = green(0), rightGreen = green(1);
            independentMeters = leftGreen > rightGreen + 20;
        }
        ok &= check(mainPage && independentMeters,
            "SEL Main L-only JACK input lights L channel meter without lighting R");
        const auto portNames = [&] {
            std::vector<QByteArray> names;
            if (const auto* ports = api.getPorts(observer.nativeHandle(), "DigitalMixer:.*", nullptr, 0)) {
                for (auto item = ports; *item; ++item) names.emplace_back(*item);
                api.freeMemory(const_cast<char**>(ports));
            }
            std::sort(names.begin(), names.end());
            return names;
        };
        const auto portsBeforeFx = portNames();
        ok &= check(!portsBeforeFx.empty(), "JACK reports mixer ports before FX insertion");
        const auto eqPath = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("nodes/native_fx/graphic_eq/mod_graphic_eq.dll"));
        const auto eqLibrary = LoadLibraryW(reinterpret_cast<LPCWSTR>(eqPath.utf16()));
        using FxCreate = WjnFxInstance* (*)(std::uint32_t);
        using FxDestroy = void (*)(WjnFxInstance*);
        using SnapshotSize = std::uint32_t (*)();
        using SnapshotExport = int (*)(const WjnFxInstance*, void*, std::uint32_t);
        const auto fxCreate = eqLibrary ? symbol<FxCreate>(eqLibrary, "wjn_fx_create_mixer") : nullptr;
        const auto fxDestroy = eqLibrary ? symbol<FxDestroy>(eqLibrary, "wjn_fx_destroy_mixer") : nullptr;
        const auto fxSize = eqLibrary ? symbol<SnapshotSize>(eqLibrary, "wjn_fx_parameter_snapshot_size") : nullptr;
        const auto fxExport = eqLibrary ? symbol<SnapshotExport>(eqLibrary, "wjn_fx_export_parameter_snapshot") : nullptr;
        ok &= check(fxCreate && fxDestroy && fxSize && fxExport,
                    "Graphic EQ offers mixer-only parameter snapshots");
        if (fxCreate && fxDestroy && fxSize && fxExport) {
            auto* guiInstance = fxCreate(0);
            QByteArray snapshot(static_cast<qsizetype>(fxSize()), '\0');
            ok &= check(guiInstance && fxExport(guiInstance, snapshot.data(), fxSize()) == 1,
                        "GUI-side Graphic EQ snapshot exports all channels");
            fxDestroy(guiInstance);
            const auto setGain = [&snapshot](float gain) {
                for (std::size_t channel = 0; channel < 8; ++channel) {
                    WinJACKNexus::Common::GraphicEqParameters parameters;
                    parameters.outputGainDb = gain;
                    std::memcpy(snapshot.data() + sizeof(WjnFxParameterSnapshotHeader)
                                + channel * sizeof(parameters), &parameters, sizeof(parameters));
                }
            };
            const auto waitPeak = [&signalProbe](float expected) {
                for (int attempt = 0; attempt < 100; ++attempt) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (std::abs(signalProbe.peak.load(std::memory_order_acquire) - expected) < .018F)
                        return true;
                }
                return false;
            };
            auto fxState = topology;
            auto* strip = fxState.strip(ch);
            strip->fxChainEnabled = true;
            strip->effects.push_back({QStringLiteral("graphic-eq"), true, -1, {}});
            setGain(-6.0206F);
            strip->effects.back().parameters.insert(QStringLiteral("snapshot"),
                QString::fromLatin1(snapshot.toBase64()));
            ok &= check(setJson(first, fxState.toJson().constData()) == 1 && waitPeak(.125F),
                        "CH Native Fx processes inside the existing JACK mixer client");
            if (mixerWidget) {
                if (auto* tab = mixerWidget->findChild<QPushButton*>(QStringLiteral("selTabFX")))
                    tab->click();
                waitUi(350);
                WinJACKNexus::UI::FxLevelMeterWidget* meter = nullptr;
                WinJACKNexus::UI::GraphicEqWidget* chart = nullptr;
                for (auto* child : mixerWidget->findChildren<QWidget*>()) {
                    if (!meter) meter = dynamic_cast<WinJACKNexus::UI::FxLevelMeterWidget*>(child);
                    if (!chart) chart = dynamic_cast<WinJACKNexus::UI::GraphicEqWidget*>(child);
                }
                ok &= check(meter && meter->inputDb() > -40.0 && meter->outputDb() > -40.0,
                    "loaded mixer FX meter remains live while JACK signal continues");
                ok &= check(chart && chart->hasSpectrumFrame(),
                    "loaded mixer Graphic EQ chart receives live spectrum frames");
                if (chart) {
                    const auto before = chart->spectrumFrameCount();
                    waitUi(250);
                    ok &= check(chart->spectrumFrameCount() > before,
                        "loaded mixer Graphic EQ chart keeps receiving spectrum frames");
                }
            }
            ok &= check(portNames() == portsBeforeFx,
                        "adding an internal FX instance creates no JACK ports");
            setGain(-12.0412F);
            strip->effects.back().parameters.insert(QStringLiteral("snapshot"),
                QString::fromLatin1(snapshot.toBase64()));
            ok &= check(setJson(first, fxState.toJson().constData()) == 1 && waitPeak(.0625F),
                        "snapshot update changes live parameters without rebuilding the FX chain");
            if (mixerWidget) {
                WinJACKNexus::UI::GraphicEqWidget* chart = nullptr;
                for (auto* child : mixerWidget->findChildren<QWidget*>())
                    if ((chart = dynamic_cast<WinJACKNexus::UI::GraphicEqWidget*>(child))) break;
                if (chart) {
                    const auto before = chart->spectrumFrameCount();
                    waitUi(250);
                    ok &= check(chart->spectrumFrameCount() > before,
                        "Graphic EQ chart remains live after a parameter change");
                }
            }
            strip->effects.back().enabled = false;
            ok &= check(setJson(first, fxState.toJson().constData()) == 1 && waitPeak(.25F),
                        "per-effect bypass restores dry CH audio");
            strip->effects.back().enabled = true;
            strip->fxChainEnabled = false;
            ok &= check(setJson(first, fxState.toJson().constData()) == 1 && waitPeak(.25F),
                        "FX chain bypass restores dry CH audio");
            ok &= check(setJson(first, next.constData()) == 1
                        && testOutput.visitConnections(nullptr, nullptr) == 1
                        && portNames() == portsBeforeFx,
                        "FX removal keeps JACK ports and their connections intact");
        }
        if (eqLibrary) FreeLibrary(eqLibrary);
        auto missingFx = topology;
        missingFx.strip(ch)->fxChainEnabled = true;
        missingFx.strip(ch)->effects.push_back({QStringLiteral("missing-native-effect"), true, -1, {}});
        ok &= check(setJson(first, missingFx.toJson().constData()) == 1
                    && std::abs(signalProbe.peak.load(std::memory_order_acquire) - .25F) < .02F
                    && portNames() == portsBeforeFx
                    && QByteArray(getJson(first)).contains("missing-native-effect"),
                    "unavailable Native FX is bypassed while its state and JACK ports remain intact");
        ok &= check(setJson(first, next.constData()) == 1,
                    "removing unavailable FX restores the normal chain");
        auto chain = topology;
        const auto aux2 = chain.add(WinJACKNexus::Common::MixerStripKind::aux,
                                    WinJACKNexus::Common::MixerLayout::mono);
        chain.strip(aux)->faderPosition = unityFader;
        chain.strip(aux2)->faderPosition = unityFader;
        chain.strip(ch)->mixToMain = false;
        chain.strip(aux)->mixToMain = false;
        chain.strip(aux2)->mixToMain = false;
        chain.strip(ch)->auxSendsEnabled = true;
        chain.strip(aux)->auxSendsEnabled = true;
        auto firstRoute = WinJACKNexus::Common::MixerAuxSend {aux, true, true,
            WinJACKNexus::Common::defaultMixerPanner(WinJACKNexus::Common::MixerLayout::stereo,
                                                       WinJACKNexus::Common::MixerLayout::mono)};
        const auto secondRoute = WinJACKNexus::Common::MixerAuxSend {aux2, true, false,
            WinJACKNexus::Common::defaultMixerPanner(WinJACKNexus::Common::MixerLayout::mono,
                                                       WinJACKNexus::Common::MixerLayout::mono)};
        ok &= check(chain.setSend(ch, firstRoute) && chain.setSend(aux, secondRoute),
                    "live CH to AUX1 to AUX2 graph is valid");
        const auto chainJson = chain.toJson();
        ok &= check(setJson(first, chainJson.constData()) == 1
                    && exists("AUX2_M", JackPortIsOutput)
                    && api.connect(observer.nativeHandle(), "DigitalMixer:AUX2_M", testInput.name()) == 0,
                    "live AUX chain has connected output");
        bool auxSignal = false;
        for (int attempt = 0; attempt < 100 && !auxSignal; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            auxSignal = signalProbe.peak.load(std::memory_order_acquire) > .01F;
        }
        ok &= check(auxSignal, "JACK samples traverse CH to AUX1 to AUX2");
        auto cyclic = chain;
        cyclic.strip(aux2)->sends.push_back({aux, false, true,
            WinJACKNexus::Common::defaultMixerPanner(WinJACKNexus::Common::MixerLayout::mono,
                                                       WinJACKNexus::Common::MixerLayout::mono)});
        const auto cyclicJson = cyclic.toJson();
        ok &= check(setJson(first, cyclicJson.constData()) == 0
                    && QByteArray(getJson(first)) == chainJson
                    && signalProbe.peak.load(std::memory_order_acquire) > .01F,
                    "rejected disabled AUX cycle preserves the live graph and samples");
        ok &= check(api.disconnect(observer.nativeHandle(), "DigitalMixer:AUX2_M", testInput.name()) == 0,
                    "AUX monitor disconnects for MIX isolation check");
        bool mainIsolated = false;
        for (int attempt = 0; attempt < 100 && !mainIsolated; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            mainIsolated = signalProbe.peak.load(std::memory_order_acquire) < .001F;
        }
        ok &= check(mainIsolated, "MIX disabled keeps AUX output while MAIN contribution is silent");
        ok &= check(api.connect(observer.nativeHandle(), "DigitalMixer:AUX2_M", testInput.name()) == 0,
                    "AUX monitor reconnects");
        chain.strip(ch)->faderPosition = 1.0;
        const auto preChainJson = chain.toJson();
        ok &= check(setJson(first, preChainJson.constData()) == 1,
                    "live CH fader reaches minus infinity");
        bool preSignal = false;
        for (int attempt = 0; attempt < 100 && !preSignal; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            preSignal = signalProbe.peak.load(std::memory_order_acquire) > .01F;
        }
        ok &= check(preSignal, "live Pre send survives minus-infinity CH fader");
        firstRoute.preFader = false;
        ok &= check(chain.setSend(ch, firstRoute), "live route switches to Post");
        const auto postChainJson = chain.toJson();
        ok &= check(setJson(first, postChainJson.constData()) == 1,
                    "live Post route commits");
        bool postSilent = false;
        for (int attempt = 0; attempt < 100 && !postSilent; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            postSilent = signalProbe.peak.load(std::memory_order_acquire) < .001F;
        }
        ok &= check(postSilent, "live Post send follows minus-infinity CH fader");
        chain.strip(ch)->faderPosition = unityFader;
        firstRoute.preFader = true;
        ok &= check(chain.setSend(ch, firstRoute), "live route restores Pre");
        chain.strip(ch)->mute = true;
        const auto mutedChainJson = chain.toJson();
        ok &= check(setJson(first, mutedChainJson.constData()) == 1,
                    "live MUTE edit commits while AUX chain is connected");
        bool auxSilent = false;
        for (int attempt = 0; attempt < 100 && !auxSilent; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            auxSilent = signalProbe.peak.load(std::memory_order_acquire) < .001F;
        }
        ok &= check(auxSilent, "live MUTE removes downstream AUX samples");
        ok &= check(setJson(first, next.constData()) == 1,
                    "live routing fixture restores original topology");
        ok &= check(api.disconnect(observer.nativeHandle(), testOutput.name(), "DigitalMixer:CH1_L") == 0,
                    "fixture disconnects CH input");
        bool disconnectedSilent = false;
        for (int attempt = 0; attempt < 100 && !disconnectedSilent; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            disconnectedSilent = signalProbe.peak.load(std::memory_order_acquire) < .001F;
        }
        ok &= check(disconnectedSilent, "disconnected live input clears MAIN output");
        ok &= check(api.connect(observer.nativeHandle(), testOutput.name(), "DigitalMixer:CH1_L") == 0,
                    "fixture reconnects CH input");
        ok &= check(topology.setLayout(ch, WinJACKNexus::Common::MixerLayout::surround51),
                    "CH layout changes in model");
        const auto surround = topology.toJson();
        ok &= check(setJson(first, surround.constData()) == 1
                    && exists("CH1_C", JackPortIsInput) && exists("CH1_Ls", JackPortIsInput)
                    && exists("MAIN_L", JackPortIsOutput)
                    && testOutput.visitConnections(nullptr, nullptr) == 1,
                    "layout transaction adds ports and preserves existing connection");
        if (failRegistration) {
            auto rejected = topology;
            rejected.add(WinJACKNexus::Common::MixerStripKind::input,
                         WinJACKNexus::Common::MixerLayout::stereo);
            failRegistration(first, 1);
            const auto attempted = rejected.toJson();
            ok &= check(setJson(first, attempted.constData()) == 0
                        && QByteArray(getJson(first)) == surround
                        && !exists("CH2_L", JackPortIsInput) && !exists("CH2_R", JackPortIsInput)
                        && exists("CH1_C", JackPortIsInput),
                        "injected second-port failure rolls back ports and model");
        }
        ok &= check(topology.remove(aux), "AUX deletion in model");
        const auto withoutAux = topology.toJson();
        ok &= check(setJson(first, withoutAux.constData()) == 1
                    && !exists("AUX1_M", JackPortIsOutput), "AUX deletion unregisters its output");
        const auto firstWindow = first->createWindow(nullptr);
        ok &= check(firstWindow != nullptr && first->createWindow(nullptr) == firstWindow,
                    "repeated open activates same window");
        auto* mixerWindow = qobject_cast<QMainWindow*>(QWidget::find(reinterpret_cast<WId>(firstWindow)));
        QMenu* inputMenu = nullptr;
        QMenu* auxMenu = nullptr;
        if (mixerWindow) {
            for (auto* menu : mixerWindow->findChildren<QMenu*>()) {
                if (menu->title() == QStringLiteral("添加输入通道")) inputMenu = menu;
                if (menu->title() == QStringLiteral("添加AUX通道")) auxMenu = menu;
            }
        }
        ok &= check(inputMenu && auxMenu, "bound mixer exposes CH and AUX commands");
        if (inputMenu) {
            for (auto* action : inputMenu->actions()) {
                if (action->text() == QStringLiteral("立体声 (2.0)")) { action->trigger(); break; }
            }
            ok &= check(exists("CH2_L", JackPortIsInput) && exists("CH2_R", JackPortIsInput),
                        "CH menu command commits ports through node");
        }
        if (auxMenu) {
            for (auto* action : auxMenu->actions()) {
                if (action->text() == QStringLiteral("5.1")) { action->trigger(); break; }
            }
            ok &= check(exists("AUX2_C", JackPortIsOutput) && exists("AUX2_Rs", JackPortIsOutput),
                        "AUX menu command commits ports through node");
        }
        testOutput.unregisterPort();
        testInput.unregisterPort();
        observer.close();
        first->destroyWindow(nullptr);
        QCoreApplication::processEvents();
        ok &= check(!create(0), "closing window does not release instance");
        ok &= check(first->createWindow(nullptr) != nullptr, "window can be reopened");
        first->shutdown();
        destroy(first);
    }
    auto* second = create(0);
    ok &= check(second != nullptr, "destroy releases factory occupancy");
    if (second) destroy(second);
    FreeLibrary(library);

    const auto patchbayPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mod_patchbay.dll"));
    const auto patchbay = LoadLibraryW(reinterpret_cast<LPCWSTR>(patchbayPath.utf16()));
    using ShowMixer = int (*)();
    const auto showMixer = patchbay ? symbol<ShowMixer>(patchbay, "wjn_patchbay_show_digital_mixer") : nullptr;
    ok &= check(showMixer && showMixer() == 0, "host query reports mixer absent without creating it");
    const auto createPatchbay = patchbay ? symbol<Create>(patchbay, "wjn_create_node") : nullptr;
    const auto destroyPatchbay = patchbay ? symbol<Destroy>(patchbay, "wjn_destroy_node") : nullptr;
    if (createPatchbay && destroyPatchbay && showMixer) {
        auto* host = createPatchbay(0);
        ok &= check(host && host->init(), "patchbay host initializes");
        if (host) {
            const auto handle = host->createWindow(nullptr);
            ok &= check(handle != nullptr && showMixer() == 0,
                        "unloaded mixer button does not create node");
            auto* window = QWidget::find(reinterpret_cast<WId>(handle));
            QTreeWidget* tree = nullptr;
            QTreeWidgetItem* item = nullptr;
            if (window) {
                for (auto* candidate : window->findChildren<QTreeWidget*>()) {
                    const auto matches = candidate->findItems(QStringLiteral("Digital Mixer"),
                        Qt::MatchExactly | Qt::MatchRecursive, 0);
                    for (auto* match : matches) {
                        if (!match->data(0, Qt::UserRole).toString().isEmpty()) {
                            tree = candidate;
                            item = match;
                            break;
                        }
                    }
                    if (item) break;
                }
            }
            ok &= check(tree && item, "patchbay scanner lists Digital Mixer metadata");
            if (tree && item) {
                const auto mixerItem = [tree] {
                    for (auto* match : tree->findItems(QStringLiteral("Digital Mixer"),
                             Qt::MatchExactly | Qt::MatchRecursive, 0))
                        if (!match->data(0, Qt::UserRole).toString().isEmpty()) return match;
                    return static_cast<QTreeWidgetItem*>(nullptr);
                };
                tree->itemDoubleClicked(item, 0);
                ok &= check(showMixer() == 1, "loaded node opens from host registry");
                auto* repeatedItem = mixerItem();
                ok &= check(repeatedItem != nullptr, "module remains discoverable after first load");
                if (repeatedItem) tree->itemDoubleClicked(repeatedItem, 0);
                const auto inHost = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
                const auto createAgain = inHost ? symbol<Create>(inHost, "wjn_create_node") : nullptr;
                ok &= check(showMixer() == 1 && createAgain && !createAgain(0),
                            "host duplicate load reuses sole module instance");
                if (inHost) FreeLibrary(inHost);

                QAction* unload = nullptr;
                for (auto* action : window->findChildren<QAction*>()) {
                    if (action->text() == QStringLiteral("卸载 Digital Mixer 节点")) {
                        unload = action;
                        break;
                    }
                }
                ok &= check(unload != nullptr, "patchbay exposes a node unload action");
                if (unload) unload->trigger();
                ok &= check(showMixer() == 0, "deletion immediately clears host registration");
                QTreeWidgetItem* reloadedItem = mixerItem();
                ok &= check(reloadedItem != nullptr, "node library remains available after deletion");
                if (reloadedItem) tree->itemDoubleClicked(reloadedItem, 0);
                ok &= check(showMixer() == 1, "node can be loaded again after deletion");
            }
            host->shutdown();
            ok &= check(showMixer() == 0, "host removal clears mixer registration");
            destroyPatchbay(host);
        }
    }
    if (patchbay) FreeLibrary(patchbay);
    return ok ? 0 : 1;
}
