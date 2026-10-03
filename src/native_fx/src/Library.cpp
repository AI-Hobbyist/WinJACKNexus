/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#ifndef WJN_FX_MODULE_INDEX
#error Each Native Fx DLL must declare WJN_FX_MODULE_INDEX
#endif
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/NativeFx/I18n.hpp>
#if WJN_FX_MODULE_INDEX == 0
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 1
#include <WinJACKNexus/NativeFx/graphic_eq/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 2
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 3
#include <WinJACKNexus/NativeFx/compressor/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 4
#include <WinJACKNexus/NativeFx/limiter/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 5
#include <WinJACKNexus/NativeFx/delay/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 6
#include <WinJACKNexus/NativeFx/parametric_eq/Ui.hpp>
#elif WJN_FX_MODULE_INDEX == 7
#include <WinJACKNexus/NativeFx/reverb/Ui.hpp>
#endif

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
using WinJACKNexus::Audio::JackClient;
using WinJACKNexus::Audio::JackPort;
#if WJN_FX_MODULE_INDEX == 0
using WinJACKNexus::Common::GateParameters;
using WinJACKNexus::Common::GateProcessor;
using WinJACKNexus::UI::GateEffectWidget;
#endif
constexpr auto channelNames = WinJACKNexus::Common::surroundChannelNames;

bool fixedNodePorts(const std::array<JackPort, 8>& inputs,
                    const std::array<JackPort, 8>& outputs) noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        char inputName[16] {}, outputName[16] {};
        std::snprintf(inputName, sizeof(inputName), "in_%s", channelNames[i]);
        std::snprintf(outputName, sizeof(outputName), "out_%s", channelNames[i]);
        const auto* actualInput = inputs[i].shortName();
        const auto* actualOutput = outputs[i].shortName();
        const auto* inputType = inputs[i].type();
        const auto* outputType = outputs[i].type();
        if (!actualInput || !actualOutput || !inputType || !outputType
            || std::strcmp(actualInput, inputName) != 0
            || std::strcmp(actualOutput, outputName) != 0
            || std::strcmp(inputType, JACK_DEFAULT_AUDIO_TYPE) != 0
            || std::strcmp(outputType, JACK_DEFAULT_AUDIO_TYPE) != 0
            || !inputs[i].isInput() || inputs[i].isOutput()
            || !outputs[i].isOutput() || outputs[i].isInput()) return false;
    }
    return true;
}


bool readNumber(const QJsonObject& object,const char* key,float minimum,float maximum,float& target) {
    const auto item=object.value(QString::fromLatin1(key));
    if(!item.isDouble() || !std::isfinite(item.toDouble()) || item.toDouble()<minimum || item.toDouble()>maximum)
        return false;
    target=static_cast<float>(item.toDouble()); return true;
}

#if WJN_FX_MODULE_INDEX == 0
#include "../effects/gate/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 1
#include "../effects/graphic_eq/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 2
#include "../effects/deesser/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 3
#include "../effects/compressor/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 4
#include "../effects/limiter/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 5
#include "../effects/delay/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 6
#include "../effects/parametric_eq/include/Logic.inc"
#endif
#if WJN_FX_MODULE_INDEX == 7
#include "../effects/reverb/include/Logic.inc"
#endif

#if WJN_FX_MODULE_INDEX == 0
using ActiveNode = GateNode;
using ActiveEditor = WinJACKNexus::UI::GateEffectWidget;
#elif WJN_FX_MODULE_INDEX == 1
using ActiveNode = GraphicEqNode;
using ActiveEditor = WinJACKNexus::UI::GraphicEqWidget;
#elif WJN_FX_MODULE_INDEX == 2
using ActiveNode = DeEsserNode;
using ActiveEditor = WinJACKNexus::UI::DeEsserWidget;
#elif WJN_FX_MODULE_INDEX == 3
using ActiveNode = CompressorNode;
using ActiveEditor = WinJACKNexus::UI::CompressorWidget;
#elif WJN_FX_MODULE_INDEX == 4
using ActiveNode = LimiterNode;
using ActiveEditor = WinJACKNexus::UI::LimiterWidget;
#elif WJN_FX_MODULE_INDEX == 5
using ActiveNode = DelayNode;
using ActiveEditor = WinJACKNexus::UI::DelayWidget;
#elif WJN_FX_MODULE_INDEX == 6
using ActiveNode = ParametricEqNode;
using ActiveEditor = WinJACKNexus::UI::ParametricEqWidget;
#elif WJN_FX_MODULE_INDEX == 7
using ActiveNode = ReverbNode;
using ActiveEditor = WinJACKNexus::UI::ReverbWidget;
#endif
}

