#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::Common
{
namespace
{

QPointF pointAtAngle(double degrees)
{
    const auto radians = degrees * 3.14159265358979323846 / 180.0;
    return {std::sin(radians) * .78, -std::cos(radians) * .78};
}

}

QString auxLayoutName(AuxChannelLayout layout)
{
    switch (layout)
    {
    case AuxChannelLayout::mono:
        return QStringLiteral("单声道");
    case AuxChannelLayout::stereo:
        return QStringLiteral("立体声 (2.0)");
    case AuxChannelLayout::surround51:
        return QStringLiteral("5.1");
    case AuxChannelLayout::surround71:
        return QStringLiteral("7.1");
    }
    return {};
}

QStringList auxLayoutChannelLabels(AuxChannelLayout layout)
{
    switch (layout)
    {
    case AuxChannelLayout::mono:
        return standardChannelLabels(1);
    case AuxChannelLayout::stereo:
        return standardChannelLabels(2);
    case AuxChannelLayout::surround51:
        return standardChannelLabels(6);
    case AuxChannelLayout::surround71:
        return standardChannelLabels(8);
    }
    return {};
}

std::vector<QPointF> auxLayoutReferencePoints(AuxChannelLayout layout)
{
    switch (layout)
    {
    case AuxChannelLayout::mono:
        return {QPointF(0.0, -.72)};
    case AuxChannelLayout::stereo:
        return {pointAtAngle(-30), pointAtAngle(30)};
    case AuxChannelLayout::surround51:
        return {pointAtAngle(-30),  pointAtAngle(30), pointAtAngle(0), {},
                pointAtAngle(-110), pointAtAngle(110)};
    case AuxChannelLayout::surround71:
        return {pointAtAngle(-30),  pointAtAngle(30),  pointAtAngle(0),    {},
                pointAtAngle(-100), pointAtAngle(100), pointAtAngle(-145), pointAtAngle(145)};
    }
    return {};
}

AuxMixerState::AuxMixerState()
{
    const auto definitions = std::array{
        std::tuple{QStringLiteral("Monitor"), AuxChannelLayout::stereo, AuxChannelLayout::stereo},
        std::tuple{QStringLiteral("Reverb"), AuxChannelLayout::stereo, AuxChannelLayout::stereo},
        std::tuple{QStringLiteral("Delay"), AuxChannelLayout::stereo, AuxChannelLayout::stereo}};
    for (const auto &[name, source, target] : definitions)
        buses_.push_back(makeBus(nextId_, name, source, target)), ++nextId_;
}

const std::vector<AuxBusState> &AuxMixerState::buses() const noexcept
{
    return buses_;
}
std::vector<AuxBusState> &AuxMixerState::buses() noexcept
{
    return buses_;
}

AuxBusState *AuxMixerState::bus(std::uint32_t id) noexcept
{
    const auto found = std::find_if(buses_.begin(), buses_.end(),
                                    [id](const auto &value) { return value.id == id; });
    return found == buses_.end() ? nullptr : &*found;
}

const AuxBusState *AuxMixerState::bus(std::uint32_t id) const noexcept
{
    const auto found = std::find_if(buses_.cbegin(), buses_.cend(),
                                    [id](const auto &value) { return value.id == id; });
    return found == buses_.cend() ? nullptr : &*found;
}

std::vector<AuxBusState> &AuxMixerState::busesForSource(const QString &sourceKey,
                                                        std::uint32_t selfAuxId)
{
    return sourceState(sourceKey, selfAuxId).buses;
}

AuxBusState *AuxMixerState::busForSource(const QString &sourceKey, std::uint32_t id,
                                         std::uint32_t selfAuxId)
{
    auto &sourceBuses = busesForSource(sourceKey, selfAuxId);
    const auto found = std::find_if(sourceBuses.begin(), sourceBuses.end(),
                                    [id](const auto &value) { return value.id == id; });
    return found == sourceBuses.end() ? nullptr : &*found;
}

AuxBusState &AuxMixerState::addBus(AuxChannelLayout layout)
{
    const auto id = nextId_++;
    buses_.push_back(makeBus(id, QStringLiteral("AUX %1").arg(id), layout, layout));
    for (auto &source : sourceStates_)
    {
        source.buses.push_back(buses_.back());
        applySelfRoute(source.buses.back(), source.selfAuxId);
    }
    return buses_.back();
}

bool auxHasSpatialPoint(AuxChannelLayout layout, int channelIndex)
{
    const auto count = auxLayoutChannelLabels(layout).size();
    return channelIndex >= 0 && channelIndex < count &&
           !((layout == AuxChannelLayout::surround51 || layout == AuxChannelLayout::surround71) &&
             channelIndex == 3);
}

bool auxSupportsClassicPan(AuxChannelLayout source, AuxChannelLayout target)
{
    return target == AuxChannelLayout::stereo &&
           (source == AuxChannelLayout::mono || source == AuxChannelLayout::stereo);
}

QPointF auxClampPannerPoint(QPointF point)
{
    const auto length = std::hypot(point.x(), point.y());
    return length > 1.0 ? point / length : point;
}

void auxSetPannerAngle(AuxBusState &bus, double angleDegrees)
{
    angleDegrees = std::clamp(angleDegrees, -180.0, 180.0);
    const auto delta = (angleDegrees - bus.angleDegrees) * 3.14159265358979323846 / 180.0;
    const auto cosine = std::cos(delta), sine = std::sin(delta);
    for (std::size_t index = 0; index < bus.adjustablePoints.size(); ++index)
    {
        if (!auxHasSpatialPoint(bus.sourceLayout, static_cast<int>(index)))
            continue;
        auto &point = bus.adjustablePoints[index];
        point = auxClampPannerPoint(
            {point.x() * cosine - point.y() * sine, point.x() * sine + point.y() * cosine});
    }
    bus.angleDegrees = angleDegrees;
}

double auxInfluencePixelRadius(double influenceRadius)
{
    return std::clamp(influenceRadius, 0.1, 0.75) * 159.0;
}

double auxInfluenceRadiusForPixels(double pixels)
{
    return std::clamp(pixels / 159.0, 0.1, 0.75);
}

void auxSetPointInfluenceRadius(AuxBusState &bus, int index, double radius)
{
    if (index < 0 || index >= static_cast<int>(bus.adjustablePoints.size()) ||
        !auxHasSpatialPoint(bus.sourceLayout, index))
        return;
    if (bus.pointInfluenceRadii.size() != bus.adjustablePoints.size())
        bus.pointInfluenceRadii.resize(bus.adjustablePoints.size(), 0.1);
    bus.pointInfluenceRadii[index] = std::clamp(radius, 0.1, 0.75);
}

std::vector<double> auxSpatialOutputGains(const AuxBusState &bus, int sourceIndex)
{
    const auto references = auxLayoutReferencePoints(bus.targetLayout);
    std::vector<double> gains(references.size());
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(bus.adjustablePoints.size()))
        return gains;
    if (!auxHasSpatialPoint(bus.sourceLayout, sourceIndex))
    {
        if (sourceIndex == 3 && references.size() > 3)
            gains[3] = 1.0;
        return gains;
    }
    const auto &source = bus.adjustablePoints[sourceIndex];
    const auto radius = sourceIndex < static_cast<int>(bus.pointInfluenceRadii.size())
                            ? std::clamp(bus.pointInfluenceRadii[sourceIndex], 0.1, 0.75)
                            : 0.1;
    std::vector<std::pair<double, int>> distances;
    distances.reserve(references.size());
    for (int target = 0; target < static_cast<int>(references.size()); ++target)
    {
        if (!auxHasSpatialPoint(bus.targetLayout, target))
            continue;
        const auto distance =
            std::hypot(source.x() - references[target].x(), source.y() - references[target].y());
        distances.emplace_back(distance, target);
        gains[target] = std::max(0.0, 1.0 - distance / radius);
    }
    if (distances.empty())
        return gains;
    std::sort(distances.begin(), distances.end());
    const auto nearestDistance = distances.front().first;
    const auto nearestTie =
        std::find_if(distances.begin(), distances.end(), [nearestDistance](const auto &item) {
            return item.first - nearestDistance > 1e-6;
        });
    if (nearestTie != distances.begin() + 1)
    {
        const auto share = 1.0 / static_cast<double>(nearestTie - distances.begin());
        for (auto item = distances.begin(); item != nearestTie; ++item)
            gains[item->second] = std::max(gains[item->second], share);
    }
    else if (distances.size() == 1 || nearestDistance < 1e-6)
    {
        gains[distances.front().second] = 1.0;
    }
    else
    {
        const auto secondDistance = distances[1].first;
        const auto secondTie = std::find_if(
            distances.begin() + 1, distances.end(),
            [secondDistance](const auto &item) { return item.first - secondDistance > 1e-6; });
        const auto nearestShare = secondDistance / (nearestDistance + secondDistance);
        const auto secondShare =
            (1.0 - nearestShare) / static_cast<double>(secondTie - (distances.begin() + 1));
        gains[distances.front().second] = std::max(gains[distances.front().second], nearestShare);
        for (auto item = distances.begin() + 1; item != secondTie; ++item)
        {
            gains[item->second] = std::max(gains[item->second], secondShare);
        }
    }
    double power = 0.0;
    for (const auto gain : gains)
        power += gain * gain;
    if (power <= 0.0)
        return gains;
    const auto normalization = std::sqrt(power);
    for (auto &gain : gains)
        gain /= normalization;
    return gains;
}

