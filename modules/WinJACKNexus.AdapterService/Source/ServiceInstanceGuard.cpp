#include <WinJACKNexus/AdapterService/ServiceInstanceGuard.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace wjn::adapter::service
{

ServiceInstanceGuard::~ServiceInstanceGuard()
{
#if JUCE_WINDOWS
    if (mutexHandle != nullptr)
        CloseHandle (static_cast<HANDLE> (mutexHandle));
#endif
}

bool ServiceInstanceGuard::acquire (const juce::String& mutexName)
{
#if JUCE_WINDOWS
    mutexHandle = CreateMutexW (nullptr, TRUE, mutexName.toWideCharPointer());
    owner = mutexHandle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;

    if (! owner && mutexHandle != nullptr)
    {
        CloseHandle (static_cast<HANDLE> (mutexHandle));
        mutexHandle = nullptr;
    }
#else
    juce::ignoreUnused (mutexName);
    owner = true;
#endif

    return owner;
}

} // namespace wjn::adapter::service