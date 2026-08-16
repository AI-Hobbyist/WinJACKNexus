#include <WinJACKNexus/AdapterService/ServiceRuntime.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace wjn::adapter::service
{

ServiceRuntime::ServiceRuntime (ServiceConfig newConfiguration,
                                ClientEngineFactory newEngineFactory)
    : config (std::move (newConfiguration)), engineFactory (std::move (newEngineFactory))
{
    if (! engineFactory)
        engineFactory = [] { return makeRealClientEngine(); };
}

ServiceRuntime::~ServiceRuntime()
{
    requestStop();
    while (! isStopped())
        tick();
}

bool ServiceRuntime::start()
{
    if (state() != ServiceRuntimeState::idle)
        return false;

    juce::String error;
    if (! config.validate (error))
        return false;

    stopRequested.store (false, std::memory_order_release);
    activeClientCountValue.store (0, std::memory_order_release);
    stopTotalClients.store (0, std::memory_order_release);
    stopRemainingClients.store (0, std::memory_order_release);
    runtimeState.store (ServiceRuntimeState::starting, std::memory_order_release);
    advanceStart();
    return true;
}

void ServiceRuntime::tick()
{
    if (isStopped())
        return;

    if (stopRequested.load (std::memory_order_acquire))
    {
        if (state() != ServiceRuntimeState::stopping)
        {
            const auto remaining = activeClients.size() + (startingClient != nullptr ? 1u : 0u);
            stopTotalClients.store (remaining, std::memory_order_release);
            stopRemainingClients.store (remaining, std::memory_order_release);
        }
        runtimeState.store (ServiceRuntimeState::stopping, std::memory_order_release);
        advanceStop();
        return;
    }

    if (state() == ServiceRuntimeState::starting)
    {
        advanceStart();
        return;
    }

    if (isRunning())
        for (auto& client : activeClients)
            client->refresh();
}

void ServiceRuntime::run()
{
    if (state() == ServiceRuntimeState::idle && ! start())
        return;

    while (! isStopped())
    {
        tick();
        if (! isStopped())
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
}

void ServiceRuntime::requestStop() noexcept
{
    stopRequested.store (true, std::memory_order_release);
}

double ServiceRuntime::startProgress() const noexcept
{
    if (isRunning())
        return 1.0;

    if (config.clients.empty())
        return state() == ServiceRuntimeState::starting ? 0.0 : 1.0;

    return std::clamp (static_cast<double> (nextClientIndex)
                           / static_cast<double> (config.clients.size()),
                       0.0, 1.0);
}

double ServiceRuntime::stopProgress() const noexcept
{
    if (isStopped())
        return 1.0;

    const auto total = stopTotalClients.load (std::memory_order_acquire);
    if (total == 0)
        return 0.0;

    const auto remaining = stopRemainingClients.load (std::memory_order_acquire);
    return std::clamp (1.0 - static_cast<double> (remaining)
                                  / static_cast<double> (total),
                       0.0, 1.0);
}

void ServiceRuntime::advanceStart()
{
    while (state() == ServiceRuntimeState::starting
           && ! stopRequested.load (std::memory_order_acquire))
    {
        if (startingClient != nullptr)
        {
            if (! startingClient->isStartComplete())
                return;

            if (startingClient->tryCompleteStart())
            {
                activeClients.push_back (std::move (startingClient));
                activeClientCountValue.store (activeClients.size(), std::memory_order_release);
            }
            else
            {
                failedClients.add (startingClient->client().clientName);
                startingClient->stop();
                startingClient.reset();
            }

            continue;
        }

        while (nextClientIndex < config.clients.size()
               && (! config.clients[nextClientIndex].enabled
                   || ! config.clients[nextClientIndex].isAvailable()))
            ++nextClientIndex;

        if (nextClientIndex >= config.clients.size())
        {
            runtimeState.store (ServiceRuntimeState::running, std::memory_order_release);
            return;
        }

        auto client = config.clients[nextClientIndex++];
        auto engine = engineFactory();
        if (engine == nullptr)
        {
            failedClients.add (client.clientName);
            continue;
        }

        startingClient = std::make_unique<ClientRuntime> (std::move (client),
                                                          std::move (engine));
        if (! startingClient->start())
        {
            failedClients.add (startingClient->client().clientName);
            startingClient.reset();
            continue;
        }

        if (! startingClient->isStartComplete())
            return;
    }
}

void ServiceRuntime::advanceStop()
{
    if (startingClient != nullptr)
    {
        startingClient->stop();
        startingClient.reset();
        stopRemainingClients.store (activeClients.size(), std::memory_order_release);
        return;
    }

    if (! activeClients.empty())
    {
        activeClients.back()->stop();
        activeClients.pop_back();
        activeClientCountValue.store (activeClients.size(), std::memory_order_release);
        stopRemainingClients.store (activeClients.size(), std::memory_order_release);
        return;
    }

    stopRemainingClients.store (0, std::memory_order_release);
    runtimeState.store (ServiceRuntimeState::stopped, std::memory_order_release);
}

} // namespace wjn::adapter::service