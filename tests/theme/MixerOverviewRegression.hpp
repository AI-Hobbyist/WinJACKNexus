/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeManagementDialog.hpp>
#include <QGraphicsOpacityEffect>
#include <QBuffer>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QDir>

template<class Check> void verifyMixerOverview(Check check)
{
    using namespace WinJACKNexus;
    Common::DigitalMixerState state;
    std::array<std::vector<std::uint64_t>,3> ids;
    for(int group=0;group<3;++group) for(int n=0;n<(group==0?12:5);++n) {
        const auto kind=group==0?Common::MixerStripKind::input:group==1?Common::MixerStripKind::aux:Common::MixerStripKind::subMix;
        const auto id=state.add(kind,Common::MixerLayout::stereo);ids[group].push_back(id);
        state.strip(id)->name=QStringLiteral("%1-%2").arg(group).arg(n);
    }
    UI::ThemeCore core;UI::ThemeManager::installThemeCore(&core);
    {
        float peak=0.F;
        UI::MixerWindow mixer(&state,[&](const auto& next,QString*){state=next;return true;},
            [&](std::uint64_t,bool){return peak;});
        mixer.resize(1024,768);
        auto* details=mixer.openView(UI::MixerWindow::View::channelDetails);
        check(details&&details->isVisible()&&!mixer.isVisible(),"details opens directly without showing the main mixer");
        auto* directOverview=mixer.openView(UI::MixerWindow::View::channelOverview);
        check(directOverview&&directOverview->isVisible()&&!mixer.isVisible(),"overview opens directly without showing the main mixer");
        mixer.openView(UI::MixerWindow::View::mixer);QApplication::processEvents();
        mixer.close();QApplication::processEvents();
        check(details&&details->isVisible()&&directOverview&&directOverview->isVisible(),"closing main mixer keeps independent views alive");
        auto* selector=details?details->findChild<QToolButton*>(QStringLiteral("selChannelSelector")):nullptr;
        auto* mainTab=details?details->findChild<QPushButton*>(QStringLiteral("selTabMain")):nullptr;
        auto* typeIcon=details?details->findChild<QLabel*>(QStringLiteral("selChannelTypeIcon")):nullptr;
        check(selector&&mainTab&&selector->geometry().right()<mainTab->geometry().left(),"categorized channel selector precedes MAIN tab");
        const auto verifyVolume=[&](QWidget* page,std::uint64_t id,bool master,int group,int stripIndex) {
            const qreal scale=std::min(page->width()/1394.,page->height()/813.);
            const qreal originY=(page->height()-813*scale)/2;
            const qreal x=master?156:224;
            const auto point=[&](qreal y){return QPointF(x*scale,originY+y*scale);};
            const auto send=[&](QEvent::Type type,const QPointF& p,Qt::MouseButton button,Qt::MouseButtons buttons) {
                QMouseEvent event(type,p,p,button,buttons,Qt::NoModifier);QApplication::sendEvent(page,&event);
            };
            const auto gain=state.strip(id)->inputGainDb;const auto old=state.strip(id)->faderPosition;
            send(QEvent::MouseButtonPress,point(366),Qt::LeftButton,Qt::LeftButton);
            send(QEvent::MouseMove,point(386),Qt::NoButton,Qt::LeftButton);
            send(QEvent::MouseButtonRelease,point(386),Qt::LeftButton,Qt::NoButton);
            check(std::abs(state.strip(id)->faderPosition-std::clamp(old+.1,0.,1.))<.001
                &&state.strip(id)->inputGainDb==gain,"SEL volume knob edits actual fader without changing input gain");
            send(QEvent::MouseButtonDblClick,point(366),Qt::LeftButton,Qt::LeftButton);
            check(std::abs(state.strip(id)->faderPosition-Common::mixerUnityFaderPosition)<.001,"SEL volume double-click resets fader to zero dB");
            send(QEvent::MouseButtonPress,point(396),Qt::LeftButton,Qt::LeftButton);
            QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            auto* editor=page->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
            check(editor&&editor->isVisible(),"SEL volume readout opens numeric editor");
            if(editor) {
                editor->setText(editor->locale().toString(-6.,'f',1));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);
                check(std::abs(state.strip(id)->faderPosition-.95*18/84)<.001,"SEL manual volume commits actual fader dB");
            }
            // Edit the matching compact fader and verify the selected detail knob follows it.
            auto* row=directOverview->findChild<QWidget*>(master?QStringLiteral("mixerOverviewFixedMasters"):
                QStringLiteral("mixerOverviewRow%1").arg(group));
            const auto before=page->grab().toImage();const qreal dpr=before.devicePixelRatio();
            const QRect knob(qRound((x-24)*scale*dpr),qRound((originY+343*scale)*dpr),qRound(48*scale*dpr),qRound(44*scale*dpr));
            const QPointF compact(master?144:stripIndex*84+60,54+(row->height()-129)*.5);
            QMouseEvent press(QEvent::MouseButtonPress,compact,compact,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,compact,compact,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(row,&press);QApplication::sendEvent(row,&release);
            check(std::abs(state.strip(id)->faderPosition-.5)<.001&&page->grab().toImage().copy(knob)!=before.copy(knob),
                "external fader edit synchronizes SEL volume knob rendering");
            send(QEvent::MouseButtonDblClick,point(366),Qt::LeftButton,Qt::LeftButton);
        };
        if(selector&&selector->menu()) {
            auto* menu=selector->menu();
            QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
            check(menu->actions().size()==4,"selector has four native channel categories");
            for(int group=0;group<3;++group) {
                auto* category=menu->actions()[group]->menu();
                check(category&&category->actions().size()==ids[group].size(),"selector category preserves all current strips");
                if(category) {
                    check(category->actions().back()->text()==state.strip(ids[group].back())->name,"selector uses custom names");
                    category->actions().back()->trigger();
                    check(typeIcon&&typeIcon->property("channelIconType").toInt()==group,"detail icon matches selected strip type");
                    auto* page=details->findChild<QWidget*>(QStringLiteral("selMainPage"));
                    const qreal scale=std::min(page->width()/1394.,page->height()/813.);
                    const qreal originY=(page->height()-813*scale)/2;
                    const auto click=[&](int column,int row) {
                        const QPointF point((29+column*(254.-16)/3+column*8+(254.-16)/6)*scale,
                                            originY+(422+row*36)*scale);
                        QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(page,&press);QApplication::sendEvent(page,&release);
                    };
                    const auto id=ids[group].back();const auto before=*state.strip(id);
                    click(0,0);check(state.strip(id)->mute!=before.mute,"SEL M commits mute for Input/AUX/Sub Mix");
                    click(1,0);check(state.strip(id)->solo!=before.solo,"SEL S commits SIP solo for Input/AUX/Sub Mix");
                    click(2,0);check(state.strip(id)->mixToMain!=before.mixToMain,"SEL MIX commits main routing for Input/AUX/Sub Mix");
                    click(0,1);check(state.strip(id)->listenMode==Common::MixerListenMode::pfl,"SEL PFL available for Input/AUX/Sub Mix");
                    click(1,1);check(state.strip(id)->listenMode==Common::MixerListenMode::afl,"SEL AFL replaces PFL for Input/AUX/Sub Mix");
                    click(1,1);check(state.strip(id)->listenMode==Common::MixerListenMode::off,"SEL listen selection toggles off");
                    click(2,1);check(state.strip(id)->fxChainEnabled!=before.fxChainEnabled,"SEL FX commits chain bypass for Input/AUX/Sub Mix");
                    click(2,1);click(0,0);click(1,0);click(2,0);
                    verifyVolume(page,id,false,group,static_cast<int>(ids[group].size()-1));
                    QDir().mkpath(QStringLiteral("tests/preview/PrePush"));
                    details->grab().save(QStringLiteral("tests/preview/PrePush/mixer-details-%1.png").arg(group));
                }
            }
            auto* masters=menu->actions().back()->menu();
            check(masters&&masters->actions().size()==2,"Listen/Master selector includes main and monitor");
            if(masters) {
                masters->actions()[0]->trigger();check(typeIcon&&typeIcon->property("channelIconType").toInt()==3,"master uses vector speaker icon");
                if(auto* page=details->findChild<QWidget*>(QStringLiteral("selMainPage"))) {
                    const qreal scale=std::min(page->width()/1394.,page->height()/813.);
                    const qreal originY=(page->height()-813*scale)/2;
                    const auto toggleMaster=[&](int row) {
                        const QPointF point(156*scale,originY+(422+row*36)*scale);
                        QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(page,&press);QApplication::sendEvent(page,&release);
                    };
                    const auto before=state.main();
                    toggleMaster(0);check(state.main().mute!=before.mute&&!state.main().solo,"MASTER single-column first row only toggles mute");
                    toggleMaster(1);check(state.main().fxChainEnabled!=before.fxChainEnabled,"MASTER second row toggles FX");
                    toggleMaster(1);toggleMaster(0);
                    verifyVolume(page,state.main().id,true,0,0);
                    details->grab().save(QStringLiteral("tests/preview/PrePush/mixer-details-master.png"));
                    const auto image=page->grab().toImage();const qreal dpr=image.devicePixelRatio();
                    bool blank=true;
                    const auto background=image.pixelColor(qRound(30*scale*dpr),qRound(330*scale*dpr));
                    for(int y=qRound(327*scale*dpr);y<qRound(400*scale*dpr);++y)
                        for(int x=qRound(29*scale*dpr);x<qRound(120*scale*dpr);++x)
                            blank=blank&&image.pixelColor(x,y)==background;
                    check(blank,"MASTER input gain area remains empty beside its centered volume knob");
                    const auto gain=state.main().inputGainDb;
                    const QPointF value(180*scale,376*scale);
                    QMouseEvent click(QEvent::MouseButtonPress,value,value,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(page,&click);
                    auto* editor=page->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
                    check(!editor||!editor->isVisible(),"MASTER removed gain area cannot open numeric editor");
                    const QPointF knob(93*scale,378*scale),drag(93*scale,350*scale);
                    QMouseEvent press(QEvent::MouseButtonPress,knob,knob,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent move(QEvent::MouseMove,drag,drag,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent reset(QEvent::MouseButtonDblClick,knob,knob,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,knob,knob,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(page,&press);QApplication::sendEvent(page,&move);
                    QApplication::sendEvent(page,&reset);QApplication::sendEvent(page,&release);
                    check(state.main().inputGainDb==gain,"MASTER removed gain area cannot drag or reset gain");
                }
                masters->actions()[1]->trigger();
                check(typeIcon&&typeIcon->property("channelIconType").toInt()==4
                    &&details->findChild<QWidget*>(QStringLiteral("monitorSelPage"))->isVisible()
                    &&mainTab->isVisible()&&!details->findChild<QPushButton*>(QStringLiteral("selTabFX"))->isVisible(),
                    "monitor uses headphones and only MAIN in shared details");
            }
            menu->actions()[0]->menu()->actions()[0]->trigger();
            const auto oldIcon=typeIcon->pixmap().toImage();
            check(core.setOverride(QStringLiteral("--wjn-theme-sel-main-control-header-icon"),QStringLiteral("#ef1278")),"header icon token registered");
            check(typeIcon->pixmap().toImage()!=oldIcon,"header icon reacts to theme token");
            core.removeOverride(QStringLiteral("--wjn-theme-sel-main-control-header-icon"));
            check(core.setOverride(QStringLiteral("--wjn-theme-sel-main-control-channel-selector-surface"),QStringLiteral("#123abc"))
                &&selector->property("surfaceColor").value<QColor>()==QColor("#123abc"),"selector token registered and applied live");
            core.removeOverride(QStringLiteral("--wjn-theme-sel-main-control-channel-selector-surface"));
            // UI rename follows the same stable strip rather than a cached menu label.
            auto* name=details->findChild<QLineEdit*>(QStringLiteral("selChannelName"));
            name->setText(QStringLiteral("重命名输入"));QMetaObject::invokeMethod(name,"editingFinished",Qt::DirectConnection);
            QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
            check(menu->actions()[0]->menu()->actions()[0]->text()==QStringLiteral("重命名输入"),"selector refreshes renamed strips");
        }
        mixer.openView(UI::MixerWindow::View::mixer);QApplication::processEvents();
        peak=.5F;
        check(core.snapshot()->hasValue(QStringLiteral("--wjn-theme-sel-main-control-mix-accent"))
            &&core.snapshot()->hasValue(QStringLiteral("--wjn-theme-sel-main-control-fx-accent"))
            &&core.snapshot()->hasValue(QStringLiteral("--wjn-theme-sel-main-control-volume-accent")),"new sidebar button theme tokens registered");
        QMenu* menu=nullptr;
        for(auto* action:mixer.menuBar()->actions())if(action->text()==QStringLiteral("查看大屏"))menu=action->menu();
        check(menu&&menu->actions().size()==2&&menu->actions()[0]->text()==QStringLiteral("通道详情")
            &&menu->actions()[1]->text()==QStringLiteral("通道总览"),"large-screen native menu exposes selected channel and overview");
        if(menu) {
            menu->actions()[0]->trigger();menu->actions()[1]->trigger();QApplication::processEvents();
            auto* overview=mixer.findChild<QWidget*>(QStringLiteral("mixerChannelOverview"));
            check(overview&&overview->isWindow()&&overview->isVisible(),"overview is a separate visible window");
            if(overview) {
                check(overview->findChildren<QTimer*>().empty(),"overview reuses mixer refresh without an independent timer");
                const auto waitFrame=[](){QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();};
                waitFrame();
                for(int group=0;group<3;++group) {
                    auto* row=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow%1").arg(group));
                    check(row&&row->findChildren<QPushButton*>().size()==ids[group].size(),"each overview row contains exactly its channel type");
                    auto* bar=overview->findChild<QScrollBar*>(QStringLiteral("overviewScrollBar%1").arg(group));
                    check(bar&&bar->isVisible(),"every overview row has a visible independent scrollbar");
                    if(!row)continue;
                    auto* select=row->findChild<QPushButton*>(QStringLiteral("overviewSEL%1").arg(ids[group].back()));
                    check(select,"last strip has a shared SEL button");
                    if(select) {
                        select->click();QApplication::processEvents();
                        auto* title=mixer.findChild<QLabel*>(QStringLiteral("selSelectedChannel"));
                        check(title&&title->text()==state.strip(ids[group].back())->name,"overview SEL switches existing large-screen channel");
                        check(mixer.property("mixerFocusedStripId").toULongLong()==ids[group].back()
                            &&std::abs(mixer.property("mixerFocusLeadingOffset").toDouble())<.001,
                            "last selected channel aligns to leading edge in main mixer for every group");
                    }
                }
                // Exercise all strip categories through the actual mouse/numeric handlers.
                for(int group=0;group<3;++group) {
                    auto* row=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow%1").arg(group));
                    if(!row)continue;
                    const auto click=[&](QEvent::Type type,const QPointF& point) {
                        QMouseEvent event(type,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                        QApplication::sendEvent(row,&event);
                        QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(row,&release);
                    };
                    const auto headerBefore=row->grab().toImage();
                    click(QEvent::MouseButtonPress,{60,54});
                    check(state.strip(ids[group][0])->faderPosition==0,"overview fader reaches maximum");
                    const auto headerAfter=row->grab().toImage();
                    const auto dpr=headerAfter.devicePixelRatio();
                    check(headerBefore.copy(4*dpr,4*dpr,76*dpr,30*dpr)==headerAfter.copy(4*dpr,4*dpr,76*dpr,30*dpr),
                        "maximum fader does not cover channel title");
                    click(QEvent::MouseButtonDblClick,{60,54});
                    check(std::abs(state.strip(ids[group][0])->faderPosition-Common::mixerUnityFaderPosition)<.001,
                        "overview double-click resets real strip to zero dB");
                    click(QEvent::MouseButtonPress,{40,qreal(row->height()-47)});
                    auto* editor=row->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
                    check(editor&&editor->isVisible(),"overview readout opens shared numeric editor");
                    if(editor) {
                        editor->setText(editor->locale().toString(-6.,'f',1));
                        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                        QApplication::sendEvent(editor,&enter);
                        check(std::abs(state.strip(ids[group][0])->faderPosition-.95*18/84)<.001,
                            "overview manual dB entry commits to real strip");
                    }
                    click(QEvent::MouseButtonDblClick,{60,54});
                }
                auto* row=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow0"));
                if(row) {
                    const auto before=row->grab().toImage();
                    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-meter-level-zone-low-active"),QStringLiteral("#123abc")),"override inherited mixer meter token");
                    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-fader-input-thumb"),QStringLiteral("#bc34ef")),"override inherited mixer fader token");
                    waitFrame();const auto after=row->grab().toImage();
                    const qreal dpr=after.devicePixelRatio();
                    const int meterY=qRound((43+(row->height()-112)*(1-48./84))*dpr);
                    check(after.pixelColor(qRound(22*dpr),meterY)==QColor("#123abc"),"overview meter inherits main mixer active-zone color");
                    const int thumbY=qRound((54+Common::mixerUnityFaderPosition*(row->height()-129)-9)*dpr);
                    check(before.pixelColor(qRound(60*dpr),thumbY)!=after.pixelColor(qRound(60*dpr),thumbY),"overview fader inherits main mixer thumb color");
                    const QPointF p(60,54+(row->height()-129)*.5);
                    QMouseEvent press(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(row,&press);QApplication::sendEvent(row,&release);
                    check(std::abs(state.strip(ids[0][0])->faderPosition-.5)<.001,"compact fader edits real mixer model");
                    peak=0;waitFrame();
                    check(row->grab().toImage()!=after,"overview renders changing levels and fader state");
                    core.removeOverride(QStringLiteral("--wjn-theme-mixer-meter-level-zone-low-active"));
                    core.removeOverride(QStringLiteral("--wjn-theme-mixer-fader-input-thumb"));
                }
                for(const auto* role:{"overview-card-surface","overview-card-border","overview-title-input-surface","overview-title-input-text","overview-title-input-border",
                    "overview-title-aux-surface","overview-title-aux-text","overview-title-aux-border","overview-title-submix-surface","overview-title-submix-text","overview-title-submix-border"})
                    check(core.snapshot()->hasValue(QStringLiteral("--wjn-theme-mixer-")+QLatin1String(role)),"overview card/title token registered");
                overview->resize(600,800);QApplication::processEvents();waitFrame();
                auto* fixed=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewFixedMasters"));
                auto* subRow=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow2"));
                auto* subBar=overview->findChild<QScrollBar*>(QStringLiteral("overviewScrollBar2"));
                check(fixed&&fixed->findChildren<QPushButton*>().size()==2
                    &&overview->findChild<QLabel*>(QStringLiteral("overviewTitle2"))->text()==QStringLiteral("Sub Mix/Monitor/Master"),
                    "third overview row includes fixed Monitor/Master and combined title");
                if(fixed&&subRow&&subBar) {
                    const auto before=fixed->mapTo(overview,QPoint());
                    check(before.x()>subRow->parentWidget()->mapTo(overview,QPoint()).x(),"Monitor/Master are fixed on the right");
                    subBar->setValue(subBar->maximum());
                    check(fixed->mapTo(overview,QPoint())==before,"scrolling Sub Mix does not move Monitor/Master");
                    const auto edit=[&](int x) {
                        const QPointF p(x,54+(fixed->height()-129)*.5);
                        QMouseEvent press(QEvent::MouseButtonPress,p,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease,p,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(fixed,&press);QApplication::sendEvent(fixed,&release);
                    };
                    edit(60);check(std::abs(state.monitorFaderPosition()-.5)<.001,"fixed monitor fader commits real monitor gain");
                    edit(144);check(std::abs(state.main().faderPosition-.5)<.001,"fixed MASTER fader commits real main gain");
                    auto* mon=fixed->findChild<QPushButton*>(QStringLiteral("overviewSEL0"));
                    mon->click();check(details->findChild<QWidget*>(QStringLiteral("monitorSelPage"))->isVisible(),"fixed monitor SEL opens monitor details");
                    auto* master=fixed->findChild<QPushButton*>(QStringLiteral("overviewSEL%1").arg(state.main().id));
                    master->click();check(typeIcon->property("channelIconType").toInt()==3,"fixed MASTER SEL opens main details");
                    subBar->setValue(0);
                }
                auto* inputBar=overview->findChild<QScrollBar*>(QStringLiteral("overviewScrollBar0"));
                auto* auxBar=overview->findChild<QScrollBar*>(QStringLiteral("overviewScrollBar1"));
                check(inputBar&&inputBar->maximum()>0,"narrow overview supports horizontal scrolling");
                if(inputBar&&auxBar) {
                    inputBar->setValue(inputBar->maximum());
                    check(auxBar->value()==0,"overview rows scroll independently");
                    core.setOverride(QStringLiteral("--wjn-theme-mixer-control-scrollbar-input-thumb"),QStringLiteral("#ef1234"));waitFrame();
                    auto expected=QColor("#ef1234");expected.setAlpha(210);
                    check(inputBar->property("fxAccent").value<QColor>()==expected,"overview scrollbar inherits corresponding mixer thumb token");
                    core.removeOverride(QStringLiteral("--wjn-theme-mixer-control-scrollbar-input-thumb"));inputBar->setValue(0);
                }
                overview->resize(1100,800);QApplication::processEvents();
                {
                    UI::ThemeManagementDialog dialog(core);
                    auto* scope=dialog.findChild<QComboBox*>(QStringLiteral("theme.backgroundScope"));
                    check(scope&&scope->findData(QStringLiteral("mixer-overview"))>=0,"background settings expose independent overview scope");
                }
                const auto solidImage=[](const QColor& color) {QImage image(2,2,QImage::Format_ARGB32);image.fill(color);QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");return png;};
                UI::ThemePackage theme;QString error;
                const QJsonObject meta{{"formatVersion",1},{"themeApiVersion",1},{"id","org.winjacknexus.overview-test"},{"name","Overview"},
                    {"version","1.0.0"},{"author","Tests"},{"description","Offline background verification"},{"homepage",""},{"kind","full"},
                    {"namespaces",QJsonArray{"--wjn-theme-patchbay-","--wjn-theme-mixer-overview-"}}};
                check(UI::ThemePackage::create(meta,QByteArray(":root {--wjn-theme-patchbay-background-image:url(\"assets/red.png\");}"),
                    {{QStringLiteral("assets/red.png"),solidImage(QColor("#f02030"))},{QStringLiteral("assets/green.png"),solidImage(QColor("#20e040"))}},&theme,&error)
                    &&core.setFullTheme(theme,&error),"install overview inherited-background fixture");
                waitFrame();
                check(core.snapshot()->resource(QStringLiteral("--wjn-theme-mixer-overview-background-image")).logicalName()==QStringLiteral("assets/red.png"),"overview image defaults to patchbay inheritance");
                check(overview->grab().toImage().pixelColor(1,1)==QColor("#f02030"),"overview renders inherited patchbay background");
                check(core.setOverride(QStringLiteral("--wjn-theme-mixer-overview-background-image"),QStringLiteral("url(\"assets/green.png\")"),theme,&error),"overview can select its own background image");waitFrame();
                check(overview->grab().toImage().pixelColor(1,1)==QColor("#20e040")
                    &&core.snapshot()->resource(QStringLiteral("--wjn-theme-patchbay-background-image")).logicalName()==QStringLiteral("assets/red.png"),"independent overview background does not change patchbay");
                core.setOverride(QStringLiteral("--wjn-theme-mixer-overview-background-component-opacity"),QStringLiteral("0.5"));waitFrame();
                auto* card=overview->findChild<QWidget*>(QStringLiteral("overviewCard0"));
                auto* opacity=card?qobject_cast<QGraphicsOpacityEffect*>(card->graphicsEffect()):nullptr;
                check(opacity&&opacity->opacity()==.5,"overview background component opacity applies to whole row");
                core.setOverride(QStringLiteral("--wjn-theme-mixer-overview-background-component-opacity"),QStringLiteral("1"));
                core.setOverride(QStringLiteral("--wjn-theme-mixer-overview-background-image"),QStringLiteral("none"));waitFrame();
                check(overview->grab().toImage().pixelColor(1,1)==QColor("#07131f"),"overview can explicitly disable inherited image");
                QDir().mkpath(QStringLiteral("tests/preview/PrePush"));overview->grab().save(QStringLiteral("tests/preview/PrePush/mixer-overview.png"));
                for(auto* action:mixer.menuBar()->actions())if(action->menu())
                    for(auto* item:action->menu()->actions())if(item->text()==QStringLiteral("添加AUX通道")&&item->menu())
                        item->menu()->actions().front()->trigger();
                waitFrame();
                auto* auxRow=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow1"));
                check(auxRow&&auxRow->findChildren<QPushButton*>().size()==6,"open overview follows newly added AUX channels");
                // Close and reopen exercises destruction of callbacks and recreates three rows.
                overview->close();QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
                menu->actions()[1]->trigger();QApplication::processEvents();
                check(mixer.findChild<QWidget*>(QStringLiteral("mixerChannelOverview"))!=nullptr,"overview can reopen safely");
            }
        }
    }
    UI::ThemeManager::installThemeCore(nullptr);
}
