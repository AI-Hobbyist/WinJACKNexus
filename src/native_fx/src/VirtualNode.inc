// SPDX-License-Identifier: AGPL-3.0-only
// Included inside each effect's node adapter; both modes use processBuffers.
uint32_t virtualPortCount() const noexcept
{
    return 16;
}
bool virtualPort(uint32_t index, WjnNodePortV1 *description) const noexcept
{
    static constexpr const char *names[]{
        "in_L",  "in_R",  "in_C",  "in_LFE",  "in_Ls",  "in_Rs",  "in_Lrb",  "in_Rrb",
        "out_L", "out_R", "out_C", "out_LFE", "out_Ls", "out_Rs", "out_Lrb", "out_Rrb"};
    if (!description || description->structSize != sizeof(*description) || index >= 16)
    {
        return false;
    }
    *description = {sizeof(*description),
                    index + 1,
                    static_cast<uint32_t>(index < 8 ? WJN_NODE_PORT_INPUT : WJN_NODE_PORT_OUTPUT),
                    WJN_NODE_PORT_AUDIO,
                    1,
                    0,
                    names[index]};
    return true;
}
bool virtualPrepare(const WjnNodePrepareV1 *context) noexcept
{
    if (mode_ != WJN_NODE_MODE_VIRT || !context || context->structSize != sizeof(*context) ||
        !std::isfinite(context->sampleRate) || context->sampleRate <= 0 ||
        context->sampleRate > 768000 || !context->maxFrames || context->maxFrames > 65536)
    {
        return false;
    }
    virtualPrepared_ = false;
    if (sampleRateChanged(static_cast<jack_nframes_t>(context->sampleRate), this) != 0)
    {
        return false;
    }
    maxVirtualFrames_ = context->maxFrames;
    virtualReset();
    virtualPrepared_ = true;
    return true;
}
void virtualProcess(WjnNodeProcessV1 *block) noexcept
{
    if (mode_ != WJN_NODE_MODE_VIRT || !block || block->structSize != sizeof(*block))
    {
        return;
    }
    std::array<const float *, 8> inputs{};
    std::array<float *, 8> outputs{};
    for (uint32_t index = 0; block->audioInputs && index < block->audioInputCount; ++index)
    {
        const auto &port = block->audioInputs[index];
        if (port.portId >= 1 && port.portId <= 8 && port.channels == 1 && port.samples)
        {
            inputs[port.portId - 1] = port.samples[0];
        }
    }
    for (uint32_t index = 0; block->audioOutputs && index < block->audioOutputCount; ++index)
    {
        const auto &port = block->audioOutputs[index];
        if (port.portId >= 9 && port.portId <= 16 && port.channels == 1 && port.samples)
        {
            outputs[port.portId - 9] = port.samples[0];
        }
    }
    if (!virtualPrepared_ || block->frames > maxVirtualFrames_)
    {
        for (auto *output : outputs)
        {
            if (output)
            {
                std::fill_n(output, block->frames, 0.0f);
            }
        }
        return;
    }
    processBuffers(block->frames, *this, inputs, outputs);
}
void virtualReset() noexcept
{
    if (mode_ == WJN_NODE_MODE_VIRT)
    {
        for (auto &processor : processors_)
        {
            processor.reset();
        }
    }
}
void virtualStop() noexcept
{
    if (mode_ == WJN_NODE_MODE_VIRT)
    {
        virtualPrepared_ = false;
        virtualReset();
        maxVirtualFrames_ = 0;
    }
}
bool hasModeLayout(uint32_t mode) const noexcept
{
    if (mode != mode_)
    {
        return false;
    }
    return mode == WJN_NODE_MODE_VIRT ? virtualPrepared_ : hasFixedPortLayout();
}

private:
const uint32_t mode_;
uint32_t maxVirtualFrames_{};
bool virtualPrepared_{};

public:
