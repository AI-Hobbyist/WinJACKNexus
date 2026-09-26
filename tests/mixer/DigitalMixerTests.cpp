/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/DigitalMixerState.hpp>
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/Common/MixerPolarAnalysis.hpp>
#include <WinJACKNexus/Common/MixerPolarCurve.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <string_view>
#include <vector>
#include <array>
#include <cmath>

using namespace WinJACKNexus::Common;
namespace {
bool check(bool value, const char* message)
{
    if (!value) std::fprintf(stderr, "FAILED: %s\n", message);
    return value;
}

bool rejects(DigitalMixerState& state, const QByteArray& bytes, const char* message)
{
    const auto before = state.toJson();
    QString error;
    return check(!state.loadJson(bytes, &error) && !error.isEmpty() && state.toJson() == before, message);
}

QByteArray edit(const QByteArray& bytes, const auto& mutation)
{
    auto root = QJsonDocument::fromJson(bytes).object();
    mutation(root);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool portsAndIdentity()
{
    bool ok = true;
    DigitalMixerState state;
    ok &= check(state.ports().size() == 2 && state.ports()[0].name == QStringLiteral("MAIN_L")
                 && state.ports()[1].name == QStringLiteral("MAIN_R"), "MAIN is permanent stereo output");
    int number = 1;
    for (const auto layout : {MixerLayout::mono, MixerLayout::stereo, MixerLayout::surround51, MixerLayout::surround71}) {
        const auto ch = state.add(MixerStripKind::input, layout);
        const auto aux = state.add(MixerStripKind::aux, layout);
        for (const auto id : {ch, aux}) {
            const auto* strip = state.strip(id);
            ok &= check(strip && strip->faderPosition == mixerUnityFaderPosition
                            && strip->inputGainDb == 0.0 && !strip->mute && !strip->solo
                            && strip->mainPanner.classicPan == 0.0
                            && strip->mainPanner.width == 1.0
                            && std::all_of(strip->mainPanner.points.cbegin(),
                                           strip->mainPanner.points.cend(),
                                           [](const auto& point) { return point.levelDb == 0.0; })
                            && strip->fxChainEnabled && strip->effects.size() == 1
                            && strip->effects.front().moduleId == QStringLiteral("parametric-eq")
                            && strip->effects.front().enabled,
                        "new Input and AUX start at unity with flat active Parametric EQ");
        }
        const auto labels = mixerChannelLabels(layout);
        int chCount = 0, auxCount = 0;
        for (const auto& port : state.ports()) {
            if (port.stripId == ch) {
                ok &= check(port.name == QStringLiteral("CH%1_%2").arg(number).arg(labels[port.channelIndex])
                                && port.direction == MixerPortDirection::input, "CH name and input direction");
                ++chCount;
            }
            if (port.stripId == aux) {
                ok &= check(port.name == QStringLiteral("AUX%1_%2").arg(number).arg(labels[port.channelIndex])
                                && port.direction == MixerPortDirection::output, "AUX name and output direction");
                ++auxCount;
            }
        }
        ok &= check(chCount == labels.size() && auxCount == labels.size(), "all layout ports generated");
        ++number;
    }
    ok &= check(state.ports().size() == 36, "exact port count for four layouts");
    const auto middle = state.inputs()[1].id;
    const auto later = state.inputs()[2].id;
    ok &= check(state.remove(middle) && state.strip(later)->number == 3, "middle deletion preserves number");
    const auto clone = state.clone(later);
    ok &= check(clone && clone != later && state.strip(clone)->number == 5, "clone gets unique ID and number");
    ok &= check(!state.remove(state.main().id) && !state.setLayout(state.main().id, MixerLayout::mono),
                "MAIN cannot be removed or changed");
    ok &= check(!state.add(MixerStripKind::main, MixerLayout::stereo)
                && !state.add(MixerStripKind::input, static_cast<MixerLayout>(3))
                && !state.setLayout(later, static_cast<MixerLayout>(3)), "invalid layout and kind rejected");
    ok &= check(state.setLayout(later, MixerLayout::mono) && state.strip(later)->number == 3,
                "layout change retains identity");
    return ok && check(state.validate(), "model validates after mutations");
}

bool jsonAndRoutes()
{
    bool ok = true;
    DigitalMixerState state;
    const auto ch = state.add(MixerStripKind::input, MixerLayout::surround71);
    const auto aux1 = state.add(MixerStripKind::aux, MixerLayout::stereo);
    const auto aux2 = state.add(MixerStripKind::aux, MixerLayout::surround51);
    auto* input = state.strip(ch);
    input->name = QStringLiteral("Vocals");
    input->comment = QStringLiteral("Live input");
    input->color = QColor(QStringLiteral("#f04382"));
    input->inputGainDb = 4.5;
    input->faderPosition = 1.0;
    input->mute = input->solo = input->auxSendsEnabled = true;
    input->mixToMain = false;
    input->mainPanner.points[6].levelDb = -12.5;
    input->mainPanner.points[6].influenceRadius = .5;
    input->mainPanner.showPolarCurves = false;
    input->effects.push_back({QStringLiteral("compressor"), false, 3,
                              QJsonObject {{"channels", QJsonArray {1.0, 2.0}}}});
    MixerAuxSend send {aux1, true, false, defaultMixerPanner(MixerLayout::surround71, MixerLayout::stereo)};
    send.panner.points[7].solo = true;
    ok &= check(state.setSend(ch, send), "CH send accepted");
    ok &= check(state.setSend(aux1, {aux2, false, true, defaultMixerPanner(MixerLayout::stereo, MixerLayout::surround51)}),
                "disabled AUX chain route accepted");
    const auto serialized = state.toJson();
    DigitalMixerState restored;
    ok &= check(restored.loadJson(serialized) && restored.toJson() == serialized,
                "all parameter state survives JSON round trip");
    ok &= check(!restored.setSend(aux2, {aux1, true, true,
                    defaultMixerPanner(MixerLayout::surround51, MixerLayout::stereo)}),
                "disabled route participates in cycle check");
    ok &= check(!restored.setSend(aux1, {aux1, true, true,
                     defaultMixerPanner(MixerLayout::stereo, MixerLayout::stereo)}), "self-route rejected");
    const auto aux3 = restored.add(MixerStripKind::aux, MixerLayout::mono);
    ok &= check(restored.setSend(aux2, {aux3, true, true,
                    defaultMixerPanner(MixerLayout::surround51, MixerLayout::mono)}),
                "third AUX stage accepted");
    ok &= check(!restored.setSend(aux3, {aux1, false, true,
                    defaultMixerPanner(MixerLayout::mono, MixerLayout::stereo)}),
                "multi-stage indirect cycle rejected even when closing edge is disabled");
    ok &= rejects(restored, QByteArray("{"), "malformed JSON rolls back");
    ok &= rejects(restored, edit(serialized, [](QJsonObject& root) { root.insert("version", 42); }),
                  "unsupported version rolls back");
    ok &= rejects(restored, edit(serialized, [](QJsonObject& root) {
        auto array = root.value("inputs").toArray(); auto item = array[0].toObject();
        item.insert("layout", 3); array[0] = item; root.insert("inputs", array);
    }), "illegal layout rolls back");
    ok &= rejects(restored, edit(serialized, [aux1](QJsonObject& root) {
        auto array = root.value("inputs").toArray(); auto item = array[0].toObject();
        item.insert("id", QString::number(aux1)); array[0] = item; root.insert("inputs", array);
    }), "duplicate ID rolls back");
    ok &= rejects(restored, edit(serialized, [aux1](QJsonObject& root) {
        auto array = root.value("auxiliaries").toArray(); auto item = array[0].toObject();
        auto sends = item.value("sends").toArray(); auto route = sends[0].toObject();
        route.insert("targetId", QString::number(aux1)); sends[0] = route;
        item.insert("sends", sends); array[0] = item; root.insert("auxiliaries", array);
    }), "self-route in JSON rolls back");
    return ok;
}

bool restoreRegression()
{
    DigitalMixerState state;
    const auto ch = state.add(MixerStripKind::input, MixerLayout::surround71);
    const auto aux1 = state.add(MixerStripKind::aux, MixerLayout::stereo);
    const auto aux2 = state.add(MixerStripKind::aux, MixerLayout::surround51);
    const auto aux3 = state.add(MixerStripKind::aux, MixerLayout::mono);
    state.strip(ch)->name = QStringLiteral("Restore CH");
    state.strip(ch)->mainPanner.points[7].levelDb = -9.0;
    state.strip(ch)->mainPanner.showInputCurve = false;
    state.strip(ch)->fxChainEnabled = true;
    QJsonArray channels;
    for (int index = 0; index < 8; ++index)
        channels.append(QJsonObject {{"gainDb", -1.5 * index}});
    state.strip(ch)->effects.push_back({QStringLiteral("graphic-eq"), true, 2,
        QJsonObject {{"channels", channels}, {"snapshot", QStringLiteral("eight-channel-state")}}});
    auto chToFirst = MixerAuxSend {aux1, true, true,
        defaultMixerPanner(MixerLayout::surround71, MixerLayout::stereo)};
    chToFirst.panner.points[6].levelDb = -11.0;
    chToFirst.panner.showOutputCurve = false;
    auto firstToSecond = MixerAuxSend {aux2, true, false,
        defaultMixerPanner(MixerLayout::stereo, MixerLayout::surround51)};
    firstToSecond.panner.points[1].influenceRadius = .5;
    auto chToThird = MixerAuxSend {aux3, false, true,
        defaultMixerPanner(MixerLayout::surround71, MixerLayout::mono)};
    bool ok = check(state.setSend(ch, chToFirst)
        && state.setSend(aux1, firstToSecond)
        && state.setSend(ch, chToThird),
        "M7 restore fixture contains an ordered AUX chain and a disabled independent send");
    const auto serialized = state.toJson();
    const auto ports = state.ports();
    DigitalMixerState restored;
    ok &= check(restored.loadJson(serialized) && restored.toJson() == serialized,
        "M7 complete project restore preserves ordered sends, panners and eight FX channel values");
    const auto recoveredPorts = restored.ports();
    bool samePorts = ports.size() == recoveredPorts.size();
    for (std::size_t index = 0; samePorts && index < ports.size(); ++index)
        samePorts &= ports[index].name == recoveredPorts[index].name
            && ports[index].stripId == recoveredPorts[index].stripId
            && ports[index].direction == recoveredPorts[index].direction;
    ok &= check(samePorts && recoveredPorts.back().name == QStringLiteral("MAIN_R"),
        "M7 restore retains stable JACK port names, directions, strip IDs and MAIN");
    auto cyclic = state;
    cyclic.strip(aux2)->sends.push_back({aux1, false, true,
        defaultMixerPanner(MixerLayout::surround51, MixerLayout::stereo)});
    ok &= rejects(restored, cyclic.toJson(),
        "M7 indirect AUX cycle in a project rolls back all restored state");
    ok &= rejects(restored, edit(serialized, [](QJsonObject& root) {
        auto inputs = root.value("inputs").toArray();
        auto item = inputs[0].toObject();
        auto effects = item.value("effects").toArray();
        auto effect = effects[0].toObject();
        effect.insert("preset", -2);
        effects[0] = effect;
        item.insert("effects", effects);
        inputs[0] = item;
        root.insert("inputs", inputs);
    }), "M7 incompatible FX state rejects the project without partial restore");
    const auto copied = restored.clone(aux1);
    bool noSelf = copied != 0;
    for (const auto& send : restored.strip(copied)->sends) noSelf &= send.targetId != copied;
    ok &= check(noSelf && restored.validate(), "M7 AUX clone cannot create a self-send");
    ok &= check(restored.remove(aux1) && restored.validate(),
        "M7 removing a middle AUX leaves a valid project graph");
    const auto* source = restored.strip(ch);
    bool retainedThird = false, removedFirst = true;
    for (const auto& send : source->sends) {
        retainedThird |= send.targetId == aux3;
        removedFirst &= send.targetId != aux1;
    }
    ok &= check(retainedThird && removedFirst && restored.strip(aux2) && restored.strip(copied),
        "M7 AUX deletion clears related sends only and preserves other routes and strips");
    return ok;
}

bool deterministicAudio()
{
    constexpr double unityFader = .95 * 12.0 / 84.0;
    constexpr std::size_t frames = 8;
    const auto near = [](float actual, float expected) { return std::abs(actual - expected) < .0005F; };
    bool ok = true;

    DigitalMixerState identity;
    const auto identityInput = identity.add(MixerStripKind::input, MixerLayout::stereo);
    identity.strip(identityInput)->faderPosition = unityFader;
    identity.strip(identity.main().id)->faderPosition = unityFader;
    DigitalMixerProcessor identityProcessor;
    std::array<float, frames> identityLeft, identityRight, identityOutputLeft, identityOutputRight;
    identityLeft.fill(.25F); identityRight.fill(-.375F);
    std::array<const float*, 2> identityInputs {identityLeft.data(), identityRight.data()};
    std::array<float*, 2> identityOutputs {identityOutputLeft.data(), identityOutputRight.data()};
    ok &= check(identityProcessor.configure(identity, frames)
                    && identityProcessor.process(identityInputs, identityOutputs, frames)
                    && near(identityOutputLeft[0], .25F) && near(identityOutputRight[0], -.375F),
                "stereo unit matrix preserves both signed channels");

    DigitalMixerState state;
    const auto first = state.add(MixerStripKind::input, MixerLayout::stereo);
    const auto second = state.add(MixerStripKind::input, MixerLayout::stereo);
    state.strip(first)->faderPosition = unityFader;
    state.strip(second)->faderPosition = unityFader;
    state.strip(state.main().id)->faderPosition = unityFader;
    DigitalMixerProcessor processor;
    ok &= check(processor.configure(state, frames), "offline processor prepares stereo input graph");
    std::array<float, frames> positive, negative, silence, mainL, mainR;
    positive.fill(.5F); negative.fill(-.5F); silence.fill(0.0F);
    std::array<const float*, 4> inputs {positive.data(), silence.data(), negative.data(), silence.data()};
    std::array<float*, 2> outputs {mainL.data(), mainR.data()};
    const auto settled = [&](const auto& source, const auto& destination) {
        for (int block = 0; block < 9; ++block)
            if (!processor.process(source, destination, frames)) return false;
        return true;
    };
    ok &= check(processor.process(inputs, outputs, frames) && near(mainL[0], 0.0F),
                "opposite-polarity samples cancel in MAIN summation");
    state.strip(first)->mixToMain = false;
    ok &= check(processor.configure(state, frames) && processor.process(inputs, outputs, frames)
                    && near(mainL[0], -.5F) && near(mainR[0], 0.0F),
                "MIX excludes only its own MAIN contribution");
    state.strip(second)->mute = true;
    ok &= check(processor.configure(state, frames) && processor.process(inputs, outputs, frames)
                    && mainL[0] < -.01F && mainL[0] > -.5F,
                "MUTE transition ramps instead of jumping to zero");
    ok &= check(settled(inputs, outputs)
                    && near(mainL[0], 0.0F), "MUTE zeros its audible path");
    state.strip(second)->mute = false;
    state.strip(second)->inputGainDb = 6.0;
    ok &= check(processor.configure(state, frames) && settled(inputs, outputs)
                    && near(mainL[0], -.5F * std::pow(10.0F, 6.0F / 20.0F)),
                "input gain scales signed samples before the fader");

    DigitalMixerState routes;
    const auto mono = routes.add(MixerStripKind::input, MixerLayout::mono);
    const auto aux1 = routes.add(MixerStripKind::aux, MixerLayout::stereo);
    const auto aux2 = routes.add(MixerStripKind::aux, MixerLayout::stereo);
    for (const auto id : {mono, aux1, aux2, routes.main().id})
        routes.strip(id)->faderPosition = unityFader;
    routes.strip(mono)->mixToMain = false;
    routes.strip(aux1)->mixToMain = false;
    routes.strip(aux2)->mixToMain = false;
    routes.strip(mono)->auxSendsEnabled = true;
    routes.strip(aux1)->auxSendsEnabled = true;
    auto firstSend = MixerAuxSend {aux1, true, true,
        defaultMixerPanner(MixerLayout::mono, MixerLayout::stereo)};
    auto secondSend = MixerAuxSend {aux2, true, true,
        defaultMixerPanner(MixerLayout::stereo, MixerLayout::stereo)};
    ok &= check(routes.setSend(mono, firstSend) && routes.setSend(aux1, secondSend),
                "CH to AUX1 to AUX2 chain is valid");
    std::array<float, frames> monoInput, aux1L, aux1R, aux2L, aux2R;
    monoInput.fill(1.0F);
    std::array<const float*, 1> routeInputs {monoInput.data()};
    std::array<float*, 6> routeOutputs {aux1L.data(), aux1R.data(), aux2L.data(), aux2R.data(),
                                         mainL.data(), mainR.data()};
    ok &= check(processor.configure(routes, frames) && processor.process(routeInputs, routeOutputs, frames)
                    && near(aux1L[0], std::sqrt(.5F)) && near(aux2L[0], std::sqrt(.5F))
                    && near(mainL[0], 0.0F),
                "multi-stage AUX signal reaches downstream output while MIX excludes MAIN");
    routes.strip(mono)->faderPosition = 1.0;
    ok &= check(processor.configure(routes, frames) && processor.process(routeInputs, routeOutputs, frames)
                    && near(aux1L[0], std::sqrt(.5F)),
                "Pre send survives a minus-infinity source fader");
    firstSend.preFader = false;
    ok &= check(routes.setSend(mono, firstSend) && processor.configure(routes, frames)
                    && settled(routeInputs, routeOutputs) && near(aux1L[0], 0.0F),
                "Post send reaches true zero at the minus-infinity fader endpoint");
    firstSend.preFader = true;
    routes.strip(mono)->mute = true;
    ok &= check(routes.setSend(mono, firstSend) && processor.configure(routes, frames)
                    && processor.process(routeInputs, routeOutputs, frames) && near(aux1L[0], 0.0F),
                "MUTE closes both Pre and Post AUX sends");
    routes.strip(mono)->mute = false;
    routes.strip(mono)->faderPosition = unityFader;
    firstSend.panner.classicPan = -1.0;
    ok &= check(routes.setSend(mono, firstSend) && processor.configure(routes, frames)
                    && processor.process(routeInputs, routeOutputs, frames)
                    && near(aux1L[0], 1.0F) && near(aux1R[0], 0.0F),
                "mono classic Pan endpoint reaches only the left channel");
    const auto before = aux1L[0];
    auto invalid = routes;
    invalid.strip(aux2)->sends.push_back({aux1, false, true,
        defaultMixerPanner(MixerLayout::stereo, MixerLayout::stereo)});
    QString error;
    ok &= check(!processor.configure(invalid, frames, &error) && !error.isEmpty()
                    && processor.process(routeInputs, routeOutputs, frames) && near(aux1L[0], before),
                "cyclic update leaves the prior audible graph intact");

    DigitalMixerState surround;
    const auto surroundInput = surround.add(MixerStripKind::input, MixerLayout::surround51);
    const auto surroundAux = surround.add(MixerStripKind::aux, MixerLayout::surround51);
    surround.strip(surroundInput)->auxSendsEnabled = true;
    surround.strip(surroundInput)->faderPosition = unityFader;
    surround.strip(surroundAux)->faderPosition = unityFader;
    surround.strip(surround.main().id)->faderPosition = unityFader;
    auto surroundSend = MixerAuxSend {surroundAux, true, false,
        defaultMixerPanner(MixerLayout::surround51, MixerLayout::surround51)};
    surroundSend.panner.points[3].levelDb = 0;
    ok &= check(surround.setSend(surroundInput, surroundSend), "5.1 spatial send is valid");
    std::array<float, frames> lfe;
    lfe.fill(.4F);
    std::array<const float*, 6> surroundInputs {silence.data(), silence.data(), silence.data(),
                                                 lfe.data(), silence.data(), silence.data()};
    std::array<std::array<float, frames>, 8> surroundOutputData {};
    std::array<float*, 8> surroundOutputs {};
    for (std::size_t i = 0; i < surroundOutputs.size(); ++i)
        surroundOutputs[i] = surroundOutputData[i].data();
    ok &= check(processor.configure(surround, frames)
                    && processor.process(surroundInputs, surroundOutputs, frames)
                    && near(surroundOutputData[3][0], .4F)
                    && near(surroundOutputData[0][0], 0.0F)
                    && near(surroundOutputData[6][0], 0.0F),
                "5.1 LFE follows its explicit AUX channel and never folds into stereo MAIN");
    DigitalMixerState surround71;
    const auto wideInput = surround71.add(MixerStripKind::input, MixerLayout::surround71);
    const auto wideAux = surround71.add(MixerStripKind::aux, MixerLayout::surround71);
    surround71.strip(wideInput)->auxSendsEnabled = true;
    surround71.strip(wideInput)->mixToMain = false;
    surround71.strip(wideAux)->mixToMain = false;
    surround71.strip(wideInput)->faderPosition = unityFader;
    surround71.strip(wideAux)->faderPosition = unityFader;
    surround71.strip(surround71.main().id)->faderPosition = unityFader;
    auto wideSend = MixerAuxSend {wideAux, true, false,
        defaultMixerPanner(MixerLayout::surround71, MixerLayout::surround71)};
    wideSend.panner.points[6].levelDb = 0;
    ok &= check(surround71.setSend(wideInput, wideSend), "7.1 spatial send is valid");
    std::array<const float*, 8> wideInputs {silence.data(), silence.data(), silence.data(),
        silence.data(), silence.data(), silence.data(), lfe.data(), silence.data()};
    std::array<std::array<float, frames>, 10> wideOutputData {};
    std::array<float*, 10> wideOutputs {};
    for (std::size_t i = 0; i < wideOutputs.size(); ++i)
        wideOutputs[i] = wideOutputData[i].data();
    ok &= check(processor.configure(surround71, frames)
                    && processor.process(wideInputs, wideOutputs, frames)
                    && near(wideOutputData[6][0], .4F)
                    && near(wideOutputData[0][0], 0.0F)
                    && near(wideOutputData[8][0], 0.0F),
                "7.1 Lb reaches its own AUX port without leaking into stereo MAIN");
    std::array<float, 32> largerSignal, largerSilence;
    largerSignal.fill(.4F); largerSilence.fill(0.0F);
    std::array<const float*, 8> largerInputs {largerSilence.data(), largerSilence.data(),
        largerSilence.data(), largerSilence.data(), largerSilence.data(), largerSilence.data(),
        largerSignal.data(), largerSilence.data()};
    std::array<std::array<float, 32>, 10> largerOutputData {};
    std::array<float*, 10> largerOutputs {};
    for (std::size_t i = 0; i < largerOutputs.size(); ++i)
        largerOutputs[i] = largerOutputData[i].data();
    ok &= check(processor.configure(surround71, 32)
                    && processor.maxFrames() == 32
                    && processor.process(largerInputs, largerOutputs, 32)
                    && near(largerOutputData[6][31], .4F),
                "processor safely rebuilds preallocated buffers for a larger JACK block");
    ok &= check(processor.configure(surround71, 32, nullptr, 96000)
                    && processor.sampleRate() == 96000
                    && processor.process(largerInputs, largerOutputs, 32)
                    && near(largerOutputData[6][31], .4F),
                "sample-rate reconfiguration preserves rate-independent routing samples");
    return ok;
}

bool polarAnalysisWindows()
{
    MixerPolarAnalyzer analyzer(11, 22, MixerLayout::stereo, MixerLayout::mono, 7, 48000);
    std::array<float, 960> left, right, cancelled;
    left.fill(.5F); right.fill(-.5F); cancelled.fill(0.0F);
    std::array<const float*, 2> input {left.data(), right.data()};
    std::array<const float*, 1> output {cancelled.data()};
    analyzer.push(input, output, 512);
    MixerPolarSnapshot snapshot;
    bool ok = check(!analyzer.readLatest(snapshot), "polar window spans JACK blocks");
    input = {left.data() + 512, right.data() + 512};
    output = {cancelled.data() + 512};
    analyzer.push(input, output, 448);
    ok &= check(analyzer.readLatest(snapshot) && snapshot.valid
                    && snapshot.sourceId == 11 && snapshot.targetId == 22
                    && snapshot.topologyVersion == 7 && snapshot.sampleRate == 48000
                    && snapshot.firstSample == 0 && snapshot.endSample == 960
                    && snapshot.sampleCount == 960
                    && std::abs(std::sqrt(snapshot.inputSumSquares[0] / 960.0) - .5) < .00001
                    && std::abs(std::sqrt(snapshot.inputSumSquares[1] / 960.0) - .5) < .00001
                    && snapshot.outputSumSquares[0] == 0.0
                    && snapshot.inputPeak[0] == .5F && snapshot.outputPeak[0] == 0.0F,
                "polar snapshot measures actual cancelled output and metadata atomically");
    left.fill(0.0F); right.fill(0.0F);
    input = {left.data(), right.data()};
    output = {cancelled.data()};
    analyzer.push(input, output, 960);
    ok &= check(analyzer.readLatest(snapshot) && snapshot.firstSample == 960
                    && snapshot.endSample == 1920 && snapshot.inputSumSquares[0] == 0.0
                    && snapshot.outputSumSquares[0] == 0.0,
                "silence produces zero-power polar window");
    for (int window = 0; window < 5; ++window) analyzer.push(input, output, 960);
    ok &= check(analyzer.droppedWindows() == 1,
                "full polar queue drops one analysis window without blocking audio");
    return ok;
}

bool polarContourMath()
{
    bool ok = check(MixerPolarCurve::radiusForPower(0.0) == 0.0F
        && std::abs(MixerPolarCurve::radiusForPower(1.0) - 72.0F / 84.0F) < .00001F
        && MixerPolarCurve::radiusForPower(100.0) == 1.0F,
        "polar radius uses a fixed -72 to +12 dBFS scale with a zero center");
    MixerPolarCurve curve;
    MixerPolarSnapshot snapshot;
    snapshot.sourceId = 3; snapshot.targetId = 4;
    snapshot.topologyVersion = 1;
    snapshot.sourceLayout = MixerLayout::surround51;
    snapshot.targetLayout = MixerLayout::surround51;
    snapshot.sampleRate = 48000;
    snapshot.sampleCount = 960;
    snapshot.endSample = 960;
    snapshot.inputSumSquares[2] = 960.0;
    snapshot.outputSumSquares[2] = 960.0;
    for (int window = 0; window < 12; ++window) {
        snapshot.firstSample = static_cast<std::uint64_t>(window) * 960;
        snapshot.endSample = snapshot.firstSample + 960;
        ok &= curve.accept(snapshot);
    }
    auto contour = curve.contour();
    ok &= check(contour.valid && contour.inputRadius[0] > .8F
                    && std::abs(contour.inputRadius[0] - contour.outputRadius[0]) < .00001F
                    && std::abs(contour.inputRadius[0] - contour.inputRadius[359]) < .03F,
                "center channel yields matching closed input/output contours without a seam");
    snapshot.topologyVersion = 2;
    snapshot.inputSumSquares.fill(0.0);
    snapshot.outputSumSquares.fill(0.0);
    snapshot.inputSumSquares[3] = 960.0;
    snapshot.outputSumSquares[3] = 960.0;
    snapshot.firstSample = 0;
    snapshot.endSample = 960;
    ok &= check(curve.accept(snapshot), "polar curve accepts a new topology version");
    contour = curve.contour();
    bool silent = true;
    for (float radius : contour.inputRadius) silent &= radius == 0.0F;
    for (float radius : contour.outputRadius) silent &= radius == 0.0F;
    ok &= check(silent, "LFE-only signal remains silent on the angular contour");
    snapshot.topologyVersion = 3;
    snapshot.inputSumSquares.fill(0.0);
    snapshot.outputSumSquares.fill(0.0);
    snapshot.outputSumSquares[2] = 960.0 * 25.0;
    snapshot.outputPeak[2] = 5.0F;
    ok &= check(curve.accept(snapshot) && curve.contour().outputOverrange,
                "output peaks above +12 dBFS set an overrange marker");
    snapshot.sourceLayout = MixerLayout::surround71;
    snapshot.targetLayout = MixerLayout::surround71;
    snapshot.inputSumSquares.fill(0.0);
    snapshot.outputSumSquares.fill(0.0);
    snapshot.inputSumSquares[6] = 960.0;
    snapshot.outputSumSquares[6] = 960.0;
    snapshot.inputPeak.fill(0.0F);
    snapshot.outputPeak.fill(0.0F);
    ok &= check(curve.accept(snapshot) && curve.contour().valid
                    && curve.contour().inputRadius[0] < .01F,
                "5.1 to 7.1 layout switch discards prior C and LFE smoothing");
    snapshot.firstSample = 960;
    snapshot.endSample = 1920;
    snapshot.inputSumSquares[6] = 0.0;
    snapshot.outputSumSquares[6] = 0.0;
    for (int window = 0; window < 200; ++window) {
        snapshot.firstSample += 960;
        snapshot.endSample += 960;
        ok &= curve.accept(snapshot);
    }
    bool fadedToCenter = true;
    for (float radius : curve.contour().inputRadius) fadedToCenter &= radius == 0.0F;
    ok &= check(fadedToCenter, "sustained silence reaches the exact center after smoothing");
    curve.clear();
    ok &= check(!curve.contour().valid, "route switch can clear stale polar history");
    return ok;
}

bool polarProcessorTaps()
{
    DigitalMixerState state;
    const auto source = state.add(MixerStripKind::input, MixerLayout::stereo);
    constexpr double unityFader = .95 * 12.0 / 84.0;
    state.strip(source)->faderPosition = unityFader;
    state.strip(state.main().id)->faderPosition = unityFader;
    DigitalMixerProcessor processor;
    std::array<float, 960> signal, silence, leftOutput, rightOutput;
    signal.fill(.25F); silence.fill(0.0F);
    std::array<const float*, 2> input {signal.data(), silence.data()};
    std::array<float*, 2> output {leftOutput.data(), rightOutput.data()};
    bool ok = check(processor.configure(state, 960)
        && processor.process(input, output, 960), "processor builds real CH-to-MAIN branch taps");
    MixerPolarSnapshot snapshot;
    ok &= check(processor.readPolar(source, state.main().id, snapshot)
                    && snapshot.sampleCount == 960
                    && std::abs(snapshot.inputSumSquares[0] / 960.0 - .0625) < .0001
                    && std::abs(snapshot.outputSumSquares[0] / 960.0 - .0625) < .0001,
                "CH polar taps measure actual before- and after-matrix samples");
    state.strip(source)->mixToMain = false;
    DigitalMixerProcessor next;
    ok &= check(next.configure(state, 960) && next.process(input, output, 960)
                    && next.readPolar(source, state.main().id, snapshot)
                    && snapshot.inputSumSquares[0] > 0.0
                    && snapshot.outputSumSquares[0] == 0.0,
                "MIX bypass preserves branch input while its measured output is silent");
    return ok;
}

bool polarSurroundScenarios()
{
    constexpr double unityFader = .95 * 12.0 / 84.0;
    bool ok = true;
    for (const auto layout : {MixerLayout::surround51, MixerLayout::surround71}) {
        DigitalMixerState state;
        const auto source = state.add(MixerStripKind::input, layout);
        const auto target = state.add(MixerStripKind::aux, layout);
        state.strip(source)->auxSendsEnabled = true;
        state.strip(source)->faderPosition = unityFader;
        state.strip(target)->faderPosition = unityFader;
        auto send = MixerAuxSend {target, true, true, defaultMixerPanner(layout, layout)};
        send.panner.points[2].levelDb = 0.0;
        const auto channels = static_cast<std::size_t>(static_cast<int>(layout));
        std::array<std::array<float, 960>, 8> inputData {};
        std::array<std::array<float, 960>, 10> outputData {};
        std::array<const float*, 8> inputs {};
        std::array<float*, 10> outputs {};
        for (std::size_t channel = 0; channel < channels; ++channel) {
            inputs[channel] = inputData[channel].data();
            outputs[channel] = outputData[channel].data();
        }
        outputs[channels] = outputData[channels].data();
        outputs[channels + 1] = outputData[channels + 1].data();
        const auto measure = [&](MixerPolarSnapshot& snapshot) {
            DigitalMixerProcessor processor;
            return state.setSend(source, send) && processor.configure(state, 960)
                && processor.process(std::span(inputs.data(), channels),
                                     std::span(outputs.data(), channels + 2), 960)
                && processor.readPolar(source, target, snapshot);
        };
        MixerPolarSnapshot snapshot;
        inputData[2].fill(.5F);
        ok &= check(measure(snapshot) && snapshot.sourceLayout == layout
            && snapshot.targetLayout == layout && snapshot.inputSumSquares[2] > 0.0
            && std::abs(snapshot.outputSumSquares[2] / snapshot.inputSumSquares[2] - 1.0) < .0001,
            "5.1/7.1 C-only branch carries a real input and output measurement");
        const auto centerPower = snapshot.outputSumSquares[2];
        send.panner.points[2].levelDb = -6.0;
        ok &= check(measure(snapshot)
            && std::abs(snapshot.inputSumSquares[2] / 240.0 - 1.0) < .0001
            && snapshot.outputSumSquares[2] < centerPower * .27
            && snapshot.outputSumSquares[2] > centerPower * .23,
            "output-only -6 dB shrinks the C contour while input is unchanged");
        send.panner.points[2].levelDb = 0.0;
        send.panner.points[2].influenceRadius = .75;
        ok &= check(measure(snapshot) && snapshot.outputSumSquares[0] > 0.0,
            "a wider source influence radius changes the measured output distribution");
        inputData[2].fill(0.0F);
        inputData[3].fill(.5F);
        ok &= check(measure(snapshot) && snapshot.inputSumSquares[3] > 0.0
            && snapshot.outputSumSquares[3] > 0.0,
            "LFE-only is retained in channel measurements");
        MixerPolarCurve curve;
        ok &= check(curve.accept(snapshot), "LFE-only branch produces a contour frame");
        bool angularSilent = true;
        for (const auto radius : curve.contour().inputRadius) angularSilent &= radius == 0.0F;
        ok &= check(angularSilent, "LFE-only input does not create angular energy");
        send.enabled = false;
        ok &= check(measure(snapshot) && snapshot.inputSumSquares[3] > 0.0
            && snapshot.outputSumSquares[3] == 0.0,
            "disabled send retains its input tap and silences its output tap");
        send.enabled = true;
        inputData[3].fill(0.0F);
        inputData[0].fill(.5F);
        inputData[1].fill(-.5F);
        send.panner.points[0].x = send.panner.points[1].x = 0.0;
        send.panner.points[0].y = send.panner.points[1].y = -1.0;
        send.panner.points[0].influenceRadius = send.panner.points[1].influenceRadius = .75;
        ok &= check(measure(snapshot) && snapshot.inputSumSquares[0] > 0.0
            && snapshot.inputSumSquares[1] > 0.0
            && snapshot.outputSumSquares[2] < .00001,
            "opposite-polarity channels sharing a spatial point cancel in the actual branch output");
    }
    return ok;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::string_view group(argv[1]);
    if (group != "offline") return 2;
    const bool passed = portsAndIdentity() && jsonAndRoutes() && restoreRegression()
        && deterministicAudio()
        && polarAnalysisWindows() && polarContourMath() && polarProcessorTaps()
        && polarSurroundScenarios();
    if (passed) std::puts("PASS: Digital Mixer M1 offline model and JSON contract");
    return passed ? 0 : 1;
}
