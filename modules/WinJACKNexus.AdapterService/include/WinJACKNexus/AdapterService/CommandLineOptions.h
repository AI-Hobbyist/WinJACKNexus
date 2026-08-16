#pragma once

#include <juce_core/juce_core.h>

namespace wjn::adapter::service
{

struct CommandLineOptions
{
    bool quiet = false;
    bool aggregate = false;
    bool showHelp = false;
    bool showVersion = false;
    bool valid = true;
    juce::String error;
    juce::File configFile;

    static CommandLineOptions parse (const juce::String& commandLine,
                                      const juce::File& defaultConfigFile);
    static juce::File defaultConfigurationFile();
    static juce::String usage (const juce::String& executableName);
};

} // namespace wjn::adapter::service