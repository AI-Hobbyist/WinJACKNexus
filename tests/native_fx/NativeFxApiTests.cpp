/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/NativeFx/graphic_eq/SpectrumAnalyzer.hpp>
#include <WinJACKNexus/NativeFx/deesser/SpectrumSample.hpp>
#include <QApplication>
#include <QLibrary>
#include <QWidget>
#include <QColor>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstddef>
#include <memory>

namespace {
bool check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bool passed = check(argc == 9, "eight independent effect DLL paths supplied");
    if (!passed) return 1;
    const char* expected[] = {"gate", "graphic-eq", "deesser", "compressor",
                              "limiter", "delay", "parametric-eq", "reverb"};
    for (int index = 0; index < 8; ++index) {
        QLibrary library(QString::fromLocal8Bit(argv[index + 1]));
        if (!check(library.load(), "effect DLL loads")) { passed = false; continue; }
        auto version = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_api_version"));
        auto count = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_count"));
        auto meta = reinterpret_cast<const WjnFxMeta* (*)(uint32_t)>(library.resolve("wjn_fx_meta"));
        auto nodeCount = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_get_node_count"));
        auto create = reinterpret_cast<WjnFxInstance* (*)(uint32_t)>(library.resolve("wjn_fx_create_mixer"));
        auto clone = reinterpret_cast<WjnFxInstance* (*)(const WjnFxInstance*)>(library.resolve("wjn_fx_clone_mixer"));
        auto destroy = reinterpret_cast<void (*)(WjnFxInstance*)>(library.resolve("wjn_fx_destroy_mixer"));
        auto prepare = reinterpret_cast<void (*)(WjnFxInstance*, double)>(library.resolve("wjn_fx_prepare"));
        auto process = reinterpret_cast<void (*)(WjnFxInstance*, uint32_t, const float*, float*, uint32_t)>(library.resolve("wjn_fx_process"));
        auto setChannel = reinterpret_cast<void (*)(WjnFxInstance*, int)>(library.resolve("wjn_fx_set_channel"));
        auto setTheme = reinterpret_cast<void (*)(WjnFxInstance*, const WjnFxTheme*)>(library.resolve("wjn_fx_set_theme"));
        auto editor = reinterpret_cast<QWidget* (*)(WjnFxInstance*, QWidget*)>(library.resolve("wjn_fx_create_mixer_editor"));
        auto presetCount = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_preset_count"));
        auto presetName = reinterpret_cast<const char* (*)(uint32_t)>(library.resolve("wjn_fx_preset_name"));
        auto currentPreset = reinterpret_cast<int (*)(const WjnFxInstance*)>(library.resolve("wjn_fx_current_preset"));
        auto selectPreset = reinterpret_cast<void (*)(WjnFxInstance*, int)>(library.resolve("wjn_fx_select_preset"));
        auto snapshotVersion = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_parameter_snapshot_version"));
        auto snapshotSize = reinterpret_cast<uint32_t (*)()>(library.resolve("wjn_fx_parameter_snapshot_size"));
        auto exportSnapshot = reinterpret_cast<int (*)(const WjnFxInstance*, void*, uint32_t)>(
            library.resolve("wjn_fx_export_parameter_snapshot"));
        auto importSnapshot = reinterpret_cast<int (*)(WjnFxInstance*, const void*, uint32_t)>(
            library.resolve("wjn_fx_import_parameter_snapshot"));
        auto readAnalysis = reinterpret_cast<int (*)(const WjnFxInstance*, float*, float*)>(
            library.resolve("wjn_fx_read_analysis"));
        auto readLive = reinterpret_cast<int (*)(const WjnFxInstance*, float*, float*, float*,
            void*, uint32_t)>(library.resolve("wjn_fx_read_live_analysis"));
        if (!check(version && count && meta && nodeCount && create && clone && destroy && prepare && process && setChannel && setTheme && editor,
                   "complete Native Fx API exports")) { passed = false; continue; }
        passed &= check(version() == WJN_FX_API_VERSION && count() == 1 && meta(1) == nullptr,
                        "one v1 effect record per DLL");
        const auto* info = meta(0);
        passed &= check(info && std::strcmp(info->id, expected[index]) == 0 && info->iconGlyph[0] != '\0'
                        && info->iconColor[0] == '#' && info->placements == (WJN_FX_MIXER | WJN_FX_NODE)
                        && nodeCount() == 1, "module metadata and node placement agree");
        auto* instance = create(0);
        passed &= check(instance && create(1) == nullptr, "mixer instance uses local module index zero");
        if (!instance) continue;
        prepare(instance, 48000);
        float input[] {0.1F, -0.2F, 0.3F}, output[3] {};
        process(instance, 0, input, output, 3);
        passed &= check(std::isfinite(output[0]) && std::isfinite(output[1]) && std::isfinite(output[2]),
                        "standalone mixer processor produces finite audio");
        float inputPeaks[8] {}, outputPeaks[8] {};
        passed &= check(readAnalysis && readAnalysis(instance, inputPeaks, outputPeaks) == 1
                            && std::abs(inputPeaks[0] - .3F) < .0001F
                            && inputPeaks[1] == 0.0F && std::isfinite(outputPeaks[0]),
                        "audio instance publishes independent per-channel block analysis");
        float reduction[8] {};
        passed &= check(readLive && readLive(instance, inputPeaks, outputPeaks,
            reduction, nullptr, 0) == 1 && std::abs(inputPeaks[0] - .3F) < .0001F,
            "live editor bridge reads current audio-instance levels");
        if (readLive && (index == 1 || index == 2 || index == 6)) {
            std::array<float, 256> samples {};
            samples.fill(.2F);
            for (int block = 0; block < 17; ++block)
                process(instance, 0, samples.data(), samples.data(), samples.size());
            if (index == 2) {
                auto frame = std::make_unique<WinJACKNexus::Common::DeEsserLiveSpectrumFrame>();
                passed &= check(readLive(instance, nullptr, nullptr, nullptr,
                    frame.get(), sizeof(*frame)) == 1 && frame->counts[0] > 0,
                    "De-Esser mixer audio provides live chart samples");
            } else {
                auto frame = std::make_unique<WinJACKNexus::Common::GraphicEqSpectrumFrame>();
                passed &= check(readLive(instance, nullptr, nullptr, nullptr,
                    frame.get(), sizeof(*frame)) == 1
                    && std::abs(frame->inputSamples[0].back()) > .01F,
                    "EQ mixer audio provides live spectrum frames");
            }
        }
        passed &= check(snapshotVersion && snapshotSize && exportSnapshot && importSnapshot
                            && snapshotVersion() == WJN_FX_PARAMETER_SNAPSHOT_VERSION
                            && snapshotSize() > sizeof(WjnFxParameterSnapshotHeader),
                        "optional eight-channel parameter snapshot capability exists");
        std::vector<std::byte> initialSnapshot;
        if (snapshotSize && exportSnapshot) {
            initialSnapshot.resize(snapshotSize());
            passed &= check(exportSnapshot(instance, initialSnapshot.data(),
                                           static_cast<uint32_t>(initialSnapshot.size())) == 1,
                            "full parameter snapshot exports without an editor");
        }
        auto* duplicate = clone(instance);
        passed &= check(duplicate && duplicate != instance && clone(nullptr) == nullptr,
                        "mixer instance clones independently");
        if (duplicate && !initialSnapshot.empty()) {
            auto* fresh = create(0);
            passed &= check(fresh && importSnapshot(fresh, initialSnapshot.data(),
                static_cast<uint32_t>(initialSnapshot.size())) == 1,
                "fresh audio instance accepts cloned parameter state");
            if (fresh) {
                float clonedAudio[32] {}, freshAudio[32] {};
                process(duplicate, 0, clonedAudio, clonedAudio, 32);
                process(fresh, 0, freshAudio, freshAudio, 32);
                bool same = true;
                for (int sample = 0; sample < 32; ++sample)
                    same &= std::abs(clonedAudio[sample] - freshAudio[sample]) < .00001F;
                passed &= check(same, "clone copies parameters without sharing DSP history");
                destroy(fresh);
            }
        }
        if (duplicate) destroy(duplicate);
        QWidget parent;
        auto* view = editor(instance, &parent);
        passed &= check(presetCount && presetName && currentPreset && selectPreset
                            && presetCount() >= 4 && presetName(0) && presetName(presetCount()) == nullptr,
                        "Native Fx exports its editor preset list");
        if (presetCount && presetName && currentPreset && selectPreset) {
            if(index<=7)passed &= check(std::strcmp(presetName(0),"Default")==0,
                                        "first preset is named Default for aligned modules");
            const int selected = currentPreset(instance);
            selectPreset(instance, 0);
            passed &= check(currentPreset(instance) == 0 && selected >= 0,
                            "Native Fx preset selection updates its embedded editor");
        }
        if (snapshotSize && exportSnapshot && importSnapshot && !initialSnapshot.empty()) {
            std::vector<std::byte> presetSnapshot(snapshotSize());
            passed &= check(exportSnapshot(instance, presetSnapshot.data(),
                                           static_cast<uint32_t>(presetSnapshot.size())) == 1,
                            "preset exports all eight channel parameters");
            auto* isolated = clone(instance);
            passed &= check(isolated && importSnapshot(isolated, initialSnapshot.data(),
                                   static_cast<uint32_t>(initialSnapshot.size())) == 1,
                            "independent audio instance restores initial snapshot");
            if (isolated) {
                WjnFxParameterSnapshotHeader header {};
                std::memcpy(&header, presetSnapshot.data(), sizeof(header));
                header.channelMask = 1U << 3;
                std::memcpy(presetSnapshot.data(), &header, sizeof(header));
                passed &= check(importSnapshot(isolated, presetSnapshot.data(),
                                               static_cast<uint32_t>(presetSnapshot.size())) == 1,
                                "snapshot applies only the selected channel");
                std::vector<std::byte> merged(snapshotSize());
                passed &= check(exportSnapshot(isolated, merged.data(),
                                               static_cast<uint32_t>(merged.size())) == 1,
                                "selective snapshot result exports");
                const auto parameterBytes = header.parameterBytes;
                const auto offset = sizeof(WjnFxParameterSnapshotHeader);
                bool masksPreserved = true;
                for (std::size_t channel = 0; channel < 8; ++channel) {
                    const auto* expected = (channel == 3 ? presetSnapshot : initialSnapshot).data()
                        + offset + channel * parameterBytes;
                    masksPreserved &= std::memcmp(merged.data() + offset + channel * parameterBytes,
                                                  expected, parameterBytes) == 0;
                }
                passed &= check(masksPreserved, "selective import preserves seven unedited channels");
                header.formatVersion = 999;
                std::memcpy(presetSnapshot.data(), &header, sizeof(header));
                passed &= check(importSnapshot(isolated, presetSnapshot.data(),
                                               static_cast<uint32_t>(presetSnapshot.size())) == 0,
                                "unknown parameter format is rejected");
                destroy(isolated);
            }
        }
        passed &= check(view && view->parentWidget() == &parent && view->findChild<QWidget*>()
                        && view->height() < view->findChild<QWidget*>()->height(),
                        "mixer editor clips its node-only channel selector");
        const WjnFxTheme theme {"#101112", "#202122", "#f0f1f2", "#12abcd", "#303132"};
        setTheme(instance, &theme);
        passed &= check(view && view->property("fxAccent").value<QColor>() == QColor("#12abcd"),
                        "theme entry applies host accent override");
        setChannel(instance, 3);
        destroy(instance);
    }
    return passed ? 0 : 1;
}
