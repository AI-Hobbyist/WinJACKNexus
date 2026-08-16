#include <WinJACKNexus/AdapterService/CommandLineOptions.h>
#include <WinJACKNexus/AdapterService/ServiceInstanceGuard.h>

#include <cstdlib>
#include <iostream>

namespace
{

using wjn::adapter::service::CommandLineOptions;
using wjn::adapter::service::ServiceInstanceGuard;

void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "CommandLineOptions test failure: " << message << '\n';
        std::exit (1);
    }
}

} // namespace

int main()
{
    const auto defaultFile = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile ("default.json");
    const auto parsed = CommandLineOptions::parse (
        "--quiet --aggregate --config \"config folder/adapter service.json\"", defaultFile);
    require (parsed.valid && parsed.quiet && parsed.aggregate,
             "Quiet mode, aggregate mode and a valid config path must be parsed");
    require (parsed.configFile == juce::File::getCurrentWorkingDirectory()
                                      .getChildFile ("config folder/adapter service.json"),
             "Relative config paths must resolve from the working directory");

    const auto padded = CommandLineOptions::parse (
        "  --config \"config folder/padded.json\"  ", defaultFile);
    require (padded.valid
                 && padded.configFile == juce::File::getCurrentWorkingDirectory()
                                             .getChildFile ("config folder/padded.json"),
             "Leading and trailing command-line whitespace must be ignored");

    const auto equalsParsed = CommandLineOptions::parse ("--config=custom.json", defaultFile);
    require (equalsParsed.valid
                 && equalsParsed.configFile == juce::File::getCurrentWorkingDirectory()
                                                    .getChildFile ("custom.json"),
             "The equals form of --config must be supported");

    const auto defaults = CommandLineOptions::parse ({}, defaultFile);
    require (defaults.valid && defaults.configFile == defaultFile,
             "The default config path must be preserved");

    const auto missingPath = CommandLineOptions::parse ("--config", defaultFile);
    require (! missingPath.valid && missingPath.error.isNotEmpty(),
             "A missing --config path must be rejected");

    const auto unknown = CommandLineOptions::parse ("--unsupported", defaultFile);
    require (! unknown.valid && unknown.error.isNotEmpty(),
             "Unknown options must be rejected");

    const auto usage = CommandLineOptions::usage ("AdapterService.exe");
    require (usage.contains ("--aggregate"),
             "Usage text must document aggregate mode");

    const auto mutexName = "WinJACKNexus.AdapterService.CommandLineOptionsTests";
    ServiceInstanceGuard first;
    ServiceInstanceGuard second;
    require (first.acquire (mutexName), "The first instance must acquire the mutex");
    require (! second.acquire (mutexName), "The second instance must be rejected");

    std::cout << "CommandLineOptions tests passed\n";
    return 0;
}