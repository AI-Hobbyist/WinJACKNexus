/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackMidiBuffer.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Audio/JackRingBuffer.hpp>
#include <WinJACKNexus/Audio/JackServerGuard.hpp>
#include <WinJACKNexus/Audio/MidiDevice.hpp>
#include <WinJACKNexus/Audio/MidiEvent.hpp>
#include <WinJACKNexus/Audio/MidiStream.hpp>
#include <WinJACKNexus/Audio/WasapiDevice.hpp>
#include <WinJACKNexus/Audio/WasapiStream.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using namespace WinJACKNexus::Audio;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

struct NameCount final {
    std::size_t count = 0;
};

bool countName(const char*, void* userData) noexcept
{
    auto* result = static_cast<NameCount*>(userData);
    if (result != nullptr)
        ++result->count;
    return true;
}

bool countWasapiDevice(const WasapiDeviceInfo&, void* userData) noexcept
{
    auto* result = static_cast<NameCount*>(userData);
    if (result != nullptr)
        ++result->count;
    return true;
}

bool countMidiDevice(const MidiDeviceInfo&, void* userData) noexcept
{
    auto* result = static_cast<NameCount*>(userData);
    if (result != nullptr)
        ++result->count;
    return true;
}

void renderSilence(float*, std::uint32_t, void*) noexcept
{
}

bool testMidiEvent()
{
    MidiEvent event;
    const std::array<std::uint8_t, 3> noteOn { 0x90, 60, 100 };
    bool passed = true;
    passed &= check(event.assign(noteOn.data(), noteOn.size(), 123), "MIDI event copies raw data");
    passed &= check(event.size == 3 && event.timestampMilliseconds == 123
                        && std::memcmp(event.data, noteOn.data(), noteOn.size()) == 0,
                    "MIDI event preserves timestamp and bytes");
    passed &= check(event.assignShortMessage(0x00643c90u, 456), "MIDI event decodes short message");
    passed &= check(event.size == 3 && event.data[0] == 0x90 && event.data[1] == 0x3c && event.data[2] == 0x64,
                    "MIDI short message has expected bytes");
    passed &= check(event.assignShortMessage(0x000040c0u), "MIDI event decodes program change");
    passed &= check(event.size == 2 && event.data[0] == 0xc0 && event.data[1] == 0x40,
                    "MIDI program change uses two bytes");
    passed &= check(!event.assign(nullptr, 0), "MIDI event rejects empty input");
    return passed;
}

bool testWasapiEnumeration()
{
    WasapiDeviceEnumerator enumerator;
    NameCount inputs;
    NameCount outputs;
    const auto inputCount = enumerator.enumerate(WasapiStreamDirection::capture, countWasapiDevice, &inputs);
    const auto outputCount = enumerator.enumerate(WasapiStreamDirection::render, countWasapiDevice, &outputs);
    bool passed = true;
    passed &= check(inputCount == inputs.count, "WASAPI input enumeration visits every device");
    passed &= check(outputCount == outputs.count, "WASAPI output enumeration visits every device");

    WasapiDeviceInfo defaultOutput;
    if (enumerator.defaultDevice(WasapiStreamDirection::render, defaultOutput)) {
        passed &= check(defaultOutput.isDefault && defaultOutput.isOutput && defaultOutput.id[0] != L'\0',
                        "WASAPI default output contains an ID");
        WasapiStream stream;
        passed &= check(stream.open(WasapiStreamDirection::render), "WASAPI shared render stream opens");
        if (stream.isOpen()) {
            const auto streamFormat = stream.format();
            passed &= check(streamFormat.sampleRate != 0 && streamFormat.channels != 0
                                && stream.bufferFrames() != 0,
                            "WASAPI shared render stream exposes its mix format");
            passed &= check(stream.start(renderSilence), "WASAPI shared render stream starts");
            if (stream.isRunning())
                Sleep(20);
            passed &= check(stream.stop(), "WASAPI shared render stream stops");
            stream.close();
        }
    }
    return passed;
}

bool testMidiEnumeration()
{
    MidiDeviceEnumerator enumerator;
    NameCount inputs;
    NameCount outputs;
    const auto inputCount = enumerator.enumerate(MidiDirection::input, countMidiDevice, &inputs);
    const auto outputCount = enumerator.enumerate(MidiDirection::output, countMidiDevice, &outputs);
    bool passed = true;
    passed &= check(inputCount == inputs.count, "WinMM input enumeration visits every device");
    passed &= check(outputCount == outputs.count, "WinMM output enumeration visits every device");

    MidiDeviceInfo device;
    if (inputCount != 0) {
        passed &= check(enumerator.firstDevice(MidiDirection::input, device), "WinMM finds first input device");
        MidiStream stream;
        passed &= check(stream.open(MidiDirection::input, MidiStreamOptions { device.deviceId }),
                        "WinMM opens first input device");
        if (stream.isOpen())
            passed &= check(stream.start() && stream.stop(), "WinMM input stream starts and stops");
    }
    if (outputCount != 0) {
        passed &= check(enumerator.firstDevice(MidiDirection::output, device), "WinMM finds first output device");
        MidiStream stream;
        passed &= check(stream.open(MidiDirection::output, MidiStreamOptions { device.deviceId }),
                        "WinMM opens first output device");
    }
    return passed;
}