void auxMixSpatialBlock(const AuxBusState &bus, std::span<const float *const> inputs,
                        std::span<float *const> outputs, std::size_t frames)
{
    if (!bus.enabled || bus.panMode != AuxPanMode::spatial || frames == 0)
        return;
    const auto sourceCount = std::min(inputs.size(), bus.sends.size());
    const auto soloed = std::any_of(bus.sends.begin(), bus.sends.begin() + sourceCount,
                                    [](const AuxSendChannel &send) { return send.solo; });
    for (std::size_t source = 0; source < sourceCount; ++source)
    {
        const auto &send = bus.sends[source];
        if (!inputs[source] || send.mute || (soloed && !send.solo))
            continue;
        const auto gains = auxSpatialOutputGains(bus, static_cast<int>(source));
        const auto level = std::pow(10.0, send.levelDb / 20.0);
        for (std::size_t target = 0; target < std::min(outputs.size(), gains.size()); ++target)
        {
            if (!outputs[target] || gains[target] <= 0.0)
                continue;
            const auto gain = static_cast<float>(level * gains[target]);
            for (std::size_t frame = 0; frame < frames; ++frame)
                outputs[target][frame] += inputs[source][frame] * gain;
        }
    }
}

void AuxMixerState::setBusAccent(std::uint32_t id, const QColor &accent)
{
    if (auto *target = bus(id))
        target->accent = accent;
    for (auto &source : sourceStates_)
        for (auto &sourceBus : source.buses)
            if (sourceBus.id == id)
                sourceBus.accent = accent;
}

