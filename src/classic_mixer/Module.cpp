// SPDX-License-Identifier: AGPL-3.0-only
#include <QApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <WinJACKNexus/Common/ClassicMixerRouting.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <deque>

namespace CM = WinJACKNexus::Common::ClassicMixer;
namespace Audio = WinJACKNexus::Audio;
namespace UI = WinJACKNexus::UI;
using WinJACKNexus::Common::LockFreeSpscQueue;

class ClassicMixerNode final : public WinJACKNexus::Node::INode, public CM::PortBackend
{
  public:
    explicit ClassicMixerNode(uint32_t mode = WJN_NODE_MODE_JACK) : mode_(mode)
    {
        updateDescriptions();
        UI::bindRefreshTimer(maintenance_, &maintenance_);
        QObject::connect(&maintenance_, &QTimer::timeout, &maintenance_,
                         [this] { maintainClock(); });
    }
    ~ClassicMixerNode() override
    {
        shutdown();
        for (auto &widget : sidebars_)
            delete widget;
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"Classic Mixer", "Classic Mixer", "1.0.0", "WinJACKNexus",
                "Reusable classic mixing console"};
    }
    bool init() noexcept override
    {
        if (mode_ != WJN_NODE_MODE_JACK)
            return false;
        try
        {
            if (client_.isActive())
                return true;
            if (!openClient(name_))
                return false;
            maintenance_.start();
            return true;
        }
        catch (...)
        {
            closeClient();
            return false;
        }
    }
    void shutdown() noexcept override
    {
        maintenance_.stop();
        closeClient();
        prepared_ = false;
    }
    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    } // S3 supplies the editor.
    void destroyWindow(HWND) noexcept override
    {
    }

    const CM::State &state() const
    {
        return state_;
    }
    const char *clientId()
    {
        nameBytes_ = name_.toUtf8();
        return nameBytes_.constData();
    }
    QString clientName() const override
    {
        return name_;
    }

    bool snapshot(std::vector<CM::Connection> &result) override
    {
        if (!client_.isActive())
            return false;
        auto &api = Audio::JackApi::instance();
        for (std::size_t index = 0; index < jackPorts_.size(); ++index)
        {
            const auto *local = jackPorts_[index].name();
            if (!local)
                return false;
            const auto **peers = api.portGetConnections(jackPorts_[index].nativeHandle());
            if (!peers)
                continue;
            for (std::size_t peer = 0; peers[peer]; ++peer)
            {
                CM::Connection connection =
                    descriptions_[index].input
                        ? CM::Connection{QString::fromUtf8(peers[peer]), QString::fromUtf8(local)}
                        : CM::Connection{QString::fromUtf8(local), QString::fromUtf8(peers[peer])};
                const bool duplicate =
                    std::any_of(result.begin(), result.end(), [&](const auto &old) {
                        return old.source == connection.source &&
                               old.destination == connection.destination;
                    });
                if (!duplicate)
                    result.push_back(std::move(connection));
            }
            api.freeMemory(peers);
        }
        return true;
    }
    bool rename(std::uint32_t id, const QString &name) override
    {
        for (std::size_t index = 0; index < descriptions_.size(); ++index)
        {
            if (descriptions_[index].id == id)
                return jackPorts_[index].rename(name.toUtf8().constData());
        }
        return false;
    }
    bool connect(const CM::Connection &connection) override
    {
        auto &api = Audio::JackApi::instance();
        const auto source = connection.source.toUtf8();
        const auto destination = connection.destination.toUtf8();
        const auto result =
            api.connect(client_.nativeHandle(), source.constData(), destination.constData());
        return result == 0 || result == EEXIST;
    }

    bool apply(const CM::State &candidate, const QString &requestedName)
    {
        if (!candidate.validate(&error_) || !validClientName(requestedName))
            return false;
        if (prepared_ && candidate.layout != state_.layout)
        {
            error_ = "Layout is fixed after preparation";
            return false;
        }
        if (configurations_.full())
        {
            error_ = "Parameter queue is busy";
            return false;
        }
        const auto nextPorts = CM::ports(candidate);
        if (client_.isActive())
        {
            const auto renamed = CM::renamePorts(*this, descriptions_, nextPorts);
            if (!renamed.success)
            {
                error_ = renamed.error;
                for (const auto &failed : renamed.failedConnections)
                    error_ += '\n' + failed.source + " -> " + failed.destination;
                return false;
            }
            const auto oldPorts = descriptions_;
            descriptions_ = nextPorts;
            if (!applyClientName(requestedName))
            {
                const auto rollback = CM::renamePorts(*this, descriptions_, oldPorts);
                descriptions_ = oldPorts;
                if (!rollback.success)
                    error_ += "; port rollback incomplete";
                return false;
            }
        }
        else
            name_ = requestedName;
        state_ = candidate;
        updateDescriptions();
        if (prepared_)
            configurations_.try_push(CM::configuration(state_, sampleRate_));
        error_.clear();
        return true;
    }
    const char *settingsJson()
    {
        auto json = state_.toJson();
        json.insert("clientName", name_);
        json_ = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return json_.constData();
    }
    bool restore(const char *json)
    {
        if (!json || strnlen_s(json, 32 * 1024 * 1024 + 1) > 32 * 1024 * 1024)
            return false;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return false;
        const auto object = document.object();
        if (!object.value("clientName").isString())
            return false;
        CM::State candidate;
        return CM::State::fromJson(object, candidate, &error_) &&
               apply(candidate, object.value("clientName").toString());
    }
    QWidget *createSidebar(QWidget *parent)
    {
        auto *widget = new UI::Controls::ClientNameWidget(
            name_, [this](const QString &name) { return apply(state_, name.trimmed()); }, parent);
        sidebars_.push_back(widget);
        return widget;
    }
    void destroySidebar(QWidget *widget)
    {
        for (auto &sidebar : sidebars_)
        {
            if (sidebar == widget)
            {
                delete widget;
                sidebar = nullptr;
                break;
            }
        }
    }

    uint32_t virtualPortCount() const noexcept
    {
        return static_cast<uint32_t>(descriptions_.size());
    }
    bool virtualPort(uint32_t index, WjnNodePortV1 *description) const noexcept
    {
        if (!description || description->structSize != sizeof(*description) ||
            index >= descriptions_.size())
            return false;
        const auto &port = descriptions_[index];
        *description = {
            sizeof(*description),
            port.id,
            static_cast<uint32_t>(port.input ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
            WJN_NODE_PORT_AUDIO,
            1,
            0,
            descriptionNames_[index]};
        return true;
    }
    bool virtualPrepare(const WjnNodePrepareV1 *context) noexcept
    {
        if (mode_ != WJN_NODE_MODE_VIRT || !context || context->structSize != sizeof(*context))
            return false;
        try
        {
            return prepare(context->sampleRate, context->maxFrames);
        }
        catch (...)
        {
            return false;
        }
    }
    void virtualProcess(WjnNodeProcessV1 *block) noexcept
    {
        if (mode_ != WJN_NODE_MODE_VIRT || !block || block->structSize != sizeof(*block))
            return;
        std::array<const float *, CM::maximumAudioInputs> inputs{};
        std::array<float *, CM::outputChannels> outputs{};
        for (uint32_t index = 0; block->audioInputs && index < block->audioInputCount; ++index)
        {
            const auto &buffer = block->audioInputs[index];
            if (buffer.channels != 1 || !buffer.samples)
                continue;
            for (int channel = 0; channel < audioInputs_; ++channel)
            {
                if (buffer.portId == inputIds_[channel])
                    inputs[channel] = buffer.samples[0];
            }
        }
        for (uint32_t index = 0; block->audioOutputs && index < block->audioOutputCount; ++index)
        {
            const auto &buffer = block->audioOutputs[index];
            if (buffer.channels == 1 && buffer.samples && buffer.portId >= 101 &&
                buffer.portId <= 108)
                outputs[buffer.portId - 101] = buffer.samples[0];
        }
        process(inputs.data(), outputs.data(), block->frames);
    }
    void virtualReset() noexcept
    {
        CM::Configuration settings;
        while (configurations_.try_pop(settings))
            processor_.setConfiguration(settings);
        processor_.reset();
        measurements_.clear();
    }
    void virtualStop() noexcept
    {
        prepared_ = false;
        configurations_.clear();
        measurements_.clear();
    }

  private:
    const uint32_t mode_;
    CM::State state_ = CM::State::create(16);
    CM::Processor processor_;
    Audio::JackClient client_;
    std::vector<Audio::JackPort> jackPorts_;
    std::vector<CM::Port> descriptions_;
    std::deque<QByteArray> nameStorage_;
    std::vector<const char *> descriptionNames_;
    QString name_ = "ClassicMixer";
    QString error_;
    QByteArray nameBytes_, json_;
    std::vector<QPointer<QWidget>> sidebars_;
    QTimer maintenance_;
    LockFreeSpscQueue<CM::Configuration, 32> configurations_;
    LockFreeSpscQueue<CM::Levels, 16> measurements_;
    std::array<uint32_t, CM::maximumAudioInputs> inputIds_{};
    int audioInputs_ = 16;
    double sampleRate_ = 48000;
    uint32_t maximumFrames_ = 0;
    bool prepared_ = false;
    std::atomic_bool clockReady_{false};
    std::atomic<uint32_t> requestedRate_{48000}, requestedFrames_{8192};

    static bool validClientName(const QString &name)
    {
        const auto bytes = name.toUtf8();
        return !name.trimmed().isEmpty() && bytes.size() < 64 &&
               std::none_of(
                   bytes.begin(), bytes.end(),
                   [](unsigned char c) { return c < 32 || c == 127 || c == ':'; });
    }
    void updateDescriptions()
    {
        descriptions_ = CM::ports(state_);
        const auto previousNames = descriptionNames_;
        descriptionNames_.clear();
        for (std::size_t index = 0; index < descriptions_.size(); ++index)
        {
            const auto &port = descriptions_[index];
            if (index < previousNames.size() &&
                QString::fromUtf8(previousNames[index]) == port.name)
            {
                descriptionNames_.push_back(previousNames[index]);
                continue;
            }
            // Borrowed ABI strings survive all subsequent renames until destruction.
            nameStorage_.push_back(port.name.toUtf8());
            descriptionNames_.push_back(nameStorage_.back().constData());
        }
    }
    bool prepare(double rate, uint32_t frames)
    {
        const auto settings = CM::configuration(state_, rate);
        if (!processor_.prepare(rate, frames, settings))
            return false;
        sampleRate_ = rate;
        maximumFrames_ = frames;
        audioInputs_ = state_.audioInputCount();
        int index = 0;
        for (const auto &port : descriptions_)
        {
            if (port.input)
                inputIds_[index++] = port.id;
        }
        configurations_.clear();
        prepared_ = true;
        return true;
    }
    bool openClient(const QString &name)
    {
        if (!client_.open({name.toUtf8().constData(), false, false, nullptr}))
            return false;
        name_ = QString::fromUtf8(client_.name());
        jackPorts_.resize(descriptions_.size());
        for (std::size_t index = 0; index < descriptions_.size(); ++index)
        {
            const auto &port = descriptions_[index];
            if (!jackPorts_[index].registerPort(client_, port.name.toUtf8().constData(),
                                                JACK_DEFAULT_AUDIO_TYPE,
                                                port.input ? JackPortIsInput : JackPortIsOutput))
            {
                closeClient();
                return false;
            }
        }
        requestedRate_ = client_.sampleRate();
        requestedFrames_ = client_.bufferSize();
        if ((!prepared_ || sampleRate_ != requestedRate_ || maximumFrames_ != requestedFrames_) &&
            !prepare(requestedRate_, requestedFrames_))
        {
            closeClient();
            return false;
        }
        if (client_.setProcessCallback(&jackProcess, this) ||
            client_.setSampleRateCallback(&rateChanged, this) ||
            client_.setBufferSizeCallback(&sizeChanged, this))
        {
            closeClient();
            return false;
        }
        clockReady_ = true;
        if (!client_.activate())
        {
            closeClient();
            return false;
        }
        return true;
    }
    void closeClient() noexcept
    {
        clockReady_ = false;
        client_.deactivate();
        jackPorts_.clear();
        client_.close();
    }
    bool applyClientName(const QString &requested)
    {
        if (name_ == requested)
            return true;
        const auto previous = name_;
        std::vector<CM::Connection> saved;
        if (!snapshot(saved))
            return false;
        closeClient();
        const auto restoreConnections = [&] {
            bool complete = true;
            for (const auto &connection : saved)
            {
                const auto remap = [&](const QString &endpoint) {
                    return endpoint.startsWith(previous + ':')
                               ? name_ + endpoint.mid(previous.size())
                               : endpoint;
                };
                const CM::Connection next{remap(connection.source), remap(connection.destination)};
                if (!connect(next))
                {
                    complete = false;
                    error_ += '\n' + next.source + " -> " + next.destination;
                }
            }
            return complete;
        };
        if (openClient(requested) && restoreConnections())
            return true;
        error_ = "Client rename failed; restoring original client" + error_;
        closeClient();
        if (!openClient(previous))
        {
            error_ += "; client rollback failed";
            return false;
        }
        if (!restoreConnections())
            error_ += "; connection rollback incomplete";
        return false;
    }

    void maintainClock()
    {
        if (mode_ != WJN_NODE_MODE_JACK || !client_.isActive() || clockReady_)
            return;
        std::vector<CM::Connection> saved;
        if (!snapshot(saved) || !client_.deactivate())
            return;
        const bool ready = prepare(requestedRate_, requestedFrames_);
        clockReady_ = ready;
        client_.activate();
        for (const auto &connection : saved)
            connect(connection);
    }
    void process(const float *const *inputs, float *const *outputs, uint32_t frames) noexcept
    {
        CM::Configuration settings;
        while (configurations_.try_pop(settings))
            processor_.setConfiguration(settings);
        if (!prepared_ || (mode_ == WJN_NODE_MODE_JACK && !clockReady_))
        {
            for (int channel = 0; channel < CM::outputChannels; ++channel)
            {
                if (outputs[channel])
                    std::fill_n(outputs[channel], frames, 0);
            }
            return;
        }
        processor_.process(inputs, outputs, frames);
        measurements_.try_push(processor_.levels());
    }
    static int jackProcess(jack_nframes_t frames, void *context) noexcept
    {
        auto &node = *static_cast<ClassicMixerNode *>(context);
        std::array<const float *, CM::maximumAudioInputs> inputs{};
        std::array<float *, CM::outputChannels> outputs{};
        for (int index = 0; index < node.audioInputs_; ++index)
            inputs[index] = node.jackPorts_[index].audioBuffer(frames);
        for (int index = 0; index < CM::outputChannels; ++index)
            outputs[index] = node.jackPorts_[node.audioInputs_ + index].audioBuffer(frames);
        node.process(inputs.data(), outputs.data(), frames);
        return 0;
    }
    static int rateChanged(jack_nframes_t rate, void *context) noexcept
    {
        auto &node = *static_cast<ClassicMixerNode *>(context);
        if (node.requestedRate_.exchange(rate) != rate)
            node.clockReady_ = false;
        return 0;
    }
    static int sizeChanged(jack_nframes_t frames, void *context) noexcept
    {
        auto &node = *static_cast<ClassicMixerNode *>(context);
        if (node.requestedFrames_.exchange(frames) != frames)
            node.clockReady_ = false;
        return 0;
    }
};

