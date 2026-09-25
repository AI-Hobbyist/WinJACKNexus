#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>

#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QLineEdit>
#include <QColorDialog>
#include <QComboBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QFontMetricsF>
#include <QRandomGenerator>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace WinJACKNexus::UI {
namespace {

struct Channel {
    QString name;
    QString source;
    QString number;
    QColor accent;
    qreal fader;
    qreal level;
    qreal target;
    bool aux = false;
    bool fx = false;
    bool mute = false;
    bool solo = false;
    bool postFaderMeter = false;
    std::uint32_t auxId {};
    std::uint32_t id {};
    Common::AuxChannelLayout layout { Common::AuxChannelLayout::mono };
    Common::AuxBusState selState;
    bool customColor {};
    qreal inputGainDb {};
};

QString auxSourceNumber(const Channel& channel)
{
    if(channel.auxId)return QStringLiteral("AUX %1").arg(channel.auxId);
    if(channel.number.startsWith(QStringLiteral("CH ")))return channel.number;
    return QStringLiteral("CH %1").arg(channel.number);
}

qreal faderDbValue(qreal position);

qreal selContentScale(const QSize& size)
{
    return std::min(size.width()/1394.0,size.height()/813.0);
}

qreal selDetailsLeft(const QSize& size)
{
    const qreal scale=selContentScale(size);
    const qreal rightRegionStart=313.0*scale;
    return rightRegionStart+(size.width()-rightRegionStart-1064.0*scale)/2.0;
}

class SelMainPage final : public QWidget {
public:
    explicit SelMainPage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("selMainPage"));
        setMinimumSize(950, 650);
        name = new QLineEdit(this);
        name->setObjectName(QStringLiteral("selChannelName"));
        comment = new QLineEdit(this);
        comment->setObjectName(QStringLiteral("selChannelComment"));
        modeButton = new QPushButton(this);
        modeButton->setObjectName(QStringLiteral("selChannelMode"));
        modeButton->setStyleSheet(QStringLiteral("QPushButton {background:#102033;color:#eff6ff;border:1px solid #36516d;border-radius:5px;font-size:15px;text-align:left;padding:4px 30px 4px 12px;} QPushButton:disabled {color:#879ab0;border-color:#36516d;}"));
        modeArrow = new QLabel(QStringLiteral("▾"), modeButton);
        modeArrow->setObjectName(QStringLiteral("selChannelModeArrow"));
        modeArrow->setAlignment(Qt::AlignCenter);
        modeArrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        modeArrow->setStyleSheet(QStringLiteral("color:#d6eaff;background:transparent;font-size:15px;"));
        connect(modeButton, &QPushButton::clicked, this, [this] {
            if (!channel || channel->number == QStringLiteral("MAIN")) return;
            QMenu menu(modeButton);
            const std::array layouts {Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                      Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71};
            for (const auto layout : layouts) {
                auto* action = menu.addAction(Common::auxLayoutName(layout));
                action->setCheckable(true);
                action->setChecked(channel->layout == layout);
                connect(action, &QAction::triggered, this, [this, layout] {
                    if (layoutChanged) layoutChanged(layout);
                });
            }
            menu.exec(modeButton->mapToGlobal(QPoint(0, modeButton->height())));
        });
        colorButton = new QPushButton(QStringLiteral("..."), this);
        colorButton->setObjectName(QStringLiteral("selCustomColor"));
        for (auto* edit : { name, comment }) {
            edit->setStyleSheet(QStringLiteral("QLineEdit {background:#102033;color:#eff6ff;border:1px solid #36516d;border-radius:5px;padding:4px 12px;font-size:15px;}"));
            connect(edit, &QLineEdit::editingFinished, this, [this] {
                if (!channel) return;
                channel->name = name->text().trimmed();
                channel->source = comment->text();
                if (changed) changed();
                if (detailsChanged) detailsChanged();
            });
        }
        colorButton->setStyleSheet(QStringLiteral("QPushButton {background:#172a40;color:#d6eaff;border:1px solid #50708c;border-radius:5px;font-size:15px;}"));
        connect(colorButton, &QPushButton::clicked, this, [this] {
            if (!channel) return;
            QColorDialog dialog(channel->accent, this);
            dialog.setWindowTitle(QStringLiteral("自定义通道颜色 · RGB / HEX"));
            dialog.setOption(QColorDialog::DontUseNativeDialog);
            dialog.setOption(QColorDialog::ShowAlphaChannel, false);
            if (dialog.exec() == QDialog::Accepted) {
                channel->accent = dialog.selectedColor();
                channel->customColor = true;
                update();
                if (changed) changed();
            }
        });
    }

    void setChannel(Channel* next)
    {
        channel = next;
        if (channel) {
            const bool switched=displayedId!=channel->id;
            if(switched){displayedId=channel->id;selectedPoint=0;}
            if (switched || !name->hasFocus()) name->setText(channel->name);
            if (switched || !comment->hasFocus()) comment->setText(channel->source);
            modeButton->setText(Common::auxLayoutName(channel->layout));
            modeButton->setEnabled(channel->number != QStringLiteral("MAIN"));
            modeArrow->setVisible(channel->number != QStringLiteral("MAIN"));
            auto& state=channel->selState;
            const bool fresh=state.sends.size()!=8;
            const bool wasEligible=Common::auxSupportsClassicPan(state.sourceLayout,state.targetLayout);
            const bool eligible=Common::auxSupportsClassicPan(channel->layout,channel->layout);
            if(fresh || wasEligible!=eligible)
                state.panMode=eligible?Common::AuxPanMode::classic:Common::AuxPanMode::spatial;
            state.linked=false;
            selectedPoint=std::clamp(selectedPoint,0,std::max(0,static_cast<int>(Common::auxLayoutChannelLabels(channel->layout).size())-1));
            if(!Common::auxHasSpatialPoint(channel->layout,selectedPoint))selectedPoint=0;
            if(state.adjustablePoints.size()!=static_cast<std::size_t>(Common::auxLayoutChannelLabels(channel->layout).size()))
                state.adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);
            if(state.pointInfluenceRadii.size()!=state.adjustablePoints.size())
                state.pointInfluenceRadii.resize(state.adjustablePoints.size(),0.1);
            state.sourceLayout=channel->layout;
            state.targetLayout=channel->layout;
            setProperty("selPanMode",static_cast<int>(state.panMode));
            setProperty("selClassicPan",state.classicPan);
            setProperty("selChannelLayout",static_cast<int>(channel->layout));
            setProperty("selActiveSendCount",Common::auxLayoutChannelLabels(channel->layout).size());
            setProperty("selInputGainDb",channel->inputGainDb);
            if(state.sends.size()!=8) {
                const std::array<QString,8> labels {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
                state.sends.resize(8);
                for(std::size_t i=0;i<labels.size();++i) {
                    state.sends[i].label=labels[i];state.sends[i].name=labels[i];
                    state.sends[i].levelDb=-2.0;state.sends[i].meterDb=-12.0;
                }
            }
            linked=state.sendsLinked;
            publishLevels();
            publishPanner();
        }
        update();
    }
    std::function<void()> changed;
    std::function<void()> detailsChanged;
    std::function<void(Common::AuxChannelLayout)> layoutChanged;

