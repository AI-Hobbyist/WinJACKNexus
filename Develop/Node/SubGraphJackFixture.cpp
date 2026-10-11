// SPDX-License-Identifier: AGPL-3.0-only
// In-process boundary substitute. It never loads or connects to a JACK server.
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct _jack_client
{
    std::string name;
    JackProcessCallback process{};
    void *context{};
    JackSampleRateCallback rateCallback{};
    void *rateContext{};
    JackBufferSizeCallback sizeCallback{};
    void *sizeContext{};
    bool active{};
};
struct _jack_port
{
    jack_client_t *client{};
    std::string name, type;
    unsigned long flags{};
    std::array<float, 8192> audio{};
    std::vector<std::pair<jack_nframes_t, std::vector<uint8_t>>> midi;
};
namespace
{
std::vector<std::unique_ptr<jack_client_t>> clients;
std::vector<std::unique_ptr<jack_port_t>> ports;
uint32_t opened{}, registered{}, callbacks{}, activations{}, deactivations{};
jack_nframes_t sampleRate = 48000, blockFrames = 64;
} // namespace
extern "C"
{
    __declspec(dllexport) void wjn_boundary_set_clock(jack_nframes_t rate, jack_nframes_t frames)
    {
        sampleRate = rate;
        blockFrames = frames;
        for (const auto &client : clients)
        {
            if (!client->active)
                continue;
            if (client->rateCallback)
                client->rateCallback(sampleRate, client->rateContext);
            if (client->sizeCallback)
                client->sizeCallback(blockFrames, client->sizeContext);
        }
    }
    __declspec(dllexport) uint32_t wjn_boundary_counter(uint32_t index)
    {
        const uint32_t values[]{
            opened,      registered,   callbacks, uint32_t(clients.size()), uint32_t(ports.size()),
            activations, deactivations};
        return index < 7 ? values[index] : 0;
    }
    __declspec(dllexport) void wjn_boundary_process()
    {
        for (const auto &client : clients)
        {
            if (client->active && client->process)
            {
                ++callbacks;
                client->process(blockFrames, client->context);
            }
        }
    }
    const char *jack_get_version_string()
    {
        return "S3 offline boundary";
    }
    jack_client_t *jack_client_open(const char *name, jack_options_t, jack_status_t *status, ...)
    {
        auto value = std::make_unique<jack_client_t>();
        value->name = name;
        auto *result = value.get();
        clients.push_back(std::move(value));
        if (status)
            *status = static_cast<jack_status_t>(0);
        ++opened;
        return result;
    }
    int jack_client_close(jack_client_t *client)
    {
        std::erase_if(ports, [&](const auto &port) { return port->client == client; });
        std::erase_if(clients, [&](const auto &value) { return value.get() == client; });
        return 0;
    }
    int jack_client_name_size()
    {
        return 256;
    }
    char *jack_get_client_name(jack_client_t *client)
    {
        return client->name.data();
    }
    int jack_activate(jack_client_t *client)
    {
        ++activations;
        if (client->rateCallback)
            client->rateCallback(sampleRate, client->rateContext);
        if (client->sizeCallback)
            client->sizeCallback(blockFrames, client->sizeContext);
        client->active = true;
        return 0;
    }
    int jack_deactivate(jack_client_t *client)
    {
        ++deactivations;
        client->active = false;
        return 0;
    }
    void jack_on_shutdown(jack_client_t *, JackShutdownCallback, void *)
    {
    }
    int jack_set_process_callback(jack_client_t *client, JackProcessCallback callback,
                                  void *context)
    {
        client->process = callback;
        client->context = context;
        return 0;
    }
    int jack_set_buffer_size_callback(jack_client_t *client, JackBufferSizeCallback callback,
                                      void *context)
    {
        client->sizeCallback = callback;
        client->sizeContext = context;
        return 0;
    }
    int jack_set_sample_rate_callback(jack_client_t *client, JackSampleRateCallback callback,
                                      void *context)
    {
        client->rateCallback = callback;
        client->rateContext = context;
        return 0;
    }
    int jack_set_graph_order_callback(jack_client_t *, JackGraphOrderCallback, void *)
    {
        return 0;
    }
    int jack_set_xrun_callback(jack_client_t *, JackXRunCallback, void *)
    {
        return 0;
    }
    jack_nframes_t jack_get_sample_rate(jack_client_t *)
    {
        return sampleRate;
    }
    jack_nframes_t jack_get_buffer_size(jack_client_t *)
    {
        return blockFrames;
    }
    float jack_cpu_load(jack_client_t *)
    {
        return 0;
    }
    jack_port_t *jack_port_register(jack_client_t *client, const char *name, const char *type,
                                    unsigned long flags, unsigned long)
    {
        auto port = std::make_unique<jack_port_t>();
        port->client = client;
        port->name = client->name + ":" + name;
        port->type = type;
        port->flags = flags;
        auto *result = port.get();
        ports.push_back(std::move(port));
        ++registered;
        return result;
    }
    int jack_port_rename(jack_client_t *client, jack_port_t *port, const char *name)
    {
        port->name = client->name + ":" + name;
        return 0;
    }
    int jack_port_unregister(jack_client_t *, jack_port_t *port)
    {
        std::erase_if(ports, [&](const auto &value) { return value.get() == port; });
        return 0;
    }
    void *jack_port_get_buffer(jack_port_t *port, jack_nframes_t)
    {
        return port->type == JACK_DEFAULT_AUDIO_TYPE ? static_cast<void *>(port->audio.data())
                                                     : port;
    }
    const char *jack_port_name(const jack_port_t *port)
    {
        return port->name.c_str();
    }
    const char *jack_port_short_name(const jack_port_t *port)
    {
        return port->name.c_str() + port->name.find(':') + 1;
    }
    int jack_port_flags(const jack_port_t *port)
    {
        return int(port->flags);
    }
    const char *jack_port_type(const jack_port_t *port)
    {
        return port->type.c_str();
    }
    const char **jack_port_get_connections(const jack_port_t *)
    {
        return nullptr;
    }
    jack_port_t *jack_port_by_name(jack_client_t *, const char *name)
    {
        for (const auto &port : ports)
            if (port->name == name)
                return port.get();
        return nullptr;
    }
    const char **jack_get_ports(jack_client_t *, const char *, const char *, unsigned long)
    {
        auto **result = static_cast<const char **>(std::calloc(ports.size() + 1, sizeof(char *)));
        for (size_t index = 0; index < ports.size(); ++index)
            result[index] = ports[index]->name.c_str();
        return result;
    }
    int jack_connect(jack_client_t *, const char *, const char *)
    {
        return 0;
    }
    int jack_disconnect(jack_client_t *, const char *, const char *)
    {
        return 0;
    }
    void jack_free(void *value)
    {
        std::free(value);
    }
    uint32_t jack_midi_get_event_count(void *buffer)
    {
        return uint32_t(static_cast<jack_port_t *>(buffer)->midi.size());
    }
    int jack_midi_event_get(jack_midi_event_t *event, void *buffer, uint32_t index)
    {
        const auto &events = static_cast<jack_port_t *>(buffer)->midi;
        if (index >= events.size())
            return -1;
        event->time = events[index].first;
        event->size = events[index].second.size();
        event->buffer = const_cast<uint8_t *>(events[index].second.data());
        return 0;
    }
    void jack_midi_clear_buffer(void *buffer)
    {
        static_cast<jack_port_t *>(buffer)->midi.clear();
    }
    size_t jack_midi_max_event_size(void *)
    {
        return 65536;
    }
    jack_midi_data_t *jack_midi_event_reserve(void *buffer, jack_nframes_t time, size_t size)
    {
        auto &events = static_cast<jack_port_t *>(buffer)->midi;
        events.emplace_back(time, std::vector<uint8_t>(size));
        return events.back().second.data();
    }
    int jack_midi_event_write(void *buffer, jack_nframes_t time, const jack_midi_data_t *data,
                              size_t size)
    {
        std::memcpy(jack_midi_event_reserve(buffer, time, size), data, size);
        return 0;
    }
    uint32_t jack_midi_get_lost_event_count(void *)
    {
        return 0;
    }
    // The patchbay does not use ringbuffers. Required loader symbols intentionally
    // provide no storage, so accidental use fails rather than imitating live audio.
    jack_ringbuffer_t *jack_ringbuffer_create(size_t)
    {
        return nullptr;
    }
    void jack_ringbuffer_free(jack_ringbuffer_t *) {}
    void jack_ringbuffer_get_read_vector(const jack_ringbuffer_t *, jack_ringbuffer_data_t *vectors)
    {
        vectors[0] = {};
        vectors[1] = {};
    }
    void jack_ringbuffer_get_write_vector(const jack_ringbuffer_t *,
                                          jack_ringbuffer_data_t *vectors)
    {
        vectors[0] = {};
        vectors[1] = {};
    }
    size_t jack_ringbuffer_read(jack_ringbuffer_t *, char *, size_t)
    {
        return 0;
    }
    size_t jack_ringbuffer_peek(jack_ringbuffer_t *, char *, size_t)
    {
        return 0;
    }
    void jack_ringbuffer_read_advance(jack_ringbuffer_t *, size_t) {}
    size_t jack_ringbuffer_read_space(const jack_ringbuffer_t *)
    {
        return 0;
    }
    int jack_ringbuffer_mlock(jack_ringbuffer_t *)
    {
        return -1;
    }
    void jack_ringbuffer_reset(jack_ringbuffer_t *) {}
    size_t jack_ringbuffer_write(jack_ringbuffer_t *, const char *, size_t)
    {
        return 0;
    }
    void jack_ringbuffer_write_advance(jack_ringbuffer_t *, size_t) {}
    size_t jack_ringbuffer_write_space(const jack_ringbuffer_t *)
    {
        return 0;
    }
}
