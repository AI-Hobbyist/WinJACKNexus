/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QEventLoop>
#include <QTimer>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
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
        float peak=.5F;
        UI::MixerWindow mixer(&state,[&](const auto& next,QString*){state=next;return true;},
            [&](std::uint64_t,bool){return peak;});
        mixer.resize(1024,768);mixer.show();QApplication::processEvents();
        QMenu* menu=nullptr;
        for(auto* action:mixer.menuBar()->actions())if(action->text()==QStringLiteral("查看大屏"))menu=action->menu();
        check(menu&&menu->actions().size()==2&&menu->actions()[0]->text()==QStringLiteral("选中通道")
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
                auto* row=overview->findChild<QWidget*>(QStringLiteral("mixerOverviewRow0"));
                if(row) {
                    const auto before=row->grab().toImage();
                    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-meter-level-zone-low-active"),QStringLiteral("#123abc")),"override inherited mixer meter token");
                    check(core.setOverride(QStringLiteral("--wjn-theme-mixer-fader-input-thumb"),QStringLiteral("#bc34ef")),"override inherited mixer fader token");
                    waitFrame();const auto after=row->grab().toImage();
                    const qreal dpr=after.devicePixelRatio();
                    const int meterY=qRound((43+(row->height()-112)*(1-48./84))*dpr);
                    check(after.pixelColor(qRound(22*dpr),meterY)==QColor("#123abc"),"overview meter inherits main mixer active-zone color");
                    const int thumbY=qRound((43+Common::mixerUnityFaderPosition*(row->height()-112)-9)*dpr);
                    check(before.pixelColor(qRound(60*dpr),thumbY)!=after.pixelColor(qRound(60*dpr),thumbY),"overview fader inherits main mixer thumb color");
                    const QPointF p(60,43+(row->height()-112)*.5);
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
