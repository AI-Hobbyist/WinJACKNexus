/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include "EffectGraph.hpp"

#include <QCoreApplication>
#include <QDirIterator>
#include <QDebug>

#include <algorithm>
#include <cstring>
#include <limits>

namespace WinJACKNexus::Mixer {
namespace {
using Count = std::uint32_t (*)();
using Meta = const WjnFxMeta* (*)(std::uint32_t);
using Create = WjnFxInstance* (*)(std::uint32_t);
using Prepare = void (*)(WjnFxInstance*, double);

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
}

bool EffectGraph::decodeSnapshot(const Common::MixerEffectState& state, const Effect& effect,
                                 QByteArray& bytes, QString* error)
{
    const auto encoded = state.parameters.value(QStringLiteral("snapshot")).toString();
    if (encoded.isEmpty()) {
        // Newly created channels use the Parametric EQ module's flat factory state.
        // It has no saved snapshot until the user edits its parameters.
        if (!state.enabled || (state.moduleId == QStringLiteral("parametric-eq")
                               && state.parameters.isEmpty())) {
            bytes.clear();
            return true;
        }
        return fail(error, QStringLiteral("%1 缺少逐声道参数快照。请在 FX 设置中重新添加。")
                    .arg(state.moduleId));
    }
    const auto decoded = QByteArray::fromBase64Encoding(
        encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return fail(error, QStringLiteral("%1 的参数快照 Base64 格式无效。").arg(state.moduleId));
    bytes = decoded.decoded;
    if (bytes.size() != static_cast<qsizetype>(effect.snapshotSize)
        || bytes.size() < static_cast<qsizetype>(sizeof(WjnFxParameterSnapshotHeader)))
        return fail(error, QStringLiteral("%1 的参数快照长度无效。") .arg(state.moduleId));
    WjnFxParameterSnapshotHeader header {};
    std::memcpy(&header, bytes.constData(), sizeof(header));
    const auto moduleId = state.moduleId.toUtf8();
    if (header.formatVersion != WJN_FX_PARAMETER_SNAPSHOT_VERSION
        || header.reserved != 0 || header.channelMask == 0
        || (header.channelMask & ~0xffU) != 0
        || header.parameterBytes == 0
        || sizeof(header) + static_cast<std::size_t>(header.parameterBytes) * 8 != effect.snapshotSize
        || moduleId.size() >= static_cast<qsizetype>(sizeof(header.moduleId))
        || std::strncmp(header.moduleId, moduleId.constData(), sizeof(header.moduleId)) != 0)
        return fail(error, QStringLiteral("%1 的参数快照版本或模块身份不匹配。") .arg(state.moduleId));
    return true;
}

bool EffectGraph::configure(const Common::DigitalMixerState& state, double sampleRate, QString* error)
{
    if (sampleRate < 8000.0) return fail(error, QStringLiteral("无效的 FX 采样率。"));
    EffectGraph candidate;
    const auto modulesPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/nodes/native_fx");
    const auto append = [&](const Common::MixerStripState& strip) -> bool {
        Chain chain;
        chain.stripId = strip.id;
        chain.enabled = strip.fxChainEnabled;
        for (const auto& stateEffect : strip.effects) {
            std::unique_ptr<Effect> loaded;
            QDirIterator files(modulesPath, {QStringLiteral("*.dll")}, QDir::Files,
                               QDirIterator::Subdirectories);
            while (files.hasNext()) {
                auto effect = std::make_unique<Effect>();
                effect->library.setFileName(files.next());
                if (!effect->library.load()) continue;
                const auto version = reinterpret_cast<Count>(effect->library.resolve("wjn_fx_api_version"));
                const auto metadata = reinterpret_cast<Meta>(effect->library.resolve("wjn_fx_meta"));
                if (!version || version() != WJN_FX_API_VERSION || !metadata) continue;
                const auto* info = metadata(0);
                if (!info || !(info->placements & WJN_FX_MIXER)
                    || QString::fromUtf8(info->id) != stateEffect.moduleId) continue;
                const auto snapshotVersion = reinterpret_cast<Count>(
                    effect->library.resolve("wjn_fx_parameter_snapshot_version"));
                const auto snapshotSize = reinterpret_cast<Count>(
                    effect->library.resolve("wjn_fx_parameter_snapshot_size"));
                const auto create = reinterpret_cast<Create>(effect->library.resolve("wjn_fx_create_mixer"));
                effect->destroy = reinterpret_cast<Effect::Destroy>(
                    effect->library.resolve("wjn_fx_destroy_mixer"));
                effect->process = reinterpret_cast<Effect::Process>(
                    effect->library.resolve("wjn_fx_process"));
                effect->import = reinterpret_cast<Effect::Import>(
                    effect->library.resolve("wjn_fx_import_parameter_snapshot"));
                effect->readLive = reinterpret_cast<Effect::ReadLive>(
                    effect->library.resolve("wjn_fx_read_live_analysis"));
                const auto prepare = reinterpret_cast<Prepare>(effect->library.resolve("wjn_fx_prepare"));
                if (!snapshotVersion || snapshotVersion() != WJN_FX_PARAMETER_SNAPSHOT_VERSION
                    || !snapshotSize || !create || !effect->destroy || !effect->process
                    || !effect->import || !prepare) continue;
                QString themeError;
                UI::ThemeNamespaceRegistry::instance().registerNativeFxModule(
                    [&library = effect->library](const char* name) { return library.resolve(name); },
                    effect->library.fileName(), &effect->themeLease, &themeError);
                if (!themeError.isEmpty()) qWarning().noquote() << "Native Fx theme descriptor:" << themeError;
                effect->snapshotSize = snapshotSize();
                effect->instance = create(0);
                if (!effect->instance) continue;
                prepare(effect->instance, sampleRate);
                effect->moduleId = stateEffect.moduleId;
                effect->enabled = stateEffect.enabled;
                loaded = std::move(effect);
                break;
            }
            if (!loaded) {
                loaded = std::make_unique<Effect>();
                loaded->moduleId = stateEffect.moduleId;
                loaded->missing = true;
                candidate.unavailableModules_.append(stateEffect.moduleId);
                candidate.warning_ += QStringLiteral("Native Fx %1 不可用，已旁通并保留配置。\n")
                    .arg(stateEffect.moduleId);
                chain.effects.push_back(std::move(loaded));
                continue;
            }
            QByteArray snapshot;
            if (!decodeSnapshot(stateEffect, *loaded, snapshot, error)) return false;
            if (!snapshot.isEmpty()
                && loaded->import(loaded->instance, snapshot.constData(), snapshot.size()) != 1)
                return fail(error, QStringLiteral("%1 的参数快照导入失败。") .arg(stateEffect.moduleId));
            loaded->snapshot = std::move(snapshot);
            chain.effects.push_back(std::move(loaded));
        }
        candidate.chains_.push_back(std::move(chain));
        return true;
    };
    for (const auto& strip : state.inputs()) if (!append(strip)) return false;
    for (const auto& strip : state.auxiliaries()) if (!append(strip)) return false;
    for (const auto& strip : state.subMixes()) if (!append(strip)) return false;
    if (!append(state.main())) return false;
    chains_ = std::move(candidate.chains_);
    warning_ = std::move(candidate.warning_);
    unavailableModules_ = std::move(candidate.unavailableModules_);
    return true;
}

bool EffectGraph::sameTopology(const Common::DigitalMixerState& state) const noexcept
{
    if (chains_.size() != state.inputs().size() + state.auxiliaries().size() + state.subMixes().size() + 1) return false;
    std::size_t index = 0;
    const auto matches = [&](const Common::MixerStripState& strip) {
        const auto& chain = chains_[index++];
        if (chain.stripId != strip.id || chain.effects.size() != strip.effects.size()) return false;
        for (std::size_t effect = 0; effect < chain.effects.size(); ++effect)
            if (chain.effects[effect]->moduleId != strip.effects[effect].moduleId) return false;
        return true;
    };
    for (const auto& strip : state.inputs()) if (!matches(strip)) return false;
    for (const auto& strip : state.auxiliaries()) if (!matches(strip)) return false;
    for (const auto& strip : state.subMixes()) if (!matches(strip)) return false;
    return matches(state.main());
}

bool EffectGraph::validateSnapshots(const Common::DigitalMixerState& state, QString* error) const
{
    if (!sameTopology(state)) return fail(error, QStringLiteral("FX 链拓扑已变更。"));
    std::size_t index = 0;
    const auto validate = [&](const Common::MixerStripState& strip) {
        const auto& chain = chains_[index++];
        for (std::size_t effect = 0; effect < chain.effects.size(); ++effect) {
            if (chain.effects[effect]->missing) continue;
            QByteArray bytes;
            if (!decodeSnapshot(strip.effects[effect], *chain.effects[effect], bytes, error))
                return false;
        }
        return true;
    };
    for (const auto& strip : state.inputs()) if (!validate(strip)) return false;
    for (const auto& strip : state.auxiliaries()) if (!validate(strip)) return false;
    for (const auto& strip : state.subMixes()) if (!validate(strip)) return false;
    return validate(state.main());
}

void EffectGraph::applySnapshots(const Common::DigitalMixerState& state)
{
    std::size_t index = 0;
    const auto apply = [&](const Common::MixerStripState& strip) {
        auto& chain = chains_[index++];
        chain.enabled = strip.fxChainEnabled;
        for (std::size_t effect = 0; effect < chain.effects.size(); ++effect) {
            auto& instance = *chain.effects[effect];
            const auto& source = strip.effects[effect];
            if (instance.missing) continue;
            instance.enabled = source.enabled;
            QByteArray bytes;
            decodeSnapshot(source, instance, bytes, nullptr);
            if (bytes == instance.snapshot) continue;
            if (!bytes.isEmpty())
                instance.import(instance.instance, bytes.constData(), bytes.size());
            instance.snapshot = std::move(bytes);
        }
    };
    for (const auto& strip : state.inputs()) apply(strip);
    for (const auto& strip : state.auxiliaries()) apply(strip);
    for (const auto& strip : state.subMixes()) apply(strip);
    apply(state.main());
}

void EffectGraph::process(std::uint64_t stripId, std::uint32_t channel,
                          float* samples, std::size_t frames) noexcept
{
    if (!samples || frames > std::numeric_limits<std::uint32_t>::max()) return;
    for (const auto& chain : chains_) {
        if (chain.stripId != stripId) continue;
        if (!chain.enabled) return;
        for (const auto& effect : chain.effects)
            if (effect->enabled && effect->instance && effect->process)
                effect->process(effect->instance, channel, samples, samples,
                                static_cast<std::uint32_t>(frames));
        return;
    }
}

bool EffectGraph::readLiveAnalysis(std::uint64_t stripId, std::size_t effectIndex,
    float input[8], float output[8], float reduction[8],
    void* spectrum, std::uint32_t spectrumBytes) noexcept
{
    for (const auto& chain : chains_) {
        if (chain.stripId != stripId) continue;
        if (!chain.enabled || effectIndex >= chain.effects.size()) return false;
        const auto& effect = chain.effects[effectIndex];
        return effect->enabled && effect->instance && effect->readLive
            && effect->readLive(effect->instance, input, output, reduction,
                                spectrum, spectrumBytes) == 1;
    }
    return false;
}
}