protected:
    void resizeEvent(QResizeEvent*) override
    {
        const qreal scale=contentScale();const QPointF origin=contentOrigin();
        const auto place = [scale,origin](QWidget* widget, int x, int y, int w, int h) {
            widget->setGeometry(qRound(origin.x()+x*scale),qRound(origin.y()+y*scale),qRound(w*scale),qRound(h*scale));
        };
        place(name, 29, 49, 254, 34);
        place(comment, 29, 129, 254, 34);
        place(modeButton, 29, 209, 254, 34);
        modeArrow->setGeometry(modeButton->width()-qRound(24*scale), 0,
                               qRound(18*scale), modeButton->height());
        place(colorButton, 252, 289, 31, 34);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#0b1a2b"));
        p.translate(contentOrigin());
        p.scale(contentScale(), contentScale());
        p.fillRect(QRect(0, 0, 1394, 813), QColor("#0b1a2b"));
        const QColor border=channel&&channel->customColor?channel->accent:QColor("#2b4662");
        const QColor ink("#e8f2ff"), muted("#aec7eb"), blue("#26bfff");
        auto panel = [&](QRectF r) { p.setPen(border); p.setBrush(QColor("#102235")); p.drawRoundedRect(r, 7, 7); };
        auto text = [&](QRectF r, const QString& s, int size=15, QColor c=QColor("#e8f2ff"), bool bold=false, int align=Qt::AlignLeft|Qt::AlignVCenter) {
            QFont f=p.font(); f.setPixelSize(size); f.setBold(bold); p.setFont(f); p.setPen(c); p.drawText(r, align, s);
        };
        auto button = [&](QRectF r, const QString& label, bool active=false, QColor accent=QColor("#26bfff")) {
            p.setPen(active ? accent : QColor("#45617e")); p.setBrush(active ? QColor("#123e61") : QColor("#192d43")); p.drawRoundedRect(r, 5, 5);
            text(r, label, 15, active ? QColor("#d7f7ff") : muted, false, Qt::AlignCenter);
        };
        auto toggle = [&](QRectF r, bool checked) {
            p.setPen(Qt::NoPen);p.setBrush(checked?QColor("#168dff"):QColor("#263e53"));
            p.drawRoundedRect(r,r.height()/2,r.height()/2);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(checked?r.right()-r.height()/2:r.left()+r.height()/2,r.center().y()),r.height()*.34,r.height()*.34);
        };
        auto modeToggle = [&](QRectF r,const QString& label,bool checked,QColor accent) {
            p.setPen(QPen(accent,1));
            QColor fill=accent;fill.setAlpha(checked?85:25);p.setBrush(fill);p.drawRoundedRect(r,5,5);
            text(r,label,16,QColor("#f7f8ff"),true,Qt::AlignCenter);
        };
        panel({14, 11, 287, 790});
        text({29, 22, 240, 21}, QStringLiteral("通道名"), 14, ink, true);
        text({29, 102, 240, 21}, QStringLiteral("注释"), 14, ink, true);
        text({29, 182, 240, 21}, QStringLiteral("通道模式"), 14, ink, true);
        text({29, 262, 240, 21}, QStringLiteral("通道颜色"), 14, ink, true);
        const std::array<QColor,8> colors { QColor("#24b8ff"), QColor("#20c5dd"), QColor("#45d877"), QColor("#ffd246"), QColor("#ffae55"), QColor("#f34d70"), QColor("#e359da"), QColor("#9963ec") };
        p.setPen(border); p.setBrush(QColor("#15263a")); p.drawRoundedRect(QRectF(29,289,216,34),5,5);
        for (int i=0;i<8;++i) { p.setPen(Qt::NoPen); p.setBrush(colors[i]); p.drawEllipse(QPointF(47+i*26,306),9,9); }
        text({29, 327, 254, 22}, QStringLiteral("输入增益  -12 ~ +12 dB"), 14, muted, false, Qt::AlignCenter);
        const QRectF gainRing(70, 355, 46, 46);
        const qreal gain=channel?std::clamp(channel->inputGainDb,-12.0,12.0):0.0;
        const qreal gainFraction=(gain+12.0)/24.0;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#254762"),5,Qt::SolidLine,Qt::RoundCap));
        p.drawArc(gainRing,225*16,-270*16);
        p.setPen(QPen(blue,5,Qt::SolidLine,Qt::RoundCap));
        p.drawArc(gainRing,225*16,qRound(-270*16*gainFraction));
        const qreal gainAngle=(135.0+270.0*gainFraction)*3.14159265358979323846/180.0;
        const QPointF gainCenter=gainRing.center();
        p.setPen(Qt::NoPen);p.setBrush(QColor("#1b3b57"));p.drawEllipse(gainCenter,18,18);
        p.setPen(QPen(QColor("#d9f4ff"),3,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(gainCenter+QPointF(std::cos(gainAngle),std::sin(gainAngle))*8,
                   gainCenter+QPointF(std::cos(gainAngle),std::sin(gainAngle))*17);
        text({126, 362, 126, 28}, QStringLiteral("%1%2 dB").arg(gain>=0?QStringLiteral("+"):QString()).arg(gain,0,'f',1),
             17, ink, true, Qt::AlignCenter);
        const bool mainOutput=channel && channel->number==QStringLiteral("MAIN");
        modeToggle({29,407,mainOutput?254.0:118.0,34},QStringLiteral("M"),channel && channel->mute,QColor("#ff4b62"));
        if(!mainOutput)modeToggle({165,407,118,34},QStringLiteral("S"),channel && channel->solo,QColor("#ffae2d"));
        text({29, 449, 200, 26}, QStringLiteral("电平表"), 14, ink, true);
        for(int m=0;m<2;++m) {
            const int x=24+m*138; panel({qreal(x),480,125,318});
            text({qreal(x),488,125,31},m?QStringLiteral("输出"):QStringLiteral("输入"),16,ink,false,Qt::AlignCenter);
            const std::array<int,6> ticks {12,0,-12,-24,-48,-72};
            constexpr qreal meterTop=528, meterBottom=745;
            for(const int tick:ticks) text({qreal(x+14),meterTop+(12-tick)*(meterBottom-meterTop)/84.0-10,40,20},QString::number(tick),12,muted,false,Qt::AlignRight|Qt::AlignVCenter);
            const qreal input=channel?channel->level+channel->inputGainDb:-72.0;
            const qreal current=channel?std::clamp(input+(m?faderDbValue(channel->fader):0.0),-72.0,12.0):-72.0;
            const qreal fraction=(current+72.0)/84.0;
            QRectF track(x+67,meterTop,28,meterBottom-meterTop);p.setPen(Qt::NoPen);p.setBrush(QColor("#07141f"));p.drawRoundedRect(track,3,3);
            const qreal litTop=meterBottom-(meterBottom-meterTop)*fraction;
            const auto segment=[&](qreal low,qreal high,QColor color){const qreal a=meterBottom-(meterBottom-meterTop)*(high+72)/84.0,b=meterBottom-(meterBottom-meterTop)*(low+72)/84.0;const qreal top=std::max(a,litTop);if(b>top){p.setBrush(color);p.drawRect(QRectF(71+x,top,20,b-top));}};
            segment(-72,-12,QColor("#17d675"));segment(-12,0,QColor("#f2df48"));segment(0,12,QColor("#f2484d"));
            text({qreal(x+22),757,96,31},QStringLiteral("%1 dB").arg(current,0,'f',1),15,ink,false,Qt::AlignCenter);
        }
        p.translate(selDetailsLeft(size())/contentScale()-313.0,0);
        panel({313, 11, 650, 544});
        panel({313, 566, 650, 235});
        panel({974, 11, 403, 790});
        text({330,20,250,29},QStringLiteral("声像器"),17,ink,true);
        const auto* pannerBus=selectedBus();
        const bool classic=pannerBus&&pannerBus->panMode==Common::AuxPanMode::classic;
        const bool canUseClassic=channel&&Common::auxSupportsClassicPan(channel->layout,channel->layout);
        button({330,52,137,34},QStringLiteral("空间声像器"),!classic);
        p.save();if(!canUseClassic)p.setOpacity(.32);
        button({477,52,137,34},QStringLiteral("经典 Pan"),classic);
        p.restore();
        if(!classic){text({638,52,118,34},QStringLiteral("显示分布图"),14,muted);toggle({761,56,42,25},distribution);}
        panel({330,95,616,448});
        const QPointF center(638,319); const qreal radius=190;
        const qreal pointRadius=159;
        if(!classic){
        p.setBrush(Qt::NoBrush);
        for(int ring=1;ring<=5;++ring){p.setPen(QPen(QColor("#244a66"),1));p.drawEllipse(center,radius*ring/5,radius*ring/5);}
        for(int angle=0;angle<360;angle+=30){
            const qreal radians=angle*3.14159265358979323846/180.0;
            p.setPen(QPen(angle%90==0?QColor("#426884"):QColor("#1b3d56"),1));
            p.drawLine(center,center+QPointF(std::sin(radians),-std::cos(radians))*radius);
        }
        const auto outputGains=pannerBus&&selectedPoint<static_cast<int>(pannerBus->adjustablePoints.size())
            ? Common::auxSpatialOutputGains(*pannerBus,selectedPoint):std::vector<double>{};
        const auto referenceLayout=channel?channel->layout:Common::AuxChannelLayout::stereo;
        const auto referencePoints=Common::auxLayoutReferencePoints(referenceLayout);
        for(int i=0;i<static_cast<int>(referencePoints.size());++i){
            if(!Common::auxHasSpatialPoint(referenceLayout,i))continue;
            const auto gain=distribution&&i<static_cast<int>(outputGains.size())?outputGains[i]:0.0;
            p.setPen(QPen(QColor(138,169,214,110+qRound(gain*145)),2));
            p.setBrush(QColor(109,141,184,75+qRound(gain*165)));
            p.drawEllipse(center+referencePoints[i]*pointRadius,10+gain*3,10+gain*3);
        }
        if(pannerBus){
            for(int i=0;i<static_cast<int>(pannerBus->adjustablePoints.size());++i){
                if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                const auto& point=pannerBus->adjustablePoints[i];
                const QPointF screen=center+point*pointRadius;
                const bool selected=i==selectedPoint;
                const auto influence=pannerBus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                    ? pannerBus->pointInfluenceRadii[i]:0.1;
                const auto outerRadius=Common::auxInfluencePixelRadius(influence);
                p.setPen(QPen(selected?QColor("#79e3ff"):QColor(85,189,233,115),selected?2.5:1.2));
                p.setBrush(QColor(22,141,255,selected?28:9));p.drawEllipse(screen,outerRadius,outerRadius);
                if(selected){p.setPen(Qt::NoPen);p.setBrush(QColor("#79e3ff"));
                    p.drawEllipse(screen+QPointF(outerRadius,0),4,4);}
                if(selected){p.setPen(QPen(QColor("#7ee8ff"),5));p.setBrush(Qt::NoBrush);p.drawEllipse(screen,14,14);}
                p.setPen(QPen(selected?QColor("#e8fbff"):QColor("#9dc3ff"),selected?3:2));p.setBrush(selected?QColor("#42a8ff"):QColor("#2e9dff"));p.drawEllipse(screen,10,10);
                const auto labels=Common::auxLayoutChannelLabels(channel->layout);
                if(i<labels.size())text({screen.x()-17,screen.y()+14,34,19},labels[i],12,ink,true,Qt::AlignCenter);
            }
        }
        p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawEllipse(center,4,4);
        text({center.x()-22,105,44,20},QStringLiteral("90°"),12,muted,false,Qt::AlignCenter);
        text({center.x()+radius+8,309,45,20},QStringLiteral("0°"),12,muted);
        text({center.x()-radius-53,309,45,20},QStringLiteral("180°"),12,muted);
        text({center.x()-24,516,48,20},QStringLiteral("270°"),12,muted,false,Qt::AlignCenter);
        }else{
            Controls::Palette panColors;
            panColors.border=QColor("#254762");panColors.accent=blue;
            panColors.panel=QColor("#1b3b57");panColors.text=QColor("#d9f4ff");
            Controls::drawPanKnob(p,center,pannerBus?pannerBus->classicPan:0.0,170,panColors);
            text({center.x()-185,center.y()+186,85,26},QStringLiteral("L100%"),15,muted,true,Qt::AlignCenter);
            text({center.x()-25,center.y()+186,50,26},QStringLiteral("C"),17,muted,true,Qt::AlignCenter);
            text({center.x()+100,center.y()+186,85,26},QStringLiteral("R100%"),15,muted,true,Qt::AlignCenter);
            text({center.x()-115,center.y()-205,230,30},QStringLiteral("声像"),19,ink,true,Qt::AlignCenter);
            text({center.x()-110,center.y()+20,220,35},QStringLiteral("%1%2%").arg(pannerBus&&pannerBus->classicPan>0?QStringLiteral("R "):pannerBus&&pannerBus->classicPan<0?QStringLiteral("L "):QStringLiteral("C ")).arg(qRound(std::abs(pannerBus?pannerBus->classicPan:0.0)*100)),16,ink,true,Qt::AlignCenter);
        }
        const auto drawKnob = [&](QPointF knobCenter, qreal radius, qreal fraction) {
            fraction=std::clamp(fraction,0.0,1.0);
            const QRectF ring(knobCenter.x()-radius,knobCenter.y()-radius,2*radius,2*radius);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#254762"),5,Qt::SolidLine,Qt::RoundCap));p.drawArc(ring,225*16,-270*16);
            p.setPen(QPen(blue,5,Qt::SolidLine,Qt::RoundCap));p.drawArc(ring,225*16,qRound(-270*16*fraction));
            const qreal angle=(135.0+270.0*fraction)*3.14159265358979323846/180.0;
            p.setPen(Qt::NoPen);p.setBrush(QColor("#1b3b57"));p.drawEllipse(knobCenter,radius-6,radius-6);
            p.setPen(QPen(QColor("#d9f4ff"),3,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(knobCenter+QPointF(std::cos(angle),std::sin(angle))*(radius*.4),
                       knobCenter+QPointF(std::cos(angle),std::sin(angle))*(radius-7));
        };
        text({330,574,350,27},QStringLiteral("空间声像参数"),17,ink,true);
        p.save();if(classic)p.setOpacity(.32);
        const std::array<QString,3> parameters {QStringLiteral("宽度"),QStringLiteral("前后"),QStringLiteral("角度")};
        for(int i=0;i<3;++i){
            const qreal x=330+i*206;
            panel({x,609,194,178});
            text({x+12,616,170,25},parameters[i],15,ink,true,Qt::AlignCenter);
            const qreal value=i==0?(pannerBus?pannerBus->width:.7):i==1?(pannerBus&&!pannerBus->adjustablePoints.empty()?(pannerBus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(pannerBus->adjustablePoints.size())-1)].y()+1)/2:.5):(pannerBus?(pannerBus->angleDegrees+180)/360:.5);
            drawKnob({x+97,694},35,value);
            const QString readout=i==0?QStringLiteral("%1%").arg(qRound(pannerBus?pannerBus->width*100:70)):
                i==1?QStringLiteral("%1%").arg(qRound(pannerBus&&!pannerBus->adjustablePoints.empty()?pannerBus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(pannerBus->adjustablePoints.size())-1)].y()*100:0)):
                     QStringLiteral("%1°").arg(qRound(pannerBus?pannerBus->angleDegrees:0));
            text({x+18,748,158,26},readout,15,ink,true,Qt::AlignCenter);
        }
        p.restore();
        text({991,20,190,29},QStringLiteral("分控旋钮"),17,ink,true);
        text({992,54,81,28},QStringLiteral("全通道链接"),13,muted);
        toggle({1077,56,42,24},linked);
        button({1256,51,105,34},QStringLiteral("重置"));
        const auto* bus=pannerBus;
        const std::array<QString,8> labels {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
        const int activeCount=channel?Common::auxLayoutChannelLabels(channel->layout).size():0;
        for(int i=0;i<8;++i) {
            p.save();if(i>=activeCount)p.setOpacity(.32);
            const qreal x=990, y=100+i*86;
            panel({x,y,370,80});
            text({x+10,y+23,35,28},i==0&&channel&&channel->layout==Common::AuxChannelLayout::mono
                     ? QStringLiteral("M") : labels[i],15,ink,true,Qt::AlignCenter);
            if(i==3&&i<activeCount)text({x+7,y+48,41,16},QStringLiteral("强度"),11,muted,false,Qt::AlignCenter);
            const qreal sendLevel=bus&&i<static_cast<int>(bus->sends.size())?bus->sends[i].levelDb:0.0;
            const qreal meterDb=std::clamp((channel?channel->level+channel->inputGainDb:-72.0)+sendLevel,-72.0,12.0);
            drawKnob({x+83,y+32},23,(sendLevel+72)/84.0);
            text({x+42,y+57,82,18},QStringLiteral("%1 dB").arg(sendLevel,0,'f',1),12,ink,true,Qt::AlignCenter);
            p.setPen(Qt::NoPen);p.setBrush(QColor("#06131f"));p.drawRoundedRect(QRectF(x+126,y+25,226,13),3,3);
            const qreal lit=226*(meterDb+72)/84.0;
            const auto segment=[&](qreal low,qreal high,QColor color){
                const qreal left=226*(low+72)/84.0,right=226*(high+72)/84.0;
                const qreal width=std::min(right,lit)-left;
                if(width>0){p.setBrush(color);p.drawRect(QRectF(x+126+left,y+27,width,9));}
            };
            segment(-72,-12,QColor("#17d675"));segment(-12,0,QColor("#f2df48"));segment(0,12,QColor("#f2484d"));
            for(const int mark:{-72,-48,-24,-12,0,12}){
                const qreal tickX=x+126+226*(mark+72)/84.0;
                p.setPen(QPen(QColor("#6685a2"),1));p.drawLine(QPointF(tickX,y+20),QPointF(tickX,y+23));
                text({tickX-15,y+3,30,16},mark==12?QStringLiteral("+12"):QString::number(mark),9,muted,false,Qt::AlignCenter);
            }
            modeToggle({x+215,y+45,62,29},QStringLiteral("M"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].mute,QColor("#ff4b62"));
            modeToggle({x+286,y+45,62,29},QStringLiteral("S"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].solo,QColor("#ffae2d"));
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!channel || event->button()!=Qt::LeftButton) return;
        const QPointF pt=designPoint(event->position());
        const std::array<QColor,8> colors { QColor("#24b8ff"),QColor("#20c5dd"),QColor("#45d877"),QColor("#ffd246"),QColor("#ffae55"),QColor("#f34d70"),QColor("#e359da"),QColor("#9963ec") };
        if (pt.y()>=289&&pt.y()<323&&pt.x()>=29&&pt.x()<245) {const int i=int((pt.x()-34)/26);if(i>=0&&i<8){channel->accent=colors[i];channel->customColor=true;}}
        else if(QRectF(65,351,200,51).contains(pt)) {
            draggedInputGain=true;
            inputGainStartY=pt.y();
            inputGainStartValue=channel->inputGainDb;
            return;
        }
        else if(QRectF(29,407,channel->number==QStringLiteral("MAIN")?254.0:118.0,34).contains(pt)) channel->mute=!channel->mute;
        else if(channel->number!=QStringLiteral("MAIN")&&QRectF(165,407,118,34).contains(pt)) channel->solo=!channel->solo;
        else if(QRectF(330,52,137,34).contains(pt)){if(auto* bus=selectedBus()){bus->panMode=Common::AuxPanMode::spatial;setProperty("selPanMode",static_cast<int>(bus->panMode));}}
        else if(QRectF(477,52,137,34).contains(pt)){if(auto* bus=selectedBus();bus&&Common::auxSupportsClassicPan(bus->sourceLayout,bus->targetLayout)){bus->panMode=Common::AuxPanMode::classic;setProperty("selPanMode",static_cast<int>(bus->panMode));}}
        else if(QRectF(638,52,165,34).contains(pt)){if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::spatial)distribution=!distribution;}
        else if(QRectF(1256,51,105,34).contains(pt)) {if(auto* bus=selectedBus()){bus->adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);bus->pointInfluenceRadii.assign(bus->adjustablePoints.size(),0.1);bus->width=.7;bus->angleDegrees=0;bus->classicPan=0;setProperty("selClassicPan",0.0);const auto active=Common::auxLayoutChannelLabels(channel->layout).size();for(int i=0;i<active;++i)bus->sends[i].levelDb=-2;publishLevels();publishPanner();}}
        else if(QRectF(992,51,127,34).contains(pt)) {linked=!linked;if(auto* bus=selectedBus())bus->sendsLinked=linked;}
        else if(pt.y()>=650&&pt.y()<739&&pt.x()>=330&&pt.x()<936){
            if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::spatial){
                const int knob=int((pt.x()-330)/206);
                if(knob>=0&&knob<3){
                    const qreal x=330+knob*206;
                    if(QRectF(x+56,653,82,82).contains(pt)){
                        draggedKnob=knob;
                        parameterStartY=pt.y();
                        parameterStartValue=knob==0?bus->width:knob==1&&!bus->adjustablePoints.empty()?
                            (bus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus->adjustablePoints.size())-1)].y()+1)/2:
                            (bus->angleDegrees+180)/360;
                    }
                }
            }
        }
        else if(QRectF(448,129,380,380).contains(pt)) {
            if(auto* bus=selectedBus();bus&&bus->panMode==Common::AuxPanMode::classic){
                if(QRectF(463,144,350,350).contains(pt)){
                    draggedClassicPan=true;classicPanStartY=pt.y();classicPanStartValue=bus->classicPan;
                }
            }else if(bus&&!bus->adjustablePoints.empty()){
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];
                    if(std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159)<=12.0){
                        selectedPoint=i;draggedPoint=i;publishPanner();update();return;
                    }
                }
                int influenceIndex=-1;
                qreal closestInfluence=9.0;
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];
                    const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
                    const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                        ? bus->pointInfluenceRadii[i]:0.1;
                    const auto error=std::abs(distance-Common::auxInfluencePixelRadius(influence));
                    if(error<closestInfluence){closestInfluence=error;influenceIndex=i;}
                }
                if(influenceIndex>=0){selectedPoint=influenceIndex;draggedInfluencePoint=influenceIndex;
                    publishPanner();update();if(changed)changed();return;}
                qreal nearest=1e9;int nearestIndex=0;
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                    if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                    const auto& point=bus->adjustablePoints[i];const qreal dx=pt.x()-638-point.x()*159,dy=pt.y()-319-point.y()*159;const qreal distance=dx*dx+dy*dy;if(distance<nearest){nearest=distance;nearestIndex=i;}
                }
                selectedPoint=nearestIndex;draggedPoint=nearestIndex;
                bus->adjustablePoints[nearestIndex]=Common::auxClampPannerPoint(QPointF((pt.x()-638)/159.0,(pt.y()-319)/159.0));
                publishPanner();
            }
        }
        else if(pt.y()>=100&&pt.y()<782&&pt.x()>=990&&pt.x()<1360) {
            auto* bus=selectedBus();
            if(!bus)return;
            const int activeCount=Common::auxLayoutChannelLabels(channel->layout).size();
            const int i=int((pt.y()-100)/86);
            const qreal x=990,y=100+i*86;
            if(i<0||i>=activeCount||i>=static_cast<int>(bus->sends.size())||pt.y()>y+80)return;
            if(QRectF(x+215,y+45,62,29).contains(pt))bus->sends[i].mute=!bus->sends[i].mute;
            else if(QRectF(x+286,y+45,62,29).contains(pt))bus->sends[i].solo=!bus->sends[i].solo;
            else if(QRectF(x+57,y+7,52,50).contains(pt)){
                draggedFader=i;
                sendStartY=pt.y();
                sendStartValue=bus->sends[i].levelDb;
            }
        }
        update();if(changed)changed();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(!channel||!(event->buttons()&Qt::LeftButton))return;
        if(draggedInputGain){
            const qreal delta=inputGainStartY-designPoint(event->position()).y();
            channel->inputGainDb=std::clamp(qRound((inputGainStartValue+delta*.2)*10)/10.0,-12.0,12.0);
            setProperty("selInputGainDb",channel->inputGainDb);
            update();if(changed)changed();return;
        }
        auto* bus=selectedBus();if(!bus||draggedFader>=static_cast<int>(bus->sends.size()))return;
        if(draggedClassicPan){
            bus->classicPan=std::clamp(classicPanStartValue+(classicPanStartY-designPoint(event->position()).y())*.01,-1.0,1.0);
            setProperty("selClassicPan",bus->classicPan);update();if(changed)changed();return;
        }
        if(draggedKnob>=0){
            const qreal fraction=parameterStartValue+(parameterStartY-designPoint(event->position()).y())*.005;
            setKnob(*bus,draggedKnob,fraction);update();if(changed)changed();return;
        }
        if(draggedInfluencePoint>=0&&draggedInfluencePoint<static_cast<int>(bus->adjustablePoints.size())){
            const auto pt=designPoint(event->position());
            const auto& point=bus->adjustablePoints[draggedInfluencePoint];
            const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
            Common::auxSetPointInfluenceRadius(*bus,draggedInfluencePoint,
                Common::auxInfluenceRadiusForPixels(distance));
            publishPanner();update();if(changed)changed();return;
        }
        if(draggedPoint>=0&&draggedPoint<static_cast<int>(bus->adjustablePoints.size())){
            const QPointF pt=designPoint(event->position());
            bus->adjustablePoints[draggedPoint]=Common::auxClampPannerPoint(QPointF((pt.x()-638)/159.0,(pt.y()-319)/159.0));publishPanner();update();if(changed)changed();return;
        }
        if(draggedFader<0)return;
        const qreal y=designPoint(event->position()).y();
        setSendLevel(*bus,draggedFader,qRound((sendStartValue+(sendStartY-y)*.2)*10)/10.0);
        update();if(changed)changed();
    }
    void mouseReleaseEvent(QMouseEvent*) override {draggedFader=-1;draggedPoint=-1;draggedInfluencePoint=-1;draggedKnob=-1;draggedInputGain=false;draggedClassicPan=false;}
    void wheelEvent(QWheelEvent* event) override
    {
        auto* bus=selectedBus();
        if(bus&&bus->panMode==Common::AuxPanMode::spatial){
            const QPointF pt=designPoint(event->position());
            for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){
                if(!Common::auxHasSpatialPoint(channel->layout,i))continue;
                const auto& point=bus->adjustablePoints[i];
                const auto distance=std::hypot(pt.x()-638-point.x()*159,pt.y()-319-point.y()*159);
                const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(i)
                    ? bus->pointInfluenceRadii[i]:0.1;
                if(distance<=20||std::abs(distance-Common::auxInfluencePixelRadius(influence))<=7){
                    selectedPoint=i;
                    Common::auxSetPointInfluenceRadius(*bus,i,influence+event->angleDelta().y()/120.0*0.02);
                    publishPanner();update();if(changed)changed();event->accept();return;
                }
            }
        }
        QWidget::wheelEvent(event);
    }
