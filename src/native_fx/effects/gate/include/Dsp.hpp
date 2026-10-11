/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/NativeFx/gate/Processor.hpp>
#include <cstdint>

struct GateDsp final
{
    using Processor = WinJACKNexus::Common::GateProcessor;
    using Parameters = WinJACKNexus::Common::GateParameters;
    static void prepare(Processor &processor, double rate)
    {
        processor.prepare(rate);
    }
    static void setParameters(Processor &processor, const Parameters &values)
    {
        processor.setParameters(values);
    }
    static void process(Processor &processor, const Parameters &values, const float *input,
                        float *output, std::uint32_t frames)
    {
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            output[frame] = processor.process(input[frame]);
    }
};
