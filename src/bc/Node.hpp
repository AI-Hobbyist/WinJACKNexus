// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <QObject>
#include <QTimer>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Common/BroadcastAudioStream.hpp>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <WinJACKNexus/Common/RealtimeObject.hpp>
#include <WinJACKNexus/Node/NodeApi.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

namespace WinJACKNexus::Broadcast
{
namespace Model = Common::Broadcast;
class BroadcastWindow;

class BroadcastNode final : public QObject, public Node::INode
{
  public:
    BroadcastNode();
    ~BroadcastNode() override;
    Node::NodeInfo info() const noexcept override;
    bool init() noexcept override;
    void shutdown() noexcept override;
    HWND createWindow(HWND parent) noexcept override;
    void destroyWindow(HWND window) noexcept override;

    const Model::State &state() const noexcept;
    bool apply(Model::State candidate);
    const char *settings();
    bool restore(const char *json);
    const char *clientId();
    float takePeak(unsigned stream) noexcept;
    quint64 stateRevision() const noexcept
    {
        return revision_;
    }

  private:
    struct Configuration
    {
        std::array<Model::Controls, 64> streams{};
        unsigned count = 0;
        bool solo = false;
    };
    struct Runtime
    {
        std::array<Audio::JackPort *, 8> inputs{};
        std::array<Audio::JackPort *, 8> outputs{};
        QString id;
        std::shared_ptr<Model::AudioStream> audio;
    };

    Model::State state_;
    Audio::JackClient client_;
    std::map<QString, std::unique_ptr<Audio::JackPort>> ports_;
    std::vector<Runtime> runtime_;
    Common::RealtimeObject<Configuration> configuration_;
    std::array<std::atomic<float>, 64> peaks_{};
    QByteArray settingsCache_, clientIdCache_;
    QTimer clockCheck_;
    std::atomic<jack_nframes_t> sourceRate_{0};
    std::unique_ptr<BroadcastWindow> window_;
    quint64 revision_ = 0;

    bool rebuildPorts(const Model::State &candidate);
    void publishConfiguration();
    void publishStreams();
    static int process(jack_nframes_t frames, void *context) noexcept;
};

} // namespace WinJACKNexus::Broadcast