private:
    qreal contentScale() const { return selContentScale(size()); }
    QPointF contentOrigin() const { const auto scale=contentScale();return {0,(height()-813*scale)/2}; }
    QPointF designPoint(const QPointF& point) const
    {
        const qreal scale=contentScale();
        const qreal right=selDetailsLeft(size());
        if(point.x()>301*scale&&point.x()<right)return {-1,-1};
        const qreal rightOffset=point.x()>=right?right-313*scale:0;
        return {(point.x()-rightOffset)/scale,(point.y()-contentOrigin().y())/scale};
    }
    void setSendLevel(Common::AuxBusState& bus,int index,qreal level)
    {
        level=std::clamp(level,-72.0,12.0);
        if(bus.sendsLinked){
            qreal lower=-84.0,upper=84.0;
            const auto count=Common::auxLayoutChannelLabels(channel->layout).size();
            for(int i=0;i<count;++i){const auto& send=bus.sends[i];lower=std::max(lower,-72.0-send.levelDb);upper=std::min(upper,12.0-send.levelDb);}
            const qreal delta=std::clamp(level-bus.sends[index].levelDb,lower,upper);
            for(int i=0;i<count;++i)bus.sends[i].levelDb+=delta;
        } else bus.sends[index].levelDb=level;
        publishLevels();
    }
    void publishLevels()
    {
        QVariantList levels;
        if(const auto* bus=selectedBus())
            for(const auto& send:bus->sends)levels.push_back(send.levelDb);
        setProperty("selSendLevels",levels);
    }
    void setKnob(Common::AuxBusState& bus,int index,qreal fraction)
    {
        fraction=std::clamp(fraction,0.0,1.0);
        if(index==0)bus.width=fraction;
        else if(index==1&&!bus.adjustablePoints.empty())
            bus.adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus.adjustablePoints.size())-1)].setY(fraction*2-1);
        else if(index==2)Common::auxSetPannerAngle(bus,fraction*360-180);
        publishPanner();
    }
    void publishPanner()
    {
        if(const auto* bus=selectedBus()){
            setProperty("selPannerWidth",bus->width);
            setProperty("selPannerAngle",bus->angleDegrees);
            const auto influence=bus->pointInfluenceRadii.size()>static_cast<std::size_t>(selectedPoint)
                ? bus->pointInfluenceRadii[selectedPoint]:0.1;
            setProperty("selPannerInfluence",influence);
            if(!bus->adjustablePoints.empty())
                setProperty("selPannerFrontBack",bus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus->adjustablePoints.size())-1)].y());
        }
    }
    Common::AuxBusState* selectedBus() const
    {
        return channel ? &channel->selState : nullptr;
    }
    Channel* channel {};
    QLineEdit* name {};
    QLineEdit* comment {};
    QPushButton* modeButton {};
    QLabel* modeArrow {};
    QPushButton* colorButton {};
    bool distribution {true};
    bool linked {true};
    int draggedFader {-1};
    int draggedPoint {-1};
    int draggedInfluencePoint {-1};
    int draggedKnob {-1};
    bool draggedInputGain {};
    qreal inputGainStartY {};
    qreal inputGainStartValue {};
    qreal sendStartY {};
    qreal sendStartValue {};
    qreal parameterStartY {};
    qreal parameterStartValue {};
    bool draggedClassicPan {};
    qreal classicPanStartY {};
    qreal classicPanStartValue {};
    int selectedPoint {};
    std::uint32_t displayedId {};
};

