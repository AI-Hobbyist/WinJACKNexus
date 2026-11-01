/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace WinJACKNexus::Common
{
namespace
{
using Matrix = std::array<std::array<float, 8>, 8>;
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t gainRampSamples = 64;

float rampValue(float current, float target, std::size_t remaining, std::size_t frame) noexcept
{
    if (remaining == 0 || frame >= remaining)
        return target;
    return current +
           (target - current) * static_cast<float>(frame + 1) / static_cast<float>(remaining);
}

void advanceRamp(float &current, float target, std::size_t &remaining, std::size_t frames) noexcept
{
    current = frames ? rampValue(current, target, remaining, frames - 1) : current;
    remaining = frames >= remaining ? 0 : remaining - frames;
}

float dbGain(double db) noexcept
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

float faderGain(double position) noexcept
{
    if (position >= .995)
        return 0.0F;
    return dbGain(12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0);
}

AuxChannelLayout auxLayout(MixerLayout layout) noexcept
{
    switch (layout)
    {
    case MixerLayout::mono:
        return AuxChannelLayout::mono;
    case MixerLayout::stereo:
        return AuxChannelLayout::stereo;
    case MixerLayout::surround51:
        return AuxChannelLayout::surround51;
    case MixerLayout::surround71:
        return AuxChannelLayout::surround71;
    }
    return AuxChannelLayout::stereo;
}

Matrix pannerMatrix(const MixerPanner &panner, MixerLayout source, MixerLayout target)
{
    Matrix matrix{};
    const auto sourceCount = static_cast<std::size_t>(static_cast<int>(source));
    const auto targetCount = static_cast<std::size_t>(static_cast<int>(target));
    if (panner.mode == MixerPanMode::classic)
    {
        if (source == MixerLayout::mono && target == MixerLayout::stereo)
        {
            const auto angle = (panner.classicPan + 1.0) * pi / 4.0;
            matrix[0][0] = static_cast<float>(std::cos(angle));
            matrix[1][0] = static_cast<float>(std::sin(angle));
        }
        else if (source == MixerLayout::stereo && target == MixerLayout::stereo)
        {
            matrix[0][0] = panner.classicPan > 0
                               ? static_cast<float>(std::cos(panner.classicPan * pi / 2.0))
                               : 1.0F;
            matrix[1][1] = panner.classicPan < 0
                               ? static_cast<float>(std::cos(-panner.classicPan * pi / 2.0))
                               : 1.0F;
        }
    }
    else
    {
        AuxBusState bus;
        bus.sourceLayout = auxLayout(source);
        bus.targetLayout = auxLayout(target);
        bus.adjustablePoints.reserve(sourceCount);
        bus.pointInfluenceRadii.reserve(sourceCount);
        for (const auto &point : panner.points)
        {
            bus.adjustablePoints.emplace_back(point.x, point.y);
            bus.pointInfluenceRadii.push_back(point.influenceRadius);
        }
        for (std::size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
        {
            const auto gains = auxSpatialOutputGains(bus, static_cast<int>(sourceIndex));
            for (std::size_t targetIndex = 0; targetIndex < std::min(gains.size(), targetCount);
                 ++targetIndex)
                matrix[targetIndex][sourceIndex] = static_cast<float>(gains[targetIndex]);
        }
    }
    const bool anySolo = std::any_of(panner.points.cbegin(), panner.points.cend(),
                                     [](const auto &point) { return point.solo && !point.mute; });
    for (std::size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
    {
        const auto &point = panner.points[sourceIndex];
        const auto gain = point.mute || (anySolo && !point.solo) ? 0.0F : dbGain(point.levelDb);
        for (std::size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex)
            matrix[targetIndex][sourceIndex] *= gain;
    }
    return matrix;
}

std::uint64_t topologyVersion(const DigitalMixerState &state) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto strip = [&](const MixerStripState &value) {
        append(value.id);
        append(static_cast<std::uint64_t>(value.layout));
        append(value.sends.size());
        for (const auto &send : value.sends)
            append(send.targetId);
    };
    for (const auto &value : state.inputs())
        strip(value);
    for (const auto &value : state.auxiliaries())
        strip(value);
    for (const auto &value : state.subMixes())
        strip(value);
    strip(state.main());
    return hash;
}

float peak(const std::vector<float> &samples, std::size_t channels, std::size_t capacity,
           std::size_t frames) noexcept
{
    float value = 0.0F;
    for (std::size_t channel = 0; channel < channels; ++channel)
        for (std::size_t frame = 0; frame < frames; ++frame)
            value = std::max(value, std::abs(samples[channel * capacity + frame]));
    return value;
}

float channelPeak(const float *samples, std::size_t frames) noexcept
{
    float value = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame)
        value = std::max(value, std::abs(samples[frame]));
    return value;
}

void addMatrix(const std::vector<float> &source, std::size_t sourceChannels,
               std::vector<float> &target, std::size_t targetChannels, const Matrix &matrix,
               std::size_t capacity, std::size_t frames) noexcept
{
    for (std::size_t destination = 0; destination < targetChannels; ++destination)
    {
        auto *output = target.data() + destination * capacity;
        for (std::size_t input = 0; input < sourceChannels; ++input)
        {
            const auto gain = matrix[destination][input];
            if (gain == 0.0F)
                continue;
            const auto *samples = source.data() + input * capacity;
            for (std::size_t frame = 0; frame < frames; ++frame)
                output[frame] += samples[frame] * gain;
        }
    }
}

std::array<const float *, 8> planes(const std::vector<float> &data, std::size_t channels,
                                    std::size_t capacity) noexcept
{
    std::array<const float *, 8> result{};
    for (std::size_t channel = 0; channel < channels; ++channel)
        result[channel] = data.data() + channel * capacity;
    return result;
}

void addPlanes(const std::vector<float> &source, std::vector<float> &target, std::size_t channels,
               std::size_t capacity, std::size_t frames) noexcept
{
    for (std::size_t channel = 0; channel < channels; ++channel)
        for (std::size_t frame = 0; frame < frames; ++frame)
            target[channel * capacity + frame] += source[channel * capacity + frame];
}
}

bool DigitalMixerProcessor::configure(const DigitalMixerState &state, std::size_t maxFrames,
                                      QString *error, std::uint32_t sampleRate)
{
    if (maxFrames == 0 || sampleRate == 0 || !state.validate(error))
        return false;
    DigitalMixerProcessor candidate;
    candidate.maxFrames_ = maxFrames;
    candidate.sampleRate_ = sampleRate;
    candidate.topologyVersion_ = topologyVersion(state);
    candidate.mainId_ = state.main().id;
    candidate.mainInput_.resize(2 * maxFrames);
    candidate.mainOutput_.resize(2 * maxFrames);
    candidate.monitorChannels_ = static_cast<std::size_t>(static_cast<int>(state.monitorLayout()));
    candidate.monitorInput_.resize(candidate.monitorChannels_ * maxFrames);
    candidate.monitorMainMatrix_ =
        pannerMatrix(defaultMixerPanner(MixerLayout::stereo, state.monitorLayout()),
                     MixerLayout::stereo, state.monitorLayout());
    candidate.monitorAnalyzer_ = std::make_unique<MixerPolarAnalyzer>(
        0, 0, state.monitorLayout(), state.monitorLayout(), candidate.topologyVersion_, sampleRate);
    candidate.monitorFaderGain_ = faderGain(state.monitorFaderPosition());
    candidate.mainMatrix_ =
        pannerMatrix(state.main().mainPanner, MixerLayout::stereo, MixerLayout::stereo);
    candidate.mainInputGain_ = dbGain(state.main().inputGainDb);
    candidate.mainFaderGain_ = state.main().mute ? 0.0F : faderGain(state.main().faderPosition);
    candidate.mainMute_ = state.main().mute;
    candidate.mainCurrentInputGain_ =
        mainInputRampRemaining_ || maxFrames_ ? mainCurrentInputGain_ : candidate.mainInputGain_;
    candidate.mainCurrentFaderGain_ =
        mainFaderRampRemaining_ || maxFrames_ ? mainCurrentFaderGain_ : candidate.mainFaderGain_;
    candidate.mainInputRampRemaining_ =
        candidate.mainCurrentInputGain_ == candidate.mainInputGain_ ? 0 : gainRampSamples;
    candidate.mainFaderRampRemaining_ =
        candidate.mainCurrentFaderGain_ == candidate.mainFaderGain_ ? 0 : gainRampSamples;

    std::vector<const MixerStripState *> buses;
    for (const auto &bus : state.auxiliaries())
        buses.push_back(&bus);
    for (const auto &bus : state.subMixes())
        buses.push_back(&bus);
    std::map<std::uint64_t, std::size_t> auxIndex;
    for (std::size_t i = 0; i < buses.size(); ++i)
        auxIndex[buses[i]->id] = i;

    const auto makeStrip = [&](const MixerStripState &source, std::size_t portOffset,
                               std::size_t levelIndex) {
        Strip strip;
        strip.id = source.id;
        strip.channels = static_cast<std::size_t>(static_cast<int>(source.layout));
        strip.portOffset = portOffset;
        strip.levelIndex = levelIndex;
        strip.inputGain = dbGain(source.inputGainDb);
        strip.faderGain = source.mute ? 0.0F : faderGain(source.faderPosition);
        strip.mute = source.mute;
        strip.solo = source.solo;
        strip.listenMode = source.listenMode;
        strip.mixToMain = source.mixToMain && !state.hasSubMix(source.id);
        strip.exposeOutput = source.kind != MixerStripKind::aux || !state.hasSubMix(source.id);
        strip.auxSendsEnabled = source.auxSendsEnabled;
        strip.subMix = source.kind == MixerStripKind::subMix;
        // MAIN channel levels belong to the shared post path. Keep only Pan in
        // the MAIN summation matrix so Post sends receive those levels once.
        auto panOnly = source.mainPanner;
        for (auto &point : panOnly.points)
        {
            point.levelDb = 0.0;
            point.mute = false;
            point.solo = false;
        }
        strip.mainMatrix = pannerMatrix(
            strip.subMix ? defaultMixerPanner(source.layout, MixerLayout::stereo) : panOnly,
            source.layout, MixerLayout::stereo);
        if (strip.subMix)
        {
            strip.outputMatrix = pannerMatrix(source.mainPanner, source.layout, source.layout);
        }
        else
        {
            const bool anyChannelSolo =
                std::any_of(source.mainPanner.points.cbegin(), source.mainPanner.points.cend(),
                            [](const auto &point) { return point.solo && !point.mute; });
            for (std::size_t channel = 0; channel < strip.channels; ++channel)
            {
                const auto &point = source.mainPanner.points[channel];
                strip.outputMatrix[channel][channel] =
                    point.mute || (anyChannelSolo && !point.solo) ? 0.0F : dbGain(point.levelDb);
            }
        }
        strip.monitorMatrix = pannerMatrix(defaultMixerPanner(source.layout, state.monitorLayout()),
                                           source.layout, state.monitorLayout());
        const auto &previous = source.kind == MixerStripKind::input ? inputs_ : auxiliaries_;
        const auto old = std::find_if(previous.cbegin(), previous.cend(), [&](const Strip &item) {
            return item.id == source.id && item.channels == strip.channels;
        });
        strip.currentInputGain = old == previous.cend() ? strip.inputGain : old->currentInputGain;
        strip.currentFaderGain = old == previous.cend() ? strip.faderGain : old->currentFaderGain;
        strip.inputRampRemaining = strip.currentInputGain == strip.inputGain ? 0 : gainRampSamples;
        strip.faderRampRemaining = strip.currentFaderGain == strip.faderGain ? 0 : gainRampSamples;
        strip.pre.resize(strip.channels * maxFrames);
        strip.gainTap.resize(strip.channels * maxFrames);
        strip.post.resize(strip.channels * maxFrames);
        strip.mainContribution.resize(2 * maxFrames);
        strip.mainAnalyzer = std::make_unique<MixerPolarAnalyzer>(
            source.id, state.main().id, source.layout, source.layout, candidate.topologyVersion_,
            sampleRate);
        for (const auto &send : source.sends)
        {
            const auto target = auxIndex.find(send.targetId);
            if (target == auxIndex.end())
                continue;
            Route route;
            route.targetIndex = target->second;
            route.targetId = send.targetId;
            route.enabled = send.enabled;
            route.subMix = buses[target->second]->kind == MixerStripKind::subMix;
            route.preFader = !route.subMix && send.preFader;
            route.matrix = pannerMatrix(send.panner, source.layout, buses[target->second]->layout);
            const auto targetLayout = buses[target->second]->layout;
            route.contribution.resize(static_cast<std::size_t>(static_cast<int>(targetLayout)) *
                                      maxFrames);
            route.analyzer = std::make_unique<MixerPolarAnalyzer>(
                source.id, send.targetId, source.layout, targetLayout, candidate.topologyVersion_,
                sampleRate);
            strip.sends.push_back(std::move(route));
        }
        return strip;
    };
    for (const auto &strip : state.inputs())
    {
        candidate.levels_.push_back({strip.id});
        candidate.inputs_.push_back(
            makeStrip(strip, candidate.inputPortCount_, candidate.levels_.size() - 1));
        candidate.inputPortCount_ += static_cast<std::size_t>(static_cast<int>(strip.layout));
    }
    for (const auto *source : buses)
    {
        const auto &strip = *source;
        candidate.levels_.push_back({strip.id});
        candidate.auxiliaries_.push_back(
            makeStrip(strip, candidate.outputPortCount_, candidate.levels_.size() - 1));
        if (candidate.auxiliaries_.back().exposeOutput)
            candidate.outputPortCount_ += static_cast<std::size_t>(static_cast<int>(strip.layout));
    }
    candidate.levels_.push_back({0});
    candidate.levels_.push_back({state.main().id});
    candidate.outputPortCount_ += candidate.monitorChannels_ + 2;

    // Validation already rejects cycles, including configured but disabled edges.
    std::vector<std::size_t> indegree(candidate.auxiliaries_.size());
    for (const auto &strip : candidate.auxiliaries_)
        for (const auto &send : strip.sends)
            ++indegree[send.targetIndex];
    for (std::size_t processed = 0; processed < candidate.auxiliaries_.size(); ++processed)
    {
        const auto next = std::find(indegree.cbegin(), indegree.cend(), 0);
        if (next == indegree.cend())
        {
            if (error)
                *error = QStringLiteral("AUX 路由存在环");
            return false;
        }
        const auto index = static_cast<std::size_t>(next - indegree.cbegin());
        candidate.auxOrder_.push_back(index);
        indegree[index] = std::numeric_limits<std::size_t>::max();
        for (const auto &send : candidate.auxiliaries_[index].sends)
            --indegree[send.targetIndex];
    }
    *this = std::move(candidate);
    return true;
}

void DigitalMixerProcessor::adoptGainHistory(const DigitalMixerProcessor &previous) noexcept
{
    const auto adopt = [](std::vector<Strip> &destination,
                          const std::vector<Strip> &source) noexcept {
        for (auto &strip : destination)
        {
            const auto old = std::find_if(source.cbegin(), source.cend(), [&](const Strip &item) {
                return item.id == strip.id && item.channels == strip.channels;
            });
            if (old == source.cend())
                continue;
            strip.currentInputGain = old->currentInputGain;
            strip.currentFaderGain = old->currentFaderGain;
            strip.inputRampRemaining =
                strip.currentInputGain == strip.inputGain ? 0 : gainRampSamples;
            strip.faderRampRemaining =
                strip.currentFaderGain == strip.faderGain ? 0 : gainRampSamples;
        }
    };
    adopt(inputs_, previous.inputs_);
    adopt(auxiliaries_, previous.auxiliaries_);
    mainCurrentInputGain_ = previous.mainCurrentInputGain_;
    mainCurrentFaderGain_ = previous.mainCurrentFaderGain_;
    mainInputRampRemaining_ = mainCurrentInputGain_ == mainInputGain_ ? 0 : gainRampSamples;
    mainFaderRampRemaining_ = mainCurrentFaderGain_ == mainFaderGain_ ? 0 : gainRampSamples;
}

void DigitalMixerProcessor::adoptPolarHistory(DigitalMixerProcessor &previous) noexcept
{
    if (topologyVersion_ != previous.topologyVersion_ || sampleRate_ != previous.sampleRate_)
        return;
    const auto adopt = [](std::vector<Strip> &destination, std::vector<Strip> &source) noexcept {
        for (auto &strip : destination)
        {
            auto old = std::find_if(source.begin(), source.end(), [&](const Strip &item) {
                return item.id == strip.id && item.channels == strip.channels;
            });
            if (old == source.end())
                continue;
            strip.mainAnalyzer = std::move(old->mainAnalyzer);
            for (auto &route : strip.sends)
            {
                auto prior =
                    std::find_if(old->sends.begin(), old->sends.end(), [&](const Route &item) {
                        return item.targetId == route.targetId;
                    });
                if (prior != old->sends.end() && route.enabled == prior->enabled &&
                    strip.auxSendsEnabled == old->auxSendsEnabled)
                    route.analyzer = std::move(prior->analyzer);
            }
        }
    };
    adopt(inputs_, previous.inputs_);
    adopt(auxiliaries_, previous.auxiliaries_);
    if (monitorChannels_ == previous.monitorChannels_)
        monitorAnalyzer_ = std::move(previous.monitorAnalyzer_);
}

bool DigitalMixerProcessor::process(std::span<const float *const> inputs,
                                    std::span<float *const> outputs, std::size_t frames) noexcept
{
    if (frames > maxFrames_ || inputs.size() != inputPortCount_ ||
        outputs.size() != outputPortCount_)
        return false;
    for (auto *output : outputs)
        if (output)
            std::fill_n(output, frames, 0.0F);
    for (auto &aux : auxiliaries_)
        for (std::size_t channel = 0; channel < aux.channels; ++channel)
            std::fill_n(aux.pre.data() + channel * maxFrames_, frames, 0.0F);
    std::fill_n(mainInput_.data(), frames, 0.0F);
    std::fill_n(mainInput_.data() + maxFrames_, frames, 0.0F);

    const bool anySolo = std::any_of(inputs_.cbegin(), inputs_.cend(),
                                     [](const Strip &strip) { return strip.solo; }) ||
                         std::any_of(auxiliaries_.cbegin(), auxiliaries_.cend(),
                                     [](const Strip &strip) { return strip.solo; });
    for (auto &strip : inputs_)
        strip.soloAudible = !anySolo || strip.solo;
    for (auto &strip : auxiliaries_)
        strip.soloAudible = !anySolo || strip.solo;

    if (anySolo)
    {
        // Mark upstream members first, then downstream group ancestors. Keep other siblings muted.
        for (std::size_t pass = 0; pass < auxiliaries_.size(); ++pass)
            for (auto *list : {&inputs_, &auxiliaries_})
                for (auto &source : *list)
                    for (const auto &route : source.sends)
                        if (route.enabled && route.subMix &&
                            auxiliaries_[route.targetIndex].soloAudible)
                            source.soloAudible = true;
        for (std::size_t pass = 0; pass < auxiliaries_.size(); ++pass)
            for (auto *list : {&inputs_, &auxiliaries_})
                for (const auto &source : *list)
                    if (source.soloAudible)
                        for (const auto &route : source.sends)
                            if (route.subMix)
                                auxiliaries_[route.targetIndex].soloAudible = true;
    }
    const auto sendRoutes = [this, frames, anySolo](Strip &source) noexcept {
        for (auto &send : source.sends)
        {
            auto &target = auxiliaries_[send.targetIndex];
            for (std::size_t channel = 0; channel < target.channels; ++channel)
                std::fill_n(send.contribution.data() + channel * maxFrames_, frames, 0.0F);
            const auto &branchInput = send.preFader ? source.pre : source.post;
            if ((send.subMix || source.auxSendsEnabled) && !source.mute && send.enabled &&
                (!anySolo || (source.soloAudible && target.soloAudible)))
            {
                addMatrix(branchInput, source.channels, send.contribution, target.channels,
                          send.matrix, maxFrames_, frames);
                addPlanes(send.contribution, target.pre, target.channels, maxFrames_, frames);
            }
            const auto inputPointers = planes(branchInput, source.channels, maxFrames_);
            const auto outputPointers = planes(send.contribution, target.channels, maxFrames_);
            send.analyzer->push(std::span(inputPointers.data(), source.channels),
                                std::span(outputPointers.data(), target.channels), frames);
        }
    };
    const auto preparePost = [this, frames](Strip &strip) noexcept {
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
            std::fill_n(strip.post.data() + channel * maxFrames_, frames, 0.0F);
        addMatrix(strip.pre, strip.channels, strip.post, strip.channels, strip.outputMatrix,
                  maxFrames_, frames);
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
        {
            auto *post = strip.post.data() + channel * maxFrames_;
            for (std::size_t frame = 0; frame < frames; ++frame)
                post[frame] = strip.soloAudible
                                  ? post[frame] * rampValue(strip.currentFaderGain, strip.faderGain,
                                                            strip.faderRampRemaining, frame)
                                  : 0.0F;
        }
    };
    for (auto &strip : inputs_)
    {
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
        {
            auto *pre = strip.pre.data() + channel * maxFrames_;
            const auto *input = inputs[strip.portOffset + channel];
            for (std::size_t frame = 0; frame < frames; ++frame)
                pre[frame] = input
                                 ? input[frame] * rampValue(strip.currentInputGain, strip.inputGain,
                                                            strip.inputRampRemaining, frame)
                                 : 0.0F;
            levels_[strip.levelIndex].channelInputPeaks[channel] = channelPeak(pre, frames);
            std::copy_n(pre, frames, strip.gainTap.data() + channel * maxFrames_);
            if (effectProcess_)
                effectProcess_(effectContext_, strip.id, static_cast<std::uint32_t>(channel), pre,
                               frames);
        }
        preparePost(strip);
        advanceRamp(strip.currentInputGain, strip.inputGain, strip.inputRampRemaining, frames);
        advanceRamp(strip.currentFaderGain, strip.faderGain, strip.faderRampRemaining, frames);
        levels_[strip.levelIndex].inputPeak =
            *std::max_element(levels_[strip.levelIndex].channelInputPeaks.begin(),
                              levels_[strip.levelIndex].channelInputPeaks.begin() + strip.channels);
        levels_[strip.levelIndex].postPeak = peak(strip.post, strip.channels, maxFrames_, frames);
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
        {
            levels_[strip.levelIndex].channelPostPeaks[channel] =
                channelPeak(strip.post.data() + channel * maxFrames_, frames);
        }
        sendRoutes(strip);
    }
    for (const auto index : auxOrder_)
    {
        auto &strip = auxiliaries_[index];
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
        {
            auto *pre = strip.pre.data() + channel * maxFrames_;
            for (std::size_t frame = 0; frame < frames; ++frame)
                pre[frame] *= rampValue(strip.currentInputGain, strip.inputGain,
                                        strip.inputRampRemaining, frame);
            levels_[strip.levelIndex].channelInputPeaks[channel] = channelPeak(pre, frames);
            std::copy_n(pre, frames, strip.gainTap.data() + channel * maxFrames_);
            if (effectProcess_)
                effectProcess_(effectContext_, strip.id, static_cast<std::uint32_t>(channel), pre,
                               frames);
        }
        preparePost(strip);
        if (strip.exposeOutput)
            for (std::size_t channel = 0; channel < strip.channels; ++channel)
                if (auto *output = outputs[strip.portOffset + channel])
                    std::copy_n(strip.post.data() + channel * maxFrames_, frames, output);
        advanceRamp(strip.currentInputGain, strip.inputGain, strip.inputRampRemaining, frames);
        advanceRamp(strip.currentFaderGain, strip.faderGain, strip.faderRampRemaining, frames);
        levels_[strip.levelIndex].inputPeak =
            *std::max_element(levels_[strip.levelIndex].channelInputPeaks.begin(),
                              levels_[strip.levelIndex].channelInputPeaks.begin() + strip.channels);
        levels_[strip.levelIndex].postPeak = peak(strip.post, strip.channels, maxFrames_, frames);
        for (std::size_t channel = 0; channel < strip.channels; ++channel)
        {
            levels_[strip.levelIndex].channelPostPeaks[channel] =
                channelPeak(strip.post.data() + channel * maxFrames_, frames);
        }
        sendRoutes(strip);
    }
    const auto mixToMain = [this, frames](Strip &strip) noexcept {
        std::fill_n(strip.mainContribution.data(), frames, 0.0F);
        std::fill_n(strip.mainContribution.data() + maxFrames_, frames, 0.0F);
        // Pan remains on the MAIN branch; channel levels are already in post.
        addMatrix(strip.post, strip.channels, strip.mainContribution, 2, strip.mainMatrix,
                  maxFrames_, frames);
        if (strip.mixToMain && strip.soloAudible)
        {
            addPlanes(strip.mainContribution, mainInput_, 2, maxFrames_, frames);
        }
        const auto inputPointers = planes(strip.gainTap, strip.channels, maxFrames_);
        const auto outputPointers = planes(strip.post, strip.channels, maxFrames_);
        strip.mainAnalyzer->push(std::span(inputPointers.data(), strip.channels),
                                 std::span(outputPointers.data(), strip.channels), frames);
    };
    for (auto &strip : inputs_)
        mixToMain(strip);
    for (auto &strip : auxiliaries_)
        mixToMain(strip);
    if (effectProcess_)
        for (std::size_t channel = 0; channel < 2; ++channel)
            effectProcess_(effectContext_, levels_.back().stripId,
                           static_cast<std::uint32_t>(channel),
                           mainInput_.data() + channel * maxFrames_, frames);
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        auto *destination = mainOutput_.data() + channel * maxFrames_;
        std::fill_n(destination, frames, 0.0F);
    }
    addMatrix(mainInput_, 2, mainOutput_, 2, mainMatrix_, maxFrames_, frames);
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        auto *source = mainOutput_.data() + channel * maxFrames_;
        auto *output = outputs[outputPortCount_ - 2 + channel];
        for (std::size_t frame = 0; frame < frames; ++frame)
            source[frame] *=
                rampValue(mainCurrentInputGain_, mainInputGain_, mainInputRampRemaining_, frame);
        levels_.back().channelInputPeaks[channel] = channelPeak(source, frames);
        if (output)
            for (std::size_t frame = 0; frame < frames; ++frame)
                output[frame] = source[frame] * rampValue(mainCurrentFaderGain_, mainFaderGain_,
                                                          mainFaderRampRemaining_, frame);
    }
    levels_.back().inputPeak =
        std::max(levels_.back().channelInputPeaks[0], levels_.back().channelInputPeaks[1]);
    const bool hasListen =
        std::any_of(inputs_.cbegin(), inputs_.cend(),
                    [](const Strip &strip) { return strip.listenMode != MixerListenMode::off; }) ||
        std::any_of(auxiliaries_.cbegin(), auxiliaries_.cend(),
                    [](const Strip &strip) { return strip.listenMode != MixerListenMode::off; });
    if (hasListen)
    {
        for (std::size_t channel = 0; channel < monitorChannels_; ++channel)
            std::fill_n(monitorInput_.data() + channel * maxFrames_, frames, 0.0F);
        const auto addListen = [this, frames](const Strip &strip) noexcept {
            if (strip.listenMode == MixerListenMode::off)
                return;
            // PFL listens after input gain, before FX and the fader/mute path.
            const auto &source =
                strip.listenMode == MixerListenMode::pfl ? strip.gainTap : strip.post;
            addMatrix(source, strip.channels, monitorInput_, monitorChannels_, strip.monitorMatrix,
                      maxFrames_, frames);
        };
        for (const auto &strip : inputs_)
            addListen(strip);
        for (const auto &strip : auxiliaries_)
            addListen(strip);
    }
    else
    {
        for (std::size_t channel = 0; channel < monitorChannels_; ++channel)
            std::fill_n(monitorInput_.data() + channel * maxFrames_, frames, 0.0F);
    }
    auto &monitorLevel = levels_[levels_.size() - 2];
    monitorLevel.inputPeak = 0.0F;
    monitorLevel.postPeak = 0.0F;
    std::array<const float *, 8> monitorInputs{};
    std::array<const float *, 8> monitorOutputs{};
    for (std::size_t channel = 0; channel < monitorChannels_; ++channel)
    {
        const auto *source = monitorInput_.data() + channel * maxFrames_;
        monitorLevel.channelInputPeaks[channel] = channelPeak(source, frames);
        auto *output = outputs[outputPortCount_ - monitorChannels_ - 2 + channel];
        if (output)
            for (std::size_t frame = 0; frame < frames; ++frame)
                output[frame] = source[frame] * monitorFaderGain_;
        monitorLevel.channelPostPeaks[channel] = output ? channelPeak(output, frames) : 0.0F;
        monitorLevel.inputPeak =
            std::max(monitorLevel.inputPeak, monitorLevel.channelInputPeaks[channel]);
        monitorLevel.postPeak =
            std::max(monitorLevel.postPeak, monitorLevel.channelPostPeaks[channel]);
        monitorInputs[channel] = source;
        monitorOutputs[channel] = output ? output : source;
    }
    monitorAnalyzer_->push(std::span(monitorInputs.data(), monitorChannels_),
                           std::span(monitorOutputs.data(), monitorChannels_), frames);
    advanceRamp(mainCurrentInputGain_, mainInputGain_, mainInputRampRemaining_, frames);
    advanceRamp(mainCurrentFaderGain_, mainFaderGain_, mainFaderRampRemaining_, frames);
    float outputPeak = 0.0F;
    for (std::size_t channel = 0; channel < 2; ++channel)
        if (const auto *output = outputs[outputPortCount_ - 2 + channel])
        {
            const float value = channelPeak(output, frames);
            levels_.back().channelPostPeaks[channel] = value;
            outputPeak = std::max(outputPeak, value);
        }
    levels_.back().postPeak = outputPeak;
    return true;
}

