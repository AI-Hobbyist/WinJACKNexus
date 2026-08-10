#include "SingleInstanceGuard.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace wjn::common
{

SingleInstanceGuard::~SingleInstanceGuard()
{
#if JUCE_WINDOWS
    if (mutexHandle != nullptr)
        CloseHandle (static_cast<HANDLE> (mutexHandle));
#endif
}

bool SingleInstanceGuard::acquire (const juce::String& mutexName, const juce::String& title)
{
    windowTitle = title;

#if JUCE_WINDOWS
    mutexHandle = CreateMutexW (nullptr, TRUE, mutexName.toWideCharPointer());
    owner = mutexHandle != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;

    if (! owner)
    {
        bringExistingWindowToFront (windowTitle);
        if (mutexHandle != nullptr)
        {
            CloseHandle (static_cast<HANDLE> (mutexHandle));
            mutexHandle = nullptr;
        }
    }
#else
    owner = true;
#endif

    return owner;
}

void SingleInstanceGuard::bringExistingWindowToFront (const juce::String& title)
{
#if JUCE_WINDOWS
    if (auto* window = FindWindowW (nullptr, title.toWideCharPointer()))
    {
        ShowWindow (window, SW_RESTORE);
        SetForegroundWindow (window);
    }
#else
    juce::ignoreUnused (title);
#endif
}

} // namespace wjn::common
