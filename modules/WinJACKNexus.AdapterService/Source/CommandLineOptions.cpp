#include <WinJACKNexus/AdapterService/CommandLineOptions.h>

namespace wjn::adapter::service
{
namespace
{

juce::File resolveConfigFile (const juce::String& path)
{
    const juce::File candidate (path);
    if (juce::File::isAbsolutePath (path))
        return candidate;

    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

void setError (CommandLineOptions& options, const juce::String& message)
{
    options.valid = false;
    options.error = message;
}

} // namespace

CommandLineOptions CommandLineOptions::parse (const juce::String& commandLine,
                                               const juce::File& defaultConfigFile)
{
    CommandLineOptions options;
    options.configFile = defaultConfigFile;
    auto tokens = juce::StringArray::fromTokens (commandLine, true);
    tokens.trim();
    tokens.removeEmptyStrings();

    for (int index = 0; index < tokens.size(); ++index)
    {
        const auto token = tokens[index].unquoted();
        if (token == "--quiet")
        {
            options.quiet = true;
        }
        else if (token == "--help" || token == "-h")
        {
            options.showHelp = true;
        }
        else if (token == "--version")
        {
            options.showVersion = true;
        }
        else if (token == "--config")
        {
            if (index + 1 >= tokens.size())
            {
                setError (options, "--config requires a file path");
                break;
            }

            options.configFile = resolveConfigFile (tokens[++index].unquoted());
        }
        else if (token.startsWith ("--config="))
        {
            const auto path = token.fromFirstOccurrenceOf ("=", false, false).unquoted();
            if (path.isEmpty())
            {
                setError (options, "--config requires a file path");
                break;
            }

            options.configFile = resolveConfigFile (path);
        }
        else
        {
            setError (options, "Unknown command-line option: " + token);
            break;
        }
    }

    return options;
}

juce::File CommandLineOptions::defaultConfigurationFile()
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile ("adapter_service.json");
}

juce::String CommandLineOptions::usage (const juce::String& executableName)
{
    return "Usage: " + executableName
         + " [--quiet] [--config <path>] [--help] [--version]";
}

} // namespace wjn::adapter::service