extern "C" {
std::uint32_t wjn_fx_node_boundary_version(void) { return WJN_FX_NODE_BOUNDARY_VERSION; }
int wjn_fx_node_validate_layout(void* node) {
    auto* effect = dynamic_cast<ActiveNode*>(static_cast<WinJACKNexus::Node::INode*>(node));
    return effect != nullptr && effect->hasFixedPortLayout() ? 1 : 0;
}
const char* wjn_fx_node_client_name(void* node) {
    auto* effect = dynamic_cast<ActiveNode*>(static_cast<WinJACKNexus::Node::INode*>(node));
    return effect == nullptr ? nullptr : effect->clientName();
}
int wjn_fx_node_set_client_name(void* node, const char* utf8Name) {
    auto* effect = dynamic_cast<ActiveNode*>(static_cast<WinJACKNexus::Node::INode*>(node));
    if (effect == nullptr || utf8Name == nullptr) return 0;
    try { return effect->setClientName(QString::fromUtf8(utf8Name)) ? 1 : 0; }
    catch (...) { return 0; }
}
const char* wjn_get_node_api_version(void) { return "2"; }
std::uint32_t wjn_get_node_count(void) {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < wjn_fx_count(); ++i)
        if (wjn_fx_meta(i)->placements & WJN_FX_NODE) ++count;
    return count;
}
static std::uint32_t nodeCatalogIndex(std::uint32_t nodeIndex) {
    for (std::uint32_t i = 0; i < wjn_fx_count(); ++i)
        if ((wjn_fx_meta(i)->placements & WJN_FX_NODE) && nodeIndex-- == 0) return i;
    return wjn_fx_count();
}
#if WJN_FX_MODULE_INDEX <= 7
std::uint32_t wjn_get_node_theme_api_version(void) { return WJN_NODE_THEME_API_VERSION; }
const char* wjn_get_node_theme_namespace_json(std::uint32_t index) {
    return wjn_fx_theme_namespace_json(nodeCatalogIndex(index));
}
#endif
const char* wjn_get_node_name(std::uint32_t index) {
    if (const auto* meta = wjn_fx_meta(nodeCatalogIndex(index))) return meta->name;
    return "";
}
const char* wjn_get_node_category(std::uint32_t index) {
    return nodeCatalogIndex(index) < wjn_fx_count() ? "Native Fx" : "";
}
// Small visual identity supplied by the Native Fx module to chain hosts.
WINJACK_NODE_API const char* wjn_get_node_icon_glyph(std::uint32_t index) {
    if (const auto* meta = wjn_fx_meta(nodeCatalogIndex(index))) return meta->iconGlyph;
    return "?";
}
WINJACK_NODE_API const char* wjn_get_node_icon_color(std::uint32_t index) {
    if (const auto* meta = wjn_fx_meta(nodeCatalogIndex(index))) return meta->iconColor;
    return "#607b99";
}
WINJACK_NODE_API void wjn_set_node_editor_channel(QWidget* editor, int channel) {
    if (auto* widget=dynamic_cast<ActiveEditor*>(editor)) widget->setSelectedChannel(channel);
}
WINJACK_NODE_API QWidget* wjn_create_embedded_effect_widget(WinJACKNexus::Node::INode* node, QWidget* parent) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) return effect->createEffectWidget(parent);
    return nullptr;
}
WinJACKNexus::Node::INode* wjn_create_node(std::uint32_t index) {
    if (nodeCatalogIndex(index) >= wjn_fx_count()) return nullptr;
    return new ActiveNode;
}
void wjn_destroy_node(WinJACKNexus::Node::INode* node) { delete node; }
void* wjn_create_settings_view(WinJACKNexus::Node::INode*,void*) { return nullptr; }
void wjn_destroy_settings_view(WinJACKNexus::Node::INode*,void*) {}
QWidget* wjn_create_settings_widget(WinJACKNexus::Node::INode* node,QWidget* parent) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) return effect->createSettingsWidget(parent);
    return nullptr;
}
void wjn_destroy_settings_widget(WinJACKNexus::Node::INode* node,QWidget* widget) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) effect->destroySettingsWidget(widget);
}
const char* wjn_get_node_settings_json(WinJACKNexus::Node::INode* node) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) return effect->settingsJson();
    return "";
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode* node,const char* json) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) return effect->restoreSettingsJson(json);
    return 0;
}
WINJACK_NODE_API const char* wjn_get_node_client_id(WinJACKNexus::Node::INode* node) {
    if (auto* effect=dynamic_cast<ActiveNode*>(node)) return effect->clientId();
    return "";
}
WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode* node) {
    return node!=nullptr && node->createWindow(nullptr)!=nullptr?1:0;
}
}
