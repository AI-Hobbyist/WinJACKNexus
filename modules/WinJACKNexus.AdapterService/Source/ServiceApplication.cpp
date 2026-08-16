#include "ServiceApplication.h"

#include "ServiceProgressWindow.h"
#include "ServiceTrayIcon.h"

#include <WinJACKNexus/AdapterService/ConfigSynchronizer.h>

#include <juce_core/juce_core.h>

#include <cstdio>
#include <exception>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace wjn::adapter::service
{
namespace
{

constexpr auto serviceMutexName = "WinJACK_Nexus_AdapterService_Lock";

#if JUCE_WINDOWS
std::atomic<ServiceApplication*> consoleApplication { nullptr };

BOOL WINAPI consoleControlHandler (DWORD controlType)
{
    switch (controlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (auto* application = consoleApplication.load (std::memory_order_acquire))
                application->requestQuit();
            return TRUE;

        default:
            return FALSE;
    }
}

void ensureConsole()
{
    if (GetConsoleWindow() != nullptr || AllocConsole() == 0)
        return;

    FILE* stream = nullptr;
    freopen_s (&stream, "CONOUT$", "w", stdout);
    freopen_s (&stream, "CONOUT$", "w", stderr);
    freopen_s (&stream, "CONIN$", "r", stdin);
}
#endif

} // namespace

ServiceApplication::~ServiceApplication() = default;

ServiceApplication::ServiceApplication() = default;

const juce::String ServiceApplication::getApplicationName()
{
    return "WinJACKNexus.AdapterService";
}

const juce::String ServiceApplication::getApplicationVersion()
{
    return JUCE_APPLICATION_VERSION_STRING;
}

bool ServiceApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void ServiceApplication::initialise (const juce::String& commandLine)
{
    options = CommandLineOptions::parse (commandLine,
                                         CommandLineOptions::defaultConfigurationFile());
    logger.setQuiet (options.quiet);

#if JUCE_WINDOWS
    if (options.quiet)
        FreeConsole();
    else
        ensureConsole();
#endif

    if (! options.valid)
    {
        finishInitialiseWithError (2, options.error);
        return;
    }

    loadLocale();

    if (options.showHelp)
    {
        logger.info (CommandLineOptions::usage (getApplicationName()));
        setApplicationReturnValue (0);
        quit();
        return;
    }

    if (options.showVersion)
    {
        logger.info (getApplicationVersion());
        setApplicationReturnValue (0);
        quit();
        return;
    }

    if (! instanceGuard.acquire (serviceMutexName))
    {
        finishInitialiseWithError (3, "Another AdapterService instance is already running");
        return;
    }

    beginStartup();
}

void ServiceApplication::shutdown()
{
    stopTimer();
    uninstallConsoleHandler();
    if (startupThread.joinable())
        startupThread.join();
    if (shutdownThread.joinable())
        shutdownThread.join();
    stopRuntimeSynchronously();
    trayIcon.reset();
    runtime.reset();
    progressWindow.reset();
}

void ServiceApplication::systemRequestedQuit()
{
    requestQuit();
}

void ServiceApplication::anotherInstanceStarted (const juce::String& /*commandLine*/)
{
    requestQuit();
}

void ServiceApplication::requestQuit() noexcept
{
    quitRequested.store (true, std::memory_order_release);
}

juce::String ServiceApplication::statusText() const
{
    if (runtime == nullptr)
        return localizedText ("adapterService.status.stopped", "已停止");

    if (runtime->isRunning())
        return localizedText ("adapterService.status.running", "运行中，活动客户端：")
             + juce::String (static_cast<int> (runtime->activeClientCount()));

    switch (runtime->state())
    {
        case ServiceRuntimeState::starting:
            return localizedText ("adapterService.status.starting", "正在启动");
        case ServiceRuntimeState::stopping:
            return localizedText ("adapterService.status.stopping", "正在卸载");
        case ServiceRuntimeState::stopped:
            return localizedText ("adapterService.status.stopped", "已停止");
        case ServiceRuntimeState::idle:
            return localizedText ("adapterService.status.idle", "空闲");
        case ServiceRuntimeState::running:  break;
    }

    return localizedText ("adapterService.status.unknown", "未知状态");
}

juce::String ServiceApplication::localizedText (const char* key, const char* fallback) const
{
    return localeManager.text (key, juce::String::fromUTF8 (fallback));
}

void ServiceApplication::timerCallback()
{
    if (startupInProgress)
    {
        if (progressWindow != nullptr)
        {
            const auto phase = startupPhase.load (std::memory_order_acquire);
            progressWindow->setMessage (phase == 0
                                            ? localizedText ("adapterService.progress.scanning",
                                                             "正在扫描音频和 MIDI 设备...")
                                            : localizedText ("adapterService.progress.readingConfig",
                                                             "正在读取服务配置..."));
            progressWindow->setProgress (startupProgress.load (std::memory_order_acquire));

            if (quitRequested.load (std::memory_order_acquire))
                progressWindow->setMessage (localizedText ("adapterService.progress.cancelLoading",
                                                           "正在停止加载..."));
        }

        if (! startupFinished.load (std::memory_order_acquire))
            return;

        finishStartup();
        if (startupInProgress)
            return;
    }

    if (runtime == nullptr)
        return;

    const auto shouldQuit = quitRequested.load (std::memory_order_acquire);
    if (shouldQuit)
    {
        if (progressWindow == nullptr)
            progressWindow = std::make_unique<ServiceProgressWindow> (
                localizedText ("adapterService.progress.title", "WinJACKNexus 适配器服务"),
                localizedText ("adapterService.progress.unloading",
                               "正在卸载 JACK 客户端..."));

        progressWindow->setMessage (localizedText ("adapterService.progress.unloading",
                                                   "正在卸载 JACK 客户端..."));
        progressWindow->setProgress (runtime->stopProgress());

        if (! shutdownThread.joinable())
        {
            runtime->requestStop();
            try
            {
                shutdownThread = std::thread ([this]
                {
                    while (! runtime->isStopped())
                        runtime->tick();
                });
            }
            catch (const std::exception& exception)
            {
                logger.error ("无法创建卸载线程："
                              + juce::String::fromUTF8 (exception.what()));
                stopRuntimeSynchronously();
            }
        }

        if (progressWindow != nullptr)
            progressWindow->setProgress (runtime->stopProgress());

        if (! runtime->isStopped())
            return;

        if (shutdownThread.joinable())
            shutdownThread.join();

        if (progressWindow != nullptr)
        {
            progressWindow->setMessage (localizedText ("adapterService.progress.unloaded",
                                                       "卸载完成"));
            progressWindow->setProgress (1.0);
            progressWindow.reset();
        }

        stopTimer();
        quit();
        return;
    }

    runtime->tick();

    if (runtime->state() == ServiceRuntimeState::starting)
    {
        if (progressWindow != nullptr)
        {
            progressWindow->setMessage (localizedText ("adapterService.progress.startingClients",
                                                       "正在启动 JACK 客户端..."));
            progressWindow->setProgress (0.55 + runtime->startProgress() * 0.45);
        }
        return;
    }

    if (runtime->isRunning() && progressWindow != nullptr)
    {
        progressWindow->setProgress (1.0);
        progressWindow.reset();
    }

    if (runtime->isStopped())
    {
        stopTimer();
        quit();
    }
}

void ServiceApplication::finishInitialiseWithError (int returnCode,
                                                    const juce::String& message)
{
    logger.error (message);
    progressWindow.reset();
    setApplicationReturnValue (returnCode);
    quit();
}

void ServiceApplication::beginStartup()
{
    progressWindow = std::make_unique<ServiceProgressWindow> (
        localizedText ("adapterService.progress.title", "WinJACKNexus 适配器服务"),
        localizedText ("adapterService.progress.scanning", "正在扫描音频和 MIDI 设备..."));
    progressWindow->setProgress (0.0);
    startupFinished.store (false, std::memory_order_release);
    startupProgress.store (0.0, std::memory_order_release);
    startupPhase.store (0, std::memory_order_release);
    startupInProgress = true;

    try
    {
        startupThread = std::thread ([this] { runStartup(); });
    }
    catch (const std::exception& exception)
    {
        startupInProgress = false;
        finishInitialiseWithError (4, "无法创建启动线程："
                                       + juce::String::fromUTF8 (exception.what()));
        return;
    }

    startTimer (50);
}

void ServiceApplication::runStartup()
{
    StartupResult result;

    try
    {
        if (! ConfigSynchronizer::synchronizeFile (options.configFile,
                                                   result.error,
                                                   &result.synchronization))
        {
            result.returnCode = 4;
        }
        else
        {
            startupProgress.store (0.5, std::memory_order_release);
            startupPhase.store (1, std::memory_order_release);
            result.config = ServiceConfig::loadFromFile (options.configFile, result.error);
            if (! result.config.isValid())
            {
                result.error = "配置重新读取失败：" + result.error;
                result.returnCode = 5;
            }
            else
            {
                result.success = true;
                result.returnCode = 0;
            }
        }
    }
    catch (const std::exception& exception)
    {
        result.error = "启动阶段发生异常：" + juce::String::fromUTF8 (exception.what());
        result.returnCode = 6;
    }
    catch (...)
    {
        result.error = "启动阶段发生未知异常";
        result.returnCode = 6;
    }

    {
        const std::scoped_lock lock (startupMutex);
        startupResult = std::move (result);
    }
    startupFinished.store (true, std::memory_order_release);
}

void ServiceApplication::finishStartup()
{
    startupThread.join();
    startupInProgress = false;

    StartupResult result;
    {
        const std::scoped_lock lock (startupMutex);
        result = std::move (startupResult);
    }

    if (quitRequested.load (std::memory_order_acquire))
    {
        progressWindow.reset();
        setApplicationReturnValue (0);
        stopTimer();
        quit();
        return;
    }

    if (! result.success)
    {
        finishInitialiseWithError (result.returnCode == 0 ? 4 : result.returnCode,
                                   "配置同步失败：" + result.error);
        return;
    }

    logger.info ("Configuration synchronized: added=" + juce::String (result.synchronization.added)
                 + ", removed=" + juce::String (result.synchronization.removed));

    runtime = std::make_unique<ServiceRuntime> (std::move (result.config));
    if (! runtime->start())
    {
        finishInitialiseWithError (6, "ServiceRuntime 启动失败");
        return;
    }

    if (options.quiet)
        trayIcon = std::make_unique<ServiceTrayIcon> (*this);
    else
        installConsoleHandler();

    startupProgress.store (0.55, std::memory_order_release);
    if (runtime->isRunning())
    {
        progressWindow->setProgress (1.0);
        progressWindow.reset();
    }

    logger.info (options.quiet ? "AdapterService started in quiet tray mode"
                              : "AdapterService started in console mode");
}

void ServiceApplication::loadLocale()
{
    const auto localeDirectory = juce::File::getSpecialLocation (
        juce::File::currentExecutableFile).getParentDirectory().getChildFile ("locales");
    juce::String error;
    if (! localeManager.load (localeDirectory.getChildFile ("zh-CN.lang"),
                              localeDirectory.getChildFile ("AdapterService")
                                  .getChildFile ("zh-CN.lang"),
                              error))
    {
        logger.error ("语言文件加载失败：" + error);
    }
}

void ServiceApplication::stopRuntimeSynchronously()
{
    if (runtime == nullptr)
        return;

    runtime->requestStop();
    while (! runtime->isStopped())
        runtime->tick();
}

void ServiceApplication::installConsoleHandler()
{
#if JUCE_WINDOWS
    consoleApplication.store (this, std::memory_order_release);
    consoleHandlerInstalled = SetConsoleCtrlHandler (consoleControlHandler, TRUE) != FALSE;
#endif
}

void ServiceApplication::uninstallConsoleHandler()
{
#if JUCE_WINDOWS
    if (! consoleHandlerInstalled)
        return;

    consoleApplication.store (nullptr, std::memory_order_release);
    SetConsoleCtrlHandler (consoleControlHandler, FALSE);
    consoleHandlerInstalled = false;
#endif
}

} // namespace wjn::adapter::service