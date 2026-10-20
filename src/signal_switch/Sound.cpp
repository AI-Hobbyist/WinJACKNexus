/* SPDX-License-Identifier: AGPL-3.0-only */
#include "Sound.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <WinJACKNexus/Common/SoundButtonPlayer.hpp>
#include <Windows.h>
#include <array>
#include <chrono>
#include <future>
#include <mutex>

namespace WinJACKNexus::SignalSwitch
{
namespace DSP = Common::SignalSwitch;
namespace
{
constexpr DWORD floatingPoint = 0x100;
constexpr DWORD decodeOnly = 0x200000;
constexpr DWORD unicodePath = 0x80000000;
constexpr DWORD musicStopBack = 0x80000;
constexpr DWORD mixerEnd = 0x10000;
constexpr DWORD downmix = 0x400000;
constexpr DWORD noRampIn = 0x800000;
constexpr DWORD endedError = 45;

// BASS stays behind a runtime-loaded boundary; no BASS API runs on the RT thread.
struct Bass
{
    QLibrary core{QCoreApplication::applicationDirPath() + "/bass.dll"};
    QLibrary mixer{QCoreApplication::applicationDirPath() + "/bassmix.dll"};
    BOOL(WINAPI *init)(int, DWORD, DWORD, HWND, const void *) {};
    BOOL(WINAPI *setDevice)(DWORD) {};
    BOOL(WINAPI *free)() {};
    int(WINAPI *errorCode)(){};
    DWORD(WINAPI *streamFile)(DWORD, const void *, uint64_t, uint64_t, DWORD) {};
    DWORD(WINAPI *musicFile)(DWORD, const void *, uint64_t, DWORD, DWORD, DWORD) {};
    BOOL(WINAPI *streamFree)(DWORD) {};
    BOOL(WINAPI *musicFree)(DWORD) {};
    DWORD(WINAPI *getData)(DWORD, void *, DWORD) {};
    DWORD(WINAPI *pluginLoad)(const char *, DWORD) {};
    BOOL(WINAPI *pluginFree)(DWORD) {};
    DWORD(WINAPI *mixerCreate)(DWORD, DWORD, DWORD) {};
    BOOL(WINAPI *mixerAdd)(DWORD, DWORD, DWORD) {};
    bool initialized{};
    QString failure;