AuxBusState *AuxMixerState::cloneBus(std::uint32_t id)
{
    const auto *original = bus(id);
    if (!original)
        return nullptr;
    AuxBusState copied = *original;
    copied.id = nextId_++;
    copied.cloneSourceId = id;
    copied.name = QStringLiteral("AUX %1").arg(copied.id);
    copied.selfRoute = false;
    buses_.push_back(copied);
    for (auto &source : sourceStates_)
    {
        const auto found = std::find_if(source.buses.cbegin(), source.buses.cend(),
                                        [id](const auto &value) { return value.id == id; });
        AuxBusState send = found == source.buses.cend() ? copied : *found;
        send.id = copied.id;
        send.name = copied.name;
        applySelfRoute(send, source.selfAuxId);
        source.buses.push_back(std::move(send));
    }
    return &buses_.back();
}

bool AuxMixerState::removeBus(std::uint32_t id, bool allowEmpty)
{
    if (buses_.size() <= 1 && !allowEmpty)
        return false;
    if (std::erase_if(buses_, [id](const auto &value) { return value.id == id; }) == 0)
        return false;
    for (auto &source : sourceStates_)
        std::erase_if(source.buses, [id](const auto &value) { return value.id == id; });
    return true;
}

void AuxMixerState::clearBuses()
{
    buses_.clear();
    sourceStates_.clear();
    nextId_ = 1;
}

