// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QString>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/MeterSnapshot.hpp>
#include <WinJACKNexus/Common/SpectrogramPalette.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>
#include <numbers>

namespace WinJACKNexus::Common
{
enum class AnalysisKind
{
    oscilloscope,
    spectrogram,
    polar,
    spectrumBars,
    spectrumCurve,
    loudness
};
struct AnalysisFrame
{
    std::array<float, 8> samples{};
    std::uint32_t sampleRate{};
};
// Exactly one audio producer and one widget or loudness-session consumer. A full queue
// drops analysis frames; the audio callback never allocates, waits or transforms.
class AnalysisCapture final
{
  public:
    AnalysisCapture(AnalysisKind kind, unsigned channels) : kind(kind), channels(channels)
    {
    }
    const AnalysisKind kind;
    const unsigned channels;
    std::atomic<bool> separate{};
    std::atomic<bool> overlay{};
    std::atomic<SpectrogramPalette> spectrogramPalette{SpectrogramPalette::rainbow};
    std::atomic<bool> verticalArrangement{true};
    std::atomic<unsigned> layoutRows{}, layoutColumns{};
    std::atomic<bool> logarithmic{};
    std::atomic<bool> numeric{true};
    std::atomic<unsigned> metricMask{0x1f};
    // GUI/control-thread setting; never accessed by the audio producer.
    QString loudnessPreset = QStringLiteral("ebu-r128");
    bool csvEnabled{};
    bool csvUseGlobal{true};
    QString csvDirectory;
    QString instanceId, clientName;
    MeterSnapshot loudnessSnapshot() const
    {
        const std::lock_guard lock(snapshotMutex_);
        return loudnessSnapshot_;
    }
    QString recordingStatus() const
    {
        const std::lock_guard lock(snapshotMutex_);
        return recordingStatus_;
    }
    // Non-real-time producer/reader only. Audio callbacks never take this mutex.
    void publishLoudness(const MeterSnapshot &snapshot, const QString &status)
    {
        const std::lock_guard lock(snapshotMutex_);
        loudnessSnapshot_ = snapshot;
        recordingStatus_ = status;
    }
    std::atomic<std::uint64_t> revision{};
    LockFreeSpscQueue<AnalysisFrame, 16384> samples;
    void push(const float *const *inputs, unsigned frames, std::uint32_t sampleRate) noexcept
    {
        AnalysisFrame frame;
        frame.sampleRate = sampleRate;
        for (unsigned index = 0; index < frames; ++index)
        {
            for (unsigned channel = 0; channel < channels; ++channel)
                frame.samples[channel] = inputs[channel] ? inputs[channel][index] : 0;
            if (!samples.try_push(frame))
            {
                revision.fetch_add(1, std::memory_order_release);
                break;
            }
        }
    }
    void reset() noexcept
    {
        revision.fetch_add(1, std::memory_order_release);
    }

  private:
    mutable std::mutex snapshotMutex_;
    MeterSnapshot loudnessSnapshot_;
    QString recordingStatus_;
};

inline std::array<double, 64> analysisSpectrumBands(const std::array<double, 512> &powers)
{
    std::array<double, 64> bands{};
    for (unsigned bin = 1; bin < powers.size(); ++bin)
        bands[bin / 8] = std::max(bands[bin / 8], powers[bin]);
    return bands;
}

inline double analysisRms(const std::array<float, 1024> &samples)
{
    double sum = 0;
    for (const auto sample : samples)
        sum += double(sample) * sample;
    return std::sqrt(sum / samples.size());
}
inline double analysisMeanPower(const std::array<std::array<double, 512>, 8> &powers, unsigned channels,
                                unsigned bin)
{
    double sum = 0;
    for (unsigned channel = 0; channel < channels; ++channel)
        sum += powers[channel][bin];
    return sum / channels;
}

// Highest displayed spectral-bin level above 0 dBFS, in the selected aggregation mode.
inline double analysisOverloadDb(const std::array<std::array<double, 512>, 8> &powers, unsigned channels,
                                 bool separate)
{
    double peak = 0;
    for (unsigned bin = 0; bin < powers[0].size(); ++bin)
    {
        if (separate)
            for (unsigned channel = 0; channel < channels; ++channel)
                peak = std::max(peak, powers[channel][bin]);
        else
            peak = std::max(peak, analysisMeanPower(powers, channels, bin));
    }
    return peak > 1 ? 10 * std::log10(peak) : 0;
}

// Hann-windowed radix-2 FFT. Runs only on the consumer/UI side. Values are
// amplitude-squared powers, allowing phase-independent channel aggregation.
inline std::array<double, 512> analysisSpectrum(const std::array<float, 1024> &samples)
{
    constexpr unsigned size = 1024;
    std::array<std::complex<double>, size> bins{};
    for (unsigned index = 0; index < size; ++index)
        bins[index] = samples[index] * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * index / (size - 1)));
    for (unsigned index = 1, reversed = 0; index < size; ++index)
    {
        unsigned bit = size >> 1;
        for (; reversed & bit; bit >>= 1)
            reversed ^= bit;
        reversed ^= bit;
        if (index < reversed)
            std::swap(bins[index], bins[reversed]);
    }
    for (unsigned length = 2; length <= size; length <<= 1)
    {
        const auto step = std::polar(1.0, -2 * std::numbers::pi / length);
        for (unsigned first = 0; first < size; first += length)
        {
            std::complex<double> rotation{1, 0};
            for (unsigned index = 0; index < length / 2; ++index)
            {
                const auto even = bins[first + index];
                const auto odd = bins[first + index + length / 2] * rotation;
                bins[first + index] = even + odd;
                bins[first + index + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
    std::array<double, 512> powers{};
    for (unsigned index = 0; index < powers.size(); ++index)
        powers[index] = std::norm(bins[index]) / (256.0 * 256.0);
    return powers;
}
} // namespace WinJACKNexus::Common
