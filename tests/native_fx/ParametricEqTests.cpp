/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/parametric_eq/Processor.hpp>
#include <WinJACKNexus/NativeFx/parametric_eq/Ui.hpp>
#include <QApplication>
#include <QImage>
#include <QLibrary>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
bool check(bool condition,const char* message) {
    std::printf("%s %s\n",condition?"PASS":"FAIL",message);std::fflush(stdout);return condition;
}
void click(QWidget& widget,int x,int y) {
    const QPointF position(x,y);
    QMouseEvent down(QEvent::MouseButtonPress,position,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&down);
    QMouseEvent up(QEvent::MouseButtonRelease,position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&up);
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);bool passed=true;
    if(argc<3)return 1;
    QLibrary module(QString::fromLocal8Bit(argv[1]));
    passed &= check(module.load(),"Native Fx module loads");
    using GetCount=std::uint32_t(*)();using GetName=const char*(*)(std::uint32_t);
    using CreateNode=WinJACKNexus::Node::INode*(*)(std::uint32_t);
    using DestroyNode=void(*)(WinJACKNexus::Node::INode*);
    using GetSettings=const char*(*)(WinJACKNexus::Node::INode*);
    using SetSettings=int(*)(WinJACKNexus::Node::INode*,const char*);
    auto count=reinterpret_cast<GetCount>(module.resolve("wjn_get_node_count"));
    auto name=reinterpret_cast<GetName>(module.resolve("wjn_get_node_name"));
    auto create=reinterpret_cast<CreateNode>(module.resolve("wjn_create_node"));
    auto destroy=reinterpret_cast<DestroyNode>(module.resolve("wjn_destroy_node"));
    auto getSettings=reinterpret_cast<GetSettings>(module.resolve("wjn_get_node_settings_json"));
    auto setSettings=reinterpret_cast<SetSettings>(module.resolve("wjn_set_node_settings_json"));
    if(!count||!name||!create||!destroy||!getSettings||!setSettings)return 1;
    passed &= check(count()==1 && std::strcmp(name(0),"Parametric EQ")==0,"Parametric EQ node is exported");
    auto* node=create(0);passed &= check(node!=nullptr,"node can be created");
    if(node){
        passed &= check(std::strcmp(node->info().category,"Native Fx")==0,"node category is Native Fx");
        const auto json=QByteArray(getSettings(node));
        passed &= check(!json.isEmpty() && setSettings(node,json.constData())==1,"eight-channel settings round-trip");
        passed &= check(setSettings(node,"{\"channels\":[]}")==0,"invalid settings rejected");
        const auto window=node->createWindow(nullptr);
        passed &= check(window!=nullptr,"standalone window opens without JACK connections");
        if(window)node->destroyWindow(window);
        destroy(node);
    }
    WinJACKNexus::UI::ParametricEqWidget widget;
    passed &= check(widget.isWindow() && widget.parentWidget()==nullptr,"UI is a top-level window");
    QImage preview(widget.size(),QImage::Format_ARGB32_Premultiplied);preview.fill(Qt::transparent);
    widget.render(&preview);
    passed &= check(preview.save(QString::fromLocal8Bit(argv[2])),"preview image saved");
    passed &= check(preview.pixelColor(preview.width()-5,840)==QColor("#081c2f"),
        "bottom output card stays inside the window's right margin");
    bool menuIconsPresent=false;
    bool menuPreviewSaved=false;
    QTimer::singleShot(80,&widget,[&] {
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(menu){
            const auto actions=menu->actions();
            menuIconsPresent=actions.size()==5 && std::all_of(actions.begin(),actions.end(),
                [](const QAction* action){return !action->icon().isNull();});
            const QImage menuPreview=menu->grab().toImage();
            const auto menuPath=QString::fromLocal8Bit(argv[2]).replace(QStringLiteral("-preview.png"),QStringLiteral("-menu-preview.png"));
            menuPreviewSaved=menuPreview.width()>=120 && menuPreview.height()>=100 && menuPreview.save(menuPath);
            menu->close();
        }
    });
    click(widget,180,602); // First band's filter-shape dropdown.
    passed &= check(menuIconsPresent && menuPreviewSaved,"filter-shape menu has five custom curve icons");
    std::array<WinJACKNexus::Common::ParametricEqParameters,8> state{};
    int changes=0;
    widget.setChangeHandler([&](std::size_t index,const auto& value){state[index]=value;++changes;});
    click(widget,533,135); // Master enable switch in the scaled design.
    passed &= check(changes==8 && std::all_of(state.begin(),state.end(),[](const auto& p){return !p.enabled;}),
        "All mode broadcasts the absolute target to eight channels");
    click(widget,120,60); // L selector.
    passed &= check(widget.selectedChannel()==0,"L channel can be selected");
    click(widget,533,135);
    passed &= check(changes==9 && state[0].enabled && !state[1].enabled,"single-channel edit preserves other channel state");
    click(widget,998,118); // Custom -> Vocal Clear.
    click(widget,998,118); // Vocal Clear -> Flat.
    passed &= check(changes==11 && state[0].bands[0].gainDb==0 && state[1].bands[0].gainDb==4.5F,
        "preset selection updates only the selected channel");
    widget.setParameters(state);
    QImage beforeSpectrum(widget.size(),QImage::Format_ARGB32_Premultiplied);beforeSpectrum.fill(Qt::transparent);
    widget.render(&beforeSpectrum);
    WinJACKNexus::Common::GraphicEqSpectrumFrame spectrumFrame;
    constexpr double twoPi=6.283185307179586;
    for(std::size_t sample=0;sample<WinJACKNexus::Common::graphicEqSpectrumFftSize;++sample){
        const float value=0.5F*std::sin(twoPi*1000.0*sample/48000.0);
        spectrumFrame.inputSamples[0][sample]=value;
        spectrumFrame.outputSamples[0][sample]=value*0.5F;
    }
    widget.setSpectrumFrameReader([&](auto& frame){frame=spectrumFrame;return true;});
    QTimer::singleShot(100,&app,&QCoreApplication::quit);
    app.exec();
    QImage spectrumPreview(widget.size(),QImage::Format_ARGB32_Premultiplied);spectrumPreview.fill(Qt::transparent);
    widget.render(&spectrumPreview);
    const auto spectrumPath=QString::fromLocal8Bit(argv[2]).replace(QStringLiteral("-preview.png"),QStringLiteral("-spectrum-preview.png"));
    passed &= check(spectrumPreview!=beforeSpectrum && spectrumPreview.save(spectrumPath),"input and processed spectrum curves render");
    widget.setSpectrumFrameReader({});
    using Processor=WinJACKNexus::Common::ParametricEqProcessor;
    WinJACKNexus::Common::ParametricEqParameters flat;
    for(auto& band:flat.bands)band.gainDb=0;
    passed &= check(std::abs(Processor::responseDb(flat,48000,1000))<0.01F,"flat preset has neutral response");
    passed &= check(Processor::responseDb({},48000,80)>Processor::responseDb({},48000,400),"default low shelf raises bass");
    Processor processor;processor.prepare(48000);processor.setParameters(flat);
    passed &= check(std::isfinite(processor.process(0.5F)),"DSP produces finite output");
    WinJACKNexus::UI::ParametricEqWidget slopeWidget;
    std::array<WinJACKNexus::Common::ParametricEqParameters,8> slopeSettings{};
    slopeWidget.setChangeHandler([&](std::size_t channel,const auto& value){slopeSettings[channel]=value;});
    const auto chooseSlope=[&](int x){
        bool opened=false;
        QTimer::singleShot(0,&slopeWidget,[&]{
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            const auto actions=menu->actions();
            opened=actions.size()==3;
            if(opened)actions[2]->trigger(); // 48 dB/Oct
            menu->close();
        });
        click(slopeWidget,x,840);
        QApplication::processEvents();
        return opened;
    };
    passed &= check(chooseSlope(300),"HPF slope box opens a three-choice dropdown");
    passed &= check(chooseSlope(740),"LPF slope box opens a three-choice dropdown");
    passed &= check(std::all_of(slopeSettings.begin(),slopeSettings.end(),[](const auto& value){
        return value.highPassSlope==48 && value.lowPassSlope==48;
    }),"slope dropdown choices apply to all channels");
    auto analysisWidget=std::make_unique<WinJACKNexus::UI::ParametricEqWidget>();
    bool analysisMenuPreviewSaved=false;
    const auto checkAnalysisMenu=[&](int y,const std::array<QString,3>& choices){
        bool opened=false;
        QTimer::singleShot(80,analysisWidget.get(),[&]{
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if(!menu)return;
            const auto actions=menu->actions();
            opened=actions.size()==3;
            for(int i=0;opened && i<3;++i)
                opened=actions[i]->text()==choices[static_cast<std::size_t>(i)];
            if(y==398){
                const auto path=QString::fromLocal8Bit(argv[2]).replace(
                    QStringLiteral("-preview.png"),QStringLiteral("-analysis-menu-preview.png"));
                analysisMenuPreviewSaved=menu->grab().toImage().save(path);
            }
            if(opened)actions[0]->trigger();
            menu->close();
        });
        click(*analysisWidget,1160,y);
        QApplication::processEvents();
        return opened;
    };
    passed &= check(checkAnalysisMenu(398,{QStringLiteral("1024"),QStringLiteral("2048"),QStringLiteral("4096")}),
        "FFT size opens a three-choice dropdown");
    passed &= check(checkAnalysisMenu(435,{QStringLiteral("低"),QStringLiteral("中"),QStringLiteral("高")}),
        "smoothing opens a three-choice dropdown");
    passed &= check(checkAnalysisMenu(473,{QStringLiteral("±12 dB"),QStringLiteral("±18 dB"),QStringLiteral("±24 dB")}),
        "display range opens a three-choice dropdown");
    passed &= check(analysisMenuPreviewSaved,"analysis settings menu preview saved");
    return passed?0:1;
}
