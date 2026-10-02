/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/Common/DigitalMixerProcessor.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QDir>
#include <QJsonDocument>
#include <cmath>
#include <map>
#include <algorithm>
template<class Check> void verifySubMix(Check check)
{
    using namespace WinJACKNexus;
    using namespace Common;
    for(const auto layout : {MixerLayout::mono,MixerLayout::stereo,MixerLayout::surround51,MixerLayout::surround71}) {
        DigitalMixerState state;
        const auto input=state.add(MixerStripKind::input,layout);
        const auto group=state.add(MixerStripKind::subMix,layout);
        check(group && state.setSubMix(input,group),"create all Sub Mix layouts and assign input");
        const auto run=[&]() {
            DigitalMixerProcessor processor;
            check(processor.configure(state,128),"configure Sub Mix offline processor");
            const auto ports=state.ports();
            std::vector<std::array<float,128>> inputData(processor.inputPortCount());
            std::vector<const float*> inputs;
            for(std::size_t i=0;i<inputData.size();++i) {inputData[i].fill(i==0?.1F:0.F);inputs.push_back(inputData[i].data());}
            std::vector<std::array<float,128>> outputData(processor.outputPortCount());
            std::vector<float*> outputs;
            for(auto& channel:outputData)outputs.push_back(channel.data());
            check(processor.process(inputs,outputs,128),"render grouped audio without JACK");
            std::map<std::uint64_t,float> peaks;std::size_t index=0;
            for(const auto& port:ports) if(port.direction==MixerPortDirection::output) {
                if(port.channelIndex==0)peaks[port.stripId]=outputData[index][127];++index;
            }
            return peaks;
        };
        state.strip(group)->inputGainDb=6;
        const auto peaks=run();
        check(std::abs(peaks.at(group)-.1F*std::pow(10.F,6.F/20.F))<.0001F,"Sub Mix gain affects its real output");
        state.strip(group)->mainPanner.points[0].levelDb=-6;
        const auto trimmed=run();
        check(std::abs(trimmed.at(group)-.1F)<.0001F,"Sub Mix SEL channel trim acts on its own output once");
        state.strip(group)->mainPanner.points[0].levelDb=0;
        const auto other=state.add(MixerStripKind::subMix,layout);
        check(state.setSubMix(group,other),"Sub Mix cascades to another bus");
        const auto cascade=run();
        check(std::abs(cascade.at(other)-peaks.at(group))<.0001F,"cascaded Sub Mix retains its input without duplicate contribution");
        const auto before=state.toJson();
        check(!state.setSubMix(other,group)&&state.toJson()==before,"cycle rejected transactionally");
        DigitalMixerState restored;
        check(restored.loadJson(before)&&restored.toJson()==before,"Sub Mix routes and controls survive JSON roundtrip");
        check(restored.remove(other)&&!restored.hasSubMix(group),"remove Sub Mix clears incoming routes");
        state.strip(group)->mute=true;state.strip(group)->listenMode=MixerListenMode::pfl;
        auto listen=run();check(listen.at(0)>0&&listen.at(group)==0,"Sub Mix PFL bypasses mute after gain");
        state.strip(group)->listenMode=MixerListenMode::afl;
        listen=run();check(listen.at(0)==0,"Sub Mix AFL follows mute");
    }
    DigitalMixerState state;
    const auto input=state.add(MixerStripKind::input,MixerLayout::stereo);
    const auto aux=state.add(MixerStripKind::aux,MixerLayout::stereo);
    const auto sub=state.add(MixerStripKind::subMix,MixerLayout::stereo);
    state.strip(input)->auxSendsEnabled=true;
    state.strip(input)->mixToMain=false;
    check(state.setSend(input,{aux,true,false,defaultMixerPanner(MixerLayout::stereo,MixerLayout::stereo)}),"feed AUX for grouped audio");
    check(state.setSubMix(aux,sub),"route AUX into Sub Mix");
    const auto ports=state.ports();
    check(std::none_of(ports.begin(),ports.end(),[&](const auto& port){return port.stripId==aux;}),"routed AUX hides output ports");
    DigitalMixerProcessor processor;check(processor.configure(state,64),"configure hidden AUX ports");
    std::array<float,64> left,right;left.fill(.2F);right.fill(.1F);
    const std::array<const float*,2> inputs {left.data(),right.data()};
    std::vector<std::array<float,64>> audio(processor.outputPortCount());std::vector<float*> outputs;
    for(auto& channel:audio)outputs.push_back(channel.data());
    check(processor.process(inputs,outputs,64)&&std::abs(audio[0][63]-.2F)<.0001F
        &&std::abs(audio[audio.size()-2][63]-.2F)<.0001F,"AUX feeds Sub Mix and MAIN once without duplicate direct contribution");
    check(state.setSubMix(aux,0),"clear Sub Mix assignment");
    const auto restoredPorts=state.ports();
    check(std::count_if(restoredPorts.begin(),restoredPorts.end(),[&](const auto& port){return port.stripId==aux;})==2,"clearing assignment restores AUX outputs");
    auto oldRoot=QJsonDocument::fromJson(state.toJson()).object();oldRoot.remove("subMixes");oldRoot.remove("nextSubMixNumber");
    DigitalMixerState legacy;check(legacy.loadJson(QJsonDocument(oldRoot).toJson()),"old version-1 project loads without Sub Mix fields");
    const auto second=state.add(MixerStripKind::subMix,MixerLayout::stereo);
    check(state.setSubMix(aux,sub)&&state.setSubMix(aux,second)&&state.strip(aux)->sends.size()==2,"AUX supports two simultaneous Sub Mix sends");
    state.strip(aux)->sends[0].enabled=false;
    check(state.hasSubMix(aux),"one enabled Sub Mix keeps AUX output hidden");
    state.strip(aux)->sends[1].enabled=false;
    check(!state.hasSubMix(aux),"all Sub Mix sends disabled restore AUX output");
    check(!state.setSubMix(sub,sub),"Sub Mix cannot route to itself");
    check(state.remove(second),"remove second regression bus");
    UI::ThemeCore core;
    for(const auto* role:{"fader-submix-thumb","fader-submix-track","fader-submix-grip","control-scrollbar-submix-thumb","resize-submix-handle"})
        check(core.snapshot()->hasValue(QStringLiteral("--wjn-theme-mixer-")+QLatin1String(role)),"Sub Mix component theme token is registered");
    UI::MixerWindow mixer(&state,[&](const auto& candidate,QString*){state=candidate;return true;});
    mixer.resize(1600,900);mixer.show();QApplication::processEvents();
    check(mixer.findChildren<UI::Controls::ChoiceField*>().empty(),"mixer strips have no Sub Mix dropdowns");
    const qreal scale=std::clamp(mixer.height()/768.0,.72,1.35);
    const qreal areaHeight=mixer.height()-40-9-10-5;
    const qreal meterBottom=40+65*scale+areaHeight*.28+16*scale;
    const QPointF point(1128,meterBottom+20*scale+28*scale*1.35+14*scale);
    QMouseEvent click(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&mixer,&click);QApplication::processEvents();
    auto* page=mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
    auto* auxTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabAUX"));
    auto* subTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabSub Mix"));
    check(page&&auxTab&&!auxTab->isVisible()&&subTab&&subTab->isVisible(),"Sub Mix SEL has MAIN and Sub Mix tabs without AUX");
    if(page) {
        page->setFixedSize(1394,813);
        for(const auto& [x,mode]:{std::pair{80,MixerListenMode::pfl},std::pair{156,MixerListenMode::afl}}) {
            QMouseEvent modeClick(QEvent::MouseButtonPress,QPointF(x,459),QPointF(x,459),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(page,&modeClick);
            check(state.strip(sub)->listenMode==mode,"Sub Mix SEL commits exclusive PFL/AFL");
        }
        QDir().mkpath(QStringLiteral("tests/preview/PrePush"));page->grab().save(QStringLiteral("tests/preview/PrePush/submix-sel.png"));
    }
    if(subTab) {
        subTab->click();QApplication::processEvents();
        auto* routes=mixer.findChild<QWidget*>(QStringLiteral("selSubMixRoutes"));
        check(routes&&routes->isVisible(),"Sub Mix tab opens shared route page");
        if(routes) routes->grab().save(QStringLiteral("tests/preview/PrePush/submix-routes.png"));
    }
    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));mixer.grab().save(QStringLiteral("tests/preview/PrePush/submix-mixer.png"));
    for(auto* top:mixer.menuBar()->actions())if(top->menu())
        for(auto* item:top->menu()->actions())if(item->text()==QStringLiteral("添加 Sub Mix 通道")&&item->menu()) {
            check(item->menu()->actions().size()==4,"channel management exposes four Sub Mix layouts");
            item->menu()->actions().front()->trigger();
            check(state.subMixes().size()==2&&state.subMixes().back().layout==MixerLayout::mono,"Sub Mix menu creates mono bus");
        }
    const QPointF inputPoint(35,point.y());
    QMouseEvent inputClick(QEvent::MouseButtonPress,inputPoint,inputPoint,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&mixer,&inputClick);QApplication::processEvents();
    subTab=mixer.findChild<QPushButton*>(QStringLiteral("selTabSub Mix"));
    if(subTab) {
        subTab->click();QApplication::processEvents();
        auto* routes=mixer.findChild<QWidget*>(QStringLiteral("selSubMixRoutes"));
        if(routes) {
            const double routeScale=std::min(routes->width()/1360.0,routes->height()/813.0);
            const QPointF origin((routes->width()-1360*routeScale)*.5,(routes->height()-813*routeScale)*.5);
            for(const int y:{98,199}) {
                const QPointF p=origin+QPointF(48*routeScale,y*routeScale);
                QMouseEvent toggle(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(routes,&toggle);
            }
            const auto& sends=state.strip(input)->sends;
            check(std::count_if(sends.begin(),sends.end(),[&](const auto& send){return send.enabled&&state.strip(send.targetId)->kind==MixerStripKind::subMix;})==2,
                "Sub Mix page enables two real simultaneous post-fader sends");
            check(std::all_of(sends.begin(),sends.end(),[](const auto& send){return !send.preFader;}),"Sub Mix route page uses post-fader sampling");
            routes->grab().save(QStringLiteral("tests/preview/PrePush/submix-routes.png"));
        }
    }

}
