/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>

#include <QByteArray>
#include <QLibrary>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace WinJACKNexus::Mixer {

class EffectGraph final {
public:
    bool configure(const Common::DigitalMixerState& state, double sampleRate, QString* error);
    bool sameTopology(const Common::DigitalMixerState& state) const noexcept;
    bool validateSnapshots(const Common::DigitalMixerState& state, QString* error) const;
    void applySnapshots(const Common::DigitalMixerState& state);
    void process(std::uint64_t stripId, std::uint32_t channel,
                 float* samples, std::size_t frames) noexcept;
    bool readLiveAnalysis(std::uint64_t stripId, std::size_t effectIndex,
                          float input[8], float output[8], float reduction[8],
                          void* spectrum, std::uint32_t spectrumBytes) noexcept;
    const QString& warning() const noexcept { return warning_; }

private:
    struct Effect final {
        using Destroy = void (*)(WjnFxInstance*);
        using Process = void (*)(WjnFxInstance*, std::uint32_t, const float*, float*, std::uint32_t);
        using Import = int (*)(WjnFxInstance*, const void*, std::uint32_t);
        using ReadLive = int (*)(const WjnFxInstance*, float[8], float[8], float[8],
                                 void*, std::uint32_t);
        QLibrary library;
        QString moduleId;
        WjnFxInstance* instance {};
        Destroy destroy {};
        Process process {};
        Import import {};
        ReadLive readLive {};
        std::uint32_t snapshotSize {};
        QByteArray snapshot;
        bool enabled {};
        bool missing {};
        ~Effect() { if (instance && destroy) destroy(instance); }
    };
    struct Chain final {
        std::uint64_t stripId {};
        bool enabled {};
        std::vector<std::unique_ptr<Effect>> effects;
    };

    static bool decodeSnapshot(const Common::MixerEffectState& state, const Effect& effect,
                               QByteArray& bytes, QString* error);
    std::vector<Chain> chains_;
    QString warning_;
};

}
