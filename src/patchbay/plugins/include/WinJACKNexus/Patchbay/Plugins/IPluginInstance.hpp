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
#pragma once

#include <WinJACKNexus/Patchbay/Plugins/PluginDescriptor.hpp>

#include <span>
#include <string>
#include <vector>

namespace WinJACKNexus::Patchbay::Plugins {

struct ParameterInfo final {
    std::string id;
    std::string name;
    float value = 0.0F;
    float minimum = 0.0F;
    float maximum = 1.0F;
};

struct ProcessBlock final {
    std::span<const float> input;
    std::span<float> output;
};

class WINJACK_PATCHBAY_PLUGINS_API IPluginInstance {
public:
    virtual ~IPluginInstance() = default;

    virtual const PluginDescriptor& descriptor() const noexcept = 0;
    virtual bool initialize(double sampleRate, unsigned int bufferSize) noexcept = 0;
    virtual void process(ProcessBlock block) noexcept = 0;
    virtual std::vector<ParameterInfo> parameters() const = 0;
    virtual bool setParameter(const std::string& id, float value) noexcept = 0;
};

}