std::atomic<std::uint32_t> processCalls { 0 };

int processCallback(jack_nframes_t, void*) noexcept
{
    processCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

bool testJackRuntime()
{
    auto& api = JackApi::instance();
    if (!api.load()) {
        std::puts("JACK runtime unavailable; JACK integration checks skipped");
        return true;
    }

    bool passed = true;
    passed &= check(api.isLoaded() && api.versionString() != nullptr, "JACK runtime loads and reports a version");

    JackRingBuffer ring(64);
    const char input[] = "jack";
    char output[sizeof(input)] {};
    passed &= check(ring.isValid(), "JACK ringbuffer is created");
    passed &= check(ring.write(input, sizeof(input)) == sizeof(input), "JACK ringbuffer writes data");
    passed &= check(ring.peek(output, sizeof(output)) == sizeof(output), "JACK ringbuffer peeks data");
    passed &= check(std::memcmp(input, output, sizeof(input)) == 0, "JACK ringbuffer preserves data");
    passed &= check(ring.read(output, sizeof(output)) == sizeof(output), "JACK ringbuffer reads data");
    passed &= check(ring.readableBytes() == 0, "JACK ringbuffer becomes empty");

    JackMidiBuffer emptyMidi;
    JackMidiEventView ignoredEvent;
    passed &= check(emptyMidi.eventCount() == 0 && !emptyMidi.event(0, ignoredEvent),
                    "JACK MIDI view rejects a null buffer");

    JackServerGuard server;
    bool serverReady = false;
    JackClient existing;
    JackClientOptions probeOptions;
    probeOptions.name = "WJNExistingProbe";
    probeOptions.autoStartServer = false;
    if (existing.open(probeOptions)) {
        serverReady = true;
    } else {
        JackServerOptions serverOptions;
        serverOptions.backend = L"dummy";
        serverOptions.sampleRate = 48000;
        serverOptions.periodFrames = 256;
        if (server.start(serverOptions))
            serverReady = server.waitUntilReady(5000);
    }

    if (!serverReady) {
        std::puts("JACK server unavailable; client/port checks skipped");
        return passed;
    }

    JackClient client;
    JackClientOptions options;
    options.name = "WJNAudioTests";
    options.autoStartServer = false;
    passed &= check(client.open(options), "JACK client opens against a running server");
    if (client.isOpen()) {
        JackPort outputPort;
        JackPort inputPort;
        passed &= check(outputPort.registerPort(client,
                                                "test_out",
                                                JACK_DEFAULT_AUDIO_TYPE,
                                                JackPortIsOutput),
                        "JACK output port registers");
        passed &= check(inputPort.registerPort(client,
                                               "test_in",
                                               JACK_DEFAULT_AUDIO_TYPE,
                                               JackPortIsInput),
                        "JACK input port registers");
        passed &= check(outputPort.isOutput() && inputPort.isInput(), "JACK port direction flags are exposed");
        NameCount ports;
        JackGraph::visitPortNames(client, "WJNAudioTests:*", JACK_DEFAULT_AUDIO_TYPE, 0, countName, &ports);
        passed &= check(ports.count >= 2 && JackGraph::portExists(client, outputPort.name()),
                        "JACK graph enumerates registered ports");
        passed &= check(client.setProcessCallback(processCallback, nullptr) == 0,
                        "JACK process callback registers");
        processCalls.store(0, std::memory_order_release);
        passed &= check(client.activate(), "JACK client activates");
        for (int attempt = 0; attempt < 100 && processCalls.load(std::memory_order_acquire) == 0; ++attempt)
            Sleep(10);
        passed &= check(processCalls.load(std::memory_order_acquire) != 0, "JACK process callback runs");
        passed &= check(client.deactivate() && client.close(), "JACK client deactivates and closes");
    }
    existing.close();
    server.stop();
    return passed;
}

}

int main()
{
    bool passed = true;
    passed &= testMidiEvent();
    passed &= testWasapiEnumeration();
    passed &= testMidiEnumeration();
    passed &= testJackRuntime();
    if (!passed)
        return 1;
    std::puts("Audio tests passed");
    return 0;
}