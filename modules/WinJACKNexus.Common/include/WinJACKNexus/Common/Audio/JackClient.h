#pragma once

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>
#include <jack/jack.h>

namespace wjn::common
{

class JackClient final
{
public:
    static constexpr int maxBlockFrames = 8192;

    struct Status
    {
        bool connected = false;
        bool running = false;
        int sampleRate = 0;
        int blockSize = 0;
        juce::uint64 callbacks = 0;
        juce::uint64 xruns = 0;
    };

    using ProcessCallback = void (*)(const float* const* inputs,
                                      int inputChannels,
                                      float* const* outputs,
                                      int outputChannels,
                                      int frameCount,
                                      void* userData) noexcept;

    JackClient() = default;
    ~JackClient();

    bool open(const juce::String& clientName, int expectedBlockSize) noexcept;
    bool configurePorts(const juce::StringArray& inputNames,
                        const juce::StringArray& outputNames) noexcept;
    bool activate() noexcept;
    void deactivate() noexcept;
    void close() noexcept;
    bool rename(const juce::String& newClientName) noexcept;

    void setProcessCallback(ProcessCallback callback, void* userData) noexcept;
    Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;

private:
    static int processCallback(jack_nframes_t frameCount, void* userData) noexcept;
    static int bufferSizeCallback(jack_nframes_t frameCount, void* userData) noexcept;
    static int sampleRateCallback(jack_nframes_t sampleRate, void* userData) noexcept;
    static int xrunCallback(void* userData) noexcept;
    static void shutdownCallback(void* userData) noexcept;

    bool registerPorts(const juce::StringArray& names, unsigned long flags,
                       std::vector<jack_port_t*>& destination) noexcept;
    void setError(const char* message) noexcept;

    jack_client_t* client = nullptr;
    std::vector<jack_port_t*> inputPorts;
    std::vector<jack_port_t*> outputPorts;
    std::vector<juce::String> inputPortNames;
    std::vector<juce::String> outputPortNames;
    std::vector<const float*> inputBuffers;
    std::vector<float*> outputBuffers;
    ProcessCallback callback = nullptr;
    void* callbackUserData = nullptr;
    std::atomic<bool> connected { false };
    std::atomic<bool> running { false };
    std::atomic<int> sampleRate { 0 };
    std::atomic<int> blockSize { 0 };
    std::atomic<juce::uint64> callbackCount { 0 };
    std::atomic<juce::uint64> xrunCount { 0 };
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JackClient)
};

} // namespace wjn::common