class SelContentHost final : public QWidget {
public:
    explicit SelContentHost(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(950,650); }

    void setPages(SelMainPage* main, QStackedWidget* details)
    {
        mainPage=main;
        detailPages=details;
        placePages();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        placePages();
    }

private:
    void placePages()
    {
        if(!mainPage||!detailPages)return;
        mainPage->setGeometry(rect());
        const qreal scale=selContentScale(size());
        const int rightRegionStart=qRound(313.0*scale);
        const int margin=qRound(8.0*scale);
        detailPages->setGeometry(rightRegionStart+margin,0,
                                 width()-rightRegionStart-2*margin,height());
    }

    SelMainPage* mainPage {};
    QStackedWidget* detailPages {};
};

QColor withAlpha(const QColor& color, int alpha)
{
    auto result = color;
    result.setAlpha(alpha);
    return result;
}

void drawText(QPainter& painter, const QRectF& rect, const QString& text, const QColor& color,
              qreal size, int alignment = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setPixelSize(std::max(8, qRound(size)));
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, alignment, text);
}

void drawAdaptiveText(QPainter& painter, const QRectF& rect, const QString& text,
                      const QColor& color, qreal preferredSize, qreal minimumSize,
                      int alignment = Qt::AlignCenter, bool bold = false)
{
    auto font = painter.font();
    font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    auto size = preferredSize;
    const auto availableWidth = std::max(1.0, rect.width() - 8.0);
    const auto availableHeight = std::max(1.0, rect.height() - 4.0);
    while (size > minimumSize) {
        font.setPixelSize(std::max(1, qRound(size)));
        const QFontMetricsF metrics(font);
        if (metrics.horizontalAdvance(text) <= availableWidth && metrics.height() <= availableHeight)
            break;
        size -= 1.0;
    }
    font.setPixelSize(std::max(1, qRound(std::max(size, minimumSize))));
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(rect, alignment, text);
}

QString faderDbText(qreal position)
{
    if (position >= .995)
        return QStringLiteral("−∞");
    const auto db = 12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0;
    const auto prefix = db > 0.0 ? QStringLiteral("+") : QString();
    return prefix + QString::number(db, 'f', 1) + QStringLiteral(" dB");
}

qreal faderDbValue(qreal position)
{
    if (position >= .995)
        return -72.0;
    return 12.0 - std::clamp(position / .95, 0.0, 1.0) * 84.0;
}

}

class MixerWindow::Impl {
public:
    explicit Impl(MixerWindow& owner)
        : owner(owner)
    {
        channels = {
            { "Mic 1", "Analog In 1", "1", "#45bfff", .66, -5.0, -5.0 },
            { "Mic 2", "Analog In 2", "2", "#45bfff", .56, -9.0, -9.0 },
            { "Line 3", "Line In 3", "3", "#8da5d8", .70, -14.0, -14.0 },
            { "Line 4", "Line In 4", "4", "#8da5d8", .79, -8.0, -8.0 },
            { "USB 5", "USB In 5", "5", "#70c9f4", .62, -11.0, -11.0 },
            { "USB 6", "USB In 6", "6", "#70c9f4", .74, -18.0, -18.0 },
            { "Bus 7", "Bus In 7", "7", "#8da5d8", .58, -7.0, -7.0 },
            { "Bus 8", "Bus In 8", "8", "#8da5d8", .81, -23.0, -23.0 }
        };
        for (auto& channel : channels)
        {
            channel.id = nextChannelId++;
            channel.layout = Common::AuxChannelLayout::stereo;
        }
        rebuildAuxChannels();
        timer.setInterval(55);
        QObject::connect(&timer, &QTimer::timeout, &owner, [this] {
            for (auto& channel : channels) {
                if (QRandomGenerator::global()->bounded(12) == 0)
                    channel.target = QRandomGenerator::global()->bounded(-480, 111) / 10.0;
                channel.level += (channel.target - channel.level) * .17;
                channel.level = std::clamp(channel.level + (QRandomGenerator::global()->bounded(7) - 3) / 10.0, -72.0, 12.0);
            }
            this->owner.update();
            updateSel();
        });
        timer.start();
    }

    int auxChannelCount() const
    {
        return static_cast<int>(auxState->buses().size());
    }

    int masterIndex() const
    {
        return inputChannelCount + auxChannelCount();
    }

