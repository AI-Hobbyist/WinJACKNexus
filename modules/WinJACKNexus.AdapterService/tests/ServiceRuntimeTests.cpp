#include <WinJACKNexus/AdapterService/ServiceRuntime.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace
{

using wjn::adapter::service::ClientEngine;
using wjn::adapter::service::ClientEngineFactory;
using wjn::adapter::service::ServiceClient;
using wjn::adapter::service::ServiceConfig;
using wjn::adapter::service::ServiceRuntime;
using wjn::adapter::service::ServiceRuntimeState;

void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "ServiceRuntime test failure: " << message << '\n';
        std::exit (1);
    }
}

ServiceClient client (const char* id, const char* name, bool enabled = true,
                      const char* status = "available")
{
    ServiceClient result;
    result.id = id;
    result.clientName = name;
    result.enabled = enabled;
    result.status = status;
    result.kind = "Audio";
    result.driver = "WASAPI";
    result.direction = "Out";
    result.streamType = "Playback";
    result.device = name;
    result.guid = juce::String (id) + "-guid";
    result.channels = { 0, 1 };
    result.sampleRate = 48000.0;
    return result;
}

struct FakePlan
{
    bool acceptStart = true;
    bool complete = true;
    bool active = true;
    int refreshCalls = 0;
    int stopCalls = 0;
};

struct FakeState
{
    std::vector<FakePlan> plans;
    std::vector<ClientEngine::Configuration> configurations;
    std::vector<int> stopOrder;
    int created = 0;
};

class FakeEngine final : public ClientEngine
{
public:
    FakeEngine (FakeState& state, int index) : state (state), index (index) {}

    bool start (Configuration configuration) override
    {
        state.configurations.push_back (std::move (configuration));
        return plan().acceptStart;
    }

    bool isStartComplete() const noexcept override
    {
        return plan().complete;
    }

    bool isActive() const noexcept override
    {
        return plan().active;
    }

    void refresh() override
    {
        ++plan().refreshCalls;
    }

    void stop() override
    {
        ++plan().stopCalls;
        state.stopOrder.push_back (index);
        plan().active = false;
    }

private:
    FakePlan& plan() { return state.plans[static_cast<size_t> (index)]; }
    const FakePlan& plan() const { return state.plans[static_cast<size_t> (index)]; }

    FakeState& state;
    int index;
};

ClientEngineFactory factoryFor (FakeState& state)
{
    return [&state]
    {
        const auto index = state.created++;
        return std::unique_ptr<ClientEngine> (new FakeEngine (state, index));
    };
}

void stopRuntime (ServiceRuntime& runtime)
{
    runtime.requestStop();
    for (int count = 0; count < 32 && ! runtime.isStopped(); ++count)
        runtime.tick();
    require (runtime.isStopped(), "Runtime must finish serial shutdown");
}

void testSkipsDisabledAndMissing()
{
    ServiceConfig config = ServiceConfig::createDefault();
    config.clients = { client ("svc-001", "Disabled", false),
                       client ("svc-002", "Missing", true, "missing"),
                       client ("svc-003", "Available") };
    config.clients.back().channels = { 0 };
    FakeState state;
    state.plans.resize (1);
    ServiceRuntime runtime (config, factoryFor (state));

    require (runtime.start(), "Runtime must start with eligible clients");
    require (state.created == 1 && state.configurations.size() == 1,
             "Disabled and missing clients must not create engines");
    require (state.configurations.front().clientName == "Available",
             "The available client must be the only started client");
    require (state.configurations.front().channels == 1,
             "A one-element channel list must map to one engine channel");
    stopRuntime (runtime);
}

void testStartsStrictlySerially()
{
    ServiceConfig config = ServiceConfig::createDefault();
    config.clients = { client ("svc-001", "First"), client ("svc-002", "Second") };
    FakeState state;
    state.plans.resize (2);
    state.plans[0].complete = false;
    ServiceRuntime runtime (config, factoryFor (state));

    require (runtime.start(), "Runtime must begin serial startup");
    require (state.created == 1,
             "The second engine must wait for the first start to complete");
    state.plans[0].complete = true;
    runtime.tick();
    require (state.created == 2 && runtime.activeClientCount() == 2,
             "The next engine must start after completion");
    stopRuntime (runtime);
}

void testContinuesAfterStartFailure()
{
    ServiceConfig config = ServiceConfig::createDefault();
    config.clients = { client ("svc-001", "Failed"), client ("svc-002", "Ready") };
    FakeState state;
    state.plans.resize (2);
    state.plans[0].acceptStart = false;
    ServiceRuntime runtime (config, factoryFor (state));

    require (runtime.start(), "A failed client must not abort runtime startup");
    require (state.created == 2 && runtime.activeClientCount() == 1,
             "A later client must start after an earlier failure");
    require (runtime.failedClientNames().size() == 1
                 && runtime.failedClientNames()[0] == "Failed",
             "The failed client must be recorded");
    stopRuntime (runtime);
}

void testRefreshesOnlyActiveClients()
{
    ServiceConfig config = ServiceConfig::createDefault();
    config.clients = { client ("svc-001", "Failed"), client ("svc-002", "Ready") };
    FakeState state;
    state.plans.resize (2);
    state.plans[0].acceptStart = false;
    ServiceRuntime runtime (config, factoryFor (state));

    require (runtime.start(), "Runtime must start the active client");
    runtime.tick();
    require (state.plans[0].refreshCalls == 0 && state.plans[1].refreshCalls == 1,
             "Refresh must only reach successfully active clients");
    stopRuntime (runtime);
}

void testStopsInReverseOrder()
{
    ServiceConfig config = ServiceConfig::createDefault();
    config.clients = { client ("svc-001", "First"), client ("svc-002", "Second"),
                       client ("svc-003", "Third") };
    FakeState state;
    state.plans.resize (3);
    ServiceRuntime runtime (config, factoryFor (state));

    require (runtime.start(), "Runtime must start all clients before shutdown");
    stopRuntime (runtime);
    require (state.stopOrder == std::vector<int> { 2, 1, 0 },
             "Clients must stop in reverse successful start order");
    require (runtime.state() == ServiceRuntimeState::stopped,
             "Runtime must report stopped after all engines stop");
}

} // namespace

int main()
{
    testSkipsDisabledAndMissing();
    testStartsStrictlySerially();
    testContinuesAfterStartFailure();
    testRefreshesOnlyActiveClients();
    testStopsInReverseOrder();
    std::cout << "ServiceRuntime tests passed\n";
    return 0;
}