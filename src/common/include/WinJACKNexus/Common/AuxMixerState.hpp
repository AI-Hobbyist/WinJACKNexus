#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <QPointF>
#include <QColor>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace WinJACKNexus::Common {

enum class AuxChannelLayout : std::uint8_t {
    mono,
    stereo,
    surround51,
    surround71
};

struct AuxSendChannel final {
    QString label;
    QString name;
    double levelDb {};
    double meterDb { -72.0 };
    bool mute {};
    bool solo {};
};

struct AuxBusState final {
    std::uint32_t id {};
    QString name;
    QColor accent;
    bool enabled { true };
    bool selfRoute {};
    bool preFader { true };
    bool sendsLinked { true };
    bool linked { true };
    bool showDirectivity { true };
    AuxChannelLayout sourceLayout { AuxChannelLayout::stereo };
    AuxChannelLayout targetLayout { AuxChannelLayout::stereo };
    double width { .7 };
    double angleDegrees {};
    double coverageDegrees { 120.0 };
    std::vector<QPointF> adjustablePoints;
    std::vector<double> pointCoverageDegrees;
    std::vector<AuxSendChannel> sends;
};

WINJACK_COMMON_API QString auxLayoutName(AuxChannelLayout layout);
WINJACK_COMMON_API QStringList auxLayoutChannelLabels(AuxChannelLayout layout);
WINJACK_COMMON_API std::vector<QPointF> auxLayoutReferencePoints(AuxChannelLayout layout);

class WINJACK_COMMON_API AuxMixerState final {
public:
    AuxMixerState();

    const std::vector<AuxBusState>& buses() const noexcept;
    std::vector<AuxBusState>& buses() noexcept;
    AuxBusState* bus(std::uint32_t id) noexcept;
    const AuxBusState* bus(std::uint32_t id) const noexcept;

    std::vector<AuxBusState>& busesForSource(const QString& sourceKey, std::uint32_t selfAuxId = 0);
    AuxBusState* busForSource(const QString& sourceKey, std::uint32_t id,
                              std::uint32_t selfAuxId = 0);

    AuxBusState& addBus(AuxChannelLayout layout = AuxChannelLayout::stereo);
    AuxBusState* cloneBus(std::uint32_t id);
    bool removeBus(std::uint32_t id);
    bool setBusLayout(std::uint32_t id, AuxChannelLayout layout);
    void setBusAccent(std::uint32_t id, const QColor& accent);
    void resetBus(const QString& sourceKey, std::uint32_t id, std::uint32_t selfAuxId = 0);
    void setLayouts(const QString& sourceKey, std::uint32_t id, AuxChannelLayout source,
                    AuxChannelLayout target, std::uint32_t selfAuxId = 0);

private:
    struct SourceState final {
        QString key;
        std::uint32_t selfAuxId {};
        std::vector<AuxBusState> buses;
    };

    AuxBusState makeBus(std::uint32_t id, const QString& name,
                        AuxChannelLayout source, AuxChannelLayout target) const;
    SourceState& sourceState(const QString& sourceKey, std::uint32_t selfAuxId);
    void applySelfRoute(AuxBusState& bus, std::uint32_t selfAuxId) const;
    void rebuildChannels(AuxBusState& bus) const;

    std::vector<AuxBusState> buses_;
    std::vector<SourceState> sourceStates_;
    std::uint32_t nextId_ { 1 };
};

}
