/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/MeterProbeRegistry.hpp>
#include <WinJACKNexus/Node/INode.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QWidget>

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

using GetStringFunction = const char* (*)();
using GetCountFunction = std::uint32_t (*)();
using GetIndexedStringFunction = const char* (*)(std::uint32_t index);
using CreateNodeFunction = WinJACKNexus::Node::INode* (*)(std::uint32_t index);
using DestroyNodeFunction = void (*)(WinJACKNexus::Node::INode*);
using CreateSettingsViewFunction = void* (*)(WinJACKNexus::Node::INode*, void*);
using DestroySettingsViewFunction = void (*)(WinJACKNexus::Node::INode*, void*);
using CreateSettingsWidgetFunction = QWidget* (*)(WinJACKNexus::Node::INode*, QWidget*);
using DestroySettingsWidgetFunction = void (*)(WinJACKNexus::Node::INode*, QWidget*);
using GetNodeSettingsJsonFunction = const char* (*)(WinJACKNexus::Node::INode*);
using SetNodeSettingsJsonFunction = int (*)(WinJACKNexus::Node::INode*, const char*);
using GetNodeClientIdFunction = const char* (*)(WinJACKNexus::Node::INode*);

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

std::optional<WinJACKNexus::Common::MeterProbeRegistration> meterProbeRegistration(std::string_view instanceId)
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    const auto iterator = std::find_if(registrations.cbegin(), registrations.cend(), [instanceId](const auto& registration) {
        return registration.descriptor.instanceId == instanceId;
    });
    return iterator == registrations.cend()
        ? std::nullopt
        : std::optional<WinJACKNexus::Common::MeterProbeRegistration> { *iterator };
}

bool waitForPublishedMeterFrame(std::string_view instanceId, std::uint64_t processedFrames)
{
    for (std::size_t attempt = 0; attempt < 8192; ++attempt) {
        const auto registration = meterProbeRegistration(instanceId);
        if (registration.has_value() && registration->realtimeState != nullptr
            && registration->realtimeState->isOnline()) {
            const auto snapshot = registration->realtimeState->frame().meter;
            if (snapshot.processedFrames > processedFrames
                && snapshot.samplePeakDbfs.valid
                && snapshot.lfePeakDbfs.valid
                && snapshot.lfePeakDbfs.value > -20.0)
                return true;
        }
        std::this_thread::yield();
    }
    return false;
}

struct MeterProbeTestSource final {
    WinJACKNexus::Audio::JackPort* output = nullptr;
};

int renderMeterProbeTestSignal(jack_nframes_t frames, void* userData) noexcept
{
    const auto* source = static_cast<MeterProbeTestSource*>(userData);
    if (source == nullptr || source->output == nullptr)
        return 0;
    auto* samples = source->output->audioBuffer(frames);
    if (samples == nullptr)
        return 0;
    for (jack_nframes_t index = 0; index < frames; ++index)
        samples[index] = 0.5F;
    return 0;
}

bool checkMeterProbePorts(std::string_view clientId,
                          std::initializer_list<std::string_view> expectedPortNames)
{
    WinJACKNexus::Audio::JackClient inspector;
    if (!check(inspector.open({ "Meter-Probe-Inspector", true, false, nullptr }),
               "meter probe test opens a JACK graph inspector"))
        return false;

    bool passed = true;
    auto& api = WinJACKNexus::Audio::JackApi::instance();
    for (const auto portName : expectedPortNames) {
        const auto portId = std::string(clientId) + ":" + std::string(portName);
        const auto* port = api.portByName(inspector.nativeHandle(), portId.c_str());
        passed &= check(port != nullptr, "meter probe registers every channel-named input port");
        if (port != nullptr) {
            const auto flags = api.portFlags(port);
            passed &= check((flags & JackPortIsInput) != 0 && (flags & JackPortIsOutput) == 0
                                && std::string_view(api.portType(port)) == JACK_DEFAULT_AUDIO_TYPE,
                            "meter probe ports are audio inputs only");
        }
    }
    inspector.close();
    return passed;
}

struct ConnectionLookup final {
    std::string_view expectedPortId;
    bool found {};
};

bool findConnection(const char* portId, void* userData) noexcept
{
    auto* lookup = static_cast<ConnectionLookup*>(userData);
    if (lookup == nullptr || portId == nullptr)
        return false;
    lookup->found = lookup->expectedPortId == portId;
    return !lookup->found;
}

