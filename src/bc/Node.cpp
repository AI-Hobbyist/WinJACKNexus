// SPDX-License-Identifier: AGPL-3.0-only
#include "Node.hpp"
#include "Service.hpp"
#include "Window.hpp"
#include <QApplication>
#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <mutex>

namespace WinJACKNexus::Broadcast
{
namespace
{
std::mutex identityMutex;
QHash<QString, BroadcastNode *> liveIdentities;
} // namespace

BroadcastNode::BroadcastNode()
{
    // Control polling only: never drives a GUI refresh. JACK's rate callback
    // only publishes an atomic value; preparation stays outside realtime code.
    clockCheck_.setInterval(20);
    connect(&clockCheck_, &QTimer::timeout, this,
            [this]
            {
                const auto rate = sourceRate_.load(std::memory_order_relaxed);
                if (!client_.isActive() || runtime_.empty() || runtime_[0].audio->rate() == rate)
                    return;
                try
                {
                    if (!client_.deactivate() || rate < 8000 || rate > 192000 || !rebuildPorts(state_) ||
                        !client_.activate())
                    {
                        shutdown();
                        return;
                    }
                    publishStreams();
                }
                catch (...)
                {
                    shutdown();
                }
            });
}

BroadcastNode::~BroadcastNode()
{
    window_.reset();
    shutdown();
    const std::lock_guard lock(identityMutex);
    for (auto entry = liveIdentities.begin(); entry != liveIdentities.end();)
        if (entry.value() == this)
            entry = liveIdentities.erase(entry);
        else
            ++entry;
}

Node::NodeInfo BroadcastNode::info() const noexcept
{
    return {"Broadcast Sender", "Broadcast", "1.0.0", "WinJACKNexus",
            "JACK broadcast inputs and local monitor outputs"};
}

bool BroadcastNode::init() noexcept
{
    try
    {
        if (client_.isActive())
            return true;
        if (!client_.isOpen() &&
            !client_.open({state_.clientName.toUtf8().constData(), false, false, nullptr}))
            return false;
        if (client_.sampleRate() < 8000 || client_.sampleRate() > 192000 ||
            client_.setProcessCallback(&BroadcastNode::process, this) != 0 || !rebuildPorts(state_))
        {
            shutdown();
            return false;
        }
        sourceRate_.store(client_.sampleRate(), std::memory_order_relaxed);
        if (client_.setSampleRateCallback(
                [](jack_nframes_t rate, void *context) -> int
                {
                    static_cast<BroadcastNode *>(context)->sourceRate_.store(rate, std::memory_order_relaxed);
                    return 0;
                },
                this) != 0)
        {
            shutdown();
            return false;
        }
        state_.clientName = QString::fromUtf8(client_.name());
        publishConfiguration();
        if (!client_.activate())
        {
            shutdown();
            return false;
        }
        publishStreams();
        clockCheck_.start();
        return true;
    }
    catch (...)
    {
        shutdown();
        return false;
    }
}

void BroadcastNode::shutdown() noexcept
{
    clockCheck_.stop();
    client_.deactivate();
    Service::instance().detach(this);
    runtime_.clear();
    ports_.clear();
    client_.close();
    for (auto &peak : peaks_)
        peak.store(0, std::memory_order_relaxed);
}

HWND BroadcastNode::createWindow(HWND) noexcept
{
    try
    {
        if (!qobject_cast<QApplication *>(QApplication::instance()))
            return nullptr;
        if (!window_)
            window_ = std::make_unique<BroadcastWindow>(*this);
        window_->show();
        window_->raise();
        window_->activateWindow();
        return reinterpret_cast<HWND>(window_->winId());
    }
    catch (...)
    {
        return nullptr;
    }
}

void BroadcastNode::destroyWindow(HWND) noexcept
{
    if (window_)
        window_->close();
}

const Model::State &BroadcastNode::state() const noexcept
{
    return state_;
}

bool BroadcastNode::apply(Model::State candidate)
{
    if (!candidate.validate())
        return false;
    const std::lock_guard lock(identityMutex);
    auto identities = liveIdentities;
    for (auto entry = identities.begin(); entry != identities.end();)
        if (entry.value() == this)
            entry = identities.erase(entry);
        else
            ++entry;
    if (identities.size() + candidate.streams.size() > 64)
        return false;
    for (auto &stream : candidate.streams)
    {
        while (identities.contains(stream.id))
            stream.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        identities.insert(stream.id, this);
    }
    const bool topologyChanged = state_.ports() != candidate.ports();
    // JACK has no client rename in the supported wrapper. A project restores
    // its name before init; an active name change requires an explicit restart.
    if (client_.isOpen() && state_.clientName != candidate.clientName)
        return false;
    const bool active = client_.isActive();
    if (topologyChanged && client_.isOpen())
    {
        if (active && !client_.deactivate())
            return false;
        try
        {
            if (!rebuildPorts(candidate))
            {
                if (active)
                    client_.activate();
                return false;
            }
        }
        catch (...)
        {
            if (active)
                client_.activate();
            throw;
        }
    }
    state_ = std::move(candidate);
    ++revision_;
    liveIdentities.swap(identities);
    publishConfiguration();
    if (topologyChanged && active)
    {
        if (!client_.activate())
            return false;
    }
    if (client_.isActive())
        publishStreams();
    return true;
}

bool BroadcastNode::rebuildPorts(const Model::State &candidate)
{
    std::map<QString, std::unique_ptr<Audio::JackPort>> additions;
    const auto descriptions = candidate.ports();
    QSet<QString> retained;
    for (const auto &description : descriptions)
    {
        retained.insert(description.name);
        if (ports_.contains(description.name))
            continue;
        auto port = std::make_unique<Audio::JackPort>();
        if (!port->registerPort(client_, description.name.toUtf8().constData(), JACK_DEFAULT_AUDIO_TYPE,
                                description.input ? JackPortIsInput : JackPortIsOutput))
            return false;
        additions.emplace(description.name, std::move(port));
    }
    std::vector<Runtime> next(candidate.streams.size());
    for (std::size_t index = 0; index < candidate.streams.size(); ++index)
    {
        const auto &stream = candidate.streams[index];
        next[index].id = stream.id;
        for (const auto &existing : runtime_)
            if (existing.id == stream.id && existing.audio->layout() == stream.layout &&
                existing.audio->rate() == client_.sampleRate())
                next[index].audio = existing.audio;
        if (!next[index].audio)
            next[index].audio = std::make_shared<Model::AudioStream>(client_.sampleRate(), stream.controls());
    }
    std::size_t portIndex = 0;
    for (std::size_t stream = 0; stream < candidate.streams.size(); ++stream)
        for (const bool input : {true, false})
            for (unsigned channel = 0; channel < Model::channelCount(candidate.streams[stream].layout);
                 ++channel)
            {
                const auto &name = descriptions[portIndex++].name;
                auto *port = ports_.contains(name) ? ports_.at(name).get() : additions.at(name).get();
                (input ? next[stream].inputs : next[stream].outputs)[channel] = port;
            }
    // All allocations and registrations succeeded. Retain existing JackPort
    // objects/connections; only genuinely removed ports are unregistered.
    ports_.merge(additions);
    for (auto entry = ports_.begin(); entry != ports_.end();)
        if (!retained.contains(entry->first))
            entry = ports_.erase(entry);
        else
            ++entry;
    runtime_.swap(next);
    return true;
}

void BroadcastNode::publishConfiguration()
{
    Configuration config;
    config.count = static_cast<unsigned>(state_.streams.size());
    for (unsigned index = 0; index < config.count; ++index)
    {
        config.streams[index] = state_.streams[index].controls();
        config.solo |= config.streams[index].solo;
    }
    configuration_.set(config);
}

void BroadcastNode::publishStreams()
{
    std::vector<Publication> publications;
    publications.reserve(state_.streams.size());
    for (std::size_t index = 0; index < state_.streams.size(); ++index)
        publications.push_back({state_.streams[index], state_.categoryPath(state_.streams[index].category),
                                runtime_[index].audio});
    Service::instance().setPublications(this, std::move(publications));
}

int BroadcastNode::process(jack_nframes_t frames, void *context) noexcept
{
    auto &node = *static_cast<BroadcastNode *>(context);
    const auto &config = node.configuration_.getRead();
    for (unsigned stream = 0; stream < config.count; ++stream)
    {
        const auto &runtime = node.runtime_[stream];
        std::array<const float *, 8> input{};
        std::array<float *, 8> output{};
        for (unsigned channel = 0; channel < Model::channelCount(config.streams[stream].layout); ++channel)
        {
            input[channel] = runtime.inputs[channel]->audioBuffer(frames);
            output[channel] = runtime.outputs[channel]->audioBuffer(frames);
        }
        const auto peak = runtime.audio->rate() == node.sourceRate_.load(std::memory_order_relaxed)
                              ? runtime.audio->render(config.streams[stream], config.solo, input.data(),
                                                      output.data(), frames)
                              : Model::process(config.streams[stream], config.solo, input.data(),
                                               output.data(), nullptr, frames);
        auto previous = node.peaks_[stream].load(std::memory_order_relaxed);
        while (previous < peak &&
               !node.peaks_[stream].compare_exchange_weak(previous, peak, std::memory_order_relaxed))
        {
        }
    }
    return 0;
}

float BroadcastNode::takePeak(unsigned stream) noexcept
{
    return stream < state_.streams.size() ? peaks_[stream].exchange(0, std::memory_order_relaxed) : 0;
}

const char *BroadcastNode::settings()
{
    settingsCache_ = QJsonDocument(state_.toJson()).toJson(QJsonDocument::Compact);
    return settingsCache_.constData();
}

bool BroadcastNode::restore(const char *json)
{
    if (!json)
        return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(QByteArray(json), &error);
    Model::State candidate;
    return error.error == QJsonParseError::NoError && document.isObject() &&
           Model::State::fromJson(document.object(), candidate) && apply(std::move(candidate));
}

const char *BroadcastNode::clientId()
{
    clientIdCache_ = state_.clientName.toUtf8();
    return clientIdCache_.constData();
}

} // namespace WinJACKNexus::Broadcast
