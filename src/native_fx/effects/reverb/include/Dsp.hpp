/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/NativeFx/reverb/Processor.hpp>
#include <cstdint>

struct ReverbDsp final {
    using Processor = WinJACKNexus::Common::ReverbProcessor;
    using Parameters = WinJACKNexus::Common::ReverbParameters;
    static void prepare(Processor& processor, double rate) { processor.setSampleRate(static_cast<float>(rate)); }
    static void setParameters(Processor&, const Parameters&) {}
    static void process(Processor& processor, const Parameters& values,
                        const float* input, float* output, std::uint32_t frames) {
        processor.process(input, output, frames, values);
    }
};