bool outputConnectsTo(const WinJACKNexus::Audio::JackPort& output, std::string_view destinationPortId)
{
    for (std::size_t attempt = 0; attempt < 256; ++attempt) {
        ConnectionLookup lookup { destinationPortId };
        output.visitConnections(&findConnection, &lookup);
        if (lookup.found)
            return true;
        std::this_thread::yield();
    }
    return false;
}

std::filesystem::path executableDirectory()
{
    std::wstring path(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

bool testApiStub(const std::filesystem::path& directory)
{
    const auto path = directory / L"node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "node.dll loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    const auto createSettingsView = reinterpret_cast<CreateSettingsViewFunction>(GetProcAddress(module, "wjn_create_settings_view"));
    const auto destroySettingsView = reinterpret_cast<DestroySettingsViewFunction>(GetProcAddress(module, "wjn_destroy_settings_view"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
                        && createSettingsView != nullptr && destroySettingsView != nullptr,
                    "node.dll exports the complete Node C API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
        && createSettingsView != nullptr && destroySettingsView != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 0 && std::string_view(name(0)) == "WinJACKNexus Node API"
                            && std::string_view(category(0)) == "Interface",
                        "node.dll reports stable API metadata");
        passed &= check(create(0) == nullptr, "node.dll remains an interface-only stub");
    }
    FreeLibrary(module);
    return passed;
}

bool testExampleNode(const std::filesystem::path& directory)
{
    const auto path = directory / L"wjn_example_node.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "example node loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "example node exports the complete Node C API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 1 && std::string_view(name(0)) == "Example Node"
                            && std::string_view(category(0)) == "Tools",
                        "example node reports stable metadata");
        auto* node = create(0);
        passed &= check(node != nullptr, "example node creates an INode instance");
        if (node != nullptr) {
            const auto info = node->info();
            passed &= check(std::string_view(info.name) == "Example Node" && std::string_view(info.version) == "1.0.0",
                            "example INode exposes NodeInfo metadata");
            passed &= check(node->init(), "example INode initializes");
            passed &= check(node->createWindow(nullptr) == nullptr, "example INode has no window skeleton implementation");
            node->shutdown();
            destroy(node);
        }
    }
    FreeLibrary(module);
    return passed;
}

