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
                              std::unique_ptr<ClientEngine> newEngine)
    : configuration (std::move (newConfiguration)), engine (std::move (newEngine))
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
    const ServiceClient& client)
{
    ClientEngine::Configuration configuration;
    configuration.clientName = client.clientName;
    configuration.midi = client.kind.equalsIgnoreCase ("Midi");
    configuration.input = client.direction.equalsIgnoreCase ("In");
    configuration.wasapiMode = client.wasapiMode.equalsIgnoreCase ("exclusive")
                                   ? juce::WASAPIDeviceMode::exclusive
                                   : juce::WASAPIDeviceMode::shared;

    if (configuration.midi)
    {
        configuration.midiDeviceIdentifier = client.guid;
    }
    else
    {
        configuration.audioDeviceName = client.guid;
        if (! client.channels.empty())
            configuration.channels = static_cast<int> (client.channels.size());
    }

    return configuration;
}

} // namespace wjn::adapter::service