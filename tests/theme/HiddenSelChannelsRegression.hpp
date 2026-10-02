/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <QApplication>
#include <QAction>
#include <QMenuBar>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QMouseEvent>
template<class Check> void verifyHiddenSelChannels(Check check)
{
    using namespace WinJACKNexus;
    const auto render = [](QWidget& widget) {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        widget.render(&painter);
        return image;
    };
    const auto knobVisible = [](const QImage& image, int x, int row) {
        const int y=100+row*86;
        const auto background=image.pixelColor(x,y+15);
        for(int dy=15;dy<50;++dy)
            for(int dx=0;dx<35;++dx)
                if(image.pixelColor(x+dx,y+dy)!=background) return true;
        return false;
    };
    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
    for (const auto count : {1,2,6,8}) {
        const auto layout=static_cast<Common::MixerLayout>(count);
        const auto auxLayout=count==1?Common::AuxChannelLayout::mono
            :count==2?Common::AuxChannelLayout::stereo
            :count==6?Common::AuxChannelLayout::surround51:Common::AuxChannelLayout::surround71;
        auto state=std::make_shared<Common::AuxMixerState>();
        UI::AuxSettingsWindow aux(state);
        aux.setSource(QStringLiteral("source"),QStringLiteral("Source"),0,auxLayout);
        aux.resize(1360,813);
        auto* bus=state->busForSource(QStringLiteral("source"),aux.selectedBusId());
        // Retained unused state must not create visible or interactive rows.
        bus->sends.resize(8);
        const auto auxImage=render(aux);
        for(int row=0;row<8;++row)
            check(knobVisible(auxImage,1045,row)==(row<count), "AUX shows only source-layout channel knobs");
        if(count<8) {
            const QPointF point(1190,100+count*86+55);
            QMouseEvent click(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(&aux,&click);
            check(!bus->sends[count].mute, "hidden AUX mute hit area is inactive");
        }
        if(count==2) auxImage.save(QStringLiteral("tests/preview/PrePush/sel-aux-active-channels.png"));
        Common::DigitalMixerState model;
        model.add(Common::MixerStripKind::input,layout);
        UI::MixerWindow mixer(&model,[&](const auto& value,QString*) {model=value;return true;});
        for(auto* action:mixer.menuBar()->actions())
            if(action->text()==QStringLiteral("查看大屏")) action->trigger();
        auto* page=mixer.findChild<QWidget*>(QStringLiteral("selMainPage"));
        check(page!=nullptr,"MAIN SEL page exists");
        if(!page) continue;
        page->setFixedSize(1394,813);
        const auto mainImage=render(*page);
        for(int row=0;row<8;++row)
            check(knobVisible(mainImage,1060,row)==(row<count), "MAIN shows only source-layout channel knobs");
        if(count==2) mainImage.save(QStringLiteral("tests/preview/PrePush/sel-main-active-channels.png"));
    }
}
