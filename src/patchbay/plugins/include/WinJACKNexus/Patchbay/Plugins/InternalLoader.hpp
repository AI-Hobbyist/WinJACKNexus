#pragma once

#include <WinJACKNexus/Patchbay/Plugins/IPluginInstance.hpp>

#include <memory>
#include <string>
#include <vector>

namespace WinJACKNexus::Patchbay::Plugins {

class WINJACK_PATCHBAY_PLUGINS_API InternalLoader final {
public:
    static std::vector<PluginDescriptor> descriptors();
    static std::unique_ptr<IPluginInstance> create(const std::string& identifier);
};

}