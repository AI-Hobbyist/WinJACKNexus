#include <WinJACKNexus/AdapterService/ClientRuntime.h>

#include <utility>

namespace wjn::adapter::service
{
namespace
{

class RealClientEngine final : public ClientEngine
{
public:
    bool start (Configuration configuration) override
    {
        return engine.start (std::move (configuration));
    }

    bool isStartComplete() const noexcept override
    {
        return engine.isStartComplete();
    }

    bool isActive() const noexcept override
    {
        return engine.isActive();
    }

    void refresh() override
    {
        engine.refresh();
    }

    void stop() override
    {
        engine.stop();
    }

private:
    wjn::adapter::RealEngine engine;
};

} // namespace

std::unique_ptr<ClientEngine> makeRealClientEngine()
{
    return std::make_unique<RealClientEngine>();
}

ClientRuntime::ClientRuntime (ServiceClient newConfiguration,
                                                            std::unique_ptr<ClientEngine> newEngine,
                                                            wjn::common::JackClientHub* newJackHub,
                                                            juce::String newJackHubClientName)
        : configuration (std::move (newConfiguration)), engine (std::move (newEngine)),
            jackHub (newJackHub), jackHubClientName (std::move (newJackHubClientName))
{
}

bool ClientRuntime::start()
{
    if (runtimeState != ClientRuntimeState::pending || engine == nullptr)
    {
        runtimeState = ClientRuntimeState::failed;
        return false;
    }

    const auto started = engine->start (makeEngineConfiguration (configuration));
    runtimeState = started ? ClientRuntimeState::starting : ClientRuntimeState::failed;
    return started;
}

bool ClientRuntime::tryCompleteStart() noexcept
{
    if (runtimeState != ClientRuntimeState::starting || ! isStartComplete())
        return false;

    if (isActive())
    {
        runtimeState = ClientRuntimeState::active;
        return true;
    }

    runtimeState = ClientRuntimeState::failed;
    return false;
}

bool ClientRuntime::isStartComplete() const noexcept
{
    return engine != nullptr && engine->isStartComplete();
}

bool ClientRuntime::isActive() const noexcept
{
    return engine != nullptr && engine->isActive();
}

void ClientRuntime::refresh()
{
    if (runtimeState == ClientRuntimeState::active && engine != nullptr)
        engine->refresh();
}

void ClientRuntime::stop()
{
    if (engine == nullptr || runtimeState == ClientRuntimeState::stopped)
        return;

    runtimeState = ClientRuntimeState::stopping;
    engine->stop();
    runtimeState = ClientRuntimeState::stopped;
}

ClientEngine::Configuration ClientRuntime::makeEngineConfiguration (
    const ServiceClient& client) const
{
    ClientEngine::Configuration engineConfiguration;
    engineConfiguration.clientName = client.clientName;
    engineConfiguration.midi = client.kind.equalsIgnoreCase ("Midi");
    engineConfiguration.input = client.direction.equalsIgnoreCase ("In");
    engineConfiguration.wasapiMode = client.wasapiMode.equalsIgnoreCase ("exclusive")
                                   ? juce::WASAPIDeviceMode::exclusive
                                   : juce::WASAPIDeviceMode::shared;
    engineConfiguration.jackClientHub = jackHub;
    engineConfiguration.jackHubClientName = jackHubClientName;

    if (engineConfiguration.midi)
    {
        engineConfiguration.midiDeviceIdentifier = client.guid;
    }
    else
    {
        engineConfiguration.audioDeviceName = client.guid;
        if (! client.channels.empty())
            engineConfiguration.channels = static_cast<int> (client.channels.size());
    }

    return engineConfiguration;
}

} // namespace wjn::adapter::service