// SPDX-License-Identifier: AGPL-3.0-only
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <WinJACKNexus/Common/SynthResourceBudget.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fluidsynth.h>
#include <mutex>
#include <set>
#include <sfizz.h>
#include <stdexcept>
#include <thread>
namespace WinJACKNexus::Common::MidiTools
{
namespace
{
bool gain(double d)
{
    return std::isfinite(d) && d >= -60 && d <= 12;
}
bool sameResources(const SynthSettings &a, const SynthSettings &b)
{
    if (a.resources.size() != b.resources.size())
    {
        return false;
    }
    for (unsigned i = 0; i < a.resources.size(); ++i)
    {
        const auto &x = a.resources[i];
        const auto &y = b.resources[i];
        if (x.id != y.id || x.path != y.path || x.format != y.format || x.msb != y.msb ||
            x.lsb != y.lsb || x.program != y.program)
        {
            return false;
        }
    }
    return true;
}
bool integer(const QJsonValue &v, int low, int high, int &n)
{
    if (!v.isDouble())
    {
        return false;
    }
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < low || d > high || d != std::floor(d))
    {
        return false;
    }
    n = int(d);
    return true;
}
bool fields(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    if (o.size() != qsizetype(keys.size()))
    {
        return false;
    }
    for (auto k : keys)
    {
        if (!o.contains(k))
        {
            return false;
        }
    }
    return true;
}
} // namespace
bool validSettings(const SynthSettings &s) noexcept
{
    if (s.clientName.empty() || s.clientName.size() > 63 ||
        s.clientName.find(':') != std::string::npos || s.resources.size() > maxResources ||
        !gain(s.masterDb))
    {
        return false;
    }
    for (unsigned char c : s.clientName)
    {
        if (c < 32 || c == 127)
        {
            return false;
        }
    }
    bool preferred = s.preferred.empty();
    for (unsigned i = 0; i < s.resources.size(); ++i)
    {
        const auto &r = s.resources[i];
        if (r.id.empty() || r.id.size() > 63 || r.path.empty() || r.path.size() > 32768 ||
            (r.format != "sf2" && r.format != "sfz") || r.msb > 127 || r.lsb > 127 ||
            r.program > 127)
        {
            return false;
        }
        if (r.id == s.preferred)
        {
            preferred = true;
        }
        for (unsigned j = 0; j < i; ++j)
        {
            if (r.id == s.resources[j].id || r.path == s.resources[j].path)
            {
                return false;
            }
        }
    }
    for (const auto &c : s.channels)
    {
        if (c.msb > 127 || c.lsb > 127 || c.program > 127 || !gain(c.gainDb))
        {
            return false;
        }
    }
    return preferred;
}
std::string settingsJson(const SynthSettings &s)
{
    QJsonArray resources, channels;
    for (const auto &r : s.resources)
    {
        resources.append(QJsonObject{{"id", QString::fromStdString(r.id)},
                                     {"path", QString::fromStdString(r.path)},
                                     {"format", QString::fromStdString(r.format)},
                                     {"msb", r.msb},
                                     {"lsb", r.lsb},
                                     {"program", r.program}});
    }
    for (const auto &c : s.channels)
    {
        channels.append(QJsonObject{{"msb", c.msb},
                                    {"lsb", c.lsb},
                                    {"program", c.program},
                                    {"gainDb", c.gainDb},
                                    {"mute", c.mute},
                                    {"solo", c.solo}});
    }
    return QJsonDocument(
               QJsonObject{{"version", 1},
                           {"clientName", QString::fromStdString(s.clientName)},
                           {"preferred", QString::fromStdString(s.preferred)},
                           {"resources", resources},
                           {"channels", channels},
                           {"master", QJsonObject{{"gainDb", s.masterDb}, {"mute", s.masterMute}}}})
        .toJson(QJsonDocument::Compact)
        .toStdString();
}
bool parseSettings(const char *json, SynthSettings &output)
{
    if (!json || strnlen_s(json, 1048577) > 1048576)
    {
        return false;
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    utf8.decode(QByteArray(json));
    if (utf8.hasError())
    {
        return false;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }
    const auto o = doc.object();
    int n;
    if (!fields(o, {"version", "clientName", "preferred", "resources", "channels", "master"}) ||
        !integer(o["version"], 1, 1, n) || !o["clientName"].isString() ||
        !o["preferred"].isString() || !o["resources"].isArray() || !o["channels"].isArray() ||
        !o["master"].isObject())
    {
        return false;
    }
    SynthSettings s;
    s.clientName = o["clientName"].toString().toStdString();
    s.preferred = o["preferred"].toString().toStdString();
    const auto resources = o["resources"].toArray(), channels = o["channels"].toArray();
    if (resources.size() > 32 || channels.size() != 16)
    {
        return false;
    }
    for (const auto &value : resources)
    {
        if (!value.isObject())
        {
            return false;
        }
        const auto r = value.toObject();
        if (!fields(r, {"id", "path", "format", "msb", "lsb", "program"}) || !r["id"].isString() ||
            !r["path"].isString() || !r["format"].isString())
        {
            return false;
        }
        ResourceSpec spec;
        spec.id = r["id"].toString().toStdString();
        spec.path = r["path"].toString().toStdString();
        spec.format = r["format"].toString().toStdString();
        if (!integer(r["msb"], 0, 127, n))
        {
            return false;
        }
        spec.msb = uint8_t(n);
        if (!integer(r["lsb"], 0, 127, n))
        {
            return false;
        }
        spec.lsb = uint8_t(n);
        if (!integer(r["program"], 0, 127, n))
        {
            return false;
        }
        spec.program = uint8_t(n);
        s.resources.push_back(std::move(spec));
    }
    for (unsigned i = 0; i < 16; ++i)
    {
        if (!channels[i].isObject())
        {
            return false;
        }
        const auto c = channels[i].toObject();
        if (!fields(c, {"msb", "lsb", "program", "gainDb", "mute", "solo"}) ||
            !c["gainDb"].isDouble() || !c["mute"].isBool() || !c["solo"].isBool())
        {
            return false;
        }
        auto &dst = s.channels[i];
        if (!integer(c["msb"], 0, 127, n))
        {
            return false;
        }
        dst.msb = uint8_t(n);
        if (!integer(c["lsb"], 0, 127, n))
        {
            return false;
        }
        dst.lsb = uint8_t(n);
        if (!integer(c["program"], 0, 127, n))
        {
            return false;
        }
        dst.program = uint8_t(n);
        dst.gainDb = c["gainDb"].toDouble();
        dst.mute = c["mute"].toBool();
        dst.solo = c["solo"].toBool();
    }
    const auto m = o["master"].toObject();
    if (!fields(m, {"gainDb", "mute"}) || !m["gainDb"].isDouble() || !m["mute"].isBool())
    {
        return false;
    }
    s.masterDb = m["gainDb"].toDouble();
    s.masterMute = m["mute"].toBool();
    if (!validSettings(s))
    {
        return false;
    }
    output = std::move(s);
    return true;
}
namespace
{
using Fluid = std::unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)>;
using FluidSettings = std::unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)>;
struct SfzDeleter
{
    void operator()(sfizz_synth_t *synth) const noexcept
    {
        sfizz_free(synth);
    }
};
using Sfz = std::unique_ptr<sfizz_synth_t, SfzDeleter>;
struct Preset
{
    int bank, program;
    std::string name;
};
struct Resource
{
    ResourceInfo info;
    FluidSettings settings{nullptr, delete_fluid_settings};
    Fluid fluid{nullptr, delete_fluid_synth};
    std::array<Sfz, 16> sfz{};
    std::vector<Preset> presets;
    int font = -1;
    uint64_t bytes{};
    bool used = false, fading = false;
    float fade = 1.f;
    bool matches(unsigned channel, unsigned msb, unsigned lsb, unsigned program) const noexcept
    {
        if (info.state != ResourceState::ready)
        {
            return false;
        }
        if (fluid)
        {
            const int bank = channel == 9 && msb == 0 && lsb == 0 ? 128 : int(msb * 128 + lsb);
            for (const auto &p : presets)
            {
                if (p.bank == bank && p.program == int(program))
                {
                    return true;
                }
            }
            return false;
        }
        const auto &s = info.spec;
        return msb == s.msb && lsb == s.lsb && program == s.program;
    }
    const char *presetName(unsigned c, unsigned msb, unsigned lsb, unsigned program) const noexcept
    {
        if (fluid)
        {
            const int bank = c == 9 && !msb && !lsb ? 128 : int(msb * 128 + lsb);
            for (const auto &p : presets)
            {
                if (p.bank == bank && p.program == int(program))
                {
                    return p.name.c_str();
                }
            }
        }
        return info.presets.empty() ? "" : info.presets.front().c_str();
    }
    void allOff(unsigned c) noexcept
    {
        if (fluid)
        {
            fluid_synth_all_sounds_off(fluid.get(), c);
        }
        else if (sfz[c])
        {
            sfizz_send_cc(sfz[c].get(), 0, 64, 0);
            sfizz_send_cc(sfz[c].get(), 0, 120, 0);
        }
    }
    void send(unsigned c, std::span<const uint8_t> b) noexcept
    {
        const unsigned type = b[0] >> 4;
        used = true;
        if (fluid)
        {
            switch (type)
            {
            case 8:
                fluid_synth_noteoff(fluid.get(), c, b[1]);
                break;
            case 9:
                fluid_synth_noteon(fluid.get(), c, b[1], b[2]);
                break;
            case 10:
                fluid_synth_key_pressure(fluid.get(), c, b[1], b[2]);
                break;
            case 11:
                fluid_synth_cc(fluid.get(), c, b[1], b[2]);
                break;
            case 13:
                fluid_synth_channel_pressure(fluid.get(), c, b[1]);
                break;
            case 14:
                fluid_synth_pitch_bend(fluid.get(), c, b[1] + 128 * b[2]);
                break;
            default:
                break;
            }
        }
        else if (sfz[c])
        {
            auto *s = sfz[c].get();
            switch (type)
            {
            case 8:
                sfizz_send_note_off(s, 0, b[1], b[2]);
                break;
            case 9:
                if (b[2])
                {
                    sfizz_send_note_on(s, 0, b[1], b[2]);
                }
                else
                {
                    sfizz_send_note_off(s, 0, b[1], 0);
                }
                break;
            case 10:
                sfizz_send_poly_aftertouch(s, 0, b[1], b[2]);
                break;
            case 11:
                sfizz_send_cc(s, 0, b[1], b[2]);
                break;
            case 13:
                sfizz_send_channel_aftertouch(s, 0, b[1]);
                break;
            case 14:
                sfizz_send_pitch_wheel(s, 0, int(b[1] + 128 * b[2]) - 8192);
                break;
            default:
                break;
            }
        }
    }
};
struct Bank
{
    std::vector<std::unique_ptr<Resource>> resources;
    double rate{};
    uint64_t revision{}, bytes{};
};
struct Controls
{
    std::array<ChannelSettings, 16> channels;
    double masterDb{};
    bool mute{};
    int preferred = -1;
    uint64_t revision{};
};
struct Packet
{
    Bank *bank{};
    Controls controls;
};
void copyText(auto &dst, const char *src) noexcept
{
    const auto n = (std::min)(dst.size() - 1, std::strlen(src));
    std::memcpy(dst.data(), src, n);
    dst[n] = 0;
}
} // namespace
struct SynthCore::Impl
{
    std::atomic<bool> running{true}, busy{true};
    std::atomic<double> requiredRate{48000};
    std::atomic<double> completedRate{};
    std::atomic<uint64_t> bytes{};
    mutable std::mutex mutex;
    SynthSettings requested;
    uint64_t requestedBudget = configuredPreparedSampleBytes();
    uint64_t preparationBudget = requestedBudget; // Loader worker only.
    bool preserve = false;
    uint64_t requestRevision = 1;
    std::atomic<uint64_t> wantedRevision{1};
    std::vector<ResourceInfo> information;
    std::unique_ptr<sfizz_synth_t, SfzDeleter> accountingSynth;
    std::thread worker;
    LockFreeSpscQueue<Packet, 2> incoming;
    LockFreeSpscQueue<Bank *, 8> retired;
    LockFreeSpscQueue<SynthStatus, 4> statuses;
    std::array<Bank *, 4> banks{};
    Bank *current{};
    Packet pending{};
    bool hasPending{};
    Controls controls{};
    SynthStatus status{};
    struct Generation
    {
        Bank *bank{};
        int resource = -1;
        uint64_t age{};
    };
    std::array<std::array<Generation, 4>, 16> generations{};
    struct Note
    {
        uint8_t key{}, generation{};
        bool pressed{};
        uint64_t age{};
    };
    std::array<std::array<Note, 64>, 16> notes{};
    std::array<unsigned, 16> noteCount{};
    std::array<int, 16> selected{};
    std::array<uint8_t, 16> msb{}, lsb{}, program{}, pendingMsb{}, pendingLsb{};
    std::array<std::array<uint8_t, 128>, 16> cc{};
    std::array<int, 16> pitch{};
    uint64_t clock{};
    float audio[32][256]{}, scratch[32][256]{};
    std::array<float, 16> channelGain{};
    float masterGain = 1;
    explicit Impl(double rate)
    {
        requiredRate = rate;
        selected.fill(-1);
        pitch.fill(8192);
        for (auto &c : cc)
        {
            c[7] = 100;
            c[11] = 127;
            c[43] = 127;
            c[8] = c[10] = 64;
            std::fill(c.begin() + 70, c.begin() + 80, 64);
            std::fill(c.begin() + 98, c.begin() + 102, 127);
        }
        worker = std::thread([this] { loadLoop(); });
    }
    ~Impl()
    {
        running = false;
        if (worker.joinable())
        {
            worker.join();
        }
        Packet p;
        while (incoming.try_pop(p))
        {
            deleteBank(p.bank);
        }
        if (hasPending)
        {
            deleteBank(pending.bank);
        }
        for (auto *bank : banks)
        {
            deleteBank(bank);
        }
        Bank *b;
        while (retired.try_pop(b))
        {
            deleteBank(b);
        }
    }
    void deleteBank(Bank *b)
    {
        if (b)
        {
            bytes.fetch_sub(b->bytes);
            delete b;
        }
    }
    bool reserve(uint64_t n)
    {
        const auto used = bytes.load();
        if (n > preparationBudget || used > preparationBudget - n)
        {
            return false;
        }
        bytes.fetch_add(n);
        return true;
    }
    std::unique_ptr<Resource> prepare(const ResourceSpec &spec, double rate, Bank &bank)
    {
        auto r = std::make_unique<Resource>();
        r->info.spec = spec;
        r->info.state = ResourceState::failed;
        try
        {
            const auto path = std::filesystem::u8path(spec.path);
            if (!std::filesystem::is_regular_file(path))
            {
                r->info.state = ResourceState::missing;
                r->info.detail = "file missing";
                return r;
            }
            const uint64_t size = std::filesystem::file_size(path);
            if (spec.format == "sf2")
            {
                // Conservative SF2 prepared-sample bound (including 24-bit planes).
                const uint64_t budget =
                    size > preparationBudget / sf2PreparedExpansion
                        ? preparationBudget + 1
                        : size * sf2PreparedExpansion;
                if (!reserve(budget))
                {
                    r->info.state = ResourceState::budget;
                    r->info.detail = "SF2 file: " + std::to_string(size) +
                                     " bytes; sample budget: " + std::to_string(preparationBudget) +
                                     " bytes; already reserved: " + std::to_string(bytes.load()) +
                                     " bytes (SF2 reserves file size x8)";
                    return r;
                }
                bank.bytes += budget;
                r->bytes = budget;
                r->settings.reset(new_fluid_settings());
                if (!r->settings)
                {
                    throw std::runtime_error("FluidSynth settings allocation");
                }
                for (const char *key :
                     {"synth.audio-groups", "synth.audio-channels", "synth.midi-channels"})
                {
                    fluid_settings_setint(r->settings.get(), key, 16);
                }
                for (const char *key : {"synth.reverb.active", "synth.chorus.active",
                                        "synth.threadsafe-api", "synth.dynamic-sample-loading"})
                {
                    fluid_settings_setint(r->settings.get(), key, 0);
                }
                fluid_settings_setint(r->settings.get(), "synth.polyphony", 1024);
                fluid_settings_setint(r->settings.get(), "synth.cpu-cores", 1);
                fluid_settings_setnum(r->settings.get(), "synth.sample-rate", rate);
                fluid_settings_setstr(r->settings.get(), "synth.midi-bank-select", "mma");
                r->fluid.reset(new_fluid_synth(r->settings.get()));
                if (!r->fluid)
                {
                    throw std::runtime_error("FluidSynth allocation");
                }
                r->font = fluid_synth_sfload(r->fluid.get(), spec.path.c_str(), 0);
                if (r->font < 0)
                {
                    throw std::runtime_error("SF2 parse/load failed");
                }
                auto *font = fluid_synth_get_sfont_by_id(r->fluid.get(), r->font);
                fluid_sfont_iteration_start(font);
                while (auto *p = fluid_sfont_iteration_next(font))
                {
                    if (r->presets.size() >= 16384)
                    {
                        throw std::runtime_error("preset index budget exceeded");
                    }
                    r->presets.push_back({fluid_preset_get_banknum(p), fluid_preset_get_num(p),
                                          fluid_preset_get_name(p)});
                    r->info.presets.push_back(r->presets.back().name);
                }
                if (r->presets.empty())
                {
                    throw std::runtime_error("SF2 has no presets");
                }
            }
            else
            {
                if (size > 1024 * 1024)
                {
                    throw std::runtime_error("SFZ text budget exceeded");
                }
                uint64_t perInstance = 0;
                if (!accountingSynth)
                {
                    accountingSynth.reset(sfizz_create_synth());
                    if (!accountingSynth)
                    {
                        throw std::runtime_error("sfizz accounting allocation");
                    }
                    sfizz_set_num_voices(accountingSynth.get(), 1);
                    sfizz_set_samples_per_block(accountingSynth.get(), 1);
                    sfizz_set_preload_size(accountingSynth.get(), 1);
                }
                for (unsigned c = 0; c < 16; ++c)
                {
                    if (!running || wantedRevision.load() != bank.revision)
                    {
                        throw std::runtime_error("loading cancelled");
                    }
                    // A worker-owned empty synth exposes the global upstream counter,
                    // allowing a before/after increment even for the first instance.
                    const uint64_t beforeBytes =
                        uint64_t((std::max)(0, sfizz_get_num_bytes(accountingSynth.get())));
                    r->sfz[c].reset(sfizz_create_synth());
                    if (!r->sfz[c])
                    {
                        throw std::runtime_error("sfizz allocation");
                    }
                    auto *s = r->sfz[c].get();
                    sfizz_set_sample_rate(s, float(rate));
                    sfizz_set_samples_per_block(s, 256);
                    sfizz_set_num_voices(s, 64);
                    sfizz_set_preload_size(s, 1048576);
                    sfizz_disable_freewheeling(s);
                    if (!sfizz_load_file(s, spec.path.c_str()) || sfizz_get_num_regions(s) == 0 ||
                        sfizz_get_num_preloaded_samples(s) == 0)
                    {
                        throw std::runtime_error("SFZ/sample parse/load failed");
                    }
                    const uint64_t totalBytes = uint64_t((std::max)(0, sfizz_get_num_bytes(s)));
                    const uint64_t measured =
                        totalBytes > beforeBytes ? totalBytes - beforeBytes : 0;
                    if (c == 0)
                    {
                        perInstance = measured;
                        const uint64_t budget = perInstance * 16;
                        if (!reserve(budget))
                        {
                            r->info.state = ResourceState::budget;
                            throw std::runtime_error("sample budget exceeded");
                        }
                        r->bytes = budget;
                        bank.bytes += budget;
                    }
                    else if (measured > perInstance)
                    {
                        const uint64_t additional = measured - perInstance;
                        if (!reserve(additional))
                        {
                            r->info.state = ResourceState::budget;
                            throw std::runtime_error("sample budget exceeded");
                        }
                        r->bytes += additional;
                        bank.bytes += additional;
                    }
                }
                r->info.presets.push_back(
                    path.filename().u8string().empty()
                        ? spec.id
                        : QString::fromStdWString(path.filename().wstring()).toStdString());
            }
            r->info.state = ResourceState::ready;
        }
        catch (const std::exception &e)
        {
            r->info.detail = e.what();
            r->fluid.reset();
            for (auto &s : r->sfz)
            {
                s.reset();
            }
            if (r->bytes)
            {
                bank.bytes -= r->bytes;
                bytes.fetch_sub(r->bytes);
                r->bytes = 0;
            }
        }
        return r;
    }
    void loadLoop()
    {
        SynthSettings built;
        uint64_t revision = 0;
        double rate = 0;
        double attemptedRate = 0;
        bool haveBank = false;
        uint64_t builtBudget = 0;
        while (running)
        {
            Bank *old;
            while (retired.try_pop(old))
            {
                deleteBank(old);
            }
            SynthSettings s;
            uint64_t next;
            bool keep;
            {
                std::lock_guard lock(mutex);
                s = requested;
                next = requestRevision;
                keep = preserve;
                preparationBudget = requestedBudget;
            }
            const double nextRate = requiredRate.load();
            if (next == revision && nextRate == attemptedRate)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            busy = true;
            Packet packet;
            packet.controls.channels = s.channels;
            packet.controls.masterDb = s.masterDb;
            packet.controls.mute = s.masterMute;
            packet.controls.revision = next;
            for (unsigned i = 0; i < s.resources.size(); ++i)
            {
                if (s.resources[i].id == s.preferred)
                {
                    packet.controls.preferred = int(i);
                }
            }
            const bool rebuild = !haveBank || nextRate != rate || !sameResources(s, built) ||
                                 preparationBudget != builtBudget;
            if (rebuild)
            {
                auto bank = std::make_unique<Bank>();
                bank->rate = nextRate;
                bank->revision = next;
                bool failed = false;
                for (const auto &spec : s.resources)
                {
                    bank->resources.push_back(prepare(spec, nextRate, *bank));
                    failed |= bank->resources.back()->info.state != ResourceState::ready;
                    bool cancel;
                    {
                        std::lock_guard lock(mutex);
                        cancel = requestRevision != next;
                    }
                    if (cancel || !running)
                    {
                        deleteBank(bank.release());
                        bank.reset();
                        break;
                    }
                }
                if (!bank)
                {
                    continue;
                }
                {
                    std::lock_guard lock(mutex);
                    information.clear();
                    for (const auto &r : bank->resources)
                    {
                        information.push_back(r->info);
                    }
                }
                if (failed && keep)
                {
                    deleteBank(bank.release());
                    revision = next;
                    attemptedRate = nextRate;
                    completedRate = nextRate;
                    {
                        std::lock_guard lock(mutex);
                        busy = requestRevision != next;
                    }
                    continue;
                }
                packet.bank = bank.release();
            }
            bool published = false;
            while (running)
            {
                if (incoming.try_push(packet))
                {
                    published = true;
                    break;
                }
                while (retired.try_pop(old))
                {
                    deleteBank(old);
                }
                bool cancel;
                {
                    std::lock_guard lock(mutex);
                    cancel = requestRevision != next;
                }
                if (cancel)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            // If cancelled while the queue was full, the candidate never transferred.
            if (!published)
            {
                deleteBank(packet.bank);
                if (!running)
                {
                    break;
                }
                continue;
            }
            built = std::move(s);
            builtBudget = preparationBudget;
            revision = next;
            rate = nextRate;
            attemptedRate = nextRate;
            completedRate = nextRate;
            haveBank = true;
            {
                std::lock_guard lock(mutex);
                busy = requestRevision != next;
            }
        }
    }
    Resource *resource(const Generation &g) noexcept
    {
        return g.bank && g.resource >= 0 && unsigned(g.resource) < g.bank->resources.size()
                   ? g.bank->resources[g.resource].get()
                   : nullptr;
    }
    void clearGeneration(unsigned c, unsigned slot) noexcept
    {
        if (auto *r = resource(generations[c][slot]))
        {
            r->allOff(c);
        }
        generations[c][slot] = {};
        for (unsigned i = 0; i < noteCount[c];)
        {
            if (notes[c][i].generation == slot)
            {
                notes[c][i] = notes[c][--noteCount[c]];
            }
            else
            {
                ++i;
            }
        }
        if (selected[c] == int(slot))
        {
            selected[c] = -1;
        }
    }
    void silence() noexcept
    {
        for (unsigned c = 0; c < 16; ++c)
        {
            for (unsigned g = 0; g < 4; ++g)
            {
                clearGeneration(c, g);
            }
        }
    }
    void select(unsigned c) noexcept
    {
        selected[c] = -1;
        auto &cs = status.channels[c];
        cs.matched = false;
        cs.resource.fill(0);
        cs.preset.fill(0);
        cs.msb = msb[c];
        cs.lsb = lsb[c];
        cs.program = program[c];
        if (!current)
        {
            return;
        }
        int match = -1;
        const int preferred = controls.preferred;
        if (preferred >= 0 && unsigned(preferred) < current->resources.size() &&
            current->resources[preferred]->matches(c, msb[c], lsb[c], program[c]))
        {
            match = preferred;
        }
        else
        {
            for (unsigned i = 0; i < current->resources.size(); ++i)
            {
                if (current->resources[i]->matches(c, msb[c], lsb[c], program[c]))
                {
                    match = int(i);
                    break;
                }
            }
        }
        if (match < 0)
        {
            return;
        }
        unsigned slot = 4;
        for (unsigned g = 0; g < 4; ++g)
        {
            if (generations[c][g].bank == current && generations[c][g].resource == match)
            {
                slot = g;
                break;
            }
        }
        if (slot == 4)
        {
            for (unsigned g = 0; g < 4; ++g)
            {
                if (!generations[c][g].bank)
                {
                    slot = g;
                    break;
                }
            }
        }
        if (slot == 4)
        {
            slot = 0;
            for (unsigned g = 1; g < 4; ++g)
            {
                if (generations[c][g].age < generations[c][slot].age)
                {
                    slot = g;
                }
            }
            clearGeneration(c, slot);
            ++status.evicted;
        }
        generations[c][slot] = {current, match, ++clock};
        selected[c] = int(slot);
        auto *r = resource(generations[c][slot]);
        if (r->fluid)
        {
            const int bank = c == 9 && !msb[c] && !lsb[c] ? 128 : int(msb[c] * 128 + lsb[c]);
            fluid_synth_program_select(r->fluid.get(), c, r->font, bank, program[c]);
        }
        for (unsigned controller = 1; controller < 120; ++controller)
        {
            if (controller != 32)
            {
                const uint8_t b[]{uint8_t(0xb0 + c), uint8_t(controller), cc[c][controller]};
                r->send(c, b);
            }
        }
        const uint8_t bend[]{uint8_t(0xe0 + c), uint8_t(pitch[c] & 127), uint8_t(pitch[c] >> 7)};
        r->send(c, bend);
        cs.matched = true;
        copyText(cs.resource, r->info.spec.id.c_str());
        copyText(cs.preset, r->presetName(c, msb[c], lsb[c], program[c]));
    }
    bool adopt(Packet p) noexcept
    {
        if (p.bank)
        {
            // Voices prepared at another rate cannot be mixed into the new-rate cycle.
            for (auto *&old : banks)
            {
                if (!old || old->rate == p.bank->rate)
                {
                    continue;
                }
                if (retired.full())
                {
                    return false;
                }
                for (unsigned channel = 0; channel < 16; ++channel)
                {
                    for (unsigned generation = 0; generation < 4; ++generation)
                    {
                        if (generations[channel][generation].bank == old)
                        {
                            clearGeneration(channel, generation);
                        }
                    }
                }
                auto *removed = old;
                if (current == removed)
                {
                    current = nullptr;
                }
                old = nullptr;
                retired.try_push(removed);
            }
            unsigned slot = 4;
            for (unsigned i = 0; i < 4; ++i)
            {
                if (!banks[i])
                {
                    slot = i;
                    break;
                }
            }
            if (slot == 4)
            {
                slot = 0;
                for (unsigned i = 1; i < 4; ++i)
                {
                    if (banks[i]->revision < banks[slot]->revision)
                    {
                        slot = i;
                    }
                }
                if (retired.full())
                {
                    return false;
                }
                for (unsigned c = 0; c < 16; ++c)
                {
                    for (unsigned g = 0; g < 4; ++g)
                    {
                        if (generations[c][g].bank == banks[slot])
                        {
                            clearGeneration(c, g);
                            ++status.evicted;
                        }
                    }
                }
                auto *old = banks[slot];
                banks[slot] = nullptr;
                retired.try_push(old);
            }
            // Removed references fade and stop receiving new events; never delete
            // user files. Their owning bank remains until render access is retired.
            for (auto *old : banks)
            {
                if (old)
                {
                    for (auto &r : old->resources)
                    {
                        bool kept = false;
                        for (const auto &next : p.bank->resources)
                        {
                            kept |= next->info.spec.id == r->info.spec.id;
                        }
                        if (!kept)
                        {
                            r->fading = true;
                        }
                    }
                }
            }
            banks[slot] = p.bank;
            current = p.bank;
        }
        const auto previous = controls;
        controls = p.controls;
        status.revision = p.controls.revision;
        for (unsigned c = 0; c < 16; ++c)
        {
            const auto &a = previous.channels[c];
            const auto &b = controls.channels[c];
            if (a.msb != b.msb || a.lsb != b.lsb || a.program != b.program)
            {
                msb[c] = pendingMsb[c] = b.msb;
                lsb[c] = pendingLsb[c] = b.lsb;
                program[c] = b.program;
            }
            if (p.bank || previous.preferred != controls.preferred || a.msb != b.msb ||
                a.lsb != b.lsb || a.program != b.program)
            {
                select(c);
            }
        }
        return true;
    }
    void event(std::span<const uint8_t> b) noexcept
    {
        if (b[0] >= 0xf0)
        {
            ++status.unsupported;
            return;
        }
        const unsigned c = b[0] & 15, type = b[0] >> 4;
        if (type == 11 && b[1] == 0)
        {
            pendingMsb[c] = b[2];
            return;
        }
        if (type == 11 && b[1] == 32)
        {
            pendingLsb[c] = b[2];
            return;
        }
        if (type == 12)
        {
            msb[c] = pendingMsb[c];
            lsb[c] = pendingLsb[c];
            program[c] = b[1];
            select(c);
            return;
        }
        const bool noteOn = type == 9 && b[2] != 0;
        if (noteOn)
        {
            if (selected[c] < 0)
            {
                ++status.dropped;
                return;
            }
            if (noteCount[c] == 64)
            {
                for (unsigned g = 0; g < 4; ++g)
                {
                    clearGeneration(c, g);
                }
                ++status.overflow;
                select(c);
            }
            const unsigned g = unsigned(selected[c]);
            if (auto *r = resource(generations[c][g]))
            {
                r->send(c, b);
            }
            notes[c][noteCount[c]++] = {b[1], uint8_t(g), true, ++clock};
            return;
        }
        if (type == 8 || (type == 9 && b[2] == 0))
        {
            unsigned index = noteCount[c];
            for (unsigned i = 0; i < noteCount[c]; ++i)
            {
                if (notes[c][i].pressed && notes[c][i].key == b[1] &&
                    (index == noteCount[c] || notes[c][i].age < notes[c][index].age))
                {
                    index = i;
                }
            }
            if (index == noteCount[c])
            {
                ++status.dropped;
                return;
            }
            auto &note = notes[c][index];
            if (auto *r = resource(generations[c][note.generation]))
            {
                r->send(c, b);
            }
            note.pressed = false;
            if (cc[c][64] < 64)
            {
                notes[c][index] = notes[c][--noteCount[c]];
            }
            return;
        }
        if (type == 11)
        {
            cc[c][b[1]] = b[2];
            if (b[1] == 121)
            {
                // CC121 retains bank, volume, pan/balance and sound/effect controls.
                // Normalize the two engines to the same MIDI controller state.
                for (unsigned controller = 0; controller < 120; ++controller)
                {
                    if (controller == 0 || controller == 32 || controller == 7 ||
                        controller == 39 || controller == 8 || controller == 40 ||
                        controller == 10 || controller == 42 ||
                        (controller >= 70 && controller <= 79) ||
                        (controller >= 91 && controller <= 95))
                    {
                        continue;
                    }
                    cc[c][controller] = 0;
                }
                cc[c][11] = cc[c][43] = 127;
                std::fill(cc[c].begin() + 98, cc[c].begin() + 102, 127);
                pitch[c] = 8192;
            }
        }
        else if (type == 14)
        {
            pitch[c] = b[1] + 128 * b[2];
        }
        for (unsigned g = 0; g < 4; ++g)
        {
            if (auto *r = resource(generations[c][g]))
            {
                r->send(c, b);
                if (type == 11 && b[1] == 121)
                {
                    for (unsigned controller = 1; controller < 120; ++controller)
                    {
                        if (controller != 32)
                        {
                            const uint8_t reset[]{uint8_t(0xb0 + c), uint8_t(controller),
                                                  cc[c][controller]};
                            r->send(c, reset);
                        }
                    }
                }
            }
        }
        if (type == 11 && (b[1] == 120 || b[1] == 123))
        {
            noteCount[c] = 0;
        }
        if (type == 11 && ((b[1] == 64 && b[2] < 64) || b[1] == 121))
        {
            for (unsigned i = 0; i < noteCount[c];)
            {
                if (!notes[c][i].pressed)
                {
                    notes[c][i] = notes[c][--noteCount[c]];
                }
                else
                {
                    ++i;
                }
            }
        }
    }
    void segment(unsigned frames, float *left, float *right, unsigned offset) noexcept
    {
        for (auto &plane : audio)
        {
            std::fill_n(plane, frames, 0.f);
        }
        for (auto *bank : banks)
        {
            if (bank)
            {
                for (auto &ptr : bank->resources)
                {
                    auto &r = *ptr;
                    if (r.info.state != ResourceState::ready || !r.used)
                    {
                        continue;
                    }
                    if (r.fluid)
                    {
                        std::array<float *, 32> output;
                        for (unsigned c = 0; c < 32; ++c)
                        {
                            std::fill_n(scratch[c], frames, 0.f);
                            output[c] = scratch[c];
                        }
                        fluid_synth_process(r.fluid.get(), frames, 0, nullptr, 32, output.data());
                    }
                    else
                    {
                        for (unsigned c = 0; c < 16; ++c)
                        {
                            std::fill_n(scratch[c * 2], frames, 0.f);
                            std::fill_n(scratch[c * 2 + 1], frames, 0.f);
                            float *output[]{scratch[c * 2], scratch[c * 2 + 1]};
                            sfizz_render_block(r.sfz[c].get(), output, 2, int(frames));
                        }
                    }
                    for (unsigned f = 0; f < frames; ++f)
                    {
                        if (r.fading)
                        {
                            r.fade = (std::max)(0.f, r.fade - float(1. / (bank->rate * 0.005)));
                        }
                        for (unsigned c = 0; c < 32; ++c)
                        {
                            audio[c][f] += scratch[c][f] * r.fade;
                        }
                    }
                    if (r.fading && r.fade == 0)
                    {
                        for (unsigned c = 0; c < 16; ++c)
                        {
                            r.allOff(c);
                        }
                        r.used = false;
                    }
                }
            }
        }
        bool solo = false;
        for (const auto &c : controls.channels)
        {
            solo |= c.solo;
        }
        std::array<float, 16> target;
        for (unsigned c = 0; c < 16; ++c)
        {
            const auto &p = controls.channels[c];
            target[c] = !p.mute && (!solo || p.solo) ? float(std::pow(10., p.gainDb / 20)) : 0.f;
        }
        const float master = controls.mute ? 0.f : float(std::pow(10., controls.masterDb / 20));
        const float step = float(1. / (requiredRate.load() * 0.005));
        for (unsigned f = 0; f < frames; ++f)
        {
            float l = 0, r = 0;
            for (unsigned c = 0; c < 16; ++c)
            {
                const float delta = target[c] - channelGain[c];
                channelGain[c] += std::clamp(delta, -step, step);
                const float cl = audio[c * 2][f] * channelGain[c],
                            cr = audio[c * 2 + 1][f] * channelGain[c];
                l += cl;
                r += cr;
                status.channels[c].peak =
                    (std::max)(status.channels[c].peak, (std::max)(std::abs(cl), std::abs(cr)));
            }
            masterGain += std::clamp(master - masterGain, -step, step);
            l *= masterGain;
            r *= masterGain;
            left[offset + f] = std::isfinite(l) ? l : 0;
            right[offset + f] = std::isfinite(r) ? r : 0;
            status.leftPeak = (std::max)(status.leftPeak, std::abs(left[offset + f]));
            status.rightPeak = (std::max)(status.rightPeak, std::abs(right[offset + f]));
        }
    }
    void render(uint32_t frames, std::span<const EventView> events, float *left, float *right,
                bool inputOverflow) noexcept
    {
        if (!left || !right)
        {
            return;
        }
        std::fill_n(left, frames, 0.f);
        std::fill_n(right, frames, 0.f);
        if (!frames || frames > maxFrames)
        {
            ++status.overflow;
            silence();
            for (unsigned channel = 0; channel < 16; ++channel)
            {
                select(channel);
            }
            statuses.try_push(status);
            return;
        }
        if (hasPending)
        {
            if (!adopt(pending))
            {
                return;
            }
            hasPending = false;
        }
        Packet p;
        for (unsigned i = 0; i < 2 && incoming.try_pop(p); ++i)
        {
            if (!adopt(p))
            {
                pending = p;
                hasPending = true;
                break;
            }
        }
        for (auto &c : status.channels)
        {
            c.peak = 0;
        }
        status.leftPeak = status.rightPeak = 0;
        status.preparing = !current || current->rate != requiredRate.load();
        if (status.preparing)
        {
            const auto boundedEvents = events.first((std::min)(events.size(), size_t(maxEvents)));
            for (const auto &event : boundedEvents)
            {
                if (validEvent(event, frames) && (event.bytes[0] & 0xf0) == 0x90 && event.bytes[2])
                {
                    ++status.dropped;
                }
            }
            statuses.try_push(status);
            return;
        }
        const bool overflowCycle = inputOverflow || events.size() > maxEvents;
        if (overflowCycle)
        {
            ++status.overflow;
            silence();
            for (unsigned c = 0; c < 16; ++c)
            {
                select(c);
            }
            events = events.first((std::min)(events.size(), size_t(maxEvents)));
        }
        unsigned position = 0;
        for (const auto &e : events)
        {
            if (!validEvent(e, frames) || e.frame < position)
            {
                ++status.dropped;
                continue;
            }
            while (position < e.frame)
            {
                const auto n = (std::min)(256u, e.frame - position);
                segment(n, left, right, position);
                position += n;
            }
            if (!overflowCycle || releaseMessage(e.bytes))
            {
                event(e.bytes);
            }
            else
            {
                ++status.dropped;
            }
        }
        while (position < frames)
        {
            const auto n = (std::min)(256u, frames - position);
            segment(n, left, right, position);
            position += n;
        }
        statuses.try_push(status);
        for (auto *&bank : banks)
        {
            if (bank && bank != current && !retired.full())
            {
                bool sounding = false;
                for (const auto &r : bank->resources)
                {
                    if (r->fading && r->used)
                    {
                        sounding = true;
                    }
                    else if (r->fluid)
                    {
                        sounding |= fluid_synth_get_active_voice_count(r->fluid.get()) > 0;
                    }
                    else
                    {
                        for (const auto &sfz : r->sfz)
                        {
                            if (sfz)
                            {
                                sounding |= sfizz_get_num_active_voices(sfz.get()) > 0;
                            }
                        }
                    }
                }
                if (!sounding)
                {
                    for (unsigned c = 0; c < 16; ++c)
                    {
                        for (unsigned g = 0; g < 4; ++g)
                        {
                            if (generations[c][g].bank == bank)
                            {
                                clearGeneration(c, g);
                            }
                        }
                    }
                    retired.try_push(bank);
                    bank = nullptr;
                }
            }
        }
    }
};
SynthCore::SynthCore(double rate) : impl_(std::make_unique<Impl>(rate)) {}
SynthCore::~SynthCore() = default;
bool SynthCore::submit(const SynthSettings &s, bool preserve)
{
    if (!validSettings(s))
    {
        return false;
    }
    std::lock_guard lock(impl_->mutex);
    impl_->requested = s;
    impl_->preserve = preserve;
    impl_->requestedBudget = configuredPreparedSampleBytes();
    ++impl_->requestRevision;
    impl_->wantedRevision = impl_->requestRevision;
    impl_->busy = true;
    return true;
}
void SynthCore::setRequiredRate(double rate) noexcept
{
    if (std::isfinite(rate) && rate >= 8000 && rate <= 384000)
    {
        impl_->requiredRate = rate;
    }
}
void SynthCore::render(uint32_t frames, std::span<const EventView> events, float *left,
                       float *right, bool inputOverflow) noexcept
{
    impl_->render(frames, events, left, right, inputOverflow);
}
void SynthCore::silence() noexcept
{
    impl_->silence();
    for (unsigned c = 0; c < 16; ++c)
    {
        impl_->select(c);
    }
}
bool SynthCore::status(SynthStatus &status) noexcept
{
    bool found = false;
    for (unsigned i = 0; i < 4 && impl_->statuses.try_pop(status); ++i)
    {
        found = true;
    }
    return found;
}
std::vector<ResourceInfo> SynthCore::resources() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->information;
}
bool SynthCore::loading() const noexcept
{
    return impl_->busy.load() || impl_->completedRate.load() != impl_->requiredRate.load();
}
uint64_t SynthCore::preparedBytes() const noexcept
{
    return impl_->bytes.load();
}
} // namespace WinJACKNexus::Common::MidiTools
