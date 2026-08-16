#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include <WinJACKNexus/AdapterService/ClientRuntime.h>

namespace wjn::adapter::service
{

enum class ServiceRuntimeState
{
    idle,
    starting,
    running,
    stopping,
    stopped
};

class ServiceRuntime final
{
public:
    explicit ServiceRuntime (ServiceConfig configuration,
                             ClientEngineFactory engineFactory = {},
                             bool aggregateMode = false);
    ~ServiceRuntime();

    bool start();
    void tick();
    void run();
    void requestStop() noexcept;

    ServiceRuntimeState state() const noexcept
    {
        return runtimeState.load (std::memory_order_acquire);
    }
    bool isRunning() const noexcept { return state() == ServiceRuntimeState::running; }
    bool isStopped() const noexcept { return state() == ServiceRuntimeState::stopped; }
        double startProgress() const noexcept;
        double stopProgress() const noexcept;
    std::size_t activeClientCount() const noexcept
    {
        return activeClientCountValue.load (std::memory_order_acquire);
    }
    const ServiceConfig& configuration() const noexcept { return config; }
    const juce::StringArray& failedClientNames() const noexcept { return failedClients; }

private:
    void advanceStart();
    void advanceStop();

    ServiceConfig config;
    ClientEngineFactory engineFactory;
    bool aggregateMode = false;
    std::unique_ptr<wjn::common::JackClientHub> inputHub;
    std::unique_ptr<wjn::common::JackClientHub> outputHub;
    std::atomic<ServiceRuntimeState> runtimeState { ServiceRuntimeState::idle };
    std::atomic<bool> stopRequested { false };
    std::size_t nextClientIndex = 0;
    std::atomic<std::size_t> activeClientCountValue { 0 };
    std::atomic<std::size_t> stopTotalClients { 0 };
    std::atomic<std::size_t> stopRemainingClients { 0 };
    std::unique_ptr<ClientRuntime> startingClient;
    std::vector<std::unique_ptr<ClientRuntime>> activeClients;
    juce::StringArray failedClients;
};

} // namespace wjn::adapter::service