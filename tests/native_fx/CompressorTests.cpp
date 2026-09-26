/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/compressor/Ui.hpp>
#include <QApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool check(bool result,const char* message) {
    if(!result) std::fprintf(stderr,"FAIL: %s\n",message);
    return result;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    bool passed=true;
    WinJACKNexus::UI::CompressorWidget widget;
    passed &= check(widget.size()==QSize(770,665),"reference layout size");
    QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);QPainter painter(&image);widget.render(&painter);painter.end();
    passed &= check(image.pixelColor(30,30)!=image.pixelColor(30,200),"channel bar and effect panel render");
    passed &= check(image.pixelColor(750,30)==QColor(22,40,60),"channel bar aligns with the effect panel");
    passed &= check(image.pixelColor(760,30)==QColor(13,28,45),"channel bar ends with the effect panel");
    const auto hasAmberFill=[&image](int x,int y) {
        const auto pixel=image.pixelColor(x,y);
        return pixel.red()>pixel.green()+4 && pixel.green()>pixel.blue();
    };
    const auto hasRedGainReduction=[&image](int x,int y) {
        const auto pixel=image.pixelColor(x,y);
        return pixel.red()>180 && pixel.green()<100 && pixel.blue()<120;
    };
    std::array<WinJACKNexus::UI::CompressorMeter,8> meters {};
    widget.setMeters(meters);widget.render(&image);
    passed &= check(!hasAmberFill(293,361),"All has no moving fill when every channel is silent");
    passed &= check(!hasRedGainReduction(366,275),"GR meter is empty when there is no active gain reduction");
    meters[0]={-6.02F,-10.5F,true,12.0F};meters[1]={-18.0F,-19.0F,true,6.0F};meters[2]={0.0F,0.0F,false,24.0F};
    widget.setMeters(meters);widget.render(&image);
    passed &= check(hasAmberFill(293,361),"All computes active-channel level and fills below the compressed curve");
    passed &= check(!hasAmberFill(354,361),"inactive channel does not expand All fill extent");
    passed &= check(hasRedGainReduction(366,275) && hasRedGainReduction(366,300),
                    "GR meter fills downward in red for active channels");
    passed &= check(!hasRedGainReduction(366,310),"inactive channel does not increase the All GR average");
    if(argc>2) passed &= check(image.save(QString::fromLocal8Bit(argv[2])),"preview saved");
    int changes=0;unsigned mask=0;
    widget.setChangeHandler([&](std::size_t channel,const auto&){++changes;mask|=1u<<channel;});
    auto click=[&](QPointF point) {QMouseEvent event(QEvent::MouseButtonPress,point,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&widget,&event);};
    click({354,190});
    passed &= check(changes==8 && mask==0xff,"All edits eight channels");
    changes=0;mask=0;click({128,82});click({200,82});click({354,190});
    passed &= check(changes==1 && mask==0x2,"channel selection is exclusive like the other Native Fx controls");
    std::array<WinJACKNexus::UI::CompressorParameters,8> changedValues{};
    widget.setChangeHandler([&](std::size_t channel,const auto& value){
        ++changes;mask|=1u<<channel;changedValues[channel]=value;
    });
    click({52,82});changes=0;mask=0;
    click({454,327});
    auto* editor=widget.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    passed &= check(editor!=nullptr,"Compressor readout opens a precise value editor");
    if(editor){
        editor->setText(QStringLiteral("-21.5"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
    }
    passed &= check(changes==8&&mask==0xff,"Compressor All numeric edit reaches eight channels");
    for(const auto& value:changedValues)
        passed &= check(std::abs(value.thresholdDb+21.5F)<0.001F,"Compressor numeric edit uses one absolute target");
    changes=0;mask=0;
    QMouseEvent reset(QEvent::MouseButtonDblClick,QPointF(454,288),QPointF(454,288),
                      Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&widget,&reset);
    passed &= check(changes==8&&mask==0xff,"Compressor double click resets All channels");
    for(const auto& value:changedValues)
        passed &= check(std::abs(value.thresholdDb-WinJACKNexus::UI::CompressorParameters{}.thresholdDb)<0.001F,
                        "Compressor double click restores the parameter default");
    if(argc>1) {
        auto* module=LoadLibraryW(QString::fromLocal8Bit(argv[1]).toStdWString().c_str());
        passed &= check(module!=nullptr,"native module loads");
        if(module) {
            using Count=std::uint32_t (*)();using Name=const char* (*)(std::uint32_t);
            using Create=WinJACKNexus::Node::INode* (*)(std::uint32_t);
            using Destroy=void (*)(WinJACKNexus::Node::INode*);
            using Get=const char* (*)(WinJACKNexus::Node::INode*);
            using Set=int (*)(WinJACKNexus::Node::INode*,const char*);
            using CreateSettings=QWidget* (*)(WinJACKNexus::Node::INode*,QWidget*);
            using DestroySettings=void (*)(WinJACKNexus::Node::INode*,QWidget*);
            using GetClientId=const char* (*)(WinJACKNexus::Node::INode*);
            const auto count=reinterpret_cast<Count>(GetProcAddress(module,"wjn_get_node_count"));
            const auto name=reinterpret_cast<Name>(GetProcAddress(module,"wjn_get_node_name"));
            const auto category=reinterpret_cast<Name>(GetProcAddress(module,"wjn_get_node_category"));
            const auto create=reinterpret_cast<Create>(GetProcAddress(module,"wjn_create_node"));
            const auto destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"wjn_destroy_node"));
            const auto get=reinterpret_cast<Get>(GetProcAddress(module,"wjn_get_node_settings_json"));
            const auto set=reinterpret_cast<Set>(GetProcAddress(module,"wjn_set_node_settings_json"));
            const auto createSettings=reinterpret_cast<CreateSettings>(GetProcAddress(module,"wjn_create_settings_widget"));
            const auto destroySettings=reinterpret_cast<DestroySettings>(GetProcAddress(module,"wjn_destroy_settings_widget"));
            const auto getClientId=reinterpret_cast<GetClientId>(GetProcAddress(module,"wjn_get_node_client_id"));
            passed &= check(count&&name&&category&&create&&destroy&&get&&set&&createSettings&&destroySettings&&getClientId,"module exports exist");
            if(count&&name&&category&&create&&destroy&&get&&set&&createSettings&&destroySettings&&getClientId) {
                passed &= check(count()==1 && std::strcmp(name(0),"Compressor")==0
                                && std::strcmp(category(0),"Native Fx")==0,"Compressor node is discoverable");
                auto* node=create(0);passed &= check(node!=nullptr,"Compressor node creates");
                if(node) {
                    const auto json=QByteArray(get(node));
                    passed &= check(QJsonDocument::fromJson(json).object().value("channels").toArray().size()==8,"eight channel settings");
                    passed &= check(set(node,json.constData())==1,"settings round trip");
                    passed &= check(set(node,"{\"channels\":[]}")==0,"invalid settings rejected");
                    QWidget* sidebar=createSettings(node,nullptr);
                    auto* editor=sidebar?sidebar->findChild<QLineEdit*>("gate.clientName"):nullptr;
                    auto* apply=sidebar?sidebar->findChild<QPushButton*>("gate.applyClientName"):nullptr;
                    passed &= check(editor&&apply,"sidebar contains client name setting");
                    if(editor&&apply) {editor->setText(QStringLiteral("CompEdited"));apply->click();}
                    passed &= check(std::strcmp(getClientId(node),"CompEdited")==0,"client name applies to node");
                    if(sidebar) destroySettings(node,sidebar);
                    const auto window=node->createWindow(nullptr);
                    passed &= check(window && IsWindow(window),"standalone effect window opens");
                    if(window) {
                        RECT clientRect {};
                        passed &= check(GetClientRect(window,&clientRect)!=FALSE
                                        && clientRect.right-clientRect.left==770,
                                        "Compressor native window client width matches its 770 px reference");
                    }
                    if(window) node->destroyWindow(window);
                    destroy(node);
                }
            }
            FreeLibrary(module);
        }
    }
    return passed?0:1;
}