bool AuxMixerState::setBusLayout(std::uint32_t id, AuxChannelLayout layout)
{
    auto *current = bus(id);
    if (!current)
        return false;
    current->sourceLayout = layout;
    current->targetLayout = layout;
    rebuildChannels(*current);
    for (auto &source : sourceStates_)
    {
        if (source.selfAuxId == id)
        {
            for (auto &sourceBus : source.buses)
            {
                sourceBus.sourceLayout = layout;
                rebuildChannels(sourceBus);
            }
        }
        const auto found = std::find_if(source.buses.begin(), source.buses.end(),
                                        [id](const auto &value) { return value.id == id; });
        if (found != source.buses.end())
        {
            const auto wasEligible =
                auxSupportsClassicPan(found->sourceLayout, found->targetLayout);
            found->targetLayout = layout;
            const auto eligible = auxSupportsClassicPan(found->sourceLayout, found->targetLayout);
            if (eligible != wasEligible || !eligible)
                found->panMode = eligible ? AuxPanMode::classic : AuxPanMode::spatial;
        }
    }
    return true;
}

void AuxMixerState::resetBus(const QString &sourceKey, std::uint32_t id, std::uint32_t selfAuxId)
{
    if (auto *current = busForSource(sourceKey, id, selfAuxId))
    {
        const auto name = current->name;
        const auto source = current->sourceLayout;
        const auto target = current->targetLayout;
        *current = makeBus(id, name, source, target);
        applySelfRoute(*current, selfAuxId);
    }
}

void AuxMixerState::setLayouts(const QString &sourceKey, std::uint32_t id, AuxChannelLayout source,
                               AuxChannelLayout target, std::uint32_t selfAuxId)
{
    if (auto *current = busForSource(sourceKey, id, selfAuxId))
    {
        const auto wasEligible =
            auxSupportsClassicPan(current->sourceLayout, current->targetLayout);
        const auto sourceChanged = current->sourceLayout != source;
        current->sourceLayout = source;
        current->targetLayout = target;
        if (sourceChanged)
            rebuildChannels(*current);
        const auto eligible = auxSupportsClassicPan(source, target);
        if (eligible != wasEligible || !eligible)
            current->panMode = eligible ? AuxPanMode::classic : AuxPanMode::spatial;
    }
}

AuxBusState AuxMixerState::makeBus(std::uint32_t id, const QString &name, AuxChannelLayout source,
                                   AuxChannelLayout target) const
{
    AuxBusState bus;
    bus.id = id;
    bus.name = name;
    bus.sourceLayout = source;
    bus.targetLayout = target;
    rebuildChannels(bus);
    return bus;
}

AuxMixerState::SourceState &AuxMixerState::sourceState(const QString &sourceKey,
                                                       std::uint32_t selfAuxId)
{
    const auto found =
        std::find_if(sourceStates_.begin(), sourceStates_.end(),
                     [&sourceKey](const auto &source) { return source.key == sourceKey; });
    if (found != sourceStates_.end())
    {
        found->selfAuxId = selfAuxId;
        for (auto &sourceBus : found->buses)
        {
            if (const auto *target = bus(sourceBus.id))
                sourceBus.name = target->name;
            applySelfRoute(sourceBus, selfAuxId);
        }
        return *found;
    }

    SourceState source;
    source.key = sourceKey;
    source.selfAuxId = selfAuxId;
    source.buses = buses_;
    for (auto &sourceBus : source.buses)
        applySelfRoute(sourceBus, selfAuxId);
    sourceStates_.push_back(std::move(source));
    return sourceStates_.back();
}

void AuxMixerState::applySelfRoute(AuxBusState &bus, std::uint32_t selfAuxId) const
{
    bus.selfRoute = selfAuxId != 0 && bus.id == selfAuxId;
    if (bus.selfRoute)
        bus.enabled = false;
}

void AuxMixerState::rebuildChannels(AuxBusState &bus) const
{
    const auto labels = auxLayoutChannelLabels(bus.sourceLayout);
    const auto defaults = auxLayoutReferencePoints(bus.sourceLayout);
    bus.adjustablePoints = defaults;
    bus.pointInfluenceRadii.assign(defaults.size(), 0.1);
    bus.sends.clear();
    bus.sends.reserve(static_cast<std::size_t>(labels.size()));
    for (int index = 0; index < labels.size(); ++index)
    {
        const auto level = 0.0;
        bus.sends.push_back({labels[index], labels[index], level, level - 3.0});
    }
    bus.linked = false;
    bus.panMode = auxSupportsClassicPan(bus.sourceLayout, bus.targetLayout) ? AuxPanMode::classic
                                                                            : AuxPanMode::spatial;
    bus.classicPan = 0.0;
}

}