    void rebuildAuxChannels()
    {
        std::vector<Channel> rebuilt;
        rebuilt.reserve(inputChannelCount + auxChannelCount() + 1);
        for (int index = 0; index < std::min(inputChannelCount, static_cast<int>(channels.size())); ++index)
            rebuilt.push_back(channels[index]);

        for (const auto& bus : auxState->buses()) {
            const auto existing = std::find_if(channels.cbegin(), channels.cend(), [&bus](const auto& channel) {
                return channel.auxId == bus.id;
            });
            Channel channel;
            if (existing != channels.cend())
                channel = *existing;
            else {
                channel.fader = .68;
                channel.level = -12.0;
                channel.target = -12.0;
                channel.accent = QColor("#33b6ff");
            }
            if (existing == channels.cend())
                channel.name = QStringLiteral("AUX %1").arg(static_cast<int>(rebuilt.size()) - inputChannelCount + 1);
            if (existing == channels.cend())
                channel.source = bus.name;
            channel.number = channel.name;
            channel.auxId = bus.id;
            channel.id = 1000000 + bus.id;
            channel.layout = bus.targetLayout;
            rebuilt.push_back(std::move(channel));
        }

        const auto existingMaster = std::find_if(channels.cbegin(), channels.cend(), [](const auto& channel) {
            return channel.number == QStringLiteral("MAIN");
        });
        if (existingMaster != channels.cend())
            rebuilt.push_back(*existingMaster);
        else
            rebuilt.push_back({ "Main L/R", "JACK Out 1/2", "MAIN", "#50dfbd", .55, 4.0, 4.0,
                                false, false, false, false, false, 0, 2000000,
                                Common::AuxChannelLayout::stereo });
        channels = std::move(rebuilt);
        renumberInputs();
        if (auxSettingsWindow && auxWindowChannelId != 0
            && std::none_of(channels.cbegin(), channels.cend(), [this](const Channel& item) {
                   return item.id == auxWindowChannelId;
               })) {
            auxSettingsWindow->close();
            auxWindowChannelId = 0;
        }
        for (auto it = fxSettingsWindows.begin(); it != fxSettingsWindows.end();) {
            const auto present = std::find_if(channels.cbegin(), channels.cend(), [id = it->first](const Channel& item) {
                return item.id == id;
            });
            if (present == channels.cend()) {
                if (it->second) { it->second->close(); it->second->deleteLater(); }
                it = fxSettingsWindows.erase(it);
            } else {
                if (it->second) it->second->setSourceChannels(fxChannels(present->layout));
                ++it;
            }
        }
        updateSel();
        auxScrollOffset = effectiveAuxScrollOffset();
    }

    void renumberInputs()
    {
        for (int index = 0; index < inputChannelCount; ++index)
            channels[index].number = QStringLiteral("CH %1").arg(index + 1);
    }

    FxSettingsWindow* fxSettingsFor(int index)
    {
        const auto id = channels[index].id;
        auto& window = fxSettingsWindows[id];
        if (window.isNull()) {
            window = new FxSettingsWindow(channels[index].name,
                fxChannels(channels[index].layout), &owner);
            if (index == masterIndex()) window->addDefaultMasterEffects();
            window->setChainEnabledHandler([this, id](bool enabled) {
                const auto found = std::find_if(channels.begin(), channels.end(), [id](const Channel& item) {
                    return item.id == id;
                });
                if (found != channels.end()) { found->fx = enabled; owner.update(); }
            });
        }
        window->setChainEnabled(channels[index].fx);
        return window;
    }

    static QStringList fxChannels(Common::AuxChannelLayout layout)
    {
        if (layout == Common::AuxChannelLayout::mono) return { QStringLiteral("M") };
        auto labels = Common::auxLayoutChannelLabels(layout);
        labels.replaceInStrings(QStringLiteral("Lrs"), QStringLiteral("Lb"));
        labels.replaceInStrings(QStringLiteral("Rrs"), QStringLiteral("Rb"));
        return labels;
    }

    void addInput(Common::AuxChannelLayout layout)
    {
        Channel channel { QStringLiteral("Input %1").arg(nextChannelId),
                          QStringLiteral("Input %1").arg(nextChannelId),
                          QString::number(nextChannelId), QColor("#45bfff"),
                          .66, -12.0, -12.0 };
        channel.id = nextChannelId++;
        channel.layout = layout;
        channels.insert(channels.begin() + inputChannelCount, channel);
        ++inputChannelCount;
        renumberInputs();
        updateSel();
        owner.update();
    }

    void removeChannel(int index)
    {
        if (index == masterIndex() || (index < inputChannelCount && inputChannelCount <= 1)
            || (index >= inputChannelCount && auxChannelCount() <= 1)) return;
        const auto channel = channels[index];
        if (auto found = fxSettingsWindows.find(channel.id); found != fxSettingsWindows.end()) {
            if (found->second) { found->second->close(); found->second->deleteLater(); }
            fxSettingsWindows.erase(found);
        }
        if (auxWindowChannelId == channel.id && auxSettingsWindow) {
            auxSettingsWindow->close();
            auxWindowChannelId = 0;
        }
        if (channel.auxId) {
            auxState->removeBus(channel.auxId);
            rebuildAuxChannels();
        } else {
            channels.erase(channels.begin() + index);
            --inputChannelCount;
            renumberInputs();
        }
        if (selectedChannelId == channel.id)
            selectedChannelId = channels.empty() ? 0 : channels.front().id;
        updateSel();
        owner.update();
    }

    void copyChannel(int index)
    {
        if (index == masterIndex()) return;
        const auto sourceId = channels[index].id;
        std::uint32_t copiedId {};
        if (channels[index].auxId) {
            auto* bus = auxState->cloneBus(channels[index].auxId);
            if (!bus) return;
            rebuildAuxChannels();
            const auto copied = std::find_if(channels.begin(), channels.end(), [bus](const Channel& item) {
                return item.auxId == bus->id;
            });
            if (copied != channels.end()) {
                const auto id = copied->id;
                *copied = channels[index];
                copied->id = id;
                copied->auxId = bus->id;
                copied->name = QStringLiteral("AUX %1").arg(auxChannelCount());
                copied->number = copied->name;
                copiedId = copied->id;
            }
        } else {
            Channel copied = channels[index];
            copied.id = nextChannelId++;
            copied.name += QStringLiteral(" 副本");
            copied.number = QString::number(copied.id);
            copiedId = copied.id;
            channels.insert(channels.begin() + index + 1, copied);
            ++inputChannelCount;
            renumberInputs();
        }
        if (copiedId) {
            const auto sourceFx = fxSettingsWindows.find(sourceId);
            if (sourceFx != fxSettingsWindows.end() && sourceFx->second) {
                const auto copied = std::find_if(channels.begin(), channels.end(), [copiedId](const Channel& item) {
                    return item.id == copiedId;
                });
                if (copied != channels.end()) {
                    auto* target = fxSettingsFor(static_cast<int>(std::distance(channels.begin(), copied)));
                    target->cloneChainFrom(*sourceFx->second);
                }
            }
        }
        updateSel();
        owner.update();
    }

    void setChannelLayout(int index, Common::AuxChannelLayout layout)
    {
        if (index == masterIndex() || channels[index].layout == layout)
            return;
        const auto id = channels[index].id;
        if (channels[index].auxId) {
            auxState->setBusLayout(channels[index].auxId, layout);
            rebuildAuxChannels();
        } else {
            channels[index].layout = layout;
        }
        if (auto found = fxSettingsWindows.find(id); found != fxSettingsWindows.end() && found->second)
            found->second->setSourceChannels(fxChannels(layout));
        if (auxSettingsWindow) {
            if (auxWindowChannelId == id) {
                const auto found = std::find_if(channels.cbegin(), channels.cend(), [id](const Channel& item) {
                    return item.id == id;
                });
                if (found != channels.cend()) {
                    const auto key = found->auxId ? QStringLiteral("aux:%1").arg(found->auxId)
                                                  : QStringLiteral("input:%1").arg(found->id);
                    auxSettingsWindow->setSource(key, found->name, found->auxId, layout,auxSourceNumber(*found));
                }
            } else {
                auxSettingsWindow->update();
            }
        }
        updateSel();
        owner.update();
    }

    void updateSel()
    {
        if (!selLabel) return;
        const auto found = std::find_if(channels.cbegin(), channels.cend(), [this](const Channel& item) {
            return item.id == selectedChannelId;
        });
        selLabel->setText(found == channels.cend() ? QStringLiteral("未选择通道") : found->name);
        if(selHeader){
            const QColor background=found!=channels.cend()&&found->customColor?found->accent:QColor("#123a65");
            if(background!=lastHeaderColor){
                lastHeaderColor=background;
                const qreal brightness=.299*background.red()+.587*background.green()+.114*background.blue();
                const QString foreground=brightness>145?QStringLiteral("#102033"):QStringLiteral("#f5fbff");
                selHeader->setStyleSheet(QStringLiteral("#selHeader {background:%1;border-radius:6px;} #selHeader QLabel {color:%2;background:transparent;}").arg(background.name(),foreground));
            }
        }
        if(selAuxTab)selAuxTab->setVisible(found!=channels.cend()&&found->number!=QStringLiteral("MAIN"));
        if(selStack&&found!=channels.cend()&&found->number==QStringLiteral("MAIN")&&selStack->currentIndex()==1){
            selStack->setCurrentIndex(0);
            selStack->hide();
            if(auto* mainTab=selWindow->findChild<QPushButton*>(QStringLiteral("selTabMain")))mainTab->setChecked(true);
            selAuxTab->setChecked(false);
        }
        if (selMain) {
            auto* current = found == channels.cend() ? nullptr : &channels[std::distance(channels.cbegin(), found)];
            selMain->setChannel(current);
            if (selMainChannelId != (current ? current->id : 0)) {
                selMainChannelId = current ? current->id : 0;
                if (current && auxSettingsWindow) {
                    const auto key = current->auxId ? QStringLiteral("aux:%1").arg(current->auxId)
                                                    : QStringLiteral("input:%1").arg(current->id);
                    auxSettingsWindow->setSource(key,current->name,current->auxId,current->layout,auxSourceNumber(*current));
                    auxSettingsWindow->setChainEnabled(current->aux);
                }
                if (selStack && selStack->currentIndex()==2) showSelFx();
            }
        }
    }

    void selectChannel(int index)
    {
        selectedChannelId = channels[index].id;
        updateSel();
        owner.update();
    }

