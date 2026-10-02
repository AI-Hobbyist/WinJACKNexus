/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
// Private composed view: all controls are drawn through the shared UI API.
#pragma once

struct OverviewChannel final {
    std::uint32_t id {};
    std::uint64_t modelId {};
    QString name;
    QColor color;
    qreal fader {};
    qreal levelDb {-72};
    qreal peakDb {-72};
    bool selected {};
};

class MixerOverviewScreen final : public QWidget {
public:
    explicit MixerOverviewScreen(QWidget* parent) : QWidget(parent,Qt::Window) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        if(auto* core=ThemeManager::installedThemeCore())
            connect(core,&ThemeCore::themeChanged,this,[this]{update();});
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(),Controls::themeTokenColor(QStringLiteral("mixer"),QStringLiteral("surface-empty"),QColor("#07131f")));
        ThemeBackground::paint(painter,rect(),QStringLiteral("mixer-overview"));
    }
};

class MixerOverviewRow final : public QWidget {
public:
    MixerOverviewRow(int group, QWidget* parent) : QWidget(parent), group_(group) {
        setMinimumHeight(170);
        setMouseTracking(true);
        setObjectName(QStringLiteral("mixerOverviewRow%1").arg(group));
    }
    std::function<void(std::uint32_t)> selected;
    std::function<void(std::uint32_t,qreal)> faderChanged;

    void setChannels(std::vector<OverviewChannel> channels) {
        bool changed=channels.size()!=channels_.size();
        for(std::size_t i=0;!changed&&i<channels.size();++i) changed=channels[i].id!=channels_[i].id;
        channels_=std::move(channels);
        setMinimumWidth(std::max(1,static_cast<int>(channels_.size())*stripWidth));
        if(changed) {
            for(auto* button:buttons_) delete button;
            buttons_.clear();
            for(const auto& channel:channels_) {
                auto* button=new Controls::ActionButton(QStringLiteral("SEL"),this);
                button->setObjectName(QStringLiteral("overviewSEL%1").arg(channel.modelId));
                button->setCheckable(true);
                connect(button,&QPushButton::clicked,this,[this,id=channel.id] {if(selected)selected(id);});
                buttons_.push_back(button);
            }
        }
        for(std::size_t i=0;i<buttons_.size();++i) {
            auto* button=buttons_[i];
            button->setGeometry(static_cast<int>(i)*stripWidth+5,height()-30,stripWidth-10,25);
            button->setChecked(channels_[i].selected);
            button->setToolTip(channels_[i].name);
            button->setProperty("surfaceColor",color("control-button-surface-normal","#16283a"));
            button->setProperty("borderColor",color("control-button-select-accent","#66809b"));
            button->setProperty("accentColor",color("control-button-select-accent","#66809b"));
            button->setProperty("textColor",color("control-button-text-normal","#dce9ff"));
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(),color("overview-card-surface","#0b1a2b"));
        const auto kind=group_==0?"input":group_==1?"aux":"submix";
        for(std::size_t i=0;i<channels_.size();++i) {
            const auto& channel=channels_[i];
            const qreal x=i*stripWidth;
            Controls::drawSurface(painter,{x+2,2,stripWidth-4,qreal(height()-4)},
                color("surface-empty","#07131f"),channel.color,5);
            Controls::drawSurface(painter,{x+4,4,stripWidth-8,30},channel.color,{},4);
            const qreal brightness=.299*channel.color.red()+.587*channel.color.green()+.114*channel.color.blue();
            const auto nameColor=brightness>145?color("control-name-text-dark","#102033"):color("control-name-text-light","#f5fbff");
            QFont nameFont=font();nameFont.setPixelSize(12);nameFont.setBold(true);
            const auto name=QFontMetrics(nameFont).elidedText(channel.name,Qt::ElideRight,stripWidth-12);
            Controls::drawText(painter,{x+5,4,stripWidth-10,30},name,nameFont,nameColor);
            const QRectF meter(x+17,43,13,std::max(24,height()-112));
            Controls::drawSurface(painter,meter.adjusted(-2,-2,2,2),color("meter-level-surface","#101a22"),
                color("meter-level-surface-border","#0a0f14"),3);
            Controls::MeterStyle style;style.vertical=true;
            style.border=color("meter-level-border","#07131f");
            style.zones={{-12,color("meter-level-zone-low-active","#26dc68"),color("meter-level-zone-low-inactive","#163e2e")},
                         {0,color("meter-level-zone-mid-active","#ffe15a"),color("meter-level-zone-mid-inactive","#4a3f1d")},
                         {12,color("meter-level-zone-high-active","#ff5353"),color("meter-level-zone-high-inactive","#4c242a")}};
            Controls::drawMeter(painter,meter,channel.levelDb,channel.peakDb,style);
            const auto yForDb=[&](qreal db){return meter.bottom()-(db+72)/84*meter.height();};
            painter.setPen(color("meter-level-threshold-line","#07131f"));
            for(const double db:{-12.,0.})painter.drawLine(QPointF(meter.left(),yForDb(db)),QPointF(meter.right(),yForDb(db)));
            Controls::FaderStyle fader;
            const auto token=[&](const char* part,const char* fallback){return color((QStringLiteral("fader-%1-%2").arg(QLatin1String(kind),QLatin1String(part))).toLatin1().constData(),fallback);};
            fader.accent=token("thumb",group_==0?"#9aa8b7":group_==1?"#33b6ff":"#37d879");
            fader.track=token("track","#0a0f15");fader.grip=token("grip","#07131f");
            fader.thumbWidth=20;fader.thumbHeight=28;
            Controls::drawFader(painter,track(i),channel.fader,fader);
            QFont readoutFont=font();readoutFont.setPixelSize(11);
            Controls::drawText(painter,{x+4,qreal(height()-57),stripWidth-8,20},faderDbText(channel.fader),readoutFont,
                color("text-primary","#dce9ff"));
        }
        if(channels_.empty()) {
            Controls::drawText(painter,rect(),QStringLiteral("暂无通道"),font(),color("text-muted","#a9bbd7"));
        }
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        for(std::size_t i=0;i<buttons_.size();++i)
            buttons_[i]->setGeometry(static_cast<int>(i)*stripWidth+5,height()-30,stripWidth-10,25);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if(event->button()!=Qt::LeftButton)return;
        for(std::size_t i=0;i<channels_.size();++i)if(track(i).adjusted(-16,-14,16,14).contains(event->position())) {
            draggingId_=channels_[i].id;setFader(i,event->position().y());return;
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if(!draggingId_ || !(event->buttons()&Qt::LeftButton))return;
        for(std::size_t i=0;i<channels_.size();++i)if(channels_[i].id==draggingId_) {setFader(i,event->position().y());return;}
    }
    void mouseReleaseEvent(QMouseEvent*) override {draggingId_=0;}
private:
    static QColor color(const char* role,const char* fallback) {
        return Controls::themeTokenColor(QStringLiteral("mixer"),QString::fromLatin1(role),QColor(fallback));
    }
    QRectF track(std::size_t index) const {return {qreal(index*stripWidth+57),43,6,qreal(std::max(24,height()-112))};}
    void setFader(std::size_t index,qreal y) {
        const auto bounds=track(index);
        const auto value=std::clamp((y-bounds.top())/bounds.height(),0.,1.);
        channels_[index].fader=value;
        if(faderChanged)faderChanged(channels_[index].id,value);
        update();
    }
    static constexpr int stripWidth=84;
    int group_ {};
    std::uint32_t draggingId_ {};
    std::vector<OverviewChannel> channels_;
    std::vector<Controls::ActionButton*> buttons_;
};
