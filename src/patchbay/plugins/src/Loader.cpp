#include <WinJACKNexus/Patchbay/Plugins/Loader.hpp>

#include <WinJACKNexus/Patchbay/Plugins/InternalLoader.hpp>

namespace WinJACKNexus::Patchbay::Plugins {

std::vector<PluginDescriptor> Loader::availablePlugins()
{
    return InternalLoader::descriptors();
}

std::unique_ptr<IPluginInstance> Loader::create(const std::string& identifier)
{
    return InternalLoader::create(identifier);
}

}