    void showLargeScreen()
    {
        if (!selWindow) {
            selWindow = new QWidget(&owner, Qt::Window);
            selWindow->setAttribute(Qt::WA_DeleteOnClose);
            selWindow->setObjectName(QStringLiteral("selLargeScreen"));
            selWindow->setWindowTitle(QStringLiteral("Channel SEL"));
            selWindow->resize(1394, 885);
            selWindow->setMinimumSize(1100, 760);
            selWindow->setStyleSheet(QStringLiteral("#selLargeScreen {background:#08192b;color:#e9f2ff;} #selHeader QPushButton {background:#1b3048;color:#d5e8ff;border:1px solid #3b5675;border-radius:6px;padding:6px 18px;font-size:16px;} #selHeader QPushButton:checked {background:#123d62;border:1px solid #2bc7ff;color:white;}"));
            auto* layout = new QVBoxLayout(selWindow);
            layout->setContentsMargins(5,8,5,5);
            layout->setSpacing(0);
            auto* header = new QWidget(selWindow);
            header->setObjectName(QStringLiteral("selHeader"));
            selHeader=header;
            header->setFixedHeight(64);
            auto* headerLayout = new QHBoxLayout(header);
            headerLayout->setContentsMargins(24,5,16,5);
            selLabel = new QLabel(selWindow);
            selLabel->setObjectName(QStringLiteral("selSelectedChannel"));
            QFont font = selLabel->font(); font.setPixelSize(26); font.setBold(true); selLabel->setFont(font);
            auto* icon = new QLabel(QStringLiteral("🎙"),header);
            QFont iconFont=icon->font();iconFont.setPixelSize(30);icon->setFont(iconFont);
            headerLayout->addWidget(icon);
            auto* prefix = new QLabel(QStringLiteral("Channel SEL -"),header);
            QFont prefixFont=prefix->font();prefixFont.setPixelSize(24);prefixFont.setBold(true);prefix->setFont(prefixFont);
            headerLayout->addWidget(prefix);
            headerLayout->addWidget(selLabel,1);
            const std::array<QString,3> tabs { QStringLiteral("Main"),QStringLiteral("AUX"),QStringLiteral("FX") };
            auto* content = new SelContentHost(selWindow);
            selMain = new SelMainPage(content);
            selStack = new QStackedWidget(content);
            selStack->setObjectName(QStringLiteral("selPages"));
            selStack->setStyleSheet(QStringLiteral("#selPages {background:#0b1a2b;}"));
            selStack->setAutoFillBackground(true);
            content->setPages(selMain,selStack);
            selMain->changed = [this] {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found!=channels.cend()&&found->auxId&&found->customColor)
                    auxState->setBusAccent(found->auxId,found->accent);
                updateSel();owner.update();if(auxSettingsWindow)auxSettingsWindow->update();
            };
            selMain->detailsChanged = [this] {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found==channels.cend())return;
                if(auxSettingsWindow&&auxWindowChannelId==found->id){
                    const auto key=found->auxId?QStringLiteral("aux:%1").arg(found->auxId):QStringLiteral("input:%1").arg(found->id);
                    auxSettingsWindow->setSource(key,found->name,found->auxId,found->layout,auxSourceNumber(*found));
                }
                if(auto fx=fxSettingsWindows.find(found->id);fx!=fxSettingsWindows.end()&&fx->second)
                    fx->second->setSourceName(found->name);
            };
            selMain->layoutChanged = [this](Common::AuxChannelLayout nextLayout) {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found!=channels.cend()&&found->number!=QStringLiteral("MAIN"))
                    setChannelLayout(static_cast<int>(std::distance(channels.cbegin(),found)),nextLayout);
            };
            selStack->addWidget(new QWidget(selStack));
            selAuxPage = new QWidget(selStack);
            auto* auxLayout = new QVBoxLayout(selAuxPage);auxLayout->setContentsMargins(0,0,0,0);
            selStack->addWidget(selAuxPage);
            selFxPage = new QWidget(selStack);
            auto* fxLayout = new QVBoxLayout(selFxPage);fxLayout->setContentsMargins(0,0,0,0);
            selStack->addWidget(selFxPage);
            for(int i=0;i<3;++i) {
                auto* tab = new QPushButton(tabs[i],header);
                tab->setObjectName(QStringLiteral("selTab%1").arg(tabs[i]));
                tab->setCheckable(true);
                tab->setChecked(i==0);
                tab->setFixedWidth(106);
                if(i==1)selAuxTab=tab;
                headerLayout->addWidget(tab);
                connect(tab,&QPushButton::clicked,selWindow,[this,i,header] {
                    if(i==1)showSelAux();
                    if(i==2)showSelFx();
                    selStack->setCurrentIndex(i);
                    selStack->setVisible(i!=0);
                    for(int j=0;j<3;++j)if(auto* item=header->findChild<QPushButton*>(QStringLiteral("selTab%1").arg(std::array<QString,3>{QStringLiteral("Main"),QStringLiteral("AUX"),QStringLiteral("FX")}[j])))item->setChecked(j==i);
                    updateSel();
                });
            }
            auto* close = new QPushButton(QStringLiteral("×"),header);
            close->setFixedWidth(48);headerLayout->addWidget(close);
            connect(close,&QPushButton::clicked,selWindow,&QWidget::close);
            layout->addWidget(header);
            layout->addWidget(content,1);
            selStack->hide();
        }
        updateSel();
        selWindow->show();
        selWindow->raise();
        selWindow->activateWindow();
    }

    void showSelAux()
    {
        if(!selStack)return;
        const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& c){return c.id==selectedChannelId;});
        if(found==channels.cend())return;
        auxWindowChannelId=found->id;
        if(!auxSettingsWindow){
            auxSettingsWindow=new AuxSettingsWindow(auxState,&owner);
            auxSettingsWindow->setAttribute(Qt::WA_DeleteOnClose);
            auxSettingsWindow->setStateChangedCallback([this]{rebuildAuxChannels();updateSel();owner.update();});
            auxSettingsWindow->setChainEnabledHandler([this](bool enabled){
                auto it=std::find_if(channels.begin(),channels.end(),[this](const Channel& c){return c.id==selectedChannelId;});
                if(it!=channels.end()){it->aux=enabled;owner.update();}
            });
        }
        const auto key=found->auxId?QStringLiteral("aux:%1").arg(found->auxId):QStringLiteral("input:%1").arg(found->id);
        auxSettingsWindow->setSource(key,found->name,found->auxId,found->layout,auxSourceNumber(*found));
        auxSettingsWindow->setChainEnabled(found->aux);
        if(auxSettingsWindow->parentWidget()!=selAuxPage){
            auxSettingsWindow->hide();auxSettingsWindow->setWindowFlags(Qt::Widget);
            auxSettingsWindow->setParent(selAuxPage,Qt::Widget);
            selAuxPage->layout()->addWidget(auxSettingsWindow);
        }
        auxSettingsWindow->show();
    }

    void showSelFx()
    {
        if(!selStack)return;
        const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& c){return c.id==selectedChannelId;});
        if(found==channels.cend())return;
        auto* fx=fxSettingsFor(static_cast<int>(std::distance(channels.cbegin(),found)));
        if(fx->parentWidget()!=selFxPage){
            auto* oldItem=selFxPage->layout()->itemAt(0);
            if(auto* old=oldItem?dynamic_cast<FxSettingsWindow*>(oldItem->widget()):nullptr){
                selFxPage->layout()->removeWidget(old);old->hide();old->setParent(&owner,Qt::Window);
            }
            fx->hide();fx->setParent(selFxPage,Qt::Widget);selFxPage->layout()->addWidget(fx);
        }
        fx->show();
    }

    QRectF channelRect(int index) const
    {
        qreal x = 0.0;
        if (index < inputChannelCount)
            x = inputViewport().left() + index * channelWidth - effectiveInputScrollOffset();
        else if (index < masterIndex())
            x = auxViewport().left() + (index - inputChannelCount) * channelWidth - effectiveAuxScrollOffset();
        else
            x = owner.width() - margin - channelWidth;
        return { x, topMargin, channelWidth, channelAreaHeight() };
    }

    QRectF inputViewport() const
    {
        const auto available = scrollableAreaWidth();
        const auto inputWidth = std::floor(available * effectiveInputRatio());
        return { margin, topMargin, inputWidth, channelAreaHeight() };
    }

    QRectF auxViewport() const
    {
        const auto input = inputViewport();
        const auto width = std::max(0.0, scrollableAreaWidth() - input.width());
        return { input.right() + groupGap, topMargin, width, channelAreaHeight() };
    }

    QRectF viewportForChannel(int index) const
    {
        return index < inputChannelCount ? inputViewport() : auxViewport();
    }

    qreal channelAreaHeight() const
    {
        return std::max(0.0, owner.height() - topMargin - margin - scrollbarHeight - scrollbarGap);
    }

    qreal scrollableAreaWidth() const
    {
        const auto masterLeft = owner.width() - margin - channelWidth;
        return std::max(0.0, masterLeft - groupGap - margin - groupGap);
    }

    qreal effectiveInputRatio() const
    {
        const auto available = scrollableAreaWidth();
        if (available <= 0.0)
            return .5;
        const auto minimumRatio = std::min(.5, channelWidth / available);
        return std::clamp(inputRatio, minimumRatio, 1.0 - minimumRatio);
    }

    QRectF groupDividerRect() const
    {
        const auto input = inputViewport();
        return { input.right(), topMargin, groupGap, owner.height() - topMargin - margin };
    }

    void setInputRatioFromPosition(qreal x)
    {
        const auto available = scrollableAreaWidth();
        if (available <= 0.0)
            return;
        inputRatio = std::clamp((x - margin) / available, 0.0, 1.0);
        inputScrollOffset = effectiveInputScrollOffset();
        auxScrollOffset = effectiveAuxScrollOffset();
    }

    qreal maximumInputScrollOffset() const
    {
        return std::max(0.0, inputChannelCount * channelWidth - inputViewport().width());
    }

    qreal maximumAuxScrollOffset() const
    {
        return std::max(0.0, auxChannelCount() * channelWidth - auxViewport().width());
    }

    qreal effectiveInputScrollOffset() const
    {
        return std::clamp(inputScrollOffset, 0.0, maximumInputScrollOffset());
    }

    qreal effectiveAuxScrollOffset() const
    {
        return std::clamp(auxScrollOffset, 0.0, maximumAuxScrollOffset());
    }

    QRectF scrollbarTrack(bool input) const
    {
        const auto viewport = input ? inputViewport() : auxViewport();
        return { viewport.left() + 4.0, viewport.bottom() + scrollbarGap,
                 std::max(0.0, viewport.width() - 8.0), scrollbarHeight };
    }

    QRectF scrollbarThumb(bool input) const
    {
        const auto track = scrollbarTrack(input);
        const auto channelCount = input ? inputChannelCount : auxChannelCount();
        const auto contentWidth = channelCount * channelWidth;
        const auto viewportWidth = input ? inputViewport().width() : auxViewport().width();
        const auto maximum = input ? maximumInputScrollOffset() : maximumAuxScrollOffset();
        const auto offset = input ? effectiveInputScrollOffset() : effectiveAuxScrollOffset();
        const auto thumbWidth = maximum <= 0.0 ? track.width()
            : std::max(32.0, track.width() * viewportWidth / contentWidth);
        const auto travel = std::max(0.0, track.width() - thumbWidth);
        const auto x = track.left() + (maximum <= 0.0 ? 0.0 : travel * offset / maximum);
        return { x, track.top(), thumbWidth, track.height() };
    }

    void setScrollFromThumb(bool input, qreal thumbLeft)
    {
        const auto track = scrollbarTrack(input);
        const auto thumb = scrollbarThumb(input);
        const auto travel = std::max(0.0, track.width() - thumb.width());
        const auto maximum = input ? maximumInputScrollOffset() : maximumAuxScrollOffset();
        const auto normalized = travel <= 0.0 ? 0.0
            : std::clamp((thumbLeft - track.left()) / travel, 0.0, 1.0);
        if (input)
            inputScrollOffset = normalized * maximum;
        else
            auxScrollOffset = normalized * maximum;
    }

    QRectF faderTrack(int index) const
    {
        const auto card = channelRect(index);
        const auto top = card.top() + card.height() * .62;
        return { card.center().x() - 3.0, top, 6.0, card.height() * .26 };
    }

    QRectF meterRect(int index, qreal heightScale) const
    {
        const auto card = channelRect(index);
        const auto headerHeight = 42.0 * heightScale;
        const auto top = card.top() + headerHeight + 65.0 * heightScale;
        return { card.center().x() - 8.0, top, 16.0, faderTrack(index).height() };
    }

    QRectF meterModeRect(int index, qreal heightScale) const
    {
        const auto meter = meterRect(index, heightScale);
        return { meter.center().x() - 19.0, meter.top() - 20.0 * heightScale,
                 38.0, 16.0 * heightScale };
    }

    QRectF meterHitRect(int index, qreal heightScale) const
    {
        return meterRect(index, heightScale).united(meterModeRect(index, heightScale)).adjusted(-4, -2, 4, 2);
    }

    QRectF channelButtonRect(int index, qreal heightScale, int row) const
    {
        static constexpr std::array<qreal, 3> offsets { 0.0, 1.35, 2.85 };
        const auto card = channelRect(index);
        const auto buttonHeight = 28.0 * heightScale;
        const auto top = meterRect(index, heightScale).bottom() + 20.0 * heightScale;
        return { card.left() + 6.0, top + buttonHeight * offsets[row], card.width() - 12.0, buttonHeight };
    }

    int hitChannel(const QPointF& position) const
    {
        if (inputViewport().contains(position)) {
            for (int index = 0; index < inputChannelCount; ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        if (auxViewport().contains(position)) {
            for (int index = inputChannelCount; index < masterIndex(); ++index) {
                if (channelRect(index).contains(position))
                    return index;
            }
        }
        return channelRect(masterIndex()).contains(position) ? masterIndex() : -1;
    }

    MixerWindow& owner;
    std::shared_ptr<Common::AuxMixerState> auxState { std::make_shared<Common::AuxMixerState>() };
    QPointer<AuxSettingsWindow> auxSettingsWindow;
    QPointer<QWidget> selWindow;
    QPointer<QLabel> selLabel;
    QPointer<QWidget> selHeader;
    QColor lastHeaderColor;
    QPointer<QStackedWidget> selStack;
    QPointer<QWidget> selAuxPage;
    QPointer<QWidget> selFxPage;
    QPointer<QPushButton> selAuxTab;
    QPointer<SelMainPage> selMain;
    std::uint32_t selMainChannelId {};
    std::uint32_t selectedChannelId { 1 };
    std::map<int, QPointer<FxSettingsWindow>> fxSettingsWindows;
    std::vector<Channel> channels;
    QTimer timer;
    int dragging = -1;
    int scrollbarDragging = 0;
    bool groupDividerDragging = false;
    qreal scrollbarDragOffset = 0.0;
    qreal inputScrollOffset = 0.0;
    qreal auxScrollOffset = 0.0;
    qreal inputRatio = .6;
    int inputChannelCount = 8;
    std::uint32_t nextChannelId = 1;
    std::uint32_t auxWindowChannelId {};

    static constexpr qreal margin = 9.0;
    static constexpr qreal topMargin = 40.0;
    static constexpr qreal groupGap = 8.0;
    static constexpr qreal channelWidth = 104.0;
    static constexpr qreal scrollbarHeight = 10.0;
    static constexpr qreal scrollbarGap = 5.0;
};

MixerWindow::MixerWindow(QWidget* parent)
    : QMainWindow(parent)
    , impl_(std::make_unique<Impl>(*this))
{
    setWindowTitle(QStringLiteral("WinJACKNexus - 调音台"));
    setMinimumSize(760, 560);
    resize(1024, 768);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    auto* menuBar = this->menuBar();
    menuBar->setObjectName(QStringLiteral("mixerMenuBar"));
    auto* channelsMenu = menuBar->addMenu(QStringLiteral("通道管理"));
    auto* inputsMenu = channelsMenu->addMenu(QStringLiteral("添加输入通道"));
    auto* auxMenu = channelsMenu->addMenu(QStringLiteral("添加AUX通道"));
    const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                               Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
    for (const auto layout : layouts) {
        connect(inputsMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->addInput(layout); });
        connect(auxMenu->addAction(Common::auxLayoutName(layout)), &QAction::triggered, this,
                [this, layout] { impl_->auxState->addBus(layout); impl_->rebuildAuxChannels(); update(); });
    }
    auto* globalMenu = menuBar->addMenu(QStringLiteral("全局"));
    connect(globalMenu->addAction(QStringLiteral("关闭所有通道独奏/静音")), &QAction::triggered, this, [this] {
        for (auto& channel : impl_->channels) { channel.solo = false; channel.mute = false; }
        update();
    });
    connect(menuBar->addAction(QStringLiteral("查看大屏")), &QAction::triggered, this,
            [this] { impl_->showLargeScreen(); });
}