const char *wjn_get_node_api_version()
{
    return "4";
}
const char *wjn_get_node_name()
{
    return "Classic Mixer";
}
const char *wjn_get_node_category()
{
    return "Classic Mixer";
}
WinJACKNexus::Node::INode *wjn_create_node()
{
    return new ClassicMixerNode;
}
void wjn_destroy_node(WinJACKNexus::Node::INode *node)
{
    delete node;
}
const char *wjn_get_node_settings_json(WinJACKNexus::Node::INode *node)
{
    try
    {
        return node ? static_cast<ClassicMixerNode *>(node)->settingsJson() : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}
int wjn_set_node_settings_json(WinJACKNexus::Node::INode *node, const char *json)
{
    try
    {
        return node && static_cast<ClassicMixerNode *>(node)->restore(json);
    }
    catch (...)
    {
        return 0;
    }
}
QWidget *wjn_create_settings_widget(WinJACKNexus::Node::INode *node, QWidget *parent)
{
    try
    {
        return node && QApplication::instance()
                   ? static_cast<ClassicMixerNode *>(node)->createSidebar(parent)
                   : nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}
void wjn_destroy_settings_widget(WinJACKNexus::Node::INode *node, QWidget *widget)
{
    if (node)
        static_cast<ClassicMixerNode *>(node)->destroySidebar(widget);
}
extern "C" WINJACK_NODE_API const char *wjn_get_node_client_id(WinJACKNexus::Node::INode *node)
{
    return node ? static_cast<ClassicMixerNode *>(node)->clientId() : nullptr;
}

#define WJN_NODE_MODULE_FEATURES 0
#define WJN_NODE_MODULE_TYPE ClassicMixerNode
#include <WinJACKNexus/Node/DualModeModuleV4.inc>
