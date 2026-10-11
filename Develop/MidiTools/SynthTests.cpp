// Real engines and memory buffers only, no JACK client/server initialization.
// SPDX-License-Identifier: AGPL-3.0-only
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <WinJACKNexus/Common/MidiToolsSynth.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace WinJACKNexus::Common::MidiTools;
void check(bool ok, const char *why)
{
    if (!ok)
    {
        throw std::runtime_error(why);
    }
}
struct Probe
{
    SynthCore core;
    SynthStatus status;
    std::array<float, 256> left{}, right{};
    void block(std::span<const EventView> events = {})
    {
        core.render(256, events, left.data(), right.data());
        core.status(status);
        for (float f : left)
        {
            check(std::isfinite(f), "finite audio");
        }
    }
    void wait(unsigned resourceCount)
    {
        for (unsigned i = 0; i < 15000; ++i)
        {
            block();
            if (!core.loading() && core.resources().size() == resourceCount)
            {
                block();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("bounded loader completion timeout");
    }
    void message(std::initializer_list<uint8_t> bytes, unsigned frame = 0)
    {
        std::vector<uint8_t> b(bytes);
        const EventView e{frame, b};
        block({&e, 1});
    }
    float peak()
    {
        float p = 0;
        for (float f : left)
        {
            p = (std::max)(p, std::abs(f));
        }
        return p;
    }
};
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        check(argc == 2, "fixture root required");
        const std::string root = argv[1];
        SynthSettings state;
        state.resources = {{"sf2", root + "/tone.sf2", "sf2", 0, 0, 0},
                           {"sfz", root + "/tone.sfz", "sfz", 0, 0, 2}};
        SynthSettings parsed;
        check(parseSettings(settingsJson(state).c_str(), parsed) &&
                  settingsJson(parsed) == settingsJson(state),
              "full state roundtrip");
        auto invalid = QJsonDocument::fromJson(settingsJson(state).c_str()).object();
        invalid["version"] = 2;
        const auto before = settingsJson(parsed);
        check(!parseSettings(QJsonDocument(invalid).toJson().constData(), parsed) &&
                  settingsJson(parsed) == before,
              "bad version atomic");
        state.resources.push_back(state.resources[0]);
        check(!validSettings(state), "duplicate resource rejected");
        state.resources.pop_back();
        Probe p;
        check(p.core.submit(state), "submit SF2/SFZ");
        p.wait(2);
        const auto info = p.core.resources();
        for (const auto &resource : info)
        {
            std::cout << "Preparation " << resource.spec.id << ": " << int(resource.state) << " "
                      << resource.detail << '\n';
        }
        check(info[0].state == ResourceState::ready && info[1].state == ResourceState::ready,
              "both genuine engines prepared");
        check(p.core.preparedBytes() <= maxPreparedSampleBytes, "global prepared budget");
        // A truncated JACK cycle and a full in-memory cycle use the same recovery.
        p.message({0x90, 60, 100});
        const auto overflowBefore = p.status.overflow;
        p.core.render(256, {}, p.left.data(), p.right.data(), true);
        p.core.status(p.status);
        check(p.status.overflow == overflowBefore + 1, "truncated input overflow diagnostic");
        for (unsigned index = 0; index < 4; ++index)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "overflow releases active voices");
        std::array<float, maxFrames + 1> largeLeft{}, largeRight{};
        p.core.render(maxFrames + 1, {}, largeLeft.data(), largeRight.data());
        p.message({0x90, 60, 100});
        check(p.peak() > 0.001f, "playback recovers after oversized frame cycle");
        p.core.silence();
        for (unsigned c = 0; c < 16; ++c)
        {
            p.core.silence();
            p.message({uint8_t(0x90 + c), 60, 100});
            check(p.peak() > 0.001f && p.status.channels[c].peak > 0.001f,
                  "16 channel stereo SF2 synthesis");
            for (unsigned other = 0; other < 16; ++other)
            {
                if (other != c)
                {
                    check(p.status.channels[other].peak < 0.00001f,
                          "independent channel mix meters");
                }
            }
        }
        p.core.silence();
        p.message({0xc0, 2});
        check(p.status.channels[0].matched &&
                  std::string(p.status.channels[0].resource.data()) == "sfz",
              "exact SFZ mapping");
        p.message({0x90, 60, 100}, 255);
        for (unsigned i = 0; i < 4; ++i)
        {
            p.block();
        }
        check(p.peak() > 0.001f, "SFZ boundary/crosscycle synthesis");
        p.message({0xb0, 64, 127});
        p.message({0xc0, 1});
        p.message({0x80, 60, 0});
        check(p.peak() > 0.001f, "old SFZ sustain remains after selecting SF2");
        p.message({0xb0, 64, 0});
        for (unsigned i = 0; i < 16; ++i)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "noteoff/pedal routed to old owner");
        p.message({0xc0, 127});
        check(!p.status.channels[0].matched, "unmapped program has no piano fallback");
        p.message({0x90, 60, 100});
        check(p.peak() < 0.00001f, "unmapped note suppressed");
        p.message({0xc0, 0});
        // UI controls are separate from MIDI volume/expression and survive reset.
        auto mixed = state;
        mixed.channels[0].gainDb = -6;
        check(p.core.submit(mixed), "gain snapshot");
        p.wait(2);
        // Fill the handoff queue while its audio consumer is deliberately paused.
        for (unsigned index = 0; index < 2; ++index)
        {
            auto queued = mixed;
            queued.channels[0].gainDb = -double(index + 1);
            check(p.core.submit(queued), "queue fill request");
            for (unsigned attempt = 0; attempt < 1000 && p.core.loading(); ++attempt)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(!p.core.loading(), "two snapshots published without audio consumer");
        }
        check(p.core.submit(mixed), "full queue request");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(p.core.loading(), "full handoff queue defers publication");
        p.wait(2);
        check(!p.core.loading(), "audio consumption resolves queue pressure");
        p.message({0x90, 60, 100});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        const float audible = p.peak();
        check(audible > 0.001f, "audible UI gain");
        p.message({0xb0, 7, 0});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < audible * 0.02f, "CC7 applied once in engine");
        p.message({0xb0, 121, 0});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < audible * 0.02f, "CC121 retains channel volume");
        p.message({0xb0, 7, 100});
        p.message({0xb0, 11, 0});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < audible * 0.02f, "CC11 engine expression");
        p.message({0xb0, 121, 0});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() > 0.001f, "reset resumes MIDI expression");
        check(std::abs(p.peak() - audible) < audible * 0.05f,
              "MIDI reset preserves post-engine UI gain");
        mixed.channels[0].mute = true;
        mixed.channels[0].solo = true;
        check(p.core.submit(mixed), "mute/solo snapshot");
        p.wait(2);
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "mute overrides solo");
        p.message({0x80, 60, 0});
        mixed.channels[0].mute = false;
        mixed.channels[0].solo = false;
        mixed.masterMute = true;
        check(p.core.submit(mixed), "master mute snapshot");
        p.wait(2);
        p.message({0x90, 60, 100});
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "master mute");
        p.message({0x80, 60, 0});
        mixed.masterMute = false;
        check(p.core.submit(mixed), "unmute");
        p.wait(2);
        for (unsigned i = 0; i < 16; ++i)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "release handled while muted");
        // Missing references restore legally; an import failure keeps the playable bank.
        auto broken = state;
        broken.resources.push_back({"missing", root + "/missing.sf2", "sf2"});
        check(p.core.submit(broken, true), "failed import request");
        p.wait(3);
        p.message({0xc0, 0});
        p.message({0x90, 60, 100});
        check(p.peak() > 0.001f, "import failure keeps current instruments");
        p.core.silence();
        check(p.core.submit(broken, false), "missing reference restore");
        p.wait(3);
        check(p.core.resources()[2].state == ResourceState::missing,
              "missing distinct from invalid JSON");
        // Preferred conflict priority and persisted import order.
        auto conflict = state;
        conflict.resources[1].program = 0;
        conflict.preferred = "sfz";
        check(p.core.submit(conflict), "preferred resource");
        p.wait(2);
        check(std::string(p.status.channels[0].resource.data()) == "sfz",
              "preferred exact-match priority");
        conflict.preferred.clear();
        check(p.core.submit(conflict), "import-order preference");
        p.wait(2);
        check(std::string(p.status.channels[0].resource.data()) == "sf2", "import order fallback");
        // Latest-wins cancellation/queue pressure and resource removal do not delete files.
        for (unsigned i = 0; i < 20; ++i)
        {
            auto next = state;
            next.channels[0].gainDb = -double(i % 8);
            check(p.core.submit(next), "bounded rapid control submission");
        }
        p.wait(2);
        p.message({0x90, 60, 100});
        auto empty = state;
        empty.resources.clear();
        check(p.core.submit(empty), "remove references");
        p.wait(0);
        for (unsigned i = 0; i < 8; ++i)
        {
            p.block();
        }
        check(p.peak() < 0.00001f && QFile::exists(QString::fromStdString(root + "/tone.sf2")),
              "remove fades old voices without deleting file");
        // Native Unicode path loading and invalid/oversized files are loader failures.
        QTemporaryDir temp;
        const auto unicode = temp.filePath(QString::fromUtf8("音色-音源-악기.sf2"));
        check(QFile::copy(QString::fromStdString(root + "/tone.sf2"), unicode),
              "Unicode fixture copy");
        SynthSettings unicodeState;
        unicodeState.resources = {{"unicode", unicode.toStdString(), "sf2"}};
        check(p.core.submit(unicodeState), "Unicode submit");
        p.wait(1);
        check(p.core.resources()[0].state == ResourceState::ready, "real Unicode SF2 path");
        QFile bad(temp.filePath("bad.sf2"));
        check(bad.open(QIODevice::WriteOnly), "bad fixture");
        bad.write("not SF2");
        bad.close();
        unicodeState.resources[0].path = bad.fileName().toStdString();
        check(p.core.submit(unicodeState), "bad asset request");
        p.wait(1);
        check(p.core.resources()[0].state == ResourceState::failed,
              "corrupt asset separate from state");
        QFile missingSample(temp.filePath("missing-sample.sfz"));
        check(missingSample.open(QIODevice::WriteOnly), "missing sample fixture");
        missingSample.write("<region> sample=absent.wav key=60");
        missingSample.close();
        unicodeState.resources[0] = {"missing-sample", missingSample.fileName().toStdString(),
                                     "sfz"};
        check(p.core.submit(unicodeState), "missing SFZ sample request");
        p.wait(1);
        check(p.core.resources()[0].state == ResourceState::failed,
              "missing SFZ sample does not become a ready instrument");
        // The first preparation gate rejects oversized SF2 before engine parsing.
        QFile oversized(temp.filePath("oversized.sf2"));
        check(oversized.open(QIODevice::WriteOnly) && oversized.resize(64ll * 1024 * 1024 + 1),
              "oversized fixture creation");
        oversized.close();
        unicodeState.resources[0].path = oversized.fileName().toStdString();
        unicodeState.resources[0].format = "sf2";
        check(p.core.submit(unicodeState), "oversized asset request");
        p.wait(1);
        check(p.core.resources()[0].state == ResourceState::budget,
              "sample budget rejects asset before allocating its engine");

        // Five playable resource owners exceed the four-generation channel bound.
        SynthSettings generations;
        for (unsigned index = 0; index < 5; ++index)
        {
            const QString destination = temp.filePath(QString("generation-%1.sf2").arg(index));
            check(QFile::copy(QString::fromStdString(root + "/tone.sf2"), destination),
                  "generation fixture copy");
            generations.resources.push_back(
                {"generation-" + std::to_string(index), destination.toStdString(), "sf2"});
        }
        check(p.core.submit(generations), "generation bank preparation");
        p.wait(5);
        for (unsigned index = 0; index < 5; ++index)
        {
            generations.preferred = generations.resources[index].id;
            check(p.core.submit(generations), "generation preference switch");
            p.wait(5);
            p.message({0x90, uint8_t(60 + index), 100});
        }
        check(p.status.evicted > 0 && p.core.preparedBytes() <= maxPreparedSampleBytes,
              "oldest channel generation cleared within the bounded budget");
        p.message({0xb0, 120, 0});
        for (unsigned index = 0; index < 4; ++index)
        {
            p.block();
        }
        check(p.peak() < 0.00001f, "all-sound-off releases every retained generation");

        p.core.setRequiredRate(44100);
        p.wait(5);
        check(p.core.preparedBytes() <= maxPreparedSampleBytes, "rate rebuild bounded");
        std::cout << "PASS genuine SF2/SFZ,16-way channel separation, exact mapping/old owners, "
                     "independent mix/reset/mute, failed/missing imports, "
                     "priority/cancel/remove/Unicode/corrupt/rate lifecycle; preparedBytes="
                  << p.core.preparedBytes() << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