MixerWindow::~MixerWindow() = default;

void MixerWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#07131f"));

    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    const auto headerHeight = 42.0 * heightScale;
    const auto label = QColor("#dce9ff");
    const auto secondary = QColor("#a9bbd7");
    const auto groups = std::array<std::tuple<int, QString, QColor>, 3> {{
        { 0, QStringLiteral("♩  Inputs"), QColor("#1977d3") },
        { impl_->inputChannelCount, QStringLiteral("◇  AUX Sends"), QColor("#654dd4") },
        { impl_->masterIndex(), QStringLiteral("◖  Main Output"), QColor("#1ca47f") }
    }};

    for (const auto& [start, title, color] : groups) {
        painter.save();
        const auto group = start == impl_->masterIndex()
            ? impl_->channelRect(start)
            : impl_->viewportForChannel(start);
        QPainterPath outline;
        outline.addRoundedRect(group, 7.0, 7.0);
        painter.setPen(QPen(withAlpha(color, 210), 1.2));
        painter.setBrush(withAlpha(color, 22));
        painter.drawPath(outline);
        const QRectF header(group.left(), group.top(), group.width(), headerHeight);
        painter.fillRect(header.adjusted(1, 1, -1, 0), withAlpha(color, 75));
        drawAdaptiveText(painter, header.adjusted(4, 0, -4, 0), title, label,
                 18 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        painter.restore();
    }

    for (int index = 0; index < static_cast<int>(impl_->channels.size()); ++index) {
        painter.save();
        if (index != impl_->masterIndex()) {
            const auto viewport = impl_->viewportForChannel(index);
            painter.setClipRect(viewport);
            if (!impl_->channelRect(index).intersects(viewport)) {
                painter.restore();
                continue;
            }
        }
        const auto& channel = impl_->channels[index];
        const auto card = impl_->channelRect(index);
        if (index != 0 && index != impl_->inputChannelCount && index != impl_->masterIndex()) {
            painter.setPen(QPen(QColor("#243c57"), 1));
            painter.drawLine(QPointF(card.left(), card.top() + headerHeight), QPointF(card.left(), card.bottom()));
        }
        if (index == impl_->inputChannelCount - 1 || index == impl_->masterIndex() - 1) {
            painter.setPen(QPen(QColor("#243c57"), 1));
            const auto right = card.right() - 0.5;
            painter.drawLine(QPointF(right, card.top() + headerHeight), QPointF(right, card.bottom()));
        }

        const auto top = card.top() + headerHeight + 2 * heightScale;
        drawAdaptiveText(painter, { card.left(), top, card.width(), 20 * heightScale }, channel.name,
                 label, 16 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        drawAdaptiveText(painter, { card.left(), top + 26 * heightScale, card.width(), 14 * heightScale }, channel.source,
                 secondary, 12 * heightScale, 8 * heightScale);

        const auto meter = impl_->meterRect(index, heightScale);
        const auto meterMode = impl_->meterModeRect(index, heightScale);
        const auto inputLevel = channel.level + channel.inputGainDb;
        const auto meterLevel = std::clamp(inputLevel
            + (channel.postFaderMeter ? faderDbValue(channel.fader) : 0.0), -72.0, 12.0);
        painter.setPen(QPen(channel.postFaderMeter ? QColor("#33b6ff") : QColor("#6f849a"), 1.0));
        painter.setBrush(channel.postFaderMeter ? QColor("#163a51") : QColor("#182735"));
        painter.drawRoundedRect(meterMode, 4, 4);
        drawText(painter, meterMode, channel.postFaderMeter ? QStringLiteral("POST") : QStringLiteral("IN"),
                 channel.postFaderMeter ? QColor("#8edcff") : secondary, 9 * heightScale, Qt::AlignCenter, true);
        painter.setPen(QPen(QColor("#0a0f14"), 4));
        painter.setBrush(QColor("#101a22"));
        painter.drawRoundedRect(meter, 3, 3);
        const auto innerMeter = meter.adjusted(2.5, 2.5, -2.5, -2.5);
        const auto yForDb = [&](qreal db) {
            const auto normalized = std::clamp((db + 72.0) / 84.0, 0.0, 1.0);
            return innerMeter.bottom() - normalized * innerMeter.height();
        };
        const auto segments = std::array<std::tuple<qreal, qreal, QColor>, 3> {{
            { -72.0, -12.0, QColor("#26dc68") },
            { -12.0, 0.0, QColor("#ffe15a") },
            { 0.0, 12.0, QColor("#ff5353") }
        }};
        for (const auto& [minimum, maximum, color] : segments) {
            const QRectF segment(innerMeter.left(), yForDb(maximum), innerMeter.width(), yForDb(minimum) - yForDb(maximum));
            painter.fillRect(segment, withAlpha(color, 38));
            const auto activeMaximum = std::clamp(meterLevel, minimum, maximum);
            if (activeMaximum > minimum) {
                const QRectF active(innerMeter.left(), yForDb(activeMaximum), innerMeter.width(), yForDb(minimum) - yForDb(activeMaximum));
                painter.fillRect(active, color);
            }
        }
        painter.setPen(QPen(QColor("#07131f"), 1));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(-12.0)), QPointF(innerMeter.right(), yForDb(-12.0)));
        painter.drawLine(QPointF(innerMeter.left(), yForDb(0.0)), QPointF(innerMeter.right(), yForDb(0.0)));
        const auto meterTicks = std::array<int, 8> { 12, 0, -12, -24, -36, -48, -60, -72 };
        for (const auto tick : meterTicks) {
            const auto y = yForDb(tick);
            const auto text = tick > 0 ? QStringLiteral("+%1").arg(tick) : QString::number(tick);
            drawText(painter, { card.left() + 2, y - 8, card.width() * .34, 16 }, text, secondary, 9 * heightScale, Qt::AlignRight | Qt::AlignVCenter);
        }

        const auto drawButton = [&](const QRectF& button, const QString& text, const QColor& color, bool active) {
            painter.setPen(QPen(active ? color : withAlpha(color, 190), 1.1));
            painter.setBrush(active ? withAlpha(color, 65) : QColor("#16283a"));
            painter.drawRoundedRect(button, 5, 5);
            drawText(painter, button, text, active ? QColor("#ffffff") : label, 12 * heightScale, Qt::AlignCenter, true);
        };
        drawButton(impl_->channelButtonRect(index, heightScale, 0), "MUTE", QColor("#ff4f55"), channel.mute);
        if(index!=impl_->masterIndex())
            drawButton(impl_->channelButtonRect(index, heightScale, 1), "SOLO", QColor("#f7aa2f"), channel.solo);
        drawButton(impl_->channelButtonRect(index, heightScale, index==impl_->masterIndex()?1:2), "SEL", QColor("#66809b"), false);

        const auto faderColor = index < impl_->inputChannelCount
            ? QColor("#9aa8b7")
            : index < impl_->masterIndex() ? QColor("#33b6ff") : QColor("#ff5353");
        const auto track = impl_->faderTrack(index);
        painter.setPen(QPen(QColor("#0a0f15"), 7, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.center().x(), track.top(), track.center().x(), track.bottom());
        painter.setPen(QPen(faderColor, 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(track.center().x(), track.bottom(), track.center().x(), track.top() + track.height() * channel.fader);
        const auto thumbY = track.top() + track.height() * channel.fader;
        QRectF thumb(card.center().x() - card.width() * .13, thumbY - 20 * heightScale, card.width() * .26, 40 * heightScale);
        QLinearGradient thumbGradient(thumb.topLeft(), thumb.bottomRight());
        thumbGradient.setColorAt(0, faderColor.lighter(130));
        thumbGradient.setColorAt(1, faderColor.darker(125));
        painter.setPen(QPen(faderColor.lighter(135)));
        painter.setBrush(thumbGradient);
        painter.drawRoundedRect(thumb, 5, 5);
        painter.setPen(QPen(withAlpha(QColor("#07131f"), 130), 1));
        painter.drawLine(thumb.left() + 3, thumb.center().y(), thumb.right() - 3, thumb.center().y());

        const auto db = faderDbText(channel.fader);
        drawText(painter, { card.left(), track.bottom() + 10 * heightScale, card.width(), 27 * heightScale }, db, label, 12 * heightScale);
        drawText(painter, { card.left(), card.bottom() - 35 * heightScale, card.width(), 28 * heightScale }, channel.number, channel.accent.lighter(125), 18 * heightScale, Qt::AlignCenter, true);
        painter.restore();
    }

    const auto drawScrollbar = [&](bool input, const QColor& color) {
        const auto track = impl_->scrollbarTrack(input);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#101c29"));
        painter.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
        painter.setBrush(withAlpha(color, 210));
        const auto thumb = impl_->scrollbarThumb(input).adjusted(1, 1, -1, -1);
        painter.drawRoundedRect(thumb, thumb.height() / 2.0, thumb.height() / 2.0);
    };
    drawScrollbar(true, QColor("#45bfff"));
    drawScrollbar(false, QColor("#8f78ff"));

    const auto divider = impl_->groupDividerRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#243c57"));
    painter.drawRoundedRect(QRectF(divider.center().x() - 1.0, divider.top() + headerHeight * .25,
                                   2.0, headerHeight * .5), 1.0, 1.0);
}

void MixerWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        const auto index = impl_->hitChannel(event->position());
        if (index < 0) return;
        QMenu menu(this);
        auto* remove = menu.addAction(QStringLiteral("删除通道"));
        auto* copy = menu.addAction(QStringLiteral("复制通道"));
        remove->setEnabled(index != impl_->masterIndex()
                           && (index < impl_->inputChannelCount ? impl_->inputChannelCount > 1
                                                               : impl_->auxChannelCount() > 1));
        copy->setEnabled(index != impl_->masterIndex());
        auto* typeMenu = menu.addMenu(QStringLiteral("通道类型"));
        typeMenu->setEnabled(index != impl_->masterIndex());
        const std::array layouts { Common::AuxChannelLayout::mono, Common::AuxChannelLayout::stereo,
                                   Common::AuxChannelLayout::surround51, Common::AuxChannelLayout::surround71 };
        for (const auto layout : layouts) {
            auto* action = typeMenu->addAction(Common::auxLayoutName(layout));
            action->setCheckable(true);
            action->setChecked(impl_->channels[index].layout == layout);
        }
        menu.addSeparator();
        auto* sel = menu.addAction(QStringLiteral("打开SEL设置"));
        const auto id = impl_->channels[index].id;
        const auto withChannel = [this, id](auto action) {
            const auto found = std::find_if(impl_->channels.begin(), impl_->channels.end(), [id](const Channel& item) {
                return item.id == id;
            });
            if (found != impl_->channels.end())
                action(static_cast<int>(std::distance(impl_->channels.begin(), found)));
        };
        connect(remove, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->removeChannel(current); });
        });
        connect(copy, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->copyChannel(current); });
        });
        for (int layoutIndex = 0; layoutIndex < static_cast<int>(layouts.size()); ++layoutIndex) {
            connect(typeMenu->actions()[layoutIndex], &QAction::triggered, this,
                    [this, withChannel, layout = layouts[layoutIndex]] {
                        withChannel([this, layout](int current) { impl_->setChannelLayout(current, layout); });
                    });
        }
        connect(sel, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->selectChannel(current); impl_->showLargeScreen(); });
        });
        menu.exec(event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (impl_->groupDividerRect().adjusted(-3, 0, 3, 0).contains(event->position())) {
        impl_->groupDividerDragging = true;
        impl_->setInputRatioFromPosition(event->position().x() - Impl::groupGap / 2.0);
        update();
        return;
    }

    for (const auto input : { true, false }) {
        const auto thumb = impl_->scrollbarThumb(input).adjusted(-2, -3, 2, 3);
        const auto track = impl_->scrollbarTrack(input).adjusted(0, -3, 0, 3);
        if (thumb.contains(event->position())) {
            impl_->scrollbarDragging = input ? 1 : 2;
            impl_->scrollbarDragOffset = event->position().x() - thumb.left();
            return;
        }
        if (track.contains(event->position())) {
            impl_->setScrollFromThumb(input, event->position().x() - impl_->scrollbarThumb(input).width() / 2.0);
            update();
            return;
        }
    }

    const auto index = impl_->hitChannel(event->position());
    if (index < 0)
        return;

    const auto card = impl_->channelRect(index);
    const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
    if (impl_->meterHitRect(index, heightScale).contains(event->position())) {
        impl_->channels[index].postFaderMeter = !impl_->channels[index].postFaderMeter;
        update();
        return;
    }
    const auto track = impl_->faderTrack(index).adjusted(-card.width() * .18, -20, card.width() * .18, 20);
    if (track.contains(event->position())) {
        impl_->dragging = index;
        mouseMoveEvent(event);
        return;
    }

    auto& channel = impl_->channels[index];
    if (impl_->channelButtonRect(index, heightScale, 0).contains(event->position()))
        channel.mute = !channel.mute;
    else if (index!=impl_->masterIndex()&&impl_->channelButtonRect(index, heightScale, 1).contains(event->position()))
        channel.solo = !channel.solo;
    else if (impl_->channelButtonRect(index, heightScale, index==impl_->masterIndex()?1:2).contains(event->position())) {
        impl_->selectChannel(index);
        impl_->showLargeScreen();
    }
    update();
}

void MixerWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (impl_->groupDividerDragging && (event->buttons() & Qt::LeftButton)) {
        impl_->setInputRatioFromPosition(event->position().x() - Impl::groupGap / 2.0);
        update();
        return;
    }
    if (impl_->scrollbarDragging != 0 && (event->buttons() & Qt::LeftButton)) {
        const auto input = impl_->scrollbarDragging == 1;
        impl_->setScrollFromThumb(input, event->position().x() - impl_->scrollbarDragOffset);
        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        if (impl_->groupDividerRect().adjusted(-3, 0, 3, 0).contains(event->position())) {
            setCursor(Qt::SplitHCursor);
        } else {
            const auto index = impl_->hitChannel(event->position());
            const auto heightScale = std::clamp(height() / 768.0, .72, 1.35);
            setCursor(index >= 0 && impl_->meterHitRect(index, heightScale).contains(event->position())
                ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        return;
    }
    if (impl_->dragging < 0)
        return;
    const auto track = impl_->faderTrack(impl_->dragging);
    impl_->channels[impl_->dragging].fader = std::clamp((event->position().y() - track.top()) / track.height(), 0.0, 1.0);
    update();
}

void MixerWindow::mouseReleaseEvent(QMouseEvent*)
{
    impl_->dragging = -1;
    impl_->scrollbarDragging = 0;
    impl_->groupDividerDragging = false;
}

void MixerWindow::wheelEvent(QWheelEvent* event)
{
    const auto input = impl_->inputViewport().contains(event->position());
    const auto aux = impl_->auxViewport().contains(event->position());
    if (!input && !aux) {
        QWidget::wheelEvent(event);
        return;
    }

    const auto angle = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
    if (input)
        impl_->inputScrollOffset = std::clamp(impl_->effectiveInputScrollOffset() - angle / 120.0 * 54.0,
                                             0.0, impl_->maximumInputScrollOffset());
    else
        impl_->auxScrollOffset = std::clamp(impl_->effectiveAuxScrollOffset() - angle / 120.0 * 54.0,
                                           0.0, impl_->maximumAuxScrollOffset());
    update();
    event->accept();
}

}
