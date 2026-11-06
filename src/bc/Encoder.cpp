// SPDX-License-Identifier: AGPL-3.0-only
#include "Encoder.hpp"
#include <QtEndian>
#include <WinJACKNexus/Common/BroadcastState.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <opus_multistream.h>
#include <samplerate.h>
#include <stdexcept>

namespace WinJACKNexus::Broadcast
{
namespace Model = Common::Broadcast;
namespace
{
struct OpusDeleter
{
    void operator()(OpusMSEncoder *encoder) const noexcept
    {
        opus_multistream_encoder_destroy(encoder);
    }
};
struct ResamplerDeleter
{
    void operator()(SRC_STATE *state) const noexcept
    {
        src_delete(state);
    }
};
void opusCheck(int error)
{
    if (error < 0)
        throw std::runtime_error(opus_strerror(error));
}

struct Variant
{
    std::unique_ptr<OpusMSEncoder, OpusDeleter> encoder;
    QByteArray head;
    std::vector<float> input;
    QByteArray packet{1104 * 32, Qt::Uninitialized};
    std::uint8_t index;
    unsigned channels;

    Variant(unsigned channelCount, std::uint8_t variant, int bitrate)
        : input(960 * channelCount), index(variant), channels(channelCount)
    {
        std::array<unsigned char, 8> mapping{};
        int streams = 0, coupled = 0, error = 0;
        const int family = channels > 2 ? 1 : 0;
        encoder.reset(opus_multistream_surround_encoder_create(
            48000, channels, family, &streams, &coupled, mapping.data(), OPUS_APPLICATION_AUDIO, &error));
        opusCheck(error);
        opusCheck(opus_multistream_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(bitrate)));
        int lookahead = 0;
        opusCheck(opus_multistream_encoder_ctl(encoder.get(), OPUS_GET_LOOKAHEAD(&lookahead)));
        head = QByteArray(19, '\0');
        head.replace(0, 8, "OpusHead");
        head[8] = 1;
        head[9] = static_cast<char>(channels);
        qToLittleEndian(static_cast<quint16>(lookahead), head.data() + 10);
        qToLittleEndian(quint32(48000), head.data() + 12);
        head[18] = static_cast<char>(family);
        if (family)
        {
            head.append(static_cast<char>(streams));
            head.append(static_cast<char>(coupled));
            head.append(reinterpret_cast<const char *>(mapping.data()), channels);
        }
    }
};
} // namespace

class Encoder::Impl
{
  public:
    Model::Layout layout;
    std::uint32_t rate, handle, epoch;
    unsigned channels;
    std::uint64_t sourcePosition = 0, pcmPosition = 0, opusPosition = 0;
    std::vector<Variant> variants;
    std::unique_ptr<SRC_STATE, ResamplerDeleter> resampler;
    std::vector<float> pcmPending, opusPending, resampled;

    Impl(Model::Layout selected, std::uint32_t sourceRate, std::uint32_t streamHandle,
         std::uint32_t formatEpoch)
        : layout(selected), rate(sourceRate), handle(streamHandle), epoch(formatEpoch),
          channels(Model::channelCount(selected))
    {
        if (Model::layoutName(layout).isEmpty() || rate < 8000 || rate > 192000 || !handle || !epoch)
            throw std::invalid_argument("Invalid broadcast encoder configuration");
        const int bitrate = channels == 1 ? 96000 : channels == 2 ? 160000 : channels == 6 ? 384000 : 512000;
        variants.reserve(channels == 1 ? 1 : channels + 1);
        variants.emplace_back(channels, 0, bitrate);
        if (channels > 1)
            for (unsigned channel = 0; channel < channels; ++channel)
                variants.emplace_back(1, static_cast<std::uint8_t>(channel + 1), 96000);
        if (rate != 48000)
        {
            int error = 0;
            resampler.reset(src_new(SRC_SINC_MEDIUM_QUALITY, channels, &error));
            if (!resampler)
                throw std::runtime_error(src_strerror(error));
            error = src_set_ratio(resampler.get(), 48000.0 / rate);
            if (error)
                throw std::runtime_error(src_strerror(error));
        }
    }

