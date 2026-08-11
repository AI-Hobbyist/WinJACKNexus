#pragma once

#include <array>
#include <functional>
#include <vector>

#include <juce_core/juce_core.h>

namespace wjn::common
{

struct HistorySample
{
    juce::Time timestamp;
    std::array<float, 7> values {};
};

using HistoryProvider = std::function<std::vector<HistorySample>()>;

} // namespace wjn::common
