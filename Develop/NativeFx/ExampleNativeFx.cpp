/* Minimal dual-placement Native Fx example. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include "../I18nExample.hpp"

#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
using WinJACKNexus::Audio::JackClient;
using WinJACKNexus::Audio::JackGraph;
using WinJACKNexus::Audio::JackPort;
namespace Controls = WinJACKNexus::UI::Controls;
constexpr const char *channels[]{"L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb"};
constexpr const char *moduleId = "example-gain";

class GainPanel final : public QWidget
{
  public:
    GainPanel(bool standalone, std::function<float(int)> read,
              std::function<void(int, float)> write, QWidget *parent = nullptr)
        : QWidget(parent), read_(std::move(read)), write_(std::move(write))
    {
        auto *layout = new QVBoxLayout(this);
        auto *group = new Controls::GroupPanel(QStringLiteral("Example Gain"), this);
        auto *form = new QFormLayout(group);
        Controls::ChoiceField *selector = nullptr;
        Controls::TextLabel *channelLabel = nullptr;
        if (standalone)
        {
            selector = new Controls::ChoiceField(group);
            selector->addItem(QStringLiteral("All"));
            for (const auto *name : channels)
                selector->addItem(QString::fromLatin1(name));
            channelLabel = new Controls::TextLabel(QStringLiteral("Channel"), group);
            form->addRow(channelLabel, selector);
            QObject::connect(selector, qOverload<int>(&QComboBox::currentIndexChanged), this,
                             [this](int index) { setChannel(index - 1); });
        }
        gain_ = new Controls::DecimalField(group);
        gain_->setRange(0.0, 2.0);
        gain_->setSingleStep(0.05);
        gain_->setDecimals(2);
        auto *gainLabel = new Controls::TextLabel(QStringLiteral("Gain"), group);
        form->addRow(gainLabel, gain_);
        QObject::connect(gain_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                         [this](double value) { write_(channel_, static_cast<float>(value)); });
        layout->addWidget(group);
        setChannel(-1);
        ExampleI18n::bind(this, [this, group, selector, channelLabel, gainLabel] {
            setWindowTitle(ExampleI18n::text("example.nativefx", "name", "Example Gain"));
            group->setTitle(ExampleI18n::text("example.nativefx", "name", "Example Gain"));
            gainLabel->setText(ExampleI18n::text("example.nativefx", "gain", "Gain"));
            if (channelLabel)
                channelLabel->setText(ExampleI18n::text("example.nativefx", "channel", "Channel"));
            if (selector)
            {
                const QSignalBlocker block(selector);
                selector->setItemText(0, ExampleI18n::text("example.nativefx", "all", "All"));
            }
        });
    }

    void setChannel(int channel)
    {
        channel_ = channel >= 0 && channel < 8 ? channel : -1;
        const QSignalBlocker block(gain_);
        gain_->setValue(read_(channel_));
    }

  private:
    std::function<float(int)> read_;
    std::function<void(int, float)> write_;
    Controls::DecimalField *gain_ = nullptr;
    int channel_ = -1;
};

struct PortConnections
{
    std::vector<std::string> peers;
    bool failed = false;
};

class ExampleGainNode final : public WinJACKNexus::Node::INode
{
  public:
    ExampleGainNode()
    {
        for (auto &gain : gains_)
            gain.store(1.0F);
    }
    ~ExampleGainNode() override
    {
        destroyWindow(nullptr);
        shutdown();
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"Example Gain", "Native Fx", "1.0.0", "WinJACKNexus",
                "Fixed eight-channel gain example"};
    }

    bool init() noexcept override
    {
        return openClient();
    }
    void shutdown() noexcept override
    {
        client_.deactivate();
        for (auto &port : inputs_)
            port.unregisterPort();
        for (auto &port : outputs_)
            port.unregisterPort();
        client_.close();
    }

    HWND createWindow(HWND) noexcept override
    {
        try
        {
            if (!window_)
            {
                window_ = new GainPanel(
                    true, [this](int channel) { return gains_[channel < 0 ? 0 : channel].load(); },
                    [this](int channel, float gain) {
                        if (channel < 0)
                            for (auto &value : gains_)
                                value.store(gain);
                        else
                            gains_[channel].store(gain);
                    });
                ExampleI18n::bind(window_, [this] {
                    window_->setWindowTitle(
                        ExampleI18n::text("example.nativefx", "name", "Example Gain") +
                        QStringLiteral(" — ") + QString::fromStdString(clientName_));
                });
            }
            window_->show();
            window_->raise();
            window_->activateWindow();
            return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void destroyWindow(HWND) noexcept override
    {
        delete window_.data();
    }
    const char *clientName() const noexcept
    {
        return clientName_.c_str();
    }
    const char *clientId() const noexcept
    {
        return clientId_.c_str();
    }

    bool fixedPortLayout() const noexcept
    {
        for (std::size_t i = 0; i < 8; ++i)
        {
            char inputName[16]{}, outputName[16]{};
            std::snprintf(inputName, sizeof(inputName), "in_%s", channels[i]);
            std::snprintf(outputName, sizeof(outputName), "out_%s", channels[i]);
            const auto *in = inputs_[i].shortName();
            const auto *out = outputs_[i].shortName();
            const auto *inType = inputs_[i].type();
            const auto *outType = outputs_[i].type();
            if (!in || !out || !inType || !outType || std::strcmp(in, inputName) ||
                std::strcmp(out, outputName) || std::strcmp(inType, JACK_DEFAULT_AUDIO_TYPE) ||
                std::strcmp(outType, JACK_DEFAULT_AUDIO_TYPE) || !inputs_[i].isInput() ||
                inputs_[i].isOutput() || !outputs_[i].isOutput() || outputs_[i].isInput())
                return false;
        }
        return true;
    }

    bool setClientName(const char *utf8) noexcept
    {
        if (!utf8)
            return false;
        const auto requested = QString::fromUtf8(utf8).trimmed();
        if (requested.isEmpty() || requested.size() > 32)
            return false;
        try
        {
            const auto next = requested.toStdString();
            if (next == clientName_)
                return true;
            if (!client_.isOpen())
            {
                clientName_ = next;
                clientId_ = next;
                if (window_)
                    window_->setWindowTitle(
                        ExampleI18n::text("example.nativefx", "name", "Example Gain") +
                        QStringLiteral(" — ") + requested);
                return true;
            }
            std::array<PortConnections, 16> connections;
            const auto collect = [](const char *name, void *context) noexcept {
                auto *entry = static_cast<PortConnections *>(context);
                try
                {
                    entry->peers.emplace_back(name);
                    return true;
                }
                catch (...)
                {
                    entry->failed = true;
                    return false;
                }
            };
            for (std::size_t i = 0; i < 8; ++i)
            {
                inputs_[i].visitConnections(collect, &connections[i]);
                outputs_[i].visitConnections(collect, &connections[i + 8]);
            }
            for (const auto &entry : connections)
                if (entry.failed)
                    return false;
            const auto old = clientName_;
            shutdown();
            clientName_ = next;
            if (!openClient())
            {
                clientName_ = old;
                if (openClient())
                    reconnect(connections);
                return false;
            }
            if (window_)
                window_->setWindowTitle(
                    ExampleI18n::text("example.nativefx", "name", "Example Gain") +
                    QStringLiteral(" — ") + requested);
            return reconnect(connections);
        }
        catch (...)
        {
            return false;
        }
    }

    const char *settingsJson() noexcept
    {
        try
        {
            QJsonArray gains;
            for (const auto &gain : gains_)
                gains.append(gain.load());
            json_ = QJsonDocument(QJsonObject{{"clientName", QString::fromStdString(clientName_)},
                                              {"gains", gains}})
                        .toJson(QJsonDocument::Compact)
                        .toStdString();
            return json_.c_str();
        }
        catch (...)
        {
            return "{}";
        }
    }

    bool restoreSettings(const char *json) noexcept
    {
        if (!json || client_.isOpen())
            return false;
        try
        {
            const auto document = QJsonDocument::fromJson(QByteArray(json));
            if (!document.isObject())
                return false;
            const auto object = document.object();
            const auto name = object.value(QStringLiteral("clientName")).toString().trimmed();
            const auto gains = object.value(QStringLiteral("gains")).toArray();
            if (name.isEmpty() || name.size() > 32 || gains.size() != 8)
                return false;
            std::array<float, 8> next{};
            for (int i = 0; i < 8; ++i)
            {
                if (!gains[i].isDouble() || gains[i].toDouble() < 0.0 || gains[i].toDouble() > 2.0)
                    return false;
                next[i] = static_cast<float>(gains[i].toDouble());
            }
            clientName_ = name.toStdString();
            clientId_ = clientName_;
            for (int i = 0; i < 8; ++i)
                gains_[i].store(next[i]);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

  private:
    bool openClient() noexcept
    {
        if (!client_.open({clientName_.c_str(), true, false, nullptr}))
            return false;
        try
        {
            clientId_ = client_.name() ? client_.name() : clientName_;
        }
        catch (...)
        {
            shutdown();
            return false;
        }
        for (std::size_t i = 0; i < 8; ++i)
        {
            char inputName[16]{}, outputName[16]{};
            std::snprintf(inputName, sizeof(inputName), "in_%s", channels[i]);
            std::snprintf(outputName, sizeof(outputName), "out_%s", channels[i]);
            if (!inputs_[i].registerPort(client_, inputName, JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsInput) ||
                !outputs_[i].registerPort(client_, outputName, JACK_DEFAULT_AUDIO_TYPE,
                                          JackPortIsOutput))
            {
                shutdown();
                return false;
            }
        }
        if (client_.setProcessCallback(&ExampleGainNode::process, this) != 0 || !client_.activate())
        {
            shutdown();
            return false;
        }
        return true;
    }

    bool reconnect(const std::array<PortConnections, 16> &connections) noexcept
    {
        bool success = true;
        for (std::size_t i = 0; i < 8; ++i)
        {
            const auto *input = inputs_[i].name();
            const auto *output = outputs_[i].name();
            if (!input || !output)
                return false;
            for (const auto &peer : connections[i].peers)
                success = JackGraph::connect(client_, peer.c_str(), input) && success;
            for (const auto &peer : connections[i + 8].peers)
                success = JackGraph::connect(client_, output, peer.c_str()) && success;
        }
        return success;
    }

    static int process(jack_nframes_t frames, void *context) noexcept
    {
        auto *self = static_cast<ExampleGainNode *>(context);
        for (std::size_t i = 0; i < 8; ++i)
        {
            const auto *input = self->inputs_[i].audioBuffer(frames);
            auto *output = self->outputs_[i].audioBuffer(frames);
            if (!output)
                continue;
            const auto gain = self->gains_[i].load(std::memory_order_relaxed);
            for (jack_nframes_t frame = 0; frame < frames; ++frame)
                output[frame] = input ? input[frame] * gain : 0.0F;
        }
        return 0;
    }

    JackClient client_;
    std::array<JackPort, 8> inputs_, outputs_;
    std::array<std::atomic<float>, 8> gains_{};
    std::string clientName_ = "ExampleGain", clientId_ = "ExampleGain", json_;
    QPointer<GainPanel> window_;
};
}

struct WjnFxInstance
{
    std::array<float, 8> gains{1, 1, 1, 1, 1, 1, 1, 1};
    int channel = -1;
    QPointer<GainPanel> editor;
    WjnFxParametersChanged changed = nullptr;
    void *changedContext = nullptr;
};

extern "C"
{
    uint32_t wjn_fx_api_version(void)
    {
        return WJN_FX_API_VERSION;
    }
    uint32_t wjn_fx_count(void)
    {
        return 1;
    }
    const WjnFxMeta *wjn_fx_meta(uint32_t index)
    {
        static const WjnFxMeta meta{moduleId, "Example Gain", "G", "#49a3cb",
                                    WJN_FX_MIXER | WJN_FX_NODE};
        return index == 0 ? &meta : nullptr;
    }
    WjnFxInstance *wjn_fx_create_mixer(uint32_t index)
    {
        return index == 0 ? new WjnFxInstance : nullptr;
    }
    WjnFxInstance *wjn_fx_clone_mixer(const WjnFxInstance *source)
    {
        if (!source)
            return nullptr;
        auto *clone = new WjnFxInstance;
        clone->gains = source->gains;
        clone->channel = source->channel;
        return clone;
    }
    void wjn_fx_destroy_mixer(WjnFxInstance *instance)
    {
        if (!instance)
            return;
        delete instance->editor.data();
        delete instance;
    }
    void wjn_fx_prepare(WjnFxInstance *, double) {}
    void wjn_fx_process(WjnFxInstance *instance, uint32_t channel, const float *input,
                        float *output, uint32_t frames)
    {
        if (!instance || !output || channel >= 8)
            return;
        const auto gain = instance->gains[channel];
        for (uint32_t i = 0; i < frames; ++i)
            output[i] = input ? input[i] * gain : 0.0F;
    }
    uint32_t wjn_fx_parameter_snapshot_version(void)
    {
        return WJN_FX_PARAMETER_SNAPSHOT_VERSION;
    }
    uint32_t wjn_fx_parameter_snapshot_size(void)
    {
        return sizeof(WjnFxParameterSnapshotHeader) + sizeof(float) * 8;
    }
    int wjn_fx_export_parameter_snapshot(const WjnFxInstance *instance, void *destination,
                                         uint32_t bytes)
    {
        if (!instance || !destination || bytes < wjn_fx_parameter_snapshot_size())
            return 0;
        WjnFxParameterSnapshotHeader header{};
        header.formatVersion = WJN_FX_PARAMETER_SNAPSHOT_VERSION;
        header.parameterBytes = sizeof(float);
        header.channelMask = 0xff;
        std::memcpy(header.moduleId, moduleId, std::strlen(moduleId));
        std::memcpy(destination, &header, sizeof(header));
        std::memcpy(static_cast<char *>(destination) + sizeof(header), instance->gains.data(),
                    sizeof(float) * 8);
        return 1;
    }
    int wjn_fx_import_parameter_snapshot(WjnFxInstance *instance, const void *source,
                                         uint32_t bytes)
    {
        if (!instance || !source || bytes != wjn_fx_parameter_snapshot_size())
            return 0;
        WjnFxParameterSnapshotHeader header{};
        std::memcpy(&header, source, sizeof(header));
        if (header.formatVersion != WJN_FX_PARAMETER_SNAPSHOT_VERSION ||
            header.parameterBytes != sizeof(float) || header.channelMask == 0 ||
            (header.channelMask & ~0xffu) ||
            std::strncmp(header.moduleId, moduleId, sizeof(header.moduleId)) != 0)
            return 0;
        std::array<float, 8> gains{};
        std::memcpy(gains.data(), static_cast<const char *>(source) + sizeof(header),
                    sizeof(float) * 8);
        for (std::size_t i = 0; i < 8; ++i)
            if ((header.channelMask & (1u << i)) &&
                (!std::isfinite(gains[i]) || gains[i] < 0 || gains[i] > 2))
                return 0;
        for (std::size_t i = 0; i < 8; ++i)
            if (header.channelMask & (1u << i))
                instance->gains[i] = gains[i];
        if (instance->editor)
            instance->editor->setChannel(instance->channel);
        if (instance->changed)
            instance->changed(instance->changedContext);
        return 1;
    }
    void wjn_fx_set_parameter_change_callback(WjnFxInstance *instance,
                                              WjnFxParametersChanged changed, void *context)
    {
        if (instance)
        {
            instance->changed = changed;
            instance->changedContext = context;
        }
    }
    void wjn_fx_set_channel(WjnFxInstance *instance, int channel)
    {
        if (!instance)
            return;
        instance->channel = channel >= 0 && channel < 8 ? channel : -1;
        if (instance->editor)
            instance->editor->setChannel(instance->channel);
    }
    QWidget *wjn_fx_create_mixer_editor(WjnFxInstance *instance, QWidget *parent)
    {
        if (!instance)
            return nullptr;
        if (!instance->editor)
        {
            instance->editor = new GainPanel(
                false,
                [instance](int channel) { return instance->gains[channel < 0 ? 0 : channel]; },
                [instance](int channel, float gain) {
                    if (channel < 0)
                        instance->gains.fill(gain);
                    else
                        instance->gains[channel] = gain;
                    if (instance->changed)
                        instance->changed(instance->changedContext);
                },
                parent);
        }
        instance->editor->setChannel(instance->channel);
        return instance->editor;
    }

    uint32_t wjn_fx_node_boundary_version(void)
    {
        return WJN_FX_NODE_BOUNDARY_VERSION;
    }
    int wjn_fx_node_validate_layout(void *node)
    {
        auto *effect =
            dynamic_cast<ExampleGainNode *>(static_cast<WinJACKNexus::Node::INode *>(node));
        return effect && effect->fixedPortLayout() ? 1 : 0;
    }
    const char *wjn_fx_node_client_name(void *node)
    {
        auto *effect =
            dynamic_cast<ExampleGainNode *>(static_cast<WinJACKNexus::Node::INode *>(node));
        return effect ? effect->clientName() : nullptr;
    }
    int wjn_fx_node_set_client_name(void *node, const char *utf8Name)
    {
        auto *effect =
            dynamic_cast<ExampleGainNode *>(static_cast<WinJACKNexus::Node::INode *>(node));
        return effect && effect->setClientName(utf8Name) ? 1 : 0;
    }
    const char *wjn_get_node_api_version(void)
    {
        return "2";
    }
    uint32_t wjn_fx_i18n_api_version(void)
    {
        return WJN_I18N_API_VERSION;
    }
    uint32_t wjn_get_node_i18n_api_version(void)
    {
        return WJN_I18N_API_VERSION;
    }
    const char *wjn_fx_i18n_descriptor_json(uint32_t index)
    {
        return index == 0 ? R"json({"descriptorVersion":1,"i18nApiVersion":1,
        "moduleId":"example-gain","domains":["example.nativefx"],"locatePath":"locate","catalogVersion":1,
        "displayNameKey":{"domain":"example.nativefx","key":"name"}})json"
                          : nullptr;
    }
    const char *wjn_get_node_i18n_descriptor_json(uint32_t index)
    {
        return wjn_fx_i18n_descriptor_json(index);
    }
    uint32_t wjn_get_node_count(void)
    {
        return 1;
    }
    const char *wjn_get_node_name(uint32_t index)
    {
        return index == 0 ? "Example Gain" : "";
    }
    const char *wjn_get_node_category(uint32_t index)
    {
        return index == 0 ? "Native Fx" : "";
    }
    WinJACKNexus::Node::INode *wjn_create_node(uint32_t index)
    {
        return index == 0 ? new ExampleGainNode : nullptr;
    }
    void wjn_destroy_node(WinJACKNexus::Node::INode *node)
    {
        delete node;
    }
    WINJACK_NODE_API const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *node)
    {
        auto *effect = dynamic_cast<ExampleGainNode *>(node);
        return effect ? effect->clientId() : nullptr;
    }
    WINJACK_NODE_API int wjn_show_node_gui(WinJACKNexus::Node::INode *node)
    {
        return node && node->createWindow(nullptr) ? 1 : 0;
    }
    const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *node)
    {
        auto *effect = dynamic_cast<ExampleGainNode *>(node);
        return effect ? effect->settingsJson() : "{}";
    }
    int wjn_set_node_settings_json(WinJACKNexus::Node::INode *node, const char *json)
    {
        auto *effect = dynamic_cast<ExampleGainNode *>(node);
        return effect && effect->restoreSettings(json) ? 1 : 0;
    }
}
