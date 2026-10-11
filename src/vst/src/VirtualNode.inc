// VIRT adapter for the existing sandbox. SPDX-License-Identifier: AGPL-3.0-only
uint32_t virtualPortCount() const noexcept
{
    return uint32_t(virtualPorts_.size());
}
bool virtualPort(uint32_t index, WjnNodePortV1 *port) const noexcept
{
    if (!port || port->structSize != sizeof(*port) || index >= virtualPorts_.size())
        return false;
    *port = virtualPorts_[index];
    return true;
}
bool virtualPrepare(const WjnNodePrepareV1 *context) noexcept
{
    if (mode_ != WJN_NODE_MODE_VIRT || !context || context->structSize != sizeof(*context) ||
        !std::isfinite(context->sampleRate) || context->sampleRate <= 0 ||
        context->sampleRate > 768000 || !context->maxFrames || context->maxFrames > 65536)
        return false;
    stopProcess();
    mapping_.close();
    virtualMaxFrames_ = context->maxFrames;
    virtualRate_ = context->sampleRate;
    initialized_ = true;
    if (!processWatcher_)
        processWatcher_ = std::make_unique<QObject>();
    return reload();
}
void virtualProcess(WjnNodeProcessV1 *block) noexcept
{
    if (mode_ != WJN_NODE_MODE_VIRT || !block || block->structSize != sizeof(*block))
        return;
    for (uint32_t index = 0; block->audioOutputs && index < block->audioOutputCount; ++index)
    {
        const auto &port = block->audioOutputs[index];
        for (uint32_t channel = 0; port.samples && channel < port.channels; ++channel)
            if (port.samples[channel])
                std::fill_n(port.samples[channel], block->frames, 0.0F);
    }
    auto *transport = mapping_.block();
    if (!transport || !block->frames || block->frames > virtualMaxFrames_)
        return;
    const auto state = InterlockedCompareExchange(&transport->state, IPC::free, IPC::free);
    if (state == IPC::complete)
    {
        if (transport->generation == virtualGeneration_ && transport->frames == block->frames)
        {
            for (uint32_t index = 0; block->audioOutputs && index < block->audioOutputCount;
                 ++index)
            {
                const auto &port = block->audioOutputs[index];
                if (port.portId > uint32_t(inputCount_) &&
                    port.portId <= uint32_t(inputCount_ + outputCount_) && port.channels == 1 &&
                    port.samples && port.samples[0])
                    std::copy_n(mapping_.audio(true, port.portId - inputCount_ - 1), block->frames,
                                port.samples[0]);
            }
        }
        InterlockedExchange(&transport->state, IPC::free);
    }
    else if (state != IPC::free)
    {
        ++virtualOverruns_;
        return;
    }
    for (uint32_t channel = 0; channel < uint32_t(inputCount_); ++channel)
        std::fill_n(mapping_.audio(false, channel), block->frames, 0.0F);
    for (uint32_t index = 0; block->audioInputs && index < block->audioInputCount; ++index)
    {
        const auto &port = block->audioInputs[index];
        if (port.portId >= 1 && port.portId <= uint32_t(inputCount_) && port.channels == 1 &&
            port.samples && port.samples[0])
            std::copy_n(port.samples[0], block->frames, mapping_.audio(false, port.portId - 1));
    }
    transport->eventCount = 0;
    transport->byteCount = 0;
    for (uint32_t index = 0; block->midiInputs && index < block->midiInputCount; ++index)
    {
        const auto &port = block->midiInputs[index];
        const auto firstMidiId = uint32_t(inputCount_ + outputCount_ + 1);
        if (port.portId < firstMidiId || port.portId >= firstMidiId + uint32_t(midiInputCount_))
            continue;
        const auto inspected = std::min(port.eventCount, IPC::maxEvents);
        virtualMidiDropped_ += port.eventCount - inspected;
        for (uint32_t eventIndex = 0; port.events && eventIndex < inspected; ++eventIndex)
        {
            const auto &event = port.events[eventIndex];
            if (!event.data || !event.size || event.frame >= block->frames ||
                transport->eventCount == IPC::maxEvents ||
                event.size > IPC::maxMidiBytes - transport->byteCount)
            {
                ++virtualMidiDropped_;
                continue;
            }
            transport->events[transport->eventCount++] = {port.portId - firstMidiId, event.frame,
                                                          transport->byteCount, event.size};
            std::memcpy(transport->bytes + transport->byteCount, event.data, event.size);
            transport->byteCount += event.size;
        }
    }
    transport->frames = block->frames;
    transport->samplePosition = block->samplePosition;
    transport->generation = virtualGeneration_;
    transport->sequence = ++virtualSequence_;
    InterlockedExchange(&transport->state, IPC::pending);
    SetEvent(mapping_.event());
}
void virtualReset() noexcept
{
    if (mode_ == WJN_NODE_MODE_VIRT)
    {
        ++virtualGeneration_;
        sendPipeCommand("reset-blocks\n", 3000);
    }
}
void virtualStop() noexcept
{
    if (mode_ == WJN_NODE_MODE_VIRT)
    {
        stopProcess();
        mapping_.close();
        processWatcher_.reset();
        initialized_ = false;
        virtualMaxFrames_ = 0;
    }
}

private:
void refreshVirtualPorts()
{
    virtualPorts_.clear();
    virtualPortNames_.clear();
    const auto count = inputCount_ + outputCount_ + midiInputCount_;
    virtualPortNames_.reserve(count);
    virtualPorts_.reserve(count);
    for (int index = 0; index < count; ++index)
    {
        const bool inputAudio = index < inputCount_;
        const bool outputAudio = index >= inputCount_ && index < inputCount_ + outputCount_;
        const auto number = inputAudio    ? index
                            : outputAudio ? index - inputCount_
                                          : index - inputCount_ - outputCount_;
        const auto label = inputAudio && number < inputLabels_.size() ? inputLabels_[number]
                           : outputAudio && number < outputLabels_.size()
                               ? outputLabels_[number]
                               : QString::number(number + 1);
        virtualPortNames_.push_back(((inputAudio    ? QStringLiteral("in_")
                                      : outputAudio ? QStringLiteral("out_")
                                                    : QStringLiteral("midi_in_")) +
                                     label)
                                        .toUtf8()
                                        .toStdString());
    }
    for (int index = 0; index < count; ++index)
    {
        const bool audio = index < inputCount_ + outputCount_;
        virtualPorts_.push_back(
            {sizeof(WjnNodePortV1), uint32_t(index + 1),
             uint32_t(index < inputCount_ || !audio ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
             uint32_t(audio ? WJN_NODE_PORT_AUDIO : WJN_NODE_PORT_MIDI), audio ? 1u : 0u, 0,
             virtualPortNames_[index].c_str()});
    }
}
const uint32_t mode_;
IPC::Mapping mapping_;
QString mappingName_;
double virtualRate_{};
uint32_t virtualMaxFrames_{};
uint32_t virtualGeneration_{};
std::atomic_uint64_t virtualSequence_{};
std::atomic_uint64_t virtualOverruns_{};
std::atomic_uint64_t virtualMidiDropped_{};
std::vector<std::string> virtualPortNames_;
std::vector<WjnNodePortV1> virtualPorts_;

public:
uint64_t virtualCounter(uint32_t index) const noexcept
{
    return index == 0   ? virtualOverruns_.load(std::memory_order_relaxed)
           : index == 1 ? virtualMidiDropped_.load(std::memory_order_relaxed)
                        : virtualSequence_.load(std::memory_order_relaxed);
}