bool testAdapterModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"nodes" / L"adapter" / L"mod_adapter.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "adapter module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    const auto createSettingsView = reinterpret_cast<CreateSettingsViewFunction>(GetProcAddress(module, "wjn_create_settings_view"));
    const auto destroySettingsView = reinterpret_cast<DestroySettingsViewFunction>(GetProcAddress(module, "wjn_destroy_settings_view"));
    const auto getNodeSettingsJson = reinterpret_cast<GetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_get_node_settings_json"));
    const auto setNodeSettingsJson = reinterpret_cast<SetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_set_node_settings_json"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && create != nullptr && destroy != nullptr
                        && createSettingsView != nullptr && destroySettingsView != nullptr
                        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr,
                    "adapter module exports the complete Node v2 API");
    if (version != nullptr && count != nullptr && name != nullptr && create != nullptr && destroy != nullptr
        && createSettingsView != nullptr && destroySettingsView != nullptr
        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr) {
        constexpr std::string_view expectedNames[] { "WASAPI In", "WASAPI Out", "System MIDI In", "System MIDI Out" };
        passed &= check(std::string_view(version()) == "2" && count() == std::size(expectedNames),
                        "adapter module reports four nodes");
        for (std::uint32_t index = 0; index < count(); ++index) {
            passed &= check(std::string_view(name(index)) == expectedNames[index], "adapter node name matches its index");
            auto* node = create(index);
            passed &= check(node != nullptr && node->init(), "adapter node skeleton initializes");
            if (node != nullptr) {
                const auto* settings = getNodeSettingsJson(node);
                passed &= check(settings != nullptr && setNodeSettingsJson(node, settings) == 1,
                                "adapter node round-trips its JSON settings");
                destroySettingsView(node, createSettingsView(node, nullptr));
                node->shutdown();
                destroy(node);
            }
        }
        passed &= check(create(count()) == nullptr, "adapter rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

bool testMeterbridgeModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"mod_meterbrigde.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "meterbridge module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr,
                    "meterbridge module exports the Node API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr) {
        passed &= check(std::string_view(version()) == "2" && count() == 1
                            && std::string_view(name(0)) == "Meterbridge"
                            && std::string_view(category(0)) == "Meter",
                        "meterbridge module reports stable metadata");
        auto* node = create(0);
        passed &= check(node != nullptr && node->init(), "meterbridge module initializes its top-level node");
        if (node != nullptr) {
            const auto info = node->info();
            passed &= check(std::string_view(info.name) == "Meterbridge" && std::string_view(info.category) == "Meter",
                            "meterbridge INode exposes stable metadata");
            node->shutdown();
            destroy(node);
        }
        passed &= check(create(count()) == nullptr, "meterbridge module rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

bool testMeterProbeModule(const std::filesystem::path& directory)
{
    const auto path = directory / L"nodes" / L"meter" / L"mod_meterbridge_probe.dll";
    const auto module = LoadLibraryW(path.c_str());
    bool passed = check(module != nullptr, "meter probe module loads dynamically");
    if (module == nullptr)
        return false;

    const auto version = reinterpret_cast<GetStringFunction>(GetProcAddress(module, "wjn_get_node_api_version"));
    const auto count = reinterpret_cast<GetCountFunction>(GetProcAddress(module, "wjn_get_node_count"));
    const auto name = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_name"));
    const auto category = reinterpret_cast<GetIndexedStringFunction>(GetProcAddress(module, "wjn_get_node_category"));
    const auto create = reinterpret_cast<CreateNodeFunction>(GetProcAddress(module, "wjn_create_node"));
    const auto destroy = reinterpret_cast<DestroyNodeFunction>(GetProcAddress(module, "wjn_destroy_node"));
    const auto createSettingsWidget = reinterpret_cast<CreateSettingsWidgetFunction>(GetProcAddress(module, "wjn_create_settings_widget"));
    const auto destroySettingsWidget = reinterpret_cast<DestroySettingsWidgetFunction>(GetProcAddress(module, "wjn_destroy_settings_widget"));
    const auto getNodeSettingsJson = reinterpret_cast<GetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_get_node_settings_json"));
    const auto setNodeSettingsJson = reinterpret_cast<SetNodeSettingsJsonFunction>(GetProcAddress(module, "wjn_set_node_settings_json"));
    const auto getNodeClientId = reinterpret_cast<GetNodeClientIdFunction>(GetProcAddress(module, "wjn_get_node_client_id"));
    passed &= check(version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
                        && createSettingsWidget != nullptr && destroySettingsWidget != nullptr
                        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr && getNodeClientId != nullptr,
                    "meter probe module exports the complete Node v2 API");
    if (version != nullptr && count != nullptr && name != nullptr && category != nullptr && create != nullptr && destroy != nullptr
        && createSettingsWidget != nullptr && destroySettingsWidget != nullptr
        && getNodeSettingsJson != nullptr && setNodeSettingsJson != nullptr && getNodeClientId != nullptr) {
        constexpr std::string_view expectedNames[] {
            "Meter Mono", "Meter Stereo", "Meter 5.1", "Meter 7.1", "Meter Custom"
        };
        constexpr std::string_view customSettings = R"({"schemaVersion":1,"nodeKind":"custom","instanceId":"meter-node-test-custom","clientName":"Meter-Node-Tests","presetId":"ebu-r128","followGlobalPreset":false,"autoReset":false,"silenceThresholdDbfs":-60.0,"silenceDurationSeconds":1.25,"channelCount":3,"channels":[{"name":"Left","role":"frontLeft","lfe":false},{"name":"LFE","role":"lfe","lfe":true},{"name":"Right","role":"frontRight","lfe":false}]})";
        constexpr std::string_view renamedCustomSettings = R"({"schemaVersion":1,"nodeKind":"custom","instanceId":"meter-node-test-custom","clientName":"Meter-Node-Tests-Renamed","presetId":"ebu-r128","followGlobalPreset":false,"autoReset":false,"silenceThresholdDbfs":-60.0,"silenceDurationSeconds":1.25,"channelCount":3,"channels":[{"name":"Custom-Left","role":"frontLeft","lfe":false},{"name":"LFE","role":"lfe","lfe":true},{"name":"Right","role":"frontRight","lfe":false}]})";
        constexpr std::string_view invalidMonoLayout = R"({"schemaVersion":1,"nodeKind":"mono","channelCount":2})";
        passed &= check(std::string_view(version()) == "2" && count() == std::size(expectedNames),
                        "meter probe module reports five probe nodes");
        for (std::uint32_t index = 0; index < count(); ++index) {
            passed &= check(std::string_view(name(index)) == expectedNames[index]
                                && std::string_view(category(index)) == "Meter",
                            "meter probe metadata matches its index");
            auto* node = create(index);
            passed &= check(node != nullptr, "meter probe creates an INode instance");
            if (node != nullptr) {
                const auto* defaultSettings = getNodeSettingsJson(node);
                passed &= check(defaultSettings != nullptr && *defaultSettings != '\0'
                                    && setNodeSettingsJson(node, defaultSettings) == 1,
                                "meter probe round-trips default JSON settings");
                if (index == 0) {
                    passed &= check(setNodeSettingsJson(node, invalidMonoLayout.data()) == 0,
                                    "fixed-layout meter probes reject channel-count changes");
                }
                if (index == 4) {
                    passed &= check(setNodeSettingsJson(node, customSettings.data()) == 1,
                                    "custom meter probe restores its layout and persisted options");
                }
                passed &= check(node->init(), "meter probe node initializes JACK inputs");
                const auto info = node->info();
                passed &= check(std::string_view(info.name) == expectedNames[index]
                                    && std::string_view(info.category) == "Meter",
                                "meter probe INode exposes stable metadata");
                const auto* clientId = getNodeClientId(node);
                passed &= check(clientId != nullptr && *clientId != '\0',
                                "meter probe reports its actual JACK client ID");
                const auto initialClientId = clientId == nullptr ? std::string {} : std::string(clientId);
                if (!initialClientId.empty()) {
                    switch (index) {
                    case 0:
                        passed &= checkMeterProbePorts(initialClientId, { "M" });
                        break;
                    case 1:
                        passed &= checkMeterProbePorts(initialClientId, { "L", "R" });
                        break;
                    case 2:
                        passed &= checkMeterProbePorts(initialClientId, { "L", "R", "C", "LFE", "Ls", "Rs" });
                        break;
                    case 3:
                        passed &= checkMeterProbePorts(initialClientId, { "L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb" });
                        break;
                    case 4:
                        passed &= checkMeterProbePorts(initialClientId, { "Left", "LFE", "Right" });
                        break;
                    default:
                        break;
                    }
                }
                auto* settingsWidget = createSettingsWidget(node, nullptr);
                passed &= check(settingsWidget != nullptr, "meter probe exposes a settings sidebar widget");
                if (settingsWidget != nullptr) {
                    auto* clientNameEditor = settingsWidget->findChild<QLineEdit*>(QStringLiteral("meterProbeClientNameEditor"));
                    auto* autoResetCheckBox = settingsWidget->findChild<QCheckBox*>(QStringLiteral("meterProbeAutoResetCheckBox"));
                    auto* applyButton = settingsWidget->findChild<QPushButton*>(QStringLiteral("meterProbeApplyButton"));
                    passed &= check(clientNameEditor != nullptr && autoResetCheckBox != nullptr && applyButton != nullptr,
                                    "all meter probe types expose client-name and auto-reset controls");
                    if (index == 0 && clientNameEditor != nullptr && applyButton != nullptr) {
                        clientNameEditor->setText(QStringLiteral("Meter-Node-Mono-Renamed"));
                        applyButton->click();
                        QCoreApplication::processEvents();
                        clientId = getNodeClientId(node);
                        passed &= check(clientId != nullptr && std::string_view(clientId).starts_with("Meter-Node-Mono-Renamed"),
                                        "fixed-layout meter probes apply client-name changes from the sidebar");
                        if (clientId != nullptr && *clientId != '\0')
                            passed &= checkMeterProbePorts(clientId, { "M" });
                    }
                    if (index == 4) {
                        auto* channelCountSpinBox = settingsWidget->findChild<QSpinBox*>(QStringLiteral("meterProbeChannelCountSpinBox"));
                        auto* channelTable = settingsWidget->findChild<QTableWidget*>(QStringLiteral("meterProbeChannelTable"));
                        passed &= check(channelCountSpinBox != nullptr && channelTable != nullptr && channelTable->rowCount() == 3,
                                        "custom meter probe exposes its channel count and channel table");
                        if (channelCountSpinBox != nullptr && channelTable != nullptr && applyButton != nullptr) {
                            passed &= check(channelTable->cellWidget(0, 1) != nullptr
                                                && channelTable->cellWidget(0, 2) != nullptr
                                                && channelTable->cellWidget(0, 3) != nullptr,
                                            "custom meter probe exposes channel name, role, and LFE controls");
                            channelCountSpinBox->setValue(4);
                            QCoreApplication::processEvents();
                            applyButton->click();
                            QCoreApplication::processEvents();
                            const auto* settingsJson = getNodeSettingsJson(node);
                            passed &= check(settingsJson != nullptr && std::string_view(settingsJson).find("\"channelCount\":4") != std::string_view::npos,
                                            "custom meter probe applies channel-count changes from the sidebar");
                            clientId = getNodeClientId(node);
                            if (clientId != nullptr && *clientId != '\0')
                                passed &= checkMeterProbePorts(clientId, { "Left", "LFE", "Right", "Ch 4" });
                        }
                    }
                    destroySettingsWidget(node, settingsWidget);
                }
                if (index == 4 && !initialClientId.empty()) {
                        constexpr std::string_view customInstanceId = "meter-node-test-custom";
                        const auto registrationBeforeConnection = meterProbeRegistration(customInstanceId);
                        passed &= check(registrationBeforeConnection.has_value()
                                            && registrationBeforeConnection->realtimeState != nullptr
                                            && registrationBeforeConnection->realtimeState->isOnline()
                                            && registrationBeforeConnection->descriptor.clientName == initialClientId
                                            && registrationBeforeConnection->descriptor.channelCount == 4,
                                        "meter probe registers its live Common descriptor after JACK activation");
                        const auto initialProcessedFrames = registrationBeforeConnection.has_value()
                            ? registrationBeforeConnection->realtimeState->frame().meter.processedFrames
                            : 0;
                    WinJACKNexus::Audio::JackClient sourceClient;
                    WinJACKNexus::Audio::JackPort sourcePort;
                        MeterProbeTestSource source { &sourcePort };
                    const auto sourceReady = sourceClient.open({ "Meter-Probe-Test-Source", true, false, nullptr })
                        && sourcePort.registerPort(sourceClient, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput)
                            && sourceClient.setProcessCallback(&renderMeterProbeTestSignal, &source) == 0
                        && sourceClient.activate();
                    passed &= check(sourceReady, "meter probe test creates an external JACK audio source");
                    const auto originalDestination = initialClientId + ":Left";
                        const auto originalLfeDestination = initialClientId + ":LFE";
                    if (sourceReady) {
                        const auto connected = WinJACKNexus::Audio::JackGraph::connect(sourceClient,
                                                                                         sourcePort.name(),
                                                                                         originalDestination.c_str());
                            const auto lfeConnected = WinJACKNexus::Audio::JackGraph::connect(sourceClient,
                                                                                                 sourcePort.name(),
                                                                                                 originalLfeDestination.c_str());
                        passed &= check(connected, "meter probe accepts an external audio connection");
                            passed &= check(lfeConnected, "meter probe accepts an external LFE audio connection");
                        if (connected) {
                            passed &= check(outputConnectsTo(sourcePort, originalDestination),
                                            "meter probe exposes the original JACK input connection");
                        }
                            if (lfeConnected) {
                                passed &= check(waitForPublishedMeterFrame(customInstanceId, initialProcessedFrames),
                                                "meter probe JACK callback publishes a non-silent Common meter frame");
                            }
                    }
                    passed &= check(setNodeSettingsJson(node, renamedCustomSettings.data()) == 1,
                                    "custom meter probe transactionally rebuilds after a client-name update");
                    clientId = getNodeClientId(node);
                    passed &= check(clientId != nullptr && std::string_view(clientId).starts_with("Meter-Node-Tests-Renamed"),
                                    "meter probe keeps the actual renamed JACK client ID");
                    if (sourceReady && clientId != nullptr && *clientId != '\0') {
                        const auto renamedDestination = std::string(clientId) + ":Custom-Left";
                        passed &= check(outputConnectsTo(sourcePort, renamedDestination),
                                        "meter probe restores JACK input connections after channel-name edits");
                            passed &= check(outputConnectsTo(sourcePort, std::string(clientId) + ":LFE"),
                                            "meter probe restores LFE connections after client rename");
                            const auto renamedRegistration = meterProbeRegistration(customInstanceId);
                            passed &= check(renamedRegistration.has_value()
                                                && renamedRegistration->descriptor.clientName == clientId
                                                && renamedRegistration->nameRevision > 1,
                                            "meter probe updates its Common descriptor after a client rename");
                    }
                    sourceClient.deactivate();
                    sourcePort.unregisterPort();
                    sourceClient.close();
                }
                node->shutdown();
                    if (index == 4)
                        passed &= check(!meterProbeRegistration("meter-node-test-custom").has_value(),
                                        "meter probe unregisters its Common descriptor on shutdown");
                destroy(node);
            }
        }
        passed &= check(create(count()) == nullptr, "meter probe module rejects an out-of-range node index");
    }
    FreeLibrary(module);
    return passed;
}

}

int main(int argc, char** argv)
{
    const auto directory = executableDirectory();
    auto passed = testApiStub(directory)
        && testExampleNode(directory)
        && testAdapterModule(directory)
        && testMeterbridgeModule(directory);
    if (passed) {
        QApplication application(argc, argv);
        passed = testMeterProbeModule(directory);
    }
    if (!passed)
        return 1;
    std::puts("Node tests passed");
    return 0;
}