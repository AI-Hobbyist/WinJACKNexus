// Offline JACK boundary for this test's private worker deployment only.
// SPDX-License-Identifier: AGPL-3.0-only
#include <Windows.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

struct _jack_port
{
    bool input{};
    bool midi{};
    std::array<float, 64> samples{};
};
struct _jack_client
{
    JackProcessCallback callback{};
    void *context{};
    std::atomic<bool> active{};
    std::thread worker;
    std::vector<std::unique_ptr<_jack_port>> ports;
    unsigned callbacks{};
};
extern "C"
{
    jack_client_t *jack_client_open(const char *, jack_options_t, jack_status_t *, ...)
    {
        return new _jack_client;
    }
    int jack_deactivate(jack_client_t *client)
    {
        client->active = false;
        if (client->worker.joinable())
            client->worker.join();
        return 0;
    }
    int jack_client_close(jack_client_t *client)
    {
        jack_deactivate(client);
        wchar_t path[32768]{};
        if (GetEnvironmentVariableW(L"WJN_S5_JACK_RECORD", path, 32768))
        {
            FILE *file{};
            if (_wfopen_s(&file, path, L"wb") == 0)
            {
                float output = 0;
                for (const auto &port : client->ports)
                    if (!port->input && !port->midi)
                    {
                        output = port->samples[0];
                        break;
                    }
                const uint32_t values[]{1, uint32_t(client->ports.size()), client->callbacks};
                fwrite(values, sizeof(values), 1, file);
                fwrite(&output, sizeof(output), 1, file);
                fclose(file);
            }
        }
        delete client;
        return 0;
    }
    int jack_set_process_callback(jack_client_t *client, JackProcessCallback callback,
                                  void *context)
    {
        client->callback = callback;
        client->context = context;
        return 0;
    }
    int jack_activate(jack_client_t *client)
    {
        client->active = true;
        client->worker = std::thread([client] {
            for (unsigned block = 0; block < 32 && client->active; ++block)
            {
                for (auto &port : client->ports)
                    if (port->input && !port->midi)
                        port->samples.fill(2.0F);
                client->callback(64, client->context);
                ++client->callbacks;
                Sleep(2);
            }
        });
        return 0;
    }
    jack_port_t *jack_port_register(jack_client_t *client, const char *, const char *type,
                                    unsigned long flags, unsigned long)
    {
        auto port = std::make_unique<_jack_port>();
        port->input = (flags & JackPortIsInput) != 0;
        port->midi = std::strcmp(type, JACK_DEFAULT_MIDI_TYPE) == 0;
        auto *result = port.get();
        client->ports.push_back(std::move(port));
        return result;
    }
    void *jack_port_get_buffer(jack_port_t *port, jack_nframes_t)
    {
        return port->midi ? static_cast<void *>(port) : port->samples.data();
    }
    uint32_t jack_midi_get_event_count(void *)
    {
        return 1;
    }
    int jack_midi_event_get(jack_midi_event_t *event, void *, uint32_t index)
    {
        static jack_midi_data_t note[]{0x90, 60, 100};
        if (index)
            return -1;
        *event = {17, 3, note};
        return 0;
    }
    jack_nframes_t jack_get_sample_rate(jack_client_t *)
    {
        return 48000;
    }
    jack_nframes_t jack_get_buffer_size(jack_client_t *)
    {
        return 64;
    }
}