    void resample(std::span<const float> input)
    {
        if (!resampler)
        {
            opusPending.insert(opusPending.end(), input.begin(), input.end());
            return;
        }
        const double ratio = 48000.0 / rate;
        while (!input.empty())
        {
            const auto frames = input.size() / channels;
            const auto capacity = static_cast<long>(std::ceil(frames * ratio)) + 256;
            resampled.resize(static_cast<std::size_t>(capacity) * channels);
            SRC_DATA data{};
            data.data_in = input.data();
            data.data_out = resampled.data();
            data.input_frames = static_cast<long>(frames);
            data.output_frames = capacity;
            data.src_ratio = ratio;
            const int error = src_process(resampler.get(), &data);
            if (error)
                throw std::runtime_error(src_strerror(error));
            if (data.input_frames_used == 0 && data.output_frames_gen == 0)
                throw std::runtime_error("Broadcast resampler made no progress");
            opusPending.insert(opusPending.end(), resampled.begin(),
                               resampled.begin() + data.output_frames_gen * channels);
            input = input.subspan(static_cast<std::size_t>(data.input_frames_used) * channels);
        }
    }

    void encodeOpus(std::vector<AudioBlock> &output)
    {
        constexpr std::array<unsigned, 6> surround51{0, 2, 1, 4, 5, 3};
        constexpr std::array<unsigned, 8> surround71{0, 2, 1, 4, 5, 6, 7, 3};
        std::size_t offset = 0;
        while (opusPending.size() - offset >= 960 * channels)
        {
            for (auto &variant : variants)
            {
                for (unsigned frame = 0; frame < 960; ++frame)
                    for (unsigned channel = 0; channel < variant.channels; ++channel)
                    {
                        const auto source = variant.index   ? unsigned(variant.index - 1)
                                            : channels == 6 ? surround51[channel]
                                            : channels == 8 ? surround71[channel]
                                                            : channel;
                        variant.input[frame * variant.channels + channel] =
                            opusPending[offset + frame * channels + source];
                    }
                const int bytes = opus_multistream_encode_float(
                    variant.encoder.get(), variant.input.data(), 960,
                    reinterpret_cast<unsigned char *>(variant.packet.data()), variant.packet.size());
                opusCheck(bytes);
                output.push_back(
                    {2, variant.index, handle, epoch, opusPosition, 960, variant.packet.left(bytes)});
            }
            offset += 960 * channels;
            opusPosition += 960;
        }
        opusPending.erase(opusPending.begin(), opusPending.begin() + offset);
    }
};

Encoder::Encoder(Model::Layout layout, std::uint32_t rate, std::uint32_t handle, std::uint32_t epoch)
    : impl_(std::make_unique<Impl>(layout, rate, handle, epoch))
{
}
Encoder::~Encoder() = default;

QJsonObject Encoder::opusHeads() const
{
    QJsonObject heads;
    const auto names = Model::channelNames(impl_->layout);
    for (const auto &variant : impl_->variants)
        heads.insert(variant.index ? names[variant.index - 1] : QStringLiteral("all"),
                     QString::fromLatin1(variant.head.toBase64()));
    if (impl_->channels == 1)
        heads.insert(QStringLiteral("M"), heads.value(QStringLiteral("all")));
    return heads;
}

std::vector<AudioBlock> Encoder::push(std::span<const float> input, std::uint64_t position)
{
    auto &state = *impl_;
    if (input.size() % state.channels || position != state.sourcePosition)
        throw std::invalid_argument("Broadcast source clock discontinuity requires a new format epoch");
    state.sourcePosition += input.size() / state.channels;
    state.pcmPending.insert(state.pcmPending.end(), input.begin(), input.end());
    std::vector<AudioBlock> output;
    // 10ms PCM fits 32 authenticated fragments even for 192kHz/7.1.
    const auto pcmFrames = state.rate / 100;
    const auto blockSamples = pcmFrames * state.channels;
    std::size_t offset = 0;
    while (state.pcmPending.size() - offset >= blockSamples)
    {
        output.push_back({1, 0, state.handle, state.epoch, state.pcmPosition,
                          static_cast<std::uint16_t>(pcmFrames),
                          pcm16(state.pcmPending.data() + offset, blockSamples)});
        offset += blockSamples;
        state.pcmPosition += pcmFrames;
    }
    state.pcmPending.erase(state.pcmPending.begin(), state.pcmPending.begin() + offset);
    state.resample(input);
    state.encodeOpus(output);
    return output;
}
} // namespace WinJACKNexus::Broadcast
