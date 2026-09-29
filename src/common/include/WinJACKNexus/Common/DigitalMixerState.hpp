/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once

#include <WinJACKNexus/Common/Export.hpp>
#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <vector>

namespace WinJACKNexus::Common {
inline constexpr double mixerUnityFaderPosition = .95 * 12.0 / 84.0;

enum class MixerLayout : int { mono = 1, stereo = 2, surround51 = 6, surround71 = 8 };
enum class MixerStripKind : int { input = 0, aux = 1, main = 2 };
enum class MixerPanMode : int { spatial = 0, classic = 1 };
enum class MixerPortDirection : int { input = 0, output = 1 };

struct WINJACK_COMMON_API MixerSourcePoint final {
    double x {};
    double y {};
    double influenceRadius { .1 };
    double levelDb {};
    bool mute {};
    bool solo {};
};

struct WINJACK_COMMON_API MixerPanner final {
    MixerPanMode mode { MixerPanMode::classic };
    double classicPan {};
    double width { 1.0 };
    double angleDegrees {};
    bool linked {};
    bool showPolarCurves { true };
    bool showInputCurve { true };
    bool showOutputCurve { true };
    std::vector<MixerSourcePoint> points;
};

struct WINJACK_COMMON_API MixerAuxSend final {
    std::uint64_t targetId {};
    bool enabled {};
    bool preFader { true };
    MixerPanner panner;
};

struct WINJACK_COMMON_API MixerEffectState final {
    QString moduleId;
    bool enabled { true };
    int preset { -1 };
    // Module-owned per-channel parameters are saved verbatim for M5's bridge.
    QJsonObject parameters;
};

enum class MixerListenMode : int { off, pfl, afl };

struct WINJACK_COMMON_API MixerStripState final {
    std::uint64_t id {};
    std::uint32_t number {};
    MixerStripKind kind { MixerStripKind::input };
    MixerLayout layout { MixerLayout::stereo };
    QString name;
    QString comment;
    QColor color { "#31c6ff" };
    double inputGainDb {};
    double faderPosition { mixerUnityFaderPosition };
    bool mute {};
    bool solo {};
    MixerListenMode listenMode { MixerListenMode::off };
    bool mixToMain { true };
    bool auxSendsEnabled {};
    bool fxChainEnabled {};
    MixerPanner mainPanner;
    std::vector<MixerAuxSend> sends;
    std::vector<MixerEffectState> effects;
};

struct WINJACK_COMMON_API MixerPortDescriptor final {
    std::uint64_t stripId {};
    QString name;
    MixerPortDirection direction { MixerPortDirection::input };
    int channelIndex {};
};

class WINJACK_COMMON_API DigitalMixerState final {
public:
    DigitalMixerState();

    const std::vector<MixerStripState>& inputs() const noexcept { return inputs_; }
    const std::vector<MixerStripState>& auxiliaries() const noexcept { return auxiliaries_; }
    const MixerStripState& main() const noexcept { return main_; }
    double monitorFaderPosition() const noexcept { return monitorFaderPosition_; }
    void setMonitorFaderPosition(double position) noexcept { monitorFaderPosition_ = position; }
    MixerLayout monitorLayout() const noexcept { return monitorLayout_; }
    void setMonitorLayout(MixerLayout layout) noexcept { monitorLayout_ = layout; }
    const MixerStripState* strip(std::uint64_t id) const noexcept;
    MixerStripState* strip(std::uint64_t id) noexcept;

    std::uint64_t add(MixerStripKind kind, MixerLayout layout);
    std::uint64_t clone(std::uint64_t id);
    bool remove(std::uint64_t id);
    bool setLayout(std::uint64_t id, MixerLayout layout);
    bool setSend(std::uint64_t sourceId, const MixerAuxSend& send, QString* error = nullptr);
    std::vector<MixerPortDescriptor> ports() const;
    bool validate(QString* error = nullptr) const;

    QByteArray toJson() const;
    bool loadJson(const QByteArray& json, QString* error = nullptr);

private:
    std::vector<MixerStripState> inputs_;
    std::vector<MixerStripState> auxiliaries_;
    MixerStripState main_;
    double monitorFaderPosition_ { mixerUnityFaderPosition };
    MixerLayout monitorLayout_ { MixerLayout::stereo };
    std::uint64_t nextId_ { 2 };
    std::uint32_t nextInputNumber_ { 1 };
    std::uint32_t nextAuxNumber_ { 1 };
};

WINJACK_COMMON_API bool isMixerLayout(MixerLayout layout) noexcept;
WINJACK_COMMON_API QStringList mixerChannelLabels(MixerLayout layout);
WINJACK_COMMON_API MixerPanner defaultMixerPanner(MixerLayout source, MixerLayout target);

}
