// SPDX-License-Identifier: AGPL-3.0-only
// Offline ABI fixture: counters represent JACK side effects without a server.
#include <WinJACKNexus/Node/NodeApi.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef WJN_FIXTURE_CAPABILITIES
#define WJN_FIXTURE_CAPABILITIES 3
#endif
namespace
{
uint32_t creations{}, initialized{}, jackClients{}, calls{};
class FixtureNode final : public WinJACKNexus::Node::INode
{
  public:
    const uint32_t mode;
    uint32_t maxFrames{};
    explicit FixtureNode(uint32_t value) : mode(value)
    {
        ++creations;
    }
    WinJACKNexus::Node::NodeInfo info() const noexcept override
    {
        return {"Node v4 fixture", "Tests", "1", "WJN", "Offline processing"};
    }
    bool init() noexcept override
    {
        ++initialized;
        if (mode == WJN_NODE_MODE_JACK)
        {
            ++jackClients;
        }
        return true;
    }
    void shutdown() noexcept override
    {
        maxFrames = 0;
    }
    HWND createWindow(HWND) noexcept override
    {
        return nullptr;
    }
    void destroyWindow(HWND) noexcept override {}
};
uint32_t portCount(void *)
{
    return 4;
}
int32_t port(void *, uint32_t index, WjnNodePortV1 *description)
{
    if (!description || description->structSize != sizeof(*description) || index >= 4)
    {
        return 0;
    }
    static const char *names[]{"Audio In", "Audio Out", "MIDI In", "MIDI Out"};
    *description = {sizeof(*description),
                    index + 1,
                    static_cast<uint32_t>(index % 2 ? WJN_NODE_PORT_OUTPUT : WJN_NODE_PORT_INPUT),
                    static_cast<uint32_t>(index < 2 ? WJN_NODE_PORT_AUDIO : WJN_NODE_PORT_MIDI),
                    index < 2 ? 1u : 0u,
                    0,
                    names[index]};
    return 1;
}
int32_t prepare(void *instance, const WjnNodePrepareV1 *context)
{
    auto *node = static_cast<FixtureNode *>(instance);
    if (!node || node->mode != WJN_NODE_MODE_VIRT || !context ||
        context->structSize != sizeof(*context) || !context->maxFrames ||
        !std::isfinite(context->sampleRate) || context->sampleRate < 8000)
    {
        return 0;
    }
    node->maxFrames = context->maxFrames;
    return 1;
}
void process(void *instance, WjnNodeProcessV1 *block)
{
    auto *node = static_cast<FixtureNode *>(instance);
    if (!node || !block || block->structSize != sizeof(*block) ||
        node->mode != WJN_NODE_MODE_VIRT || block->frames > node->maxFrames)
    {
        return;
    }
    ++calls;
    for (uint32_t index = 0; index < block->audioOutputCount; ++index)
    {
        auto &output = block->audioOutputs[index];
        for (uint32_t channel = 0; channel < output.channels; ++channel)
        {
            auto *destination = output.samples[channel];
            const auto *input = block->audioInputCount ? &block->audioInputs[0] : nullptr;
            const auto *source =
                input && channel < input->channels ? input->samples[channel] : nullptr;
            if (source)
            {
                std::copy_n(source, block->frames, destination);
            }
            else
            {
                std::fill_n(destination, block->frames, 0.f);
            }
        }
    }
    for (uint32_t index = 0; index < block->midiOutputCount; ++index)
    {
        auto &output = block->midiOutputs[index];
        output.eventCount = output.byteCount = 0;
        if (!block->midiInputCount)
        {
            continue;
        }
        const auto &input = block->midiInputs[0];
        for (uint32_t eventIndex = 0; eventIndex < input.eventCount; ++eventIndex)
        {
            const auto &event = input.events[eventIndex];
            if (event.frame >= block->frames || !event.data || !event.size ||
                output.eventCount == output.eventCapacity ||
                event.size > output.byteCapacity - output.byteCount)
            {
                ++output.dropped;
                continue;
            }
            auto *destination = output.bytes + output.byteCount;
            std::memcpy(destination, event.data, event.size);
            output.events[output.eventCount++] = {event.frame, event.size, destination};
            output.byteCount += event.size;
        }
    }
}
void reset(void *) {}
void stop(void *instance)
{
    static_cast<FixtureNode *>(instance)->shutdown();
}
const WjnNodeVirtualApiV1 api{
    sizeof(api), WJN_NODE_VIRTUAL_API_VERSION, portCount, port, prepare, process, reset, stop};
}

extern "C" WINJACK_NODE_API uint32_t wjn_fixture_counter(uint32_t index)
{
    const uint32_t counters[]{creations, initialized, jackClients, calls};
    return index < 4 ? counters[index] : 0;
}
const char *wjn_get_node_api_version(void)
{
#ifdef WJN_FIXTURE_OLD
    return "3";
#else
    return "4";
#endif
}
#ifndef WJN_FIXTURE_MISSING_METADATA
int32_t wjn_get_node_metadata_v4(WjnNodeMetadataV4 *metadata)
{
    if (!metadata || metadata->structSize != sizeof(*metadata))
    {
        return 0;
    }
    *metadata = {sizeof(*metadata), WJN_NODE_METADATA_VERSION, WJN_FIXTURE_CAPABILITIES, 0, {0, 0}};
    return 1;
}
#endif
const char *wjn_get_node_name(void)
{
    return "Node v4 fixture";
}
const char *wjn_get_node_category(void)
{
    return "Tests";
}
WinJACKNexus::Node::INode *wjn_create_node_v4(uint32_t mode)
{
    if ((mode != WJN_NODE_MODE_JACK && mode != WJN_NODE_MODE_VIRT) ||
        (WJN_FIXTURE_CAPABILITIES & mode) == 0)
    {
        return nullptr;
    }
    return new FixtureNode(mode);
}
WinJACKNexus::Node::INode *wjn_create_node(void)
{
    return wjn_create_node_v4(WJN_NODE_MODE_JACK);
}
void wjn_destroy_node(WinJACKNexus::Node::INode *node)
{
    delete node;
}
const WjnNodeVirtualApiV1 *wjn_get_node_virtual_api_v1(void)
{
#ifdef WJN_FIXTURE_INCOMPLETE
    static auto incomplete = api;
    incomplete.stop = nullptr;
    return &incomplete;
#else
    return (WJN_FIXTURE_CAPABILITIES & WJN_NODE_VIRT_NODE) ? &api : nullptr;
#endif
}
