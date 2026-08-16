#include <WinJACKNexus/AdapterService/ServiceLogger.h>

#include <iostream>

namespace wjn::adapter::service
{

void ServiceLogger::info (const juce::String& message) const
{
    write ("INFO", message);
}

void ServiceLogger::error (const juce::String& message) const
{
    write ("ERROR", message);
}

void ServiceLogger::write (const char* prefix, const juce::String& message) const
{
    if (quiet)
        return;

    std::cout << '[' << prefix << "] " << message.toStdString() << '\n';
    std::cout.flush();
}

} // namespace wjn::adapter::service