/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <WinJACKNexus/Patchbay/Plugins/InternalLoader.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace WinJACKNexus::Patchbay::Plugins {

namespace {

class InternalInstance final : public IPluginInstance {
public:
    explicit InternalInstance(PluginDescriptor descriptor)
        : descriptor_(std::move(descriptor))
    {
        if (descriptor_.identifier == "internal:gain")
            parameters_.push_back({ "gain", "Gain", 1.0F, 0.0F, 4.0F });
    }

    const PluginDescriptor& descriptor() const noexcept override
    {
        return descriptor_;
    }

    bool initialize(double sampleRate, unsigned int bufferSize) noexcept override
    {
        return sampleRate > 0.0 && bufferSize != 0;
    }

    void process(ProcessBlock block) noexcept override
    {
        const auto count = std::min(block.input.size(), block.output.size());
        if (descriptor_.identifier == "internal:bypass") {
            std::copy_n(block.input.begin(), count, block.output.begin());
            return;
        }
        if (descriptor_.identifier == "internal:meter") {
            peak_ = 0.0F;
            for (std::size_t index = 0; index < count; ++index) {
                block.output[index] = block.input[index];
                peak_ = std::max(peak_, std::abs(block.input[index]));
            }
            return;
        }

        const float gain = parameters_.empty() ? 1.0F : parameters_.front().value;
        for (std::size_t index = 0; index < count; ++index)
            block.output[index] = block.input[index] * gain;
    }

    std::vector<ParameterInfo> parameters() const override
    {
        auto values = parameters_;
        if (descriptor_.identifier == "internal:meter")
            values.push_back({ "peak", "Peak", peak_, 0.0F, 1.0F });
        return values;
    }

    bool setParameter(const std::string& id, float value) noexcept override
    {
        const auto item = std::find_if(parameters_.begin(), parameters_.end(), [&id](const ParameterInfo& parameter) {
            return parameter.id == id;
        });
        if (item == parameters_.end())
            return false;
        item->value = std::clamp(value, item->minimum, item->maximum);
        return true;
    }

private:
    PluginDescriptor descriptor_;
    std::vector<ParameterInfo> parameters_;
    float peak_ = 0.0F;
};

}

std::vector<PluginDescriptor> InternalLoader::descriptors()
{
    return {
        { PluginKind::internal, "internal:gain", "Gain", "WinJACKNexus", "0.1.0", {}, true },
        { PluginKind::internal, "internal:bypass", "Bypass", "WinJACKNexus", "0.1.0", {}, true },
        { PluginKind::internal, "internal:meter", "Peak Meter", "WinJACKNexus", "0.1.0", {}, true }
    };
}

std::unique_ptr<IPluginInstance> InternalLoader::create(const std::string& identifier)
{
    const auto values = descriptors();
    const auto item = std::find_if(values.cbegin(), values.cend(), [&identifier](const PluginDescriptor& descriptor) {
        return descriptor.identifier == identifier;
    });
    return item == values.cend() ? nullptr : std::make_unique<InternalInstance>(*item);
}

}