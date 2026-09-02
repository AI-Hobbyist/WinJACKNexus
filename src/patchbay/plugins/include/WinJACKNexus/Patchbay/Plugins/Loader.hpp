#pragma once

#include <WinJACKNexus/Patchbay/Plugins/IPluginInstance.hpp>

#include <memory>
#include <string>
#include <vector>

namespace WinJACKNexus::Patchbay::Plugins {

class WINJACK_PATCHBAY_PLUGINS_API Loader final {
public:
    static std::vector<PluginDescriptor> availablePlugins();
    static std::unique_ptr<IPluginInstance> create(const std::string& identifier);
};

}