    Bass()
    {
        if (!core.load() || !mixer.load())
        {
            failure = core.errorString() + "\n" + mixer.errorString();
            return;
        }
#define WJN_BASS_BIND(member, library, symbol)                                                                         \
    member = reinterpret_cast<decltype(member)>(library.resolve(symbol));                                              \
    if (!member)                                                                                                       \
    {                                                                                                                  \
        failure = QStringLiteral("Missing BASS export: ") + symbol;                                                    \
        return;                                                                                                        \
    }
        WJN_BASS_BIND(init, core, "BASS_Init");
        WJN_BASS_BIND(setDevice, core, "BASS_SetDevice");
        WJN_BASS_BIND(free, core, "BASS_Free");
        WJN_BASS_BIND(errorCode, core, "BASS_ErrorGetCode");
        WJN_BASS_BIND(streamFile, core, "BASS_StreamCreateFile");
        WJN_BASS_BIND(musicFile, core, "BASS_MusicLoad");
        WJN_BASS_BIND(streamFree, core, "BASS_StreamFree");
        WJN_BASS_BIND(musicFree, core, "BASS_MusicFree");
        WJN_BASS_BIND(getData, core, "BASS_ChannelGetData");
        WJN_BASS_BIND(pluginLoad, core, "BASS_PluginLoad");
        WJN_BASS_BIND(pluginFree, core, "BASS_PluginFree");
        WJN_BASS_BIND(mixerCreate, mixer, "BASS_Mixer_StreamCreate");
        WJN_BASS_BIND(mixerAdd, mixer, "BASS_Mixer_StreamAddChannel");
#undef WJN_BASS_BIND
        initialized = init(0, 48000, 0, nullptr, nullptr);
        if (!initialized)
        {
            failure = QString("BASS_Init: %1").arg(errorCode());
            return;
        }
        const QDir plugins(QCoreApplication::applicationDirPath() + "/signal_switch/bass_plugins");
        for (const auto &file : plugins.entryList({"*.dll"}, QDir::Files, QDir::Name))
            pluginLoad(reinterpret_cast<const char *>(plugins.absoluteFilePath(file).utf16()), unicodePath);
    }
    ~Bass()
    {
        if (initialized)
        {
            setDevice(0);
            pluginFree(0);
            free();
        }
    }
};

struct Decoded
{
    std::unique_ptr<DSP::SoundClip> clip;
    QString path;
    QString error;
};

Decoded decode(const QString &path, const std::atomic_bool &cancel)
{
    // BASS device selection is thread-local. Serialize decoding jobs and keep all
    // channel creation/destruction on the same worker that selected device 0.
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (cancel.load())
        return {};
    static Bass bass;
    if (!bass.initialized)
        return {{}, path, bass.failure};
    bass.setDevice(0);
    const auto flags = floatingPoint | decodeOnly | unicodePath;
    DWORD source = bass.streamFile(0, path.utf16(), 0, 0, flags);
    const bool music = !source;
    if (music)
        source = bass.musicFile(0, path.utf16(), 0, 0, flags | musicStopBack, 48000);
    if (!source)
        return {{}, path, QString("BASS: %1").arg(bass.errorCode())};
    const DWORD mixer = bass.mixerCreate(48000, 2, floatingPoint | decodeOnly | mixerEnd);
    struct Cleanup
    {
        Bass &bass;
        DWORD mixer, source;
        bool music;
        ~Cleanup()
        {
            if (mixer)
                bass.streamFree(mixer);
            if (music)
                bass.musicFree(source);
            else
                bass.streamFree(source);
        }
    } cleanup{bass, mixer, source, music};
    if (!mixer || !bass.mixerAdd(mixer, source, downmix | noRampIn))
        return {{}, path, QString("BASSmix: %1").arg(bass.errorCode())};
    auto clip = std::make_unique<DSP::SoundClip>();
    std::array<float, 8192> buffer;
    for (;;)
    {
        if (cancel.load())
            return {};
        const DWORD bytes = bass.getData(mixer, buffer.data(), DWORD(sizeof(buffer)));
        if (bytes == DWORD(-1))
        {
            const auto code = bass.errorCode();
            if (code == endedError)
                break;
            return {{}, path, QString("BASS decode: %1").arg(code)};
        }
        if (!bytes)
            break;
        if (bytes > sizeof(buffer) || bytes % (sizeof(float) * 2))
            return {{}, path, "Invalid stereo decode buffer"};
        clip->samples.insert(clip->samples.end(), buffer.begin(), buffer.begin() + bytes / sizeof(float));
    }
    if (clip->samples.empty())
        return {{}, path, "No decoded audio"};
    return {std::move(clip), path, {}};
}
} // namespace

struct Sound::Impl
{
    DSP::SoundButtonPlayer player;
    std::unique_ptr<DSP::SoundClip> clip;
    std::vector<std::unique_ptr<DSP::SoundClip>> retired;
    std::future<Decoded> pending;
    std::atomic_bool cancel{};
    QString path, error;
};

Sound::Sound() : impl_(std::make_unique<Impl>())
{
}
Sound::~Sound()
{
    impl_->cancel = true;
    if (impl_->pending.valid())
        impl_->pending.wait();
}
bool Sound::load(const QString &path, bool restoring)
{
    if (impl_->pending.valid())
        return false;
    if (restoring)
        impl_->path = path;
    impl_->error.clear();
    impl_->pending = std::async(std::launch::async, [path, state = impl_.get()] {
        try
        {
            return decode(path, state->cancel);
        }
        catch (const std::exception &error)
        {
            return Decoded{{}, path, QString::fromUtf8(error.what())};
        }
    });
    return true;
}
bool Sound::poll()
{
    std::erase_if(impl_->retired, [this](const auto &clip) { return !impl_->player.inUse(clip.get()); });
    if (!impl_->pending.valid() || impl_->pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return false;
    auto result = impl_->pending.get();
    impl_->error = result.error;
    if (result.clip)
    {
        impl_->player.publish(result.clip.get());
        if (impl_->clip)
            impl_->retired.push_back(std::move(impl_->clip));
        impl_->clip = std::move(result.clip);
        impl_->path = result.path;
    }
    return true;
}
void Sound::trigger()
{
    if (ready())
        impl_->player.trigger();
}
void Sound::stop()
{
    impl_->player.stop();
}
bool Sound::ready() const
{
    return bool(impl_->clip);
}
bool Sound::loading() const
{
    return impl_->pending.valid();
}
bool Sound::playing() const
{
    return impl_->player.playing();
}
QString Sound::path() const
{
    return impl_->path;
}
QString Sound::error() const
{
    return impl_->error;
}
void Sound::render(float *left, float *right, unsigned frames, double rate) noexcept
{
    impl_->player.render(left, right, frames, rate);
}
} // namespace WinJACKNexus::SignalSwitch
