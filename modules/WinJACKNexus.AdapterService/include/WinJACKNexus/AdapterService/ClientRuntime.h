#pragma once

#include <functional>
#include <memory>

#include <WinJACKNexus/AdapterBackend/RealEngine.h>
#include <WinJACKNexus/AdapterService/ServiceConfig.h>

namespace wjn::adapter::service
{

class ClientEngine
{
public:
    using Configuration = wjn::adapter::RealEngine::Configuration;

    virtual ~ClientEngine() = default;

    virtual bool start (Configuration configuration) = 0;
    virtual bool isStartComplete() const noexcept = 0;
    virtual bool isActive() const noexcept = 0;
    virtual void refresh() = 0;
    virtual void stop() = 0;
};

using ClientEngineFactory = std::function<std::unique_ptr<ClientEngine>()>;

std::unique_ptr<ClientEngine> makeRealClientEngine();

enum class ClientRuntimeState
{
    pending,
    starting,
    active,
    failed,
    stopping,
    stopped
};

class ClientRuntime final
{
public:
    ClientRuntime (ServiceClient client, std::unique_ptr<ClientEngine> engine,
                   wjn::common::JackClientHub* jackHub = nullptr,
                   juce::String jackHubClientName = {});

    bool start();
    bool tryCompleteStart() noexcept;
    bool isStartComplete() const noexcept;
    bool isActive() const noexcept;
    void refresh();
    void stop();

    const ServiceClient& client() const noexcept { return configuration; }
    ClientRuntimeState state() const noexcept { return runtimeState; }

private:
    ClientEngine::Configuration makeEngineConfiguration (const ServiceClient& client) const;

    ServiceClient configuration;
    std::unique_ptr<ClientEngine> engine;
    wjn::common::JackClientHub* jackHub = nullptr;
    juce::String jackHubClientName;
    ClientRuntimeState runtimeState = ClientRuntimeState::pending;
};

} // namespace wjn::adapter::service