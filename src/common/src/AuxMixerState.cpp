#include <WinJACKNexus/Common/AuxMixerState.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::Common {
namespace {

std::vector<QPointF> pointsOnCircle(int count, double startDegrees)
{
    std::vector<QPointF> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto angle = (startDegrees + index * 360.0 / count) * 3.14159265358979323846 / 180.0;
        points.emplace_back(std::sin(angle) * .78, -std::cos(angle) * .78);
    }
    return points;
}

}

QString auxLayoutName(AuxChannelLayout layout)
{
    switch (layout) {
    case AuxChannelLayout::mono: return QStringLiteral("单声道");
    case AuxChannelLayout::stereo: return QStringLiteral("立体声 (2.0)");
    case AuxChannelLayout::surround51: return QStringLiteral("5.1");
    case AuxChannelLayout::surround71: return QStringLiteral("7.1");
    }
    return {};
}

QStringList auxLayoutChannelLabels(AuxChannelLayout layout)
{
    switch (layout) {
    case AuxChannelLayout::mono: return { QStringLiteral("M") };
    case AuxChannelLayout::stereo: return { QStringLiteral("L"), QStringLiteral("R") };
    case AuxChannelLayout::surround51:
        return { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
                 QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs") };
    case AuxChannelLayout::surround71:
        return { QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
                 QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
                 QStringLiteral("Lrs"), QStringLiteral("Rrs") };
    }
    return {};
}

std::vector<QPointF> auxLayoutReferencePoints(AuxChannelLayout layout)
{
    switch (layout) {
    case AuxChannelLayout::mono: return { QPointF(0.0, -.72) };
    case AuxChannelLayout::stereo: return { QPointF(-.56, -.58), QPointF(.56, -.58) };
    case AuxChannelLayout::surround51: return pointsOnCircle(6, -60.0);
    case AuxChannelLayout::surround71: return pointsOnCircle(8, -67.5);
    }
    return {};
}

AuxMixerState::AuxMixerState()
{
    const auto definitions = std::array {
        std::tuple { QStringLiteral("Monitor"), AuxChannelLayout::stereo, AuxChannelLayout::stereo },
        std::tuple { QStringLiteral("Reverb"), AuxChannelLayout::stereo, AuxChannelLayout::stereo },
        std::tuple { QStringLiteral("Delay"), AuxChannelLayout::stereo, AuxChannelLayout::stereo }
    };
    for (const auto& [name, source, target] : definitions)
        buses_.push_back(makeBus(nextId_, name, source, target)), ++nextId_;
}

const std::vector<AuxBusState>& AuxMixerState::buses() const noexcept { return buses_; }
std::vector<AuxBusState>& AuxMixerState::buses() noexcept { return buses_; }

AuxBusState* AuxMixerState::bus(std::uint32_t id) noexcept
{
    const auto found = std::find_if(buses_.begin(), buses_.end(), [id](const auto& value) { return value.id == id; });
    return found == buses_.end() ? nullptr : &*found;
}

const AuxBusState* AuxMixerState::bus(std::uint32_t id) const noexcept
{
    const auto found = std::find_if(buses_.cbegin(), buses_.cend(), [id](const auto& value) { return value.id == id; });
    return found == buses_.cend() ? nullptr : &*found;
}

std::vector<AuxBusState>& AuxMixerState::busesForSource(const QString& sourceKey, std::uint32_t selfAuxId)
{
    return sourceState(sourceKey, selfAuxId).buses;
}

AuxBusState* AuxMixerState::busForSource(const QString& sourceKey, std::uint32_t id,
                                         std::uint32_t selfAuxId)
{
    auto& sourceBuses = busesForSource(sourceKey, selfAuxId);
    const auto found = std::find_if(sourceBuses.begin(), sourceBuses.end(), [id](const auto& value) {
        return value.id == id;
    });
    return found == sourceBuses.end() ? nullptr : &*found;
}

AuxBusState& AuxMixerState::addBus(AuxChannelLayout layout)
{
    const auto id = nextId_++;
    buses_.push_back(makeBus(id, QStringLiteral("AUX %1").arg(id), layout, layout));
    for (auto& source : sourceStates_) {
        source.buses.push_back(buses_.back());
        applySelfRoute(source.buses.back(), source.selfAuxId);
    }
    return buses_.back();
}

void AuxMixerState::setBusAccent(std::uint32_t id, const QColor& accent)
{
    if (auto* target=bus(id)) target->accent=accent;
    for (auto& source:sourceStates_)
        for (auto& sourceBus:source.buses)
            if (sourceBus.id==id) sourceBus.accent=accent;
}

AuxBusState* AuxMixerState::cloneBus(std::uint32_t id)
{
    const auto* original = bus(id);
    if (!original) return nullptr;
    AuxBusState copied = *original;
    copied.id = nextId_++;
    copied.name = QStringLiteral("AUX %1").arg(copied.id);
    copied.selfRoute = false;
    buses_.push_back(copied);
    for (auto& source : sourceStates_) {
        const auto found = std::find_if(source.buses.cbegin(), source.buses.cend(), [id](const auto& value) {
            return value.id == id;
        });
        AuxBusState send = found == source.buses.cend() ? copied : *found;
        send.id = copied.id;
        send.name = copied.name;
        applySelfRoute(send, source.selfAuxId);
        source.buses.push_back(std::move(send));
    }
    return &buses_.back();
}

bool AuxMixerState::removeBus(std::uint32_t id)
{
    if (buses_.size() <= 1)
        return false;
    if (std::erase_if(buses_, [id](const auto& value) { return value.id == id; }) == 0)
        return false;
    for (auto& source : sourceStates_)
        std::erase_if(source.buses, [id](const auto& value) { return value.id == id; });
    return true;
}

bool AuxMixerState::setBusLayout(std::uint32_t id, AuxChannelLayout layout)
{
    auto* current = bus(id);
    if (!current)
        return false;
    current->sourceLayout = layout;
    current->targetLayout = layout;
    rebuildChannels(*current);
    for (auto& source : sourceStates_) {
        if (source.selfAuxId == id) {
            for (auto& sourceBus : source.buses) {
                sourceBus.sourceLayout = layout;
                rebuildChannels(sourceBus);
            }
        }
        const auto found = std::find_if(source.buses.begin(), source.buses.end(), [id](const auto& value) {
            return value.id == id;
        });
        if (found != source.buses.end())
            found->targetLayout = layout;
    }
    return true;
}

void AuxMixerState::resetBus(const QString& sourceKey, std::uint32_t id, std::uint32_t selfAuxId)
{
    if (auto* current = busForSource(sourceKey, id, selfAuxId)) {
        const auto name = current->name;
        const auto source = current->sourceLayout;
        const auto target = current->targetLayout;
        *current = makeBus(id, name, source, target);
        applySelfRoute(*current, selfAuxId);
    }
}

void AuxMixerState::setLayouts(const QString& sourceKey, std::uint32_t id, AuxChannelLayout source,
                               AuxChannelLayout target, std::uint32_t selfAuxId)
{
    if (auto* current = busForSource(sourceKey, id, selfAuxId)) {
        const auto sourceChanged = current->sourceLayout != source;
        current->sourceLayout = source;
        current->targetLayout = target;
        if (sourceChanged)
            rebuildChannels(*current);
    }
}

AuxBusState AuxMixerState::makeBus(std::uint32_t id, const QString& name,
                                    AuxChannelLayout source, AuxChannelLayout target) const
{
    AuxBusState bus;
    bus.id = id;
    bus.name = name;
    bus.sourceLayout = source;
    bus.targetLayout = target;
    rebuildChannels(bus);
    return bus;
}

AuxMixerState::SourceState& AuxMixerState::sourceState(const QString& sourceKey, std::uint32_t selfAuxId)
{
    const auto found = std::find_if(sourceStates_.begin(), sourceStates_.end(), [&sourceKey](const auto& source) {
        return source.key == sourceKey;
    });
    if (found != sourceStates_.end()) {
        found->selfAuxId = selfAuxId;
        for (auto& sourceBus : found->buses)
            applySelfRoute(sourceBus, selfAuxId);
        return *found;
    }

    SourceState source;
    source.key = sourceKey;
    source.selfAuxId = selfAuxId;
    source.buses = buses_;
    for (auto& sourceBus : source.buses)
        applySelfRoute(sourceBus, selfAuxId);
    sourceStates_.push_back(std::move(source));
    return sourceStates_.back();
}

void AuxMixerState::applySelfRoute(AuxBusState& bus, std::uint32_t selfAuxId) const
{
    bus.selfRoute = selfAuxId != 0 && bus.id == selfAuxId;
    if (bus.selfRoute)
        bus.enabled = false;
}

void AuxMixerState::rebuildChannels(AuxBusState& bus) const
{
    const auto labels = auxLayoutChannelLabels(bus.sourceLayout);
    const auto defaults = auxLayoutReferencePoints(bus.sourceLayout);
    bus.adjustablePoints = defaults;
    bus.pointCoverageDegrees.assign(defaults.size(), bus.coverageDegrees);
    bus.sends.clear();
    bus.sends.reserve(static_cast<std::size_t>(labels.size()));
    for (int index = 0; index < labels.size(); ++index) {
        const auto level = index < 2 ? 0.0 : -6.0;
        bus.sends.push_back({ labels[index], labels[index], level, level - 3.0 });
    }
    bus.linked = bus.sourceLayout == AuxChannelLayout::stereo;
}

}
