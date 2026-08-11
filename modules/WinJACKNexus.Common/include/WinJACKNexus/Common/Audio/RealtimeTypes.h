#pragma once

namespace wjn::common
{

struct AudioProcessContext
{
    const float* const* inputs = nullptr;
    float* const* outputs = nullptr;
    int inputChannels = 0;
    int outputChannels = 0;
    int frameCount = 0;
};

class AudioProcessCallback
{
public:
    virtual ~AudioProcessCallback() = default;
    virtual void process(AudioProcessContext& context) noexcept = 0;
};

} // namespace wjn::common
