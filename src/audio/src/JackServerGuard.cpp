#include <WinJACKNexus/Audio/JackServerGuard.hpp>

#include <WinJACKNexus/Audio/JackApi.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>

#include <Windows.h>

#include <filesystem>
#include <new>
#include <string>
#include <vector>

namespace WinJACKNexus::Audio {

namespace {

std::wstring quoteArgument(const std::wstring& argument)
{
    std::wstring quoted = L"\"";
    quoted += argument;
    quoted += L"\"";
    return quoted;
}

std::wstring findExecutable(const wchar_t* explicitPath)
{
    std::vector<std::filesystem::path> candidates;
    if (explicitPath != nullptr && *explicitPath != L'\0')
        candidates.emplace_back(explicitPath);
    if (const auto* environmentPath = _wgetenv(L"WJN_JACKD"); environmentPath != nullptr && *environmentPath != L'\0')
        candidates.emplace_back(environmentPath);

    wchar_t searchPath[MAX_PATH] {};
    if (SearchPathW(nullptr, L"jackd.exe", nullptr, static_cast<DWORD>(std::size(searchPath)), searchPath, nullptr) != 0)
        candidates.emplace_back(searchPath);

    const auto moduleDirectory = Common::PathUtils::moduleDirectory();
    candidates.emplace_back(moduleDirectory / L"jackd.exe");
    candidates.emplace_back(moduleDirectory / L".." / L"jackd.exe");

    if (const auto* programFiles = _wgetenv(L"ProgramFiles"); programFiles != nullptr && *programFiles != L'\0')
        candidates.emplace_back(std::filesystem::path(programFiles) / L"JACK2" / L"jackd.exe");
    if (const auto* programFiles = _wgetenv(L"ProgramW6432"); programFiles != nullptr && *programFiles != L'\0')
        candidates.emplace_back(std::filesystem::path(programFiles) / L"JACK2" / L"jackd.exe");

    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return std::filesystem::absolute(candidate, error).wstring();
    }
    return {};
}

bool serverResponds() noexcept
{
    auto& api = JackApi::instance();
    if (!api.load())
        return false;

    jack_status_t status = JackFailure;
    const auto options = static_cast<jack_options_t>(JackNoStartServer);
    auto* client = api.clientOpen("WJNServerProbe", options, &status);
    if (client == nullptr)
        return false;
    api.clientClose(client);
    return true;
}

}

struct JackServerGuard::Impl final {
    std::wstring path;
    HANDLE process = nullptr;
};

JackServerGuard::JackServerGuard() noexcept
    : impl_(new (std::nothrow) Impl {})
{
}

JackServerGuard::~JackServerGuard() noexcept
{
    stop();
    delete impl_;
}

JackServerGuard::JackServerGuard(JackServerGuard&& other) noexcept
    : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

JackServerGuard& JackServerGuard::operator=(JackServerGuard&& other) noexcept
{
    if (this == &other)
        return *this;
    stop();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool JackServerGuard::locate(const wchar_t* explicitPath) noexcept
{
    if (impl_ == nullptr)
        impl_ = new (std::nothrow) Impl {};
    if (impl_ == nullptr)
        return false;
    impl_->path = findExecutable(explicitPath);
    return !impl_->path.empty();
}

bool JackServerGuard::start(const JackServerOptions& options) noexcept
{
    if (impl_ == nullptr)
        return false;
    if (isRunning())
        return true;
    if (!locate(options.jackdPath))
        return false;

    const auto backend = options.backend != nullptr && *options.backend != L'\0' ? options.backend : L"dummy";
    std::wstring commandLine = quoteArgument(impl_->path);
    if (options.enableRealtime)
        commandLine += L" -R";
    commandLine += L" -d ";
    commandLine += backend;
    commandLine += L" -r ";
    commandLine += std::to_wstring(options.sampleRate);
    commandLine += L" -p ";
    commandLine += std::to_wstring(options.periodFrames);

    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startupInfo {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION processInfo {};
    if (!CreateProcessW(nullptr,
                        commandBuffer.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo)) {
        WJN_LOG_ERROR("Unable to start jackd.exe, error=%lu", GetLastError());
        return false;
    }
    CloseHandle(processInfo.hThread);
    impl_->process = processInfo.hProcess;
    return true;
}

bool JackServerGuard::waitUntilReady(std::uint32_t timeoutMilliseconds) noexcept
{
    const auto startTime = GetTickCount64();
    do {
        if (serverResponds())
            return true;
        if (impl_ != nullptr && impl_->process != nullptr && WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0)
            return false;
        Sleep(10);
    } while (GetTickCount64() - startTime < timeoutMilliseconds);
    return serverResponds();
}

bool JackServerGuard::stop() noexcept
{
    if (impl_ == nullptr || impl_->process == nullptr)
        return true;

    bool success = true;
    if (WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
        success = TerminateProcess(impl_->process, 0) != FALSE;
        if (success)
            WaitForSingleObject(impl_->process, 2000);
    }
    CloseHandle(impl_->process);
    impl_->process = nullptr;
    return success;
}

bool JackServerGuard::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->process != nullptr
        && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
}

const wchar_t* JackServerGuard::executablePath() const noexcept
{
    return impl_ != nullptr && !impl_->path.empty() ? impl_->path.c_str() : nullptr;
}

}