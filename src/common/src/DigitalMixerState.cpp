/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace WinJACKNexus::Common {
namespace {

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

bool validPanMode(MixerPanMode mode)
{
    return mode == MixerPanMode::spatial || mode == MixerPanMode::classic;
}

bool supportsClassic(MixerLayout source, MixerLayout target)
{
    return (source == MixerLayout::mono && target == MixerLayout::stereo)
        || (source == MixerLayout::stereo && target == MixerLayout::stereo);
}

QJsonObject pointJson(const MixerSourcePoint& point)
{
    return {{"x", point.x}, {"y", point.y}, {"radius", point.influenceRadius},
            {"levelDb", point.levelDb}, {"mute", point.mute}, {"solo", point.solo}};
}

QJsonObject pannerJson(const MixerPanner& panner)
{
    QJsonArray points;
    for (const auto& point : panner.points) points.append(pointJson(point));
    return {{"mode", static_cast<int>(panner.mode)}, {"classicPan", panner.classicPan},
            {"width", panner.width}, {"angleDegrees", panner.angleDegrees},
            {"linked", panner.linked}, {"showPolarCurves", panner.showPolarCurves},
            {"showInputCurve", panner.showInputCurve},
            {"showOutputCurve", panner.showOutputCurve},
            {"points", points}};
}

QJsonObject stripJson(const MixerStripState& strip)
{
    QJsonArray sends;
    for (const auto& send : strip.sends) {
        sends.append(QJsonObject {{"targetId", QString::number(send.targetId)},
                                  {"enabled", send.enabled}, {"preFader", send.preFader},
                                  {"panner", pannerJson(send.panner)}});
    }
    QJsonArray effects;
    for (const auto& effect : strip.effects) {
        effects.append(QJsonObject {{"moduleId", effect.moduleId}, {"enabled", effect.enabled},
                                    {"preset", effect.preset}, {"parameters", effect.parameters}});
    }
    return {{"id", QString::number(strip.id)}, {"number", static_cast<int>(strip.number)},
            {"kind", static_cast<int>(strip.kind)}, {"layout", static_cast<int>(strip.layout)},
            {"name", strip.name}, {"comment", strip.comment}, {"color", strip.color.name(QColor::HexRgb)},
            {"inputGainDb", strip.inputGainDb}, {"faderPosition", strip.faderPosition},
            {"mute", strip.mute}, {"solo", strip.solo},
            {"listenMode", static_cast<int>(strip.listenMode)}, {"mixToMain", strip.mixToMain},
            {"auxSendsEnabled", strip.auxSendsEnabled}, {"fxChainEnabled", strip.fxChainEnabled},
            {"mainPanner", pannerJson(strip.mainPanner)},
            {"sends", sends}, {"effects", effects}};
}

bool getObject(const QJsonObject& object, const char* key, QJsonObject& value)
{
    const auto item = object.value(QLatin1String(key));
    if (!item.isObject()) return false;
    value = item.toObject();
    return true;
}

bool getArray(const QJsonObject& object, const char* key, QJsonArray& value)
{
    const auto item = object.value(QLatin1String(key));
    if (!item.isArray()) return false;
    value = item.toArray();
    return true;
}

bool getString(const QJsonObject& object, const char* key, QString& value)
{
    const auto item = object.value(QLatin1String(key));
    if (!item.isString()) return false;
    value = item.toString();
    return true;
}

bool getBool(const QJsonObject& object, const char* key, bool& value)
{
    const auto item = object.value(QLatin1String(key));
    if (!item.isBool()) return false;
    value = item.toBool();
    return true;
}

bool getNumber(const QJsonObject& object, const char* key, double& value)
{
    const auto item = object.value(QLatin1String(key));
    if (!item.isDouble()) return false;
    value = item.toDouble();
    return std::isfinite(value);
}

bool getInt(const QJsonObject& object, const char* key, int& value)
{
    double number = 0;
    if (!getNumber(object, key, number) || number != std::trunc(number)
        || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
    value = static_cast<int>(number);
    return true;
}

bool getId(const QJsonObject& object, const char* key, std::uint64_t& value)
{
    QString text;
    if (!getString(object, key, text) || text.isEmpty() || text[0] == QLatin1Char('+')) return false;
    bool ok = false;
    value = text.toULongLong(&ok, 10);
    return ok && value != 0 && QString::number(value) == text;
}

bool readPanner(const QJsonObject& object, MixerPanner& result)
{
    int mode = 0;
    QJsonArray points;
    if (!getInt(object, "mode", mode)
        || !getNumber(object, "classicPan", result.classicPan)
        || !getNumber(object, "width", result.width)
        || !getNumber(object, "angleDegrees", result.angleDegrees)
        || !getBool(object, "linked", result.linked)
        || !getBool(object, "showPolarCurves", result.showPolarCurves)
        || !getArray(object, "points", points)) return false;
    if (object.contains("showInputCurve")
        && !getBool(object, "showInputCurve", result.showInputCurve)) return false;
    if (object.contains("showOutputCurve")
        && !getBool(object, "showOutputCurve", result.showOutputCurve)) return false;
    result.mode = static_cast<MixerPanMode>(mode);
    result.points.clear();
    for (const auto& item : points) {
        if (!item.isObject()) return false;
        const auto objectPoint = item.toObject();
        MixerSourcePoint point;
        if (!getNumber(objectPoint, "x", point.x)
            || !getNumber(objectPoint, "y", point.y)
            || !getNumber(objectPoint, "radius", point.influenceRadius)
            || !getNumber(objectPoint, "levelDb", point.levelDb)
            || !getBool(objectPoint, "mute", point.mute)
            || !getBool(objectPoint, "solo", point.solo)) return false;
        result.points.push_back(point);
    }
    return true;
}

bool readStrip(const QJsonObject& object, MixerStripState& result)
{
    int kind = 0, layout = 0, number = 0;
    QString color;
    QJsonObject mainPanner;
    QJsonArray sends, effects;
    if (!getId(object, "id", result.id) || !getInt(object, "number", number)
        || !getInt(object, "kind", kind) || !getInt(object, "layout", layout)
        || !getString(object, "name", result.name) || !getString(object, "comment", result.comment)
        || !getString(object, "color", color)
        || !getNumber(object, "inputGainDb", result.inputGainDb)
        || !getNumber(object, "faderPosition", result.faderPosition)
        || !getBool(object, "mute", result.mute) || !getBool(object, "solo", result.solo)
        || !getBool(object, "mixToMain", result.mixToMain)
        || !getBool(object, "auxSendsEnabled", result.auxSendsEnabled)
        || !getObject(object, "mainPanner", mainPanner)
        || !getArray(object, "sends", sends) || !getArray(object, "effects", effects)
        || number < 0 || color.size() != 7 || !color.startsWith(QLatin1Char('#'))) return false;
    if (object.contains("fxChainEnabled") && !getBool(object, "fxChainEnabled", result.fxChainEnabled))
        return false;
    if (object.contains("listenMode")) {
        int mode = 0;
        if (!getInt(object, "listenMode", mode) || mode < 0 || mode > 2) return false;
        result.listenMode = static_cast<MixerListenMode>(mode);
    }
    result.number = static_cast<std::uint32_t>(number);
    result.kind = static_cast<MixerStripKind>(kind);
    result.layout = static_cast<MixerLayout>(layout);
    result.color = QColor(color);
    if (!result.color.isValid() || !readPanner(mainPanner, result.mainPanner)) return false;
    for (const auto& item : sends) {
        if (!item.isObject()) return false;
        MixerAuxSend send;
        QJsonObject panner;
        const auto source = item.toObject();
        if (!getId(source, "targetId", send.targetId)
            || !getBool(source, "enabled", send.enabled)
            || !getBool(source, "preFader", send.preFader)
            || !getObject(source, "panner", panner)
            || !readPanner(panner, send.panner)) return false;
        result.sends.push_back(std::move(send));
    }
    for (const auto& item : effects) {
        if (!item.isObject()) return false;
        MixerEffectState effect;
        const auto source = item.toObject();
        if (!getString(source, "moduleId", effect.moduleId)
            || !getBool(source, "enabled", effect.enabled)
            || !getInt(source, "preset", effect.preset)
            || !getObject(source, "parameters", effect.parameters)) return false;
        result.effects.push_back(std::move(effect));
    }
    return true;
}

bool validPanner(const MixerPanner& panner, MixerLayout source, MixerLayout target)
{
    if (!validPanMode(panner.mode) || (panner.mode == MixerPanMode::classic && !supportsClassic(source, target))
        || !inRange(panner.classicPan, -1, 1) || !inRange(panner.width, 0, 1)
        || !inRange(panner.angleDegrees, -180, 180)
        || panner.points.size() != static_cast<std::size_t>(static_cast<int>(source))) return false;
    for (const auto& point : panner.points) {
        if (!inRange(point.x, -1, 1) || !inRange(point.y, -1, 1)
            || point.x * point.x + point.y * point.y > 1.000001
            || !inRange(point.influenceRadius, .1, .75)
            || !inRange(point.levelDb, -72, 12)) return false;
    }
    return true;
}

} // namespace

bool isMixerLayout(MixerLayout layout) noexcept
{
    return layout == MixerLayout::mono || layout == MixerLayout::stereo
        || layout == MixerLayout::surround51 || layout == MixerLayout::surround71;
}

QStringList mixerChannelLabels(MixerLayout layout)
{
    switch (layout) {
    case MixerLayout::mono: return {QStringLiteral("M")};
    case MixerLayout::stereo: return {QStringLiteral("L"), QStringLiteral("R")};
    case MixerLayout::surround51: return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
                                           QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs")};
    case MixerLayout::surround71: return {QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"),
                                           QStringLiteral("LFE"), QStringLiteral("Ls"), QStringLiteral("Rs"),
                                           QStringLiteral("Lb"), QStringLiteral("Rb")};
    }
    return {};
}

MixerPanner defaultMixerPanner(MixerLayout source, MixerLayout target)
{
    MixerPanner panner;
    panner.mode = supportsClassic(source, target) ? MixerPanMode::classic : MixerPanMode::spatial;
    const auto count = static_cast<int>(source);
    panner.points.resize(count);
    const auto sourceLayout = source == MixerLayout::mono ? AuxChannelLayout::mono
        : source == MixerLayout::stereo ? AuxChannelLayout::stereo
        : source == MixerLayout::surround51 ? AuxChannelLayout::surround51
        : AuxChannelLayout::surround71;
    const auto reference = auxLayoutReferencePoints(sourceLayout);
    for (int i = 0; i < count; ++i) {
        auto& point = panner.points[static_cast<std::size_t>(i)];
        point.levelDb = 0;
        if (static_cast<std::size_t>(i) < reference.size()) {
            point.x = reference[static_cast<std::size_t>(i)].x();
            point.y = reference[static_cast<std::size_t>(i)].y();
        }
    }
    return panner;
}

DigitalMixerState::DigitalMixerState()
{
    main_.id = 1;
    main_.kind = MixerStripKind::main;
    main_.layout = MixerLayout::stereo;
    main_.name = QStringLiteral("Main L/R");
    main_.comment = QStringLiteral("JACK Out 1/2");
    main_.color = QColor(QStringLiteral("#50dfbd"));
    main_.faderPosition = mixerUnityFaderPosition;
    main_.solo = false;
    main_.mainPanner = defaultMixerPanner(MixerLayout::stereo, MixerLayout::stereo);
}

const MixerStripState* DigitalMixerState::strip(std::uint64_t id) const noexcept
{
    if (id == main_.id) return &main_;
    for (const auto* list : {&inputs_, &auxiliaries_}) {
        const auto it = std::find_if(list->begin(), list->end(), [id](const auto& item) { return item.id == id; });
        if (it != list->end()) return &*it;
    }
    return nullptr;
}

MixerStripState* DigitalMixerState::strip(std::uint64_t id) noexcept
{
    return const_cast<MixerStripState*>(std::as_const(*this).strip(id));
}

std::uint64_t DigitalMixerState::add(MixerStripKind kind, MixerLayout layout)
{
    if ((kind != MixerStripKind::input && kind != MixerStripKind::aux) || !isMixerLayout(layout)
        || nextId_ == std::numeric_limits<std::uint64_t>::max()) return 0;
    auto& list = kind == MixerStripKind::input ? inputs_ : auxiliaries_;
    auto& nextNumber = kind == MixerStripKind::input ? nextInputNumber_ : nextAuxNumber_;
    if (nextNumber >= static_cast<std::uint32_t>(std::numeric_limits<int>::max())) return 0;
    MixerStripState item;
    item.id = nextId_++;
    item.number = nextNumber++;
    item.kind = kind;
    item.layout = layout;
    item.faderPosition = mixerUnityFaderPosition;
    item.name = QStringLiteral("%1%2").arg(kind == MixerStripKind::input ? QStringLiteral("CH") : QStringLiteral("AUX"))
                                     .arg(item.number);
    item.mainPanner = defaultMixerPanner(layout, MixerLayout::stereo);
    const auto id = item.id;
    list.push_back(std::move(item));
    return id;
}

std::uint64_t DigitalMixerState::clone(std::uint64_t id)
{
    const auto* source = strip(id);
    if (!source || source->kind == MixerStripKind::main) return 0;
    const auto copied = *source;
    const auto newId = add(copied.kind, copied.layout);
    if (!newId) return 0;
    auto* target = strip(newId);
    const auto number = target->number;
    *target = copied;
    target->id = newId;
    target->number = number;
    target->name = QStringLiteral("%1%2").arg(target->kind == MixerStripKind::input ? QStringLiteral("CH") : QStringLiteral("AUX"))
                                         .arg(number);
    target->sends.erase(std::remove_if(target->sends.begin(), target->sends.end(), [newId](const auto& send) {
        return send.targetId == newId;
    }), target->sends.end());
    return newId;
}

bool DigitalMixerState::remove(std::uint64_t id)
{
    if (id == main_.id) return false;
    for (auto* list : {&inputs_, &auxiliaries_}) {
        const auto it = std::find_if(list->begin(), list->end(), [id](const auto& item) { return item.id == id; });
        if (it == list->end()) continue;
        list->erase(it);
        for (auto* remaining : {&inputs_, &auxiliaries_}) {
            for (auto& strip : *remaining) {
                strip.sends.erase(std::remove_if(strip.sends.begin(), strip.sends.end(), [id](const auto& send) {
                    return send.targetId == id;
                }), strip.sends.end());
            }
        }
        return true;
    }
    return false;
}

bool DigitalMixerState::setLayout(std::uint64_t id, MixerLayout layout)
{
    auto* target = strip(id);
    if (!target || target->kind == MixerStripKind::main || !isMixerLayout(layout)) return false;
    target->layout = layout;
    target->mainPanner = defaultMixerPanner(layout, MixerLayout::stereo);
    for (auto& send : target->sends) {
        const auto* destination = strip(send.targetId);
        send.panner = defaultMixerPanner(layout, destination->layout);
    }
    if (target->kind == MixerStripKind::aux) {
        for (auto* list : {&inputs_, &auxiliaries_}) {
            for (auto& source : *list) {
                for (auto& send : source.sends) {
                    if (send.targetId == id)
                        send.panner = defaultMixerPanner(source.layout, layout);
                }
            }
        }
    }
    return true;
}

bool DigitalMixerState::setSend(std::uint64_t sourceId, const MixerAuxSend& send, QString* error)
{
    auto* source = strip(sourceId);
    const auto* destination = strip(send.targetId);
    if (!source || source->kind == MixerStripKind::main || !destination
        || destination->kind != MixerStripKind::aux || sourceId == send.targetId)
        return fail(error, QStringLiteral("无效或自发 AUX 路由"));
    if (!validPanner(send.panner, source->layout, destination->layout))
        return fail(error, QStringLiteral("AUX 声像参数无效"));
    auto copy = *this;
    auto* copySource = copy.strip(sourceId);
    const auto it = std::find_if(copySource->sends.begin(), copySource->sends.end(), [&send](const auto& item) {
        return item.targetId == send.targetId;
    });
    if (it == copySource->sends.end()) copySource->sends.push_back(send);
    else *it = send;
    if (!copy.validate(error)) return false;
    *this = std::move(copy);
    return true;
}

std::vector<MixerPortDescriptor> DigitalMixerState::ports() const
{
    std::vector<MixerPortDescriptor> result;
    const auto append = [&result](const MixerStripState& strip) {
        const auto labels = mixerChannelLabels(strip.layout);
        const auto prefix = strip.kind == MixerStripKind::main ? QStringLiteral("MAIN")
                          : QStringLiteral("%1%2").arg(strip.kind == MixerStripKind::input
                                  ? QStringLiteral("CH") : QStringLiteral("AUX")).arg(strip.number);
        for (int channel = 0; channel < labels.size(); ++channel) {
            result.push_back({strip.id, prefix + QLatin1Char('_') + labels[channel],
                              strip.kind == MixerStripKind::input ? MixerPortDirection::input
                                                                   : MixerPortDirection::output,
                              channel});
        }
    };
    for (const auto& item : inputs_) append(item);
    for (const auto& item : auxiliaries_) append(item);
    const auto monitorLabels = mixerChannelLabels(monitorLayout_);
    for (int channel = 0; channel < monitorLabels.size(); ++channel)
        result.push_back({0, QStringLiteral("MONITOR_") + monitorLabels[channel],
                          MixerPortDirection::output, channel});
    append(main_);
    return result;
}

bool DigitalMixerState::validate(QString* error) const
{
    if (main_.id != 1 || main_.number != 0 || main_.kind != MixerStripKind::main
        || main_.layout != MixerLayout::stereo || main_.solo
        || main_.listenMode != MixerListenMode::off || !main_.sends.empty())
        return fail(error, QStringLiteral("MAIN 必须是不可删除的立体声总线"));
    if (!inRange(monitorFaderPosition_, 0, 1) || !isMixerLayout(monitorLayout_))
        return fail(error, QStringLiteral("监听推子参数无效"));
    std::set<std::uint64_t> ids;
    std::set<std::uint32_t> inputNumbers, auxNumbers;
    const auto checkStrip = [&](const MixerStripState& item, MixerStripKind expected) {
        if (item.kind != expected || !isMixerLayout(item.layout) || item.id == 0
            || !ids.insert(item.id).second || item.id >= nextId_
            || !item.color.isValid() || item.name.trimmed().isEmpty()
            || !inRange(item.inputGainDb, -12, 12) || !inRange(item.faderPosition, 0, 1)
            || static_cast<int>(item.listenMode) < 0 || static_cast<int>(item.listenMode) > 2
            || !validPanner(item.mainPanner, item.layout, MixerLayout::stereo)) return false;
        if (expected != MixerStripKind::main) {
            auto& numbers = expected == MixerStripKind::input ? inputNumbers : auxNumbers;
            const auto next = expected == MixerStripKind::input ? nextInputNumber_ : nextAuxNumber_;
            if (item.number == 0 || item.number >= next || !numbers.insert(item.number).second) return false;
        }
        for (const auto& effect : item.effects) {
            if (effect.moduleId.trimmed().isEmpty() || effect.preset < -1) return false;
        }
        return true;
    };
    if (!checkStrip(main_, MixerStripKind::main)) return fail(error, QStringLiteral("MAIN 参数无效"));
    for (const auto& item : inputs_) {
        if (!checkStrip(item, MixerStripKind::input)) return fail(error, QStringLiteral("CH 参数或身份无效"));
    }
    for (const auto& item : auxiliaries_) {
        if (!checkStrip(item, MixerStripKind::aux)) return fail(error, QStringLiteral("AUX 参数或身份无效"));
    }
    for (const auto* list : {&inputs_, &auxiliaries_}) {
        for (const auto& source : *list) {
            std::set<std::uint64_t> targets;
            for (const auto& send : source.sends) {
                const auto* target = strip(send.targetId);
                if (!target || target->kind != MixerStripKind::aux || target->id == source.id
                    || !targets.insert(target->id).second
                    || !validPanner(send.panner, source.layout, target->layout))
                    return fail(error, QStringLiteral("AUX 路由无效或重复"));
            }
        }
    }
    // Every configured edge participates, including disabled routes.
    std::set<std::uint64_t> visiting, visited;
    const auto visit = [&](auto&& self, std::uint64_t id) -> bool {
        if (visiting.contains(id)) return false;
        if (visited.contains(id)) return true;
        visiting.insert(id);
        const auto* source = strip(id);
        for (const auto& send : source->sends) {
            if (!self(self, send.targetId)) return false;
        }
        visiting.erase(id);
        visited.insert(id);
        return true;
    };
    for (const auto& aux : auxiliaries_) {
        if (!visit(visit, aux.id)) return fail(error, QStringLiteral("AUX 路由存在环"));
    }
    return true;
}

QByteArray DigitalMixerState::toJson() const
{
    QJsonArray inputs, auxiliaries;
    for (const auto& item : inputs_) inputs.append(stripJson(item));
    for (const auto& item : auxiliaries_) auxiliaries.append(stripJson(item));
    return QJsonDocument(QJsonObject {
        {"version", 1}, {"nextId", QString::number(nextId_)},
        {"nextInputNumber", static_cast<int>(nextInputNumber_)},
        {"nextAuxNumber", static_cast<int>(nextAuxNumber_)},
        {"inputs", inputs}, {"auxiliaries", auxiliaries}, {"main", stripJson(main_)},
        {"monitorFaderPosition", monitorFaderPosition_},
        {"monitorLayout", static_cast<int>(monitorLayout_)}
    }).toJson(QJsonDocument::Compact);
}

bool DigitalMixerState::loadJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("JSON 格式错误"));
    const auto root = document.object();
    int version = 0, inputNumber = 0, auxNumber = 0;
    QJsonArray inputs, auxiliaries;
    QJsonObject main;
    DigitalMixerState candidate;
    if (!getInt(root, "version", version) || version != 1
        || !getId(root, "nextId", candidate.nextId_)
        || !getInt(root, "nextInputNumber", inputNumber) || inputNumber < 1
        || !getInt(root, "nextAuxNumber", auxNumber) || auxNumber < 1
        || !getArray(root, "inputs", inputs) || !getArray(root, "auxiliaries", auxiliaries)
        || !getObject(root, "main", main)) return fail(error, QStringLiteral("JSON 版本或字段无效"));
    candidate.nextInputNumber_ = static_cast<std::uint32_t>(inputNumber);
    candidate.nextAuxNumber_ = static_cast<std::uint32_t>(auxNumber);
    if (root.contains("monitorFaderPosition")
        && !getNumber(root, "monitorFaderPosition", candidate.monitorFaderPosition_))
        return fail(error, QStringLiteral("监听推子 JSON 无效"));
    if (root.contains("monitorLayout")) {
        int layout = 0;
        if (!getInt(root, "monitorLayout", layout))
            return fail(error, QStringLiteral("监听布局 JSON 无效"));
        candidate.monitorLayout_ = static_cast<MixerLayout>(layout);
    }
    if (!readStrip(main, candidate.main_)) return fail(error, QStringLiteral("MAIN JSON 无效"));
    for (const auto& item : inputs) {
        MixerStripState strip;
        if (!item.isObject() || !readStrip(item.toObject(), strip))
            return fail(error, QStringLiteral("CH JSON 无效"));
        candidate.inputs_.push_back(std::move(strip));
    }
    for (const auto& item : auxiliaries) {
        MixerStripState strip;
        if (!item.isObject() || !readStrip(item.toObject(), strip))
            return fail(error, QStringLiteral("AUX JSON 无效"));
        candidate.auxiliaries_.push_back(std::move(strip));
    }
    if (!candidate.validate(error)) return false;
    *this = std::move(candidate);
    return true;
}

}
