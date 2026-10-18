// SPDX-License-Identifier: AGPL-3.0-only
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <WinJACKNexus/Common/ClassicMixer.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace WinJACKNexus::Common::ClassicMixer
{
namespace
{
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

bool validEq(const Equalizer &eq)
{
    return inRange(eq.low, -12, 12) && inRange(eq.mid, -12, 12) && inRange(eq.high, -12, 12) &&
           inRange(eq.frequency, 100, 8000);
}

QJsonObject eqJson(const Equalizer &eq)
{
    return {{"enabled", eq.enabled},
            {"low", eq.low},
            {"mid", eq.mid},
            {"high", eq.high},
            {"frequency", eq.frequency}};
}

bool readNumber(const QJsonObject &json, const char *key, double &value)
{
    const auto entry = json.value(QLatin1String(key));
    if (!entry.isDouble() || !std::isfinite(entry.toDouble()))
        return false;
    value = entry.toDouble();
    return true;
}

bool readBool(const QJsonObject &json, const char *key, bool &value)
{
    const auto entry = json.value(QLatin1String(key));
    if (!entry.isBool())
        return false;
    value = entry.toBool();
    return true;
}

bool readEq(const QJsonValue &value, Equalizer &eq)
{
    if (!value.isObject())
        return false;
    const auto json = value.toObject();
    return readBool(json, "enabled", eq.enabled) && readNumber(json, "low", eq.low) &&
           readNumber(json, "mid", eq.mid) && readNumber(json, "high", eq.high) &&
           readNumber(json, "frequency", eq.frequency) && validEq(eq);
}

bool validName(const QString &name)
{
    // Leave room for client prefix and channel suffix within JACK's port name
    // limit.
    if (name.trimmed().isEmpty() || name.toUtf8().size() > 96 || name.contains(':'))
        return false;
    for (const auto character : name)
    {
        if (character.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

Coefficients band(double frequency, double db, double rate, int kind)
{
    frequency = std::min(frequency, rate * 0.45);
    const double a = std::pow(10.0, db / 40.0);
    const double w = 2 * std::numbers::pi * frequency / rate;
    const double c = std::cos(w);
    const double s = std::sin(w);
    double b0, b1, b2, a0, a1, a2;
    if (kind == 1)
    {
        const double alpha = s / 2; // Q = 1
        b0 = 1 + alpha * a;
        b1 = -2 * c;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * c;
        a2 = 1 - alpha / a;
    }
    else
    {
        const double beta = std::sqrt(2 * a) * s; // RBJ shelf slope = 1
        const double polarity = kind == 0 ? 1 : -1;
        b0 = a * ((a + 1) - polarity * (a - 1) * c + beta);
        b1 = 2 * polarity * a * ((a - 1) - polarity * (a + 1) * c);
        b2 = a * ((a + 1) - polarity * (a - 1) * c - beta);
        a0 = (a + 1) + polarity * (a - 1) * c + beta;
        a1 = -2 * polarity * ((a - 1) + polarity * (a + 1) * c);
        a2 = (a + 1) + polarity * (a - 1) * c - beta;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

EqConfiguration eqConfiguration(const Equalizer &eq, double rate)
{
    return {{band(100, eq.low, rate, 0), band(eq.frequency, eq.mid, rate, 1),
             band(10000, eq.high, rate, 2)},
            eq.enabled};
}

float finiteSample(const float *channel, std::uint32_t frame)
{
    const float sample = channel ? channel[frame] : 0;
    return std::isfinite(sample) ? sample : 0;
}

void measure(std::array<float, 2> &peaks, const std::array<double, 2> &samples)
{
    for (int side = 0; side < 2; ++side)
        peaks[side] = std::max(peaks[side], static_cast<float>(std::abs(samples[side])));
}
} // namespace

State State::create(int layout)
{
    State state;
    state.layout = layout;
    if (layout != 8 && layout != 16 && layout != 24)
        return state;
    const int mono = layout == 8 ? 6 : layout == 16 ? 8 : 16;
    const int stereo = layout == 8 ? 1 : 4;
    for (int index = 0; index < mono + stereo; ++index)
    {
        Input input;
        input.stereo = index >= mono;
        input.name = input.stereo ? QStringLiteral("Stereo %1").arg(index - mono + 1)
                                  : QStringLiteral("Mono %1").arg(index + 1);
        state.inputs.push_back(input);
    }
    return state;
}

int State::audioInputCount() const noexcept
{
    int count = 0;
    for (const auto &input : inputs)
        count += input.stereo ? 2 : 1;
    return count;
}

bool State::validate(QString *error) const
{
    if (layout != 8 && layout != 16 && layout != 24)
        return fail(error, "Invalid layout");
    const auto expected = create(layout);
    if (inputs.size() != expected.inputs.size())
        return fail(error, "Invalid strip count");
    QSet<QString> names;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto &input = inputs[index];
        if (input.stereo != expected.inputs[index].stereo || !validName(input.name) ||
            names.contains(input.name.toCaseFolded()))
            return fail(error, "Invalid or duplicate channel name");
        names.insert(input.name.toCaseFolded());
        if (!validEq(input.eq) || !inRange(input.gain, -12, 12) ||
            !inRange(input.fader, silenceDb, 12) || !inRange(input.pan, -1, 1) ||
            (input.listen != Listen::off && input.listen != Listen::afl &&
             input.listen != Listen::pfl))
            return fail(error, "Invalid channel parameters");
        for (double send : input.sends)
        {
            if (!inRange(send, silenceDb, 12))
                return fail(error, "Invalid AUX send");
        }
    }
    QSet<QString> portNames;
    for (const auto &port : ports(*this))
    {
        const auto name = port.name.toCaseFolded();
        if (portNames.contains(name))
            return fail(error, "Duplicate audio port name");
        portNames.insert(name);
    }
    if (!validEq(masterEq) || !inRange(masterFader, silenceDb, 12) ||
        !inRange(monitorFader, silenceDb, 12))
        return fail(error, "Invalid bus parameters");
    for (double gain : auxMaster)
    {
        if (!inRange(gain, silenceDb, 12))
            return fail(error, "Invalid AUX master");
    }
    if (presets.size() > 128)
        return fail(error, "Too many presets");
    QSet<QString> presetIds;
    for (const auto &preset : presets)
    {
        if (preset.id.isEmpty() || preset.name.trimmed().isEmpty() || preset.name.size() > 128 ||
            presetIds.contains(preset.id) || preset.payload.size() > 131072)
            return fail(error, "Invalid preset record");
        presetIds.insert(preset.id);
    }
    if (!selectedPreset.isEmpty() && !presetIds.contains(selectedPreset))
        return fail(error, "Unknown selected preset");
    return true;
}

QJsonObject State::toJson(bool includePresets) const
{
    QJsonArray strips;
    for (const auto &input : inputs)
    {
        strips.append(QJsonObject{{"name", input.name},
                                  {"stereo", input.stereo},
                                  {"gain", input.gain},
                                  {"eq", eqJson(input.eq)},
                                  {"sends", QJsonArray{input.sends[0], input.sends[1]}},
                                  {"pre", QJsonArray{input.pre[0], input.pre[1]}},
                                  {"enabled", QJsonArray{input.enabled[0], input.enabled[1]}},
                                  {"pan", input.pan},
                                  {"fader", input.fader},
                                  {"mix", input.mix},
                                  {"mute", input.mute},
                                  {"solo", input.solo},
                                  {"listen", static_cast<int>(input.listen)}});
    }
    QJsonObject result{{"schemaVersion", 1},
                       {"layoutId", layout},
                       {"inputs", strips},
                       {"masterEq", eqJson(masterEq)},
                       {"auxMaster", QJsonArray{auxMaster[0], auxMaster[1]}},
                       {"masterFader", masterFader},
                       {"monitorFader", monitorFader},
                       {"masterMute", masterMute},
                       {"postMeter", postMeter},
                       {"layoutChosen", layoutChosen}};
    if (includePresets)
    {
        QJsonArray saved;
        for (const auto &preset : presets)
            saved.append(
                QJsonObject{{"id", preset.id},
                            {"name", preset.name},
                            {"payloadBase64", QString::fromLatin1(preset.payload.toBase64())}});
        result.insert("presets", saved);
        result.insert("selectedPreset", selectedPreset);
    }
    return result;
}

bool State::fromJson(const QJsonObject &json, State &result, QString *error)
{
    if (json.value("schemaVersion") != QJsonValue(1) || !json.value("layoutId").isDouble() ||
        !json.value("inputs").isArray())
        return fail(error, "Unsupported state schema");
    const double layout = json.value("layoutId").toDouble();
    if (layout != 8 && layout != 16 && layout != 24)
        return fail(error, "Invalid layout");
    State candidate = create(static_cast<int>(layout));
    const auto strips = json.value("inputs").toArray();
    if (strips.size() != candidate.inputs.size())
        return fail(error, "Invalid strip count");
    for (qsizetype index = 0; index < strips.size(); ++index)
    {
        auto &input = candidate.inputs[index];
        const auto strip = strips[index].toObject();
        double listen = 0;
        if (!strip.value("name").isString() || !readBool(strip, "stereo", input.stereo) ||
            !readNumber(strip, "gain", input.gain) || !readEq(strip.value("eq"), input.eq) ||
            !readNumber(strip, "pan", input.pan) || !readNumber(strip, "fader", input.fader) ||
            !readBool(strip, "mix", input.mix) || !readBool(strip, "mute", input.mute) ||
            !readBool(strip, "solo", input.solo) || !readNumber(strip, "listen", listen) ||
            (listen != 0 && listen != 1 && listen != 2))
            return fail(error, "Malformed input state");
        input.name = strip.value("name").toString();
        input.listen = static_cast<Listen>(static_cast<int>(listen));
        const auto sends = strip.value("sends").toArray();
        const auto pre = strip.value("pre").toArray();
        const auto enabled = strip.value("enabled").toArray();
        if (sends.size() != 2 || pre.size() != 2 || enabled.size() != 2)
            return fail(error, "Malformed AUX state");
        for (int aux = 0; aux < 2; ++aux)
        {
            if (!sends[aux].isDouble() || !pre[aux].isBool() || !enabled[aux].isBool())
                return fail(error, "Malformed AUX values");
            input.sends[aux] = sends[aux].toDouble();
            input.pre[aux] = pre[aux].toBool();
            input.enabled[aux] = enabled[aux].toBool();
        }
    }
    const auto aux = json.value("auxMaster").toArray();
    if (aux.size() != 2 || !aux[0].isDouble() || !aux[1].isDouble() ||
        !readEq(json.value("masterEq"), candidate.masterEq) ||
        !readNumber(json, "masterFader", candidate.masterFader) ||
        !readNumber(json, "monitorFader", candidate.monitorFader) ||
        !readBool(json, "masterMute", candidate.masterMute) ||
        !readBool(json, "postMeter", candidate.postMeter) ||
        !readBool(json, "layoutChosen", candidate.layoutChosen))
        return fail(error, "Malformed bus state");
    candidate.auxMaster = {aux[0].toDouble(), aux[1].toDouble()};
    if (json.contains("presets"))
    {
        if (!json.value("presets").isArray() || json.value("presets").toArray().size() > 128 ||
            !json.value("selectedPreset").isString())
            return fail(error, "Malformed preset library");
        for (const auto &value : json.value("presets").toArray())
        {
            const auto record = value.toObject();
            if (!record.value("id").isString() || !record.value("name").isString() ||
                !record.value("payloadBase64").isString())
                return fail(error, "Malformed preset record");
            const auto encoded = record.value("payloadBase64").toString().toLatin1();
            const auto decoded =
                QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || encoded.size() > 180000)
                return fail(error, "Invalid preset Base64");
            candidate.presets.push_back(
                {record.value("id").toString(), record.value("name").toString(), decoded.decoded});
        }
        candidate.selectedPreset = json.value("selectedPreset").toString();
    }
    if (!candidate.validate(error))
        return false;
    result = std::move(candidate);
    return true;
}

bool State::savePreset(const QString &name, const QString &overwriteId)
{
    if (name.trimmed().isEmpty() || name.size() > 128)
        return false;
    auto found = std::find_if(presets.begin(), presets.end(),
                              [&](const Preset &preset) { return preset.id == overwriteId; });
    const auto payload = QJsonDocument(toJson(false)).toJson(QJsonDocument::Compact);
    if (found != presets.end())
    {
        found->name = name;
        found->payload = payload;
        selectedPreset = found->id;
    }
    else
    {
        if (!overwriteId.isEmpty() || presets.size() >= 128)
            return false;
        selectedPreset = QUuid::createUuid().toString(QUuid::WithoutBraces);
        presets.push_back({selectedPreset, name, payload});
    }
    return true;
}

bool State::applyPreset(const QString &id, State &result, QString *error) const
{
    const auto found = std::find_if(presets.begin(), presets.end(),
                                    [&](const Preset &preset) { return preset.id == id; });
    if (found == presets.end())
        return fail(error, "Preset not found");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(found->payload, &parseError);
    State candidate;
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().contains("presets") || !fromJson(document.object(), candidate, error))
        return fail(error, "Invalid preset payload");
    if (candidate.layout != layout)
        return fail(error, "Preset layout mismatch");
    candidate.presets = presets;
    candidate.selectedPreset = id;
    candidate.layoutChosen = layoutChosen;
    result = std::move(candidate);
    return true;
}

std::vector<Port> ports(const State &state)
{
    std::vector<Port> result;
    for (std::size_t index = 0; index < state.inputs.size(); ++index)
    {
        const auto &input = state.inputs[index];
        const auto id = static_cast<std::uint32_t>(index * 2 + 1);
        result.push_back({id, true, input.name + (input.stereo ? "_L" : "_M")});
        if (input.stereo)
            result.push_back({id + 1, true, input.name + "_R"});
    }
    const char *names[]{"Master_L", "Master_R", "AUX1_L",    "AUX1_R",
                        "AUX2_L",   "AUX2_R",   "Monitor_L", "Monitor_R"};
    for (int index = 0; index < outputChannels; ++index)
        result.push_back(
            {static_cast<std::uint32_t>(101 + index), false, QLatin1String(names[index])});
    return result;
}

double gainForDb(double db) noexcept
{
    return db <= silenceDb ? 0 : std::pow(10.0, db / 20.0);
}

double faderDb(double position) noexcept
{
    return position >= 0.995 ? silenceDb : 12 - 84 * std::clamp(position / 0.95, 0.0, 1.0);
}

double faderPosition(double db) noexcept
{
    return db <= silenceDb ? 1 : std::clamp((12 - db) / 84 * 0.95, 0.0, 0.95);
}

Configuration configuration(const State &state, double sampleRate)
{
    Configuration result;
    result.inputCount = static_cast<int>(state.inputs.size());
    result.masterEq = eqConfiguration(state.masterEq, sampleRate);
    result.masterFader = gainForDb(state.masterFader);
    result.monitorFader = gainForDb(state.monitorFader);
    result.masterMute = state.masterMute;
    for (int aux = 0; aux < 2; ++aux)
        result.auxMaster[aux] = gainForDb(state.auxMaster[aux]);
    for (int index = 0; index < result.inputCount; ++index)
    {
        const auto &input = state.inputs[index];
        auto &target = result.inputs[index];
        target.eq = eqConfiguration(input.eq, sampleRate);
        target.gain = gainForDb(input.gain);
        target.fader = gainForDb(input.fader);
        target.stereo = input.stereo;
        target.mix = input.mix;
        target.mute = input.mute;
        target.solo = input.solo;
        target.listen = input.listen;
        target.pre = input.pre;
        if (input.stereo)
            target.pan = {input.pan <= 0 ? 1 : 1 - input.pan, input.pan >= 0 ? 1 : 1 + input.pan};
        else
            target.pan = {std::cos((input.pan + 1) * std::numbers::pi / 4),
                          std::sin((input.pan + 1) * std::numbers::pi / 4)};
        for (int aux = 0; aux < 2; ++aux)
            target.sends[aux] = input.enabled[aux] ? gainForDb(input.sends[aux]) : 0;
    }
    return result;
}

bool Processor::prepare(double sampleRate, std::uint32_t maximumFrames,
                        const Configuration &settings) noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000 ||
        maximumFrames == 0 || maximumFrames > 65536 || settings.inputCount < 1 ||
        settings.inputCount > maximumInputs)
        return false;
    maximumFrames_ = maximumFrames;
    smoothing_ = 1 - std::exp(-1 / (sampleRate * 0.005));
    target_ = current_ = settings;
    reset();
    return true;
}

void Processor::setConfiguration(const Configuration &settings) noexcept
{
    // Topology changes require prepare; ordinary parameter changes preserve
    // histories.
    if (settings.inputCount == target_.inputCount)
    {
        for (int index = 0; index < settings.inputCount; ++index)
        {
            if (settings.inputs[index].eq.enabled != target_.inputs[index].eq.enabled)
                history_[index] = {};
        }
        if (settings.masterEq.enabled != target_.masterEq.enabled)
            history_[maximumInputs] = {};
        target_ = settings;
    }
}

void Processor::reset() noexcept
{
    history_ = {};
    levels_ = {};
    current_ = target_;
}

double Processor::Filter::process(double sample, const Coefficients &c) noexcept
{
    const double output = c.b0 * sample + z1;
    z1 = c.b1 * sample - c.a1 * output + z2;
    z2 = c.b2 * sample - c.a2 * output;
    return output;
}

double Processor::equalize(double value, int side, EqHistory &history,
                           const EqConfiguration &eq) noexcept
{
    if (eq.enabled)
    {
        for (int band = 0; band < 3; ++band)
            value = history[side][band].process(value, eq.bands[band]);
    }
    return value;
}

void Processor::process(const float *const *inputs, float *const *outputs,
                        std::uint32_t frames) noexcept
{
    levels_ = {};
    if (!outputs)
        return;
    for (int channel = 0; channel < outputChannels; ++channel)
    {
        if (outputs[channel])
            std::fill_n(outputs[channel], frames, 0);
    }
    if (!maximumFrames_ || frames > maximumFrames_)
        return;
    const bool soloActive =
        std::any_of(target_.inputs.begin(), target_.inputs.begin() + target_.inputCount,
                    [](const auto &input) { return input.solo; });
    const bool listening =
        std::any_of(target_.inputs.begin(), target_.inputs.begin() + target_.inputCount,
                    [](const auto &input) { return input.listen != Listen::off; });
    const auto smooth = [this](double &value, double target) {
        value += (target - value) * smoothing_;
        if (std::abs(value - target) < 1e-12)
            value = target;
        return value;
    };
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        std::array<double, 2> master{}, listen{};
        std::array<std::array<double, 2>, 2> aux{};
        int audioIndex = 0;
        for (int index = 0; index < target_.inputCount; ++index)
        {
            const auto &target = target_.inputs[index];
            auto &current = current_.inputs[index];
            std::array<double, 2> raw{finiteSample(inputs ? inputs[audioIndex] : nullptr, frame),
                                      0};
            ++audioIndex;
            raw[1] = target.stereo ? finiteSample(inputs ? inputs[audioIndex++] : nullptr, frame)
                                   : raw[0];
            measure(levels_.input[index], raw);
            const auto gain = smooth(current.gain, target.gain);
            std::array<double, 2> gained{raw[0] * gain, raw[1] * gain};
            if (target.listen != Listen::off)
            {
                const auto &source = target.listen == Listen::afl ? gained : raw;
                const double listenGain = target.stereo ? 1 : std::numbers::sqrt2 / 2;
                for (int side = 0; side < 2; ++side)
                    listen[side] += source[side] * listenGain;
            }
            std::array<double, 2> pre{}, post{};
            const double fader = smooth(current.fader, target.fader);
            for (int side = 0; side < 2; ++side)
            {
                const double eq = equalize(gained[side], side, history_[index], target.eq);
                const double pan = smooth(current.pan[side], target.pan[side]);
                pre[side] = eq * pan;
                post[side] = pre[side] * fader;
                if (!target.mute && target.mix && (!soloActive || target.solo))
                    master[side] += post[side];
                const double meter = target.stereo ? post[side] : eq * fader;
                levels_.post[index][side] =
                    std::max(levels_.post[index][side],
                             static_cast<float>(target.mute ? 0 : std::abs(meter)));
            }
            for (int bus = 0; bus < 2; ++bus)
            {
                const double send = smooth(current.sends[bus], target.sends[bus]);
                for (int side = 0; side < 2; ++side)
                    aux[bus][side] +=
                        target.mute ? 0 : (target.pre[bus] ? pre[side] : post[side]) * send;
            }
        }
        const int masterIndex = target_.inputCount;
        measure(levels_.input[masterIndex], master);
        const double masterFader = smooth(current_.masterFader, target_.masterFader);
        const double monitorFader = smooth(current_.monitorFader, target_.monitorFader);
        for (int side = 0; side < 2; ++side)
        {
            const double equalized =
                equalize(master[side], side, history_[maximumInputs], target_.masterEq);
            master[side] = target_.masterMute ? 0 : equalized * masterFader;
        }
        measure(levels_.post[masterIndex], master);
        const auto monitor = listening ? listen : master;
        measure(levels_.input[masterIndex + 1], monitor);
        const std::array<double, 2> monitored{monitor[0] * monitorFader, monitor[1] * monitorFader};
        measure(levels_.post[masterIndex + 1], monitored);
        for (int bus = 0; bus < 2; ++bus)
        {
            const auto gain = smooth(current_.auxMaster[bus], target_.auxMaster[bus]);
            for (int side = 0; side < 2; ++side)
            {
                if (outputs[2 + 2 * bus + side])
                    outputs[2 + 2 * bus + side][frame] = static_cast<float>(aux[bus][side] * gain);
            }
        }
        for (int side = 0; side < 2; ++side)
        {
            if (outputs[side])
                outputs[side][frame] = static_cast<float>(master[side]);
            if (outputs[6 + side])
                outputs[6 + side][frame] = static_cast<float>(monitored[side]);
        }
    }
}
} // namespace WinJACKNexus::Common::ClassicMixer