bool DigitalMixerProcessor::readPolar(std::uint64_t sourceId, std::uint64_t targetId,
                                      MixerPolarSnapshot &snapshot) noexcept
{
    if (sourceId == 0 && targetId == 0 && monitorAnalyzer_)
        return monitorAnalyzer_->readLatest(snapshot);
    const auto read = [&](std::vector<Strip> &strips) {
        for (auto &strip : strips)
        {
            if (strip.id != sourceId)
                continue;
            if (targetId == mainId_)
                return strip.mainAnalyzer->readLatest(snapshot);
            for (auto &send : strip.sends)
                if (send.targetId == targetId)
                {
                    if ((!send.subMix && !strip.auxSendsEnabled) || !send.enabled)
                        return false;
                    return send.analyzer->readLatest(snapshot);
                }
        }
        return false;
    };
    return read(inputs_) || read(auxiliaries_);
}

std::uint64_t DigitalMixerProcessor::polarDropped(std::uint64_t sourceId,
                                                  std::uint64_t targetId) const noexcept
{
    const auto dropped = [&](const std::vector<Strip> &strips) {
        for (const auto &strip : strips)
        {
            if (strip.id != sourceId)
                continue;
            if (targetId == mainId_)
                return strip.mainAnalyzer->droppedWindows();
            for (const auto &send : strip.sends)
                if (send.targetId == targetId)
                    return send.analyzer->droppedWindows();
        }
        return std::uint64_t{};
    };
    const auto input = dropped(inputs_);
    return input ? input : dropped(auxiliaries_);
}

}
