#pragma once

#include <WinJACKNexus/Patchbay/Plugins/Export.hpp>

#include <string>

namespace WinJACKNexus::Patchbay::Plugins {

enum class PluginKind : unsigned char {
    internal,
    vst2,
    vst3
};

struct WINJACK_PATCHBAY_PLUGINS_API PluginDescriptor final {
    PluginKind kind = PluginKind::internal;
    std::string identifier;
    std::string name;
    std::string author;
    std::string version;
    std::string path;
    bool available = false;
    std::string unavailableReason;
};

}