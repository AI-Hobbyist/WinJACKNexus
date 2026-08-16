#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <juce_gui_basics/juce_gui_basics.h>

#include <WinJACKNexus/AdapterService/ConfigSynchronizer.h>
#include <WinJACKNexus/AdapterService/CommandLineOptions.h>
#include <WinJACKNexus/AdapterService/ServiceInstanceGuard.h>
#include <WinJACKNexus/AdapterService/ServiceLogger.h>
#include <WinJACKNexus/AdapterService/ServiceRuntime.h>
#include <WinJACKNexus/Common/Localization/LocaleManager.h>

namespace wjn::adapter::service
{

class ServiceTrayIcon;
class ServiceProgressWindow;

class ServiceApplication final : public juce::JUCEApplication,
                                 private juce::Timer
{
public:
    ServiceApplication();
    ~ServiceApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;
    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

    void requestQuit() noexcept;
    juce::String statusText() const;
    juce::String localizedText (const char* key, const char* fallback) const;

private:
    struct StartupResult
    {
        bool success = false;
        int returnCode = 0;
        juce::String error;
        SynchronizationResult synchronization;
        ServiceConfig config;
    };

    void timerCallback() override;
    void beginStartup();
    void runStartup();
    void finishStartup();
    void loadLocale();
    void finishInitialiseWithError (int returnCode, const juce::String& message);
    void stopRuntimeSynchronously();
    void installConsoleHandler();
    void uninstallConsoleHandler();

    CommandLineOptions options;
    ServiceLogger logger;
    ServiceInstanceGuard instanceGuard;
    std::unique_ptr<ServiceRuntime> runtime;
    std::unique_ptr<ServiceTrayIcon> trayIcon;
    std::unique_ptr<ServiceProgressWindow> progressWindow;
    wjn::common::LocaleManager localeManager;
    std::thread startupThread;
    std::thread shutdownThread;
    std::mutex startupMutex;
    StartupResult startupResult;
    std::atomic<bool> startupFinished { false };
    std::atomic<double> startupProgress { 0.0 };
    std::atomic<int> startupPhase { 0 };
    std::atomic<bool> quitRequested { false };
    bool startupInProgress = false;
    bool consoleHandlerInstalled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ServiceApplication)
};

} // namespace wjn::adapter::service