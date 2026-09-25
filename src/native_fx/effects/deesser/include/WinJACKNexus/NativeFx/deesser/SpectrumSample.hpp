/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

namespace WinJACKNexus::Common {

// Kept as a pair in the SPSC queue so input and processed spectra always use
// samples from the same audio frames.
struct DeEsserSpectrumSample final {
    float input = 0.0F;
    float processed = 0.0F;
};

}
