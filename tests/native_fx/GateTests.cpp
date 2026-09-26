/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/gate/Processor.hpp>
#include <WinJACKNexus/Audio/JackClient.hpp>
#include <WinJACKNexus/Audio/JackGraph.hpp>
#include <WinJACKNexus/Audio/JackPort.hpp>
#include <WinJACKNexus/Node/INode.hpp>
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#include <QApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>
#include <QFontDatabase>
#include <Windows.h>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QVariant>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>

namespace {
bool check(bool condition,const char* message) {
    if(!condition) std::fprintf(stderr,"FAILED: %s\n",message);
    return condition;
}
float run(WinJACKNexus::Common::GateProcessor& gate,float input,int frames) {
    float output=0;
    for(int i=0;i<frames;++i) output=gate.process(input);
    return output;
}
}

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    bool passed=true;
    passed &= check(!QFontDatabase::families().isEmpty(),"Qt can render fonts");
    WinJACKNexus::Common::GateProcessor gate;
    gate.prepare(48000);
    WinJACKNexus::Common::GateParameters settings;
    settings.sidechainFilter=false;
    gate.setParameters(settings);
    const float low=run(gate,0.001F,48000);
    passed &= check(std::abs(low)<0.00001F,"quiet signal is strongly attenuated");
    const float high=run(gate,0.1F,48000);
    passed &= check(std::abs(high-0.1F)<0.003F,"signal above threshold opens gate");
    settings.enabled=false;
    gate.setParameters(settings);
    passed &= check(gate.process(0.125F)==0.125F,"bypass is bit exact");
    gate.reset();
    settings.enabled=true;
    settings.attackMs=1;
    settings.releaseMs=1;
    settings.holdDb=3;
    gate.setParameters(settings);
    run(gate,0.04F,48000);
    const float hysteresisMid=run(gate,0.02F,48000);
    const float hysteresisLow=run(gate,0.01F,48000);
    passed &= check(hysteresisMid>0.018F && hysteresisLow<0.004F,
                    "3 dB hysteresis keeps the gate open near threshold and closes lower");

    WinJACKNexus::UI::GateEffectWidget widget;
    std::array<WinJACKNexus::Common::GateParameters,8> channels{};
    widget.setParameters(channels);
    int changedCount=0;
    std::size_t changedChannel=99;
    std::array<WinJACKNexus::Common::GateParameters,8> lastChanged{};
    widget.setChangeHandler([&](std::size_t index,const auto& parameters) {
        ++changedCount; changedChannel=index;
        lastChanged[index]=parameters;
    });
    QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();
    if(argc>2) passed &= check(image.save(QString::fromLocal8Bit(argv[2])),"preview saved");
    passed &= check(image.pixelColor(32,80)!=image.pixelColor(32,300),"channel and graph panels render");
    passed &= check(image.pixelColor(150,440)==QColor(13,31,50),"transfer curve area has no fill");
    widget.setProperty("fxAccent",QColor(255,40,40));
    QImage themed(widget.size(),QImage::Format_ARGB32_Premultiplied);
    themed.fill(Qt::transparent);
    widget.render(&themed);
    int themedKnobPixels=0;
    for(int y=253;y<=325;++y)for(int x=404;x<=476;++x){
        const auto color=themed.pixelColor(x,y);
        if(color.red()>180&&color.green()<100&&color.blue()<100)++themedKnobPixels;
    }
    passed &= check(themedKnobPixels>20,"host accent override reaches shared Gate knob rendering");
    widget.setProperty("fxAccent",QVariant());
    auto click=[&](QPointF position) {
        QMouseEvent event(QEvent::MouseButtonPress,position,position,position,
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&widget,&event);
    };
    click({350,188});
    passed &= check(changedCount==8,"All applies to all eight channels");
    changedCount=0;
    click({270,86}); // C selector
    click({350,188});
    passed &= check(changedCount==1 && changedChannel==2,"C changes independently");
    QMouseEvent dialPress(QEvent::MouseButtonPress,{440,289},{440,289},{440,289},
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dialMove(QEvent::MouseMove,{440,269},{440,269},{440,269},
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent dialRelease(QEvent::MouseButtonRelease,{440,269},{440,269},{440,269},
                            Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&dialPress);
    auto* dragRefresh=widget.findChild<QTimer*>(QStringLiteral("nativeFxDragRefreshTimer"));
    passed &= check(dragRefresh && dragRefresh->isActive(),
                    "Gate dial drag uses the shared application frame timer");
    QApplication::sendEvent(&widget,&dialMove);
    QApplication::sendEvent(&widget,&dialRelease);
    passed &= check(dragRefresh && !dragRefresh->isActive(),
                    "Gate dial drag timer stops on release");
    passed &= check(changedChannel==2 && lastChanged[2].thresholdDb>-32.0F,
                    "dragging a dial changes its channel parameter");
    click({440,326});
    auto* exactEditor=widget.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    passed &= check(exactEditor&&!exactEditor->isHidden(),
                    "Gate dial readout opens the shared exact-value editor");
    if(exactEditor) {
        exactEditor->setText(QStringLiteral("-18.5"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(exactEditor,&enter);
        passed &= check(changedChannel==2&&std::abs(lastChanged[2].thresholdDb+18.5F)<.001F,
                        "Gate exact value updates only the selected channel");
    }
    QMouseEvent dialDouble(QEvent::MouseButtonDblClick,{440,289},{440,289},{440,289},
                           Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&dialDouble);
    passed &= check(changedChannel==2&&std::abs(lastChanged[2].thresholdDb+32.0F)<.001F,
                    "Gate double-click restores the parameter default");
    auto dialPointer=[&](const std::array<WinJACKNexus::Common::GateParameters,8>& values,
                         QPoint center,double& renderedArcEndAngle) {
        widget.setParameters(values);
        QImage rendered(widget.size(),QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        QPainter draw(&rendered);
        widget.render(&draw);
        draw.end();
        double sumX=0.0,sumY=0.0;
        int pixels=0;
        std::array<int,360> arcPixelsByAngle{};
        for(int y=center.y()-36;y<=center.y()+36;++y) {
            for(int x=center.x()-36;x<=center.x()+36;++x) {
                const int dx=x-center.x(),dy=y-center.y();
                const int radiusSquared=dx*dx+dy*dy;
                if(radiusSquared>36*36) continue;
                const QColor color=rendered.pixelColor(x,y);
                if(color.blue()>150 && color.green()>120 && color.blue()>color.red()+50) {
                    if(radiusSquared>=14*14 && radiusSquared<=24*24) {
                        sumX+=dx;
                        sumY+=dy;
                        ++pixels;
                    } else if(radiusSquared>=28*28) {
                        constexpr double degreesPerRadian=180.0/3.141592653589793;
                        const int angle=static_cast<int>(std::lround(std::atan2(double(dy),double(dx))
                                                                     *degreesPerRadian));
                        arcPixelsByAngle[(angle+360)%360]++;
                    }
                }
            }
        }
        int firstArcAngle=0,lastArcAngle=359;
        while(firstArcAngle<360 && arcPixelsByAngle[firstArcAngle]==0) ++firstArcAngle;
        while(lastArcAngle>=0 && arcPixelsByAngle[lastArcAngle]==0) --lastArcAngle;
        renderedArcEndAngle=firstArcAngle>=360 || lastArcAngle<0
            ? 0.0 : lastArcAngle*3.141592653589793/180.0;
        if(pixels==0) return QPointF();
        return QPointF(sumX/pixels,sumY/pixels);
    };
    std::array<WinJACKNexus::Common::GateParameters,8> lowerThreshold{};
    lowerThreshold[2].thresholdDb=-20.0F;
    std::array<WinJACKNexus::Common::GateParameters,8> higherThreshold{};
    higherThreshold[2].thresholdDb=-10.0F;
    double ignoredArcEnd=0.0;
    const QPointF lowerPointer=dialPointer(lowerThreshold,{440,289},ignoredArcEnd);
    const QPointF higherPointer=dialPointer(higherThreshold,{440,289},ignoredArcEnd);
    std::array<WinJACKNexus::Common::GateParameters,8> screenshotRatio{};
    screenshotRatio[2].ratio=7.4F;
    double ratioArcEndAngle=0.0;
    const QPointF ratioPointer=dialPointer(screenshotRatio,{562,289},ratioArcEndAngle);
    const double clockwiseCross=lowerPointer.x()*higherPointer.y()-lowerPointer.y()*higherPointer.x();
    const double pointerAngle=std::atan2(ratioPointer.y(),ratioPointer.x());
    const double pointerArcAngleDifference=std::remainder(pointerAngle-ratioArcEndAngle,
                                                          2.0*3.141592653589793);
    passed &= check(std::hypot(lowerPointer.x(),lowerPointer.y())>10.0
                    && std::hypot(higherPointer.x(),higherPointer.y())>10.0
                    && clockwiseCross>0.0
                    && std::hypot(ratioPointer.x(),ratioPointer.y())>10.0
                    && std::abs(pointerArcAngleDifference)<6.0*3.141592653589793/180.0,
                    "7.4:1 pointer aligns with the rendered outer-ring endpoint while values rotate clockwise");
    passed &= check(widget.size()==QSize(754,654),"reference-size panel and channel bar");
    if(argc>1) {
        const auto path=QString::fromLocal8Bit(argv[1]).toStdWString();
        auto* module=LoadLibraryW(path.c_str());
        passed &= check(module!=nullptr,"native gate DLL loads");
        if(module) {
            using GetVersion=const char* (*)();
            using GetCount=unsigned int (*)();
            using GetText=const char* (*)(unsigned int);
            using Create=WinJACKNexus::Node::INode* (*)(unsigned int);
            using Destroy=void (*)(WinJACKNexus::Node::INode*);
            using GetSettings=const char* (*)(WinJACKNexus::Node::INode*);
            using SetSettings=int (*)(WinJACKNexus::Node::INode*,const char*);
            using GetClientId=const char* (*)(WinJACKNexus::Node::INode*);
            using CreateSettingsWidget=QWidget* (*)(WinJACKNexus::Node::INode*,QWidget*);
            using DestroySettingsWidget=void (*)(WinJACKNexus::Node::INode*,QWidget*);
            auto version=reinterpret_cast<GetVersion>(GetProcAddress(module,"wjn_get_node_api_version"));
            auto count=reinterpret_cast<GetCount>(GetProcAddress(module,"wjn_get_node_count"));
            auto category=reinterpret_cast<GetText>(GetProcAddress(module,"wjn_get_node_category"));
            auto create=reinterpret_cast<Create>(GetProcAddress(module,"wjn_create_node"));
            auto destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"wjn_destroy_node"));
            auto getSettings=reinterpret_cast<GetSettings>(GetProcAddress(module,"wjn_get_node_settings_json"));
            auto setSettings=reinterpret_cast<SetSettings>(GetProcAddress(module,"wjn_set_node_settings_json"));
            auto getClientId=reinterpret_cast<GetClientId>(GetProcAddress(module,"wjn_get_node_client_id"));
            auto createSettingsWidget=reinterpret_cast<CreateSettingsWidget>(GetProcAddress(module,"wjn_create_settings_widget"));
            auto destroySettingsWidget=reinterpret_cast<DestroySettingsWidget>(GetProcAddress(module,"wjn_destroy_settings_widget"));
            passed &= check(version && count && category && create && destroy && getSettings && setSettings,
                            "node API exports are present");
            if(version && count && category && create && destroy && getSettings && setSettings) {
                passed &= check(std::strcmp(version(),"2")==0 && count()==1 &&
                                std::strcmp(category(0),"Native Fx")==0,"node API metadata");
                auto* node=create(0);
                passed &= check(node!=nullptr,"gate node created");
                if(node) {
                    const std::string json=getSettings(node);
                    passed &= check(json.find("channels")!=std::string::npos,"eight-channel settings exported");
                    passed &= check(setSettings(node,json.c_str())==1,"settings round trip");
                    passed &= check(setSettings(node,"{\"channels\":[]}")==0,"invalid channel count rejected");
                    auto settingsObject=QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
                    auto settingsArray=settingsObject.value(QStringLiteral("channels")).toArray();
                    auto third=settingsArray.at(2).toObject();
                    third.insert(QStringLiteral("thresholdDb"),-17.0);
                    settingsArray.replace(2,third);
                    settingsObject.insert(QStringLiteral("channels"),settingsArray);
                    const auto changedJson=QJsonDocument(settingsObject).toJson(QJsonDocument::Compact);
                    passed &= check(setSettings(node,changedJson.constData())==1,"independent C settings restore");
                    const auto restored=QJsonDocument::fromJson(QByteArray(getSettings(node)))
                                            .object().value(QStringLiteral("channels")).toArray();
                    passed &= check(restored.at(2).toObject().value(QStringLiteral("thresholdDb")).toDouble()==-17.0
                                    && restored.at(0).toObject().value(QStringLiteral("thresholdDb")).toDouble()==-32.0,
                                    "C threshold persists without changing L");
                    passed &= check(setSettings(node,json.c_str())==1,"default settings restored");
                    QWidget* sidebar=createSettingsWidget?createSettingsWidget(node,nullptr):nullptr;
                    passed &= check(sidebar!=nullptr && sidebar->findChild<QLineEdit*>("gate.clientName")!=nullptr
                                    && sidebar->findChild<QPushButton*>("gate.applyClientName")!=nullptr,
                                    "sidebar contains the client-name editor");
                    passed &= check(sidebar==nullptr || dynamic_cast<WinJACKNexus::UI::GateEffectWidget*>(sidebar)==nullptr,
                                    "effect controls are not in the sidebar");
                    if(sidebar && destroySettingsWidget) destroySettingsWidget(node,sidebar);
                    const HWND effectWindow=node->createWindow(nullptr);
                    passed &= check(effectWindow!=nullptr && IsWindow(effectWindow),"effect opens in a separate window");
                    if(effectWindow) node->destroyWindow(effectWindow);
                    if(qEnvironmentVariableIntValue("WJN_GATE_INTEGRATION")==1) {
                        passed &= check(node->init(),"JACK Gate node starts");
                        if(getClientId) {
                            WinJACKNexus::Audio::JackClient observer;
                            passed &= check(observer.open({"GateTestObserver",false,false,nullptr}),
                                            "JACK observer starts");
                            const std::string id=getClientId(node);
                            constexpr const char* names[]={"L","R","C","LFE","Ls","Rs","Lb","Rb"};
                            for(const char* name:names) {
                                const std::string input=id+":in_"+name;
                                const std::string output=id+":out_"+name;
                                passed &= check(WinJACKNexus::Audio::JackGraph::portExists(observer,input.c_str()),
                                                "JACK input exists");
                                passed &= check(WinJACKNexus::Audio::JackGraph::portExists(observer,output.c_str()),
                                                "JACK output exists");
                            }
                            observer.close();

                            struct Source {
                                WinJACKNexus::Audio::JackPort port;
                                std::atomic<float> level {0.1F};
                                float phase=0;
                                float step=0;
                            } source;
                            struct Sink {
                                WinJACKNexus::Audio::JackPort port;
                                std::atomic<float> last {0.0F};
                            } sink;
                            WinJACKNexus::Audio::JackClient sourceClient,sinkClient;
                            bool audioReady=sourceClient.open({"GateTestSource",false,false,nullptr})
                                && sinkClient.open({"GateTestSink",false,false,nullptr})
                                && source.port.registerPort(sourceClient,"out",JACK_DEFAULT_AUDIO_TYPE,JackPortIsOutput)
                                && sink.port.registerPort(sinkClient,"in",JACK_DEFAULT_AUDIO_TYPE,JackPortIsInput);
                            passed &= check(audioReady,"JACK test audio ports start");
                            if(audioReady) {
                                source.step=2.0F*3.14159265F*1000.0F/sourceClient.sampleRate();
                                sourceClient.setProcessCallback([](jack_nframes_t frames,void* context) noexcept {
                                    auto* state=static_cast<Source*>(context);
                                    if(auto* buffer=state->port.audioBuffer(frames)) {
                                        const float level=state->level.load(std::memory_order_relaxed);
                                        for(jack_nframes_t i=0;i<frames;++i) {
                                            buffer[i]=level*std::sin(state->phase);
                                            state->phase+=state->step;
                                            if(state->phase>6.2831853F) state->phase-=6.2831853F;
                                        }
                                    }
                                    return 0;
                                },&source);
                                sinkClient.setProcessCallback([](jack_nframes_t frames,void* context) noexcept {
                                    auto* state=static_cast<Sink*>(context);
                                    if(const auto* buffer=state->port.audioBuffer(frames);buffer && frames) {
                                        float peak=0;
                                        for(jack_nframes_t i=0;i<frames;++i) peak=std::max(peak,std::abs(buffer[i]));
                                        state->last.store(peak,std::memory_order_relaxed);
                                    }
                                    return 0;
                                },&sink);
                                audioReady=sourceClient.activate() && sinkClient.activate();
                                const std::string sourcePort=std::string(sourceClient.name())+":out";
                                const std::string sinkPort=std::string(sinkClient.name())+":in";
                                audioReady=audioReady
                                    && WinJACKNexus::Audio::JackGraph::connect(sourceClient,sourcePort.c_str(),
                                                                                (id+":in_L").c_str())
                                    && WinJACKNexus::Audio::JackGraph::connect(sourceClient,
                                                                                (id+":out_L").c_str(),sinkPort.c_str());
                                passed &= check(audioReady,"JACK Gate audio path connects");
                                if(audioReady) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(350));
                                    passed &= check(sink.last.load()>0.09F,"JACK audio passes above threshold");
                                    source.level.store(0.001F);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(850));
                                    passed &= check(std::abs(sink.last.load())<0.0001F,
                                                    "JACK audio attenuates below threshold");
                                    settingsObject.insert(QStringLiteral("clientName"),QStringLiteral("GateRenamed"));
                                    const auto renameJson=QJsonDocument(settingsObject).toJson(QJsonDocument::Compact);
                                    source.level.store(0.1F);
                                    passed &= check(setSettings(node,renameJson.constData())==1,
                                                    "live client name change applies");
                                    const std::string renamedId=getClientId(node);
                                    passed &= check(renamedId=="GateRenamed","client id refreshes after rename");
                                    std::this_thread::sleep_for(std::chrono::milliseconds(350));
                                    int sourceConnections=0,sinkConnections=0;
                                    source.port.visitConnections([](const char*,void* value) noexcept {
                                        ++*static_cast<int*>(value); return true;
                                    },&sourceConnections);
                                    sink.port.visitConnections([](const char*,void* value) noexcept {
                                        ++*static_cast<int*>(value); return true;
                                    },&sinkConnections);
                                    passed &= check(sourceConnections==1 && sinkConnections==1
                                                    && sink.last.load()>0.09F,
                                                    "audio connections survive client rename");
                                }
                            }
                            sinkClient.deactivate(); sourceClient.deactivate();
                            sink.port.unregisterPort(); source.port.unregisterPort();
                            sinkClient.close(); sourceClient.close();
                        }
                        node->shutdown();
                    }
                    destroy(node);
                }
            }
            FreeLibrary(module);
        }
    }
    if(passed) std::puts(qEnvironmentVariableIntValue("WJN_GATE_INTEGRATION")==1
                         ? "Gate DSP, UI, settings and JACK audio path passed"
                         : "Gate DSP, UI and settings passed");
    return passed?0:1;
}
