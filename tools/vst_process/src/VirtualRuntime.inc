// Included after AudioPlugin; isolated worker only. SPDX-License-Identifier: AGPL-3.0-only
class VirtualRuntime final
{
  public:
    ~VirtualRuntime()
    {
        stop();
    }
    bool start(const std::wstring &name, AudioPlugin &plugin, std::string &error)
    {
        if (!mapping_.open(name))
        {
            error = "Incompatible VST block transport v1";
            return false;
        }
        auto *block = mapping_.block();
        if (plugin.inputCount() < 0 || plugin.outputCount() < 0 || plugin.midiInputCount() < 0 ||
            plugin.inputCount() > int(IPC::maxChannels) ||
            plugin.outputCount() > int(IPC::maxChannels) ||
            plugin.midiInputCount() > int(IPC::maxChannels) || !std::isfinite(block->sampleRate) ||
            block->sampleRate <= 0 || !plugin.prepare(block->sampleRate, block->maxFrames, error))
            return false;
        plugin_ = &plugin;
        block->inputCount = plugin.inputCount();
        block->outputCount = plugin.outputCount();
        block->midiInputCount = plugin.midiInputCount();
        for (uint32_t channel = 0; channel < IPC::maxChannels; ++channel)
        {
            inputs_[channel] = mapping_.audio(false, channel);
            outputs_[channel] = mapping_.audio(true, channel);
        }
        active_ = true;
        worker_ = std::thread([this] { run(); });
        return true;
    }
    bool isActive() const noexcept
    {
        return active_;
    }
    std::mutex &controlMutex() noexcept
    {
        return mutex_;
    }
    bool reset(std::string &error)
    {
        const auto *block = mapping_.block();
        return plugin_->prepare(block->sampleRate, block->maxFrames, error);
    }
    void stop() noexcept
    {
        active_ = false;
        if (mapping_.event())
            SetEvent(mapping_.event());
        if (worker_.joinable())
            worker_.join();
        mapping_.close();
        plugin_ = nullptr;
    }

  private:
    void run()
    {
        while (active_)
        {
            WaitForSingleObject(mapping_.event(), INFINITE);
            if (!active_)
                break;
            auto *block = mapping_.block();
            if (InterlockedCompareExchange(&block->state, IPC::processing, IPC::pending) !=
                IPC::pending)
                continue;
            if (!block->frames || block->frames > block->maxFrames ||
                block->eventCount > IPC::maxEvents || block->byteCount > IPC::maxMidiBytes)
            {
                InterlockedExchange(&block->state, IPC::free);
                continue;
            }
            uint32_t count = 0;
            for (uint32_t index = 0; index < block->eventCount; ++index)
            {
                const auto &event = block->events[index];
                if (event.port >= block->midiInputCount || event.frame >= block->frames ||
                    event.offset > block->byteCount || event.size > block->byteCount - event.offset)
                    continue;
                midi_[count] = {int32_t(event.port), int32_t(event.frame), int32_t(count),
                                block->bytes + event.offset, event.size};
                ++count;
            }
            std::sort(midi_.begin(), midi_.begin() + count,
                      [](const auto &left, const auto &right) {
                          return left.sampleOffset < right.sampleOffset ||
                                 (left.sampleOffset == right.sampleOffset &&
                                  left.sequence < right.sequence);
                      });
            {
                std::lock_guard<std::mutex> control(mutex_);
                for (uint32_t channel = 0; channel < block->outputCount; ++channel)
                    std::fill_n(outputs_[channel], block->frames, 0.0F);
                plugin_->setSamplePosition(block->samplePosition);
                plugin_->process(inputs_.data(), outputs_.data(), block->frames, midi_.data(),
                                 count);
            }
            InterlockedExchange(&block->state, IPC::complete);
        }
    }
    IPC::Mapping mapping_;
    AudioPlugin *plugin_{};
    std::atomic<bool> active_{};
    std::thread worker_;
    std::mutex mutex_;
    std::array<float *, IPC::maxChannels> inputs_{};
    std::array<float *, IPC::maxChannels> outputs_{};
    std::array<MidiInputEvent, IPC::maxEvents> midi_{};
};
