#pragma once

#include "JackClient.h"

#include <array>
#include <atomic>
#include <mutex>

#include <jack/midiport.h>

namespace wjn::common
{

class JackClientHub final
{
public:
    static constexpr int maxGroups = 256;
    static constexpr int maxPortsPerGroup = 8;
    static constexpr int invalidPortHandle = -1;

    using AudioProcessCallback = JackClient::ProcessCallback;
    using MidiProcessCallback = void (*) (void* buffer, jack_nframes_t frameCount,
                                          void* userData) noexcept;
    using PortHandle = int;

    JackClientHub() = default;
    ~JackClientHub();

    bool open (const juce::String& clientName, int expectedBlockSize) noexcept;
    bool start (PortHandle handle) noexcept;
    void stop (PortHandle handle) noexcept;
    void close() noexcept;

    PortHandle registerAudioPorts (const juce::StringArray& inputNames,
                                   const juce::StringArray& outputNames,
                                   AudioProcessCallback callback,
                                   void* userData) noexcept;
    PortHandle registerMidiPort (const juce::String& portName,
                                 unsigned long flags,
                                 MidiProcessCallback callback,
                                 void* userData) noexcept;
    bool unregister (PortHandle handle) noexcept;
    bool renameAudioPorts (PortHandle handle,
                           const juce::StringArray& inputNames,
                           const juce::StringArray& outputNames) noexcept;
    bool renameMidiPort (PortHandle handle, const juce::String& portName) noexcept;

    bool isRouteOpen (PortHandle handle) const noexcept;
    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;

    static juce::String audioPortName (const juce::String& clientName,
                                       int channelIndex);
    static juce::String midiPortName (const juce::String& clientName);

private:
    enum class GroupKind
    {
        audio,
        midi
    };

    struct Group
    {
        bool inUse = false;
        GroupKind kind = GroupKind::audio;
        std::atomic<bool> active { false };
        std::array<jack_port_t*, maxPortsPerGroup> inputPorts {};
        std::array<jack_port_t*, maxPortsPerGroup> outputPorts {};
        std::array<const float*, maxPortsPerGroup> inputBuffers {};
        std::array<float*, maxPortsPerGroup> outputBuffers {};
        std::array<juce::String, maxPortsPerGroup> inputNames {};
        std::array<juce::String, maxPortsPerGroup> outputNames {};
        int inputCount = 0;
        int outputCount = 0;
        jack_port_t* midiPort = nullptr;
        AudioProcessCallback audioCallback = nullptr;
        MidiProcessCallback midiCallback = nullptr;
        void* callbackUserData = nullptr;
    };

    static int processCallback (jack_nframes_t frameCount, void* userData) noexcept;
    static int bufferSizeCallback (jack_nframes_t frameCount, void* userData) noexcept;
    static int sampleRateCallback (jack_nframes_t sampleRate, void* userData) noexcept;
    static int xrunCallback (void* userData) noexcept;
    static void shutdownCallback (void* userData) noexcept;

    int findFreeGroup() const noexcept;
    void resetGroup (Group& group) noexcept;
    void setError (const char* message) noexcept;

    jack_client_t* client = nullptr;
    std::array<Group, maxGroups> groups {};
    mutable std::mutex structureMutex;
    int activeRouteCount = 0;
    std::atomic<bool> connected { false };
    std::atomic<bool> running { false };
    std::atomic<int> sampleRate { 0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<juce::uint64> callbackCount { 0 };
    std::atomic<juce::uint64> xrunCount { 0 };
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JackClientHub)
};

} // namespace wjn::common