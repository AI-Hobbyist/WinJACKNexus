#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
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
};

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
        colorButton = new QPushButton(QStringLiteral("..."), this);
        colorButton->setObjectName(QStringLiteral("selCustomColor"));
        for (auto* edit : { name, comment }) {
            edit->setStyleSheet(QStringLiteral("QLineEdit {background:#102033;color:#eff6ff;border:1px solid #36516d;border-radius:5px;padding:6px 12px;font-size:17px;}"));
            connect(edit, &QLineEdit::editingFinished, this, [this] {
                if (!channel) return;
                channel->name = name->text().trimmed();
                channel->source = comment->text();
                if (changed) changed();
                if (detailsChanged) detailsChanged();
            });
        }
        colorButton->setStyleSheet(QStringLiteral("QPushButton {background:#172a40;color:#d6eaff;border:1px solid #50708c;border-radius:5px;font-size:16px;}"));
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
            auto& state=channel->selState;
            selectedPoint=std::clamp(selectedPoint,0,std::max(0,static_cast<int>(Common::auxLayoutChannelLabels(channel->layout).size())-1));
            if(state.adjustablePoints.size()!=static_cast<std::size_t>(Common::auxLayoutChannelLabels(channel->layout).size()))
                state.adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);
            state.sourceLayout=channel->layout;
            state.targetLayout=channel->layout;
            setProperty("selChannelLayout",static_cast<int>(channel->layout));
            setProperty("selActiveSendCount",Common::auxLayoutChannelLabels(channel->layout).size());
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
        place(name, 29, 53, 254, 42);
        place(comment, 29, 148, 254, 51);
        place(colorButton, 252, 254, 31, 36);
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
        panel({14, 11, 287, 790}); panel({313, 11, 1064, 443}); panel({313, 465, 1064, 336});
        text({29, 22, 240, 25}, QStringLiteral("通道名"), 16, ink, true);
        text({29, 117, 240, 25}, QStringLiteral("注释"), 16, ink, true);
        text({29, 218, 240, 25}, QStringLiteral("通道颜色"), 16, ink, true);
        const std::array<QColor,8> colors { QColor("#24b8ff"), QColor("#20c5dd"), QColor("#45d877"), QColor("#ffd246"), QColor("#ffae55"), QColor("#f34d70"), QColor("#e359da"), QColor("#9963ec") };
        p.setPen(border); p.setBrush(QColor("#15263a")); p.drawRoundedRect(QRectF(29,254,216,36),5,5);
        for (int i=0;i<8;++i) { p.setPen(Qt::NoPen); p.setBrush(colors[i]); p.drawEllipse(QPointF(47+i*26,272),9,9); }
        text({29, 317, 200, 25}, QStringLiteral("M / S"), 16, muted);
        modeToggle({29,351,118,39},QStringLiteral("M"),channel && channel->mute,QColor("#ff4b62"));
        modeToggle({165,351,118,39},QStringLiteral("S"),channel && channel->solo,QColor("#ffae2d"));
        text({29, 418, 200, 26}, QStringLiteral("电平表"), 16, ink, true);
        for(int m=0;m<2;++m) {
            const int x=24+m*138; panel({qreal(x),449,125,347});
            text({qreal(x),457,125,31},m?QStringLiteral("输出"):QStringLiteral("输入"),17,ink,false,Qt::AlignCenter);
            const std::array<int,7> ticks {12,0,-6,-12,-24,-48,-72};
            for(int t=0;t<7;++t) text({qreal(x+14),qreal(492+t*32),40,20},QString::number(ticks[t]),12,muted,false,Qt::AlignRight|Qt::AlignVCenter);
            const qreal current=channel?std::clamp(channel->level,-72.0,12.0):-72.0;
            const qreal fraction=(current+72.0)/84.0;
            QRectF track(x+67,497,28,201);p.setPen(Qt::NoPen);p.setBrush(QColor("#07141f"));p.drawRoundedRect(track,3,3);
            const qreal litTop=698-201*fraction;
            const auto segment=[&](qreal low,qreal high,QColor color){const qreal a=698-201*(high+72)/84.0,b=698-201*(low+72)/84.0;const qreal top=std::max(a,litTop);if(b>top){p.setBrush(color);p.drawRect(QRectF(71+x,top,20,b-top));}};
            segment(-72,-12,QColor("#17d675"));segment(-12,0,QColor("#f2df48"));segment(0,12,QColor("#f2484d"));
            text({qreal(x+22),710,96,31},QStringLiteral("%1 dB").arg(channel ? channel->level : -3.2,0,'f',1),15,ink,false,Qt::AlignCenter);
        }
        text({330,20,250,29},QStringLiteral("空间声像器"),17,ink,true);
        button({330,52,80,34},QStringLiteral("2D"),!threeD); button({421,52,80,34},QStringLiteral("3D"),threeD);
        button({521,52,165,34},Common::auxLayoutName(channel?channel->layout:Common::AuxChannelLayout::stereo)
               +(channel&&channel->number!=QStringLiteral("MAIN")?QStringLiteral(" ▾"):QString()));
        text({718,52,120,34},QStringLiteral("显示分布图"),15,muted);
        toggle({849,55,42,25},distribution);
        panel({918,52,143,360}); text({928,55,80,28},QStringLiteral("视图"),15,muted);
        const std::array<QString,3> viewNames {QStringLiteral("顶视图"),QStringLiteral("前视图"),QStringLiteral("侧视图")};
        button({928,86,120,35},viewNames[viewMode]+QStringLiteral("  ▾"));
        text({928,135,110,24},QStringLiteral("Main 参考点"),15,muted);
        const auto referenceLabels=Common::auxLayoutChannelLabels(channel?channel->layout:Common::AuxChannelLayout::stereo);
        for(int i=0;i<std::min(8,static_cast<int>(referenceLabels.size()));++i) {
            const qreal y=160+i*25.0;
            text({934,y,68,24},referenceLabels[i],14,muted);
            toggle({1003,y,38,23},selectedPoint==i);
        }
        toggle({918,377,40,24},headphones);
        button({968,369,89,40},QStringLiteral("重置"));
        button({1134,52,108,35},QStringLiteral("3D 视图"),threeD);button({1252,52,110,35},QStringLiteral("重置视角"));
        panel({1067,96,294,344});
        // Surround room perspective and reference markers.
        p.setPen(QPen(QColor("#25445d"),1));
        for(int i=0;i<7;++i) p.drawLine(QPointF(1085+i*42,432),QPointF(1206+(i-3)*13,245));
        for(int i=0;i<6;++i) p.drawLine(QPointF(1075,265+i*30),QPointF(1361,265+i*30));
        QPainterPath room;room.moveTo(1205,152);room.lineTo(1303,194);room.lineTo(1263,252);room.lineTo(1145,252);room.lineTo(1105,194);room.closeSubpath();
        p.setPen(Qt::NoPen);p.setBrush(QColor(18,113,207,95));p.drawPath(room);
        const std::array<QPointF,6> topSpeakers {QPointF(1114,185),QPointF(1296,185),QPointF(1205,150),QPointF(1093,346),QPointF(1318,346),QPointF(1205,369)};
        const std::array<QPointF,6> frontSpeakers {QPointF(1097,231),QPointF(1313,231),QPointF(1205,186),QPointF(1115,362),QPointF(1295,362),QPointF(1205,379)};
        const std::array<QPointF,6> sideSpeakers {QPointF(1120,205),QPointF(1239,247),QPointF(1300,181),QPointF(1097,342),QPointF(1256,355),QPointF(1198,380)};
        const auto& speaker=viewMode==0?topSpeakers:viewMode==1?frontSpeakers:sideSpeakers;
        const std::array<QString,6> speakerNames {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("LFE")};
        for(int i=0;i<6;++i) {p.setPen(QColor("#90b5df"));p.setBrush(QColor("#314b68"));p.drawRoundedRect(QRectF(speaker[i].x()-10,speaker[i].y()-10,20,24),3,3);text({speaker[i].x()-18,speaker[i].y()-31,36,19},speakerNames[i],12,ink,false,Qt::AlignCenter);}
        p.setPen(Qt::NoPen);p.setBrush(QColor("#a2bed7"));p.drawEllipse(QRectF(1190,222,30,45));
        panel({330,95,429,345});
        const QPointF center(542,269); const qreal radius=143;
        p.setBrush(Qt::NoBrush);
        for(int ring=1;ring<=5;++ring){p.setPen(QPen(QColor("#244a66"),1));p.drawEllipse(center,143.0*ring/5,143.0*ring/5);}
        for(int angle=0;angle<360;angle+=30){
            const qreal radians=angle*3.14159265358979323846/180.0;
            p.setPen(QPen(angle%90==0?QColor("#426884"):QColor("#1b3d56"),1));
            p.drawLine(center,center+QPointF(std::sin(radians),-std::cos(radians))*143);
        }
        const auto* pannerBus=selectedBus();
        if(distribution&&pannerBus&&!pannerBus->adjustablePoints.empty()){
            const qreal coverage=pannerBus->pointCoverageDegrees.size()>static_cast<std::size_t>(selectedPoint)
                ? pannerBus->pointCoverageDegrees[selectedPoint]:pannerBus->coverageDegrees;
            QPainterPath sector;sector.moveTo(center);
            sector.arcTo(QRectF(center.x()-120,center.y()-120,240,240),90-(pannerBus->angleDegrees+coverage*.5),coverage);
            sector.closeSubpath();p.setPen(QPen(QColor(47,197,255,180),2));p.setBrush(QColor(21,143,208,55));p.drawPath(sector);
        }
        const auto referencePoints=Common::auxLayoutReferencePoints(channel?channel->layout:Common::AuxChannelLayout::stereo);
        for(const auto& point:referencePoints){p.setPen(QPen(QColor(138,169,214,110),2));p.setBrush(QColor(109,141,184,75));p.drawEllipse(QPointF(center.x()+point.x()*120,center.y()+point.y()*120),10,10);}
        if(pannerBus){
            if(pannerBus->linked&&pannerBus->adjustablePoints.size()==2){
                const auto& a=pannerBus->adjustablePoints[0];const auto& b=pannerBus->adjustablePoints[1];
                p.setPen(QPen(QColor("#5bd6ff"),3));p.drawLine(center+QPointF(a.x()*120,a.y()*120),center+QPointF(b.x()*120,b.y()*120));
            }
            for(int i=0;i<static_cast<int>(pannerBus->adjustablePoints.size());++i){
                const auto& point=pannerBus->adjustablePoints[i];
                const QPointF screen(center.x()+point.x()*120,center.y()+point.y()*120);
                const bool selected=i==selectedPoint;
                p.setPen(QPen(selected?QColor("#79e3ff"):QColor(85,189,233,115),selected?2.5:1.2));
                p.setBrush(QColor(22,141,255,selected?28:9));p.drawEllipse(screen,selected?36:27,selected?36:27);
                if(selected){p.setPen(QPen(QColor("#7ee8ff"),5));p.setBrush(Qt::NoBrush);p.drawEllipse(screen,14,14);}
                p.setPen(QPen(selected?QColor("#e8fbff"):QColor("#9dc3ff"),selected?3:2));p.setBrush(selected?QColor("#42a8ff"):QColor("#2e9dff"));p.drawEllipse(screen,10,10);
                const auto labels=Common::auxLayoutChannelLabels(channel->layout);
                if(i<labels.size())text({screen.x()-17,screen.y()+14,34,19},labels[i],12,ink,true,Qt::AlignCenter);
            }
        }
        p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawEllipse(center,4,4);
        text({522,103,40,20},QStringLiteral("90°"),12,muted,false,Qt::AlignCenter);text({690,259,45,20},QStringLiteral("0°"),12,muted);text({348,259,44,20},QStringLiteral("180°"),12,muted);text({519,412,47,20},QStringLiteral("270°"),12,muted,false,Qt::AlignCenter);
        const std::array<QString,3> knobs {QStringLiteral("宽度"),QStringLiteral("前后"),QStringLiteral("角度")};
        for(int i=0;i<3;++i){
            const int y=111+i*109;text({780.0,qreal(y),90,24},knobs[i],15,muted);
            const qreal value=i==0?(pannerBus?pannerBus->width:.7):i==1?(pannerBus&&!pannerBus->adjustablePoints.empty()?(pannerBus->adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(pannerBus->adjustablePoints.size())-1)].y()+1)/2:.5):(pannerBus?(pannerBus->angleDegrees+180)/360:.5);
            p.setPen(QPen(QColor("#203e55"),7,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(794,y+54),QPointF(864,y+54));
            p.setPen(QPen(QColor("#2fc5ff"),7,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(794,y+54),QPointF(794+70*value,y+54));
            p.setPen(QPen(QColor("#68a7ff"),4));p.drawLine(QPointF(794+70*value,y+44),QPointF(794+70*value,y+64));
            text({794.0,qreal(y+78),69,23},i==0?QStringLiteral("%1%").arg(qRound(pannerBus?pannerBus->width*100:70)):(i==1?QStringLiteral("%1%").arg(qRound(pannerBus&&!pannerBus->adjustablePoints.empty()?pannerBus->adjustablePoints[0].y()*100:0)) : QStringLiteral("%1°").arg(qRound(pannerBus?pannerBus->angleDegrees:0))),14,ink,false,Qt::AlignCenter);
        }
        text({329,476,200,28},QStringLiteral("分控推子"),17,ink,true);
        text({924,478,75,28},QStringLiteral("通道模式"),15,muted);button({1000,477,124,34},Common::auxLayoutName(channel?channel->layout:Common::AuxChannelLayout::stereo)
               +(channel&&channel->number!=QStringLiteral("MAIN")?QStringLiteral(" ▾"):QString()));
        text({1128,477,78,34},QStringLiteral("全通道链接"),14,muted);
        toggle({1208,482,44,24},linked);button({1263,477,99,34},QStringLiteral("重置"));
        const auto* bus=pannerBus;
        const std::array<QString,8> labels {QStringLiteral("L"),QStringLiteral("R"),QStringLiteral("C"),QStringLiteral("LFE"),QStringLiteral("Ls"),QStringLiteral("Rs"),QStringLiteral("Lb"),QStringLiteral("Rb")};
        const int activeCount=channel?Common::auxLayoutChannelLabels(channel->layout).size():0;
        const int count=8;const qreal gap=12, stripWidth=(1034.0-gap*(count-1))/count;
        for(int i=0;i<count;++i) {
            p.save();if(i>=activeCount)p.setOpacity(.32);
            const qreal x=328+i*(stripWidth+gap);panel({x,520,stripWidth,267});
            text({x,523,stripWidth,26},labels[i],16,ink,true,Qt::AlignCenter);
            const qreal sendLevel=bus&&i<static_cast<int>(bus->sends.size())?bus->sends[i].levelDb:0.0;
            const qreal meterDb=std::clamp((channel?channel->level:-72.0)+sendLevel,-72.0,12.0);
            QRectF meter(x+37,561,19,139);p.setPen(Qt::NoPen);p.setBrush(QColor("#06131f"));p.drawRoundedRect(meter,2,2);
            const qreal litTop=700-139*(meterDb+72)/84.0;
            const auto meterSegment=[&](qreal low,qreal high,QColor color){
                const qreal top=std::max(litTop,700-139*(high+72)/84.0),bottom=700-139*(low+72)/84.0;
                if(bottom>top){p.setBrush(color);p.drawRect(QRectF(x+40,top,13,bottom-top));}
            };
            meterSegment(-72,-12,QColor("#17d675"));
            meterSegment(-12,0,QColor("#f2df48"));
            meterSegment(0,12,QColor("#f2484d"));
            for(const int mark:{12,0,-24,-48,-72})
                text({x+2,700-139*(mark+72)/84.0-8,32,16},mark==12?QStringLiteral("+12"):QString::number(mark),10,muted,false,Qt::AlignRight|Qt::AlignVCenter);
            const qreal fader=bus&&i<static_cast<int>(bus->sends.size())?std::clamp((bus->sends[i].levelDb+72)/84.0,0.0,1.0):.66;const qreal top=700-fader*139;
            p.setPen(QPen(QColor("#7198bd"),2));p.drawLine(QPointF(x+stripWidth-34,561),QPointF(x+stripWidth-34,700));
            p.setPen(QColor("#b6d6f1"));p.setBrush(i<2?QColor("#64c8fa"):QColor("#b2c3e8"));p.drawRoundedRect(QRectF(x+stripWidth-46,top-17,24,34),3,3);
            p.setPen(QPen(QColor("#315d75"),2));p.drawLine(QPointF(x+stripWidth-43,top),QPointF(x+stripWidth-25,top));
            text({x+10,705,stripWidth-20,29},QStringLiteral("%1 dB").arg(-72+84*fader,0,'f',1),14,ink,false,Qt::AlignCenter);
            modeToggle({x+16,744,(stripWidth-43)/2,30},QStringLiteral("M"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].mute,QColor("#ff4b62"));
            modeToggle({x+27+(stripWidth-43)/2,744,(stripWidth-43)/2,30},QStringLiteral("S"),bus&&i<static_cast<int>(bus->sends.size())&&bus->sends[i].solo,QColor("#ffae2d"));
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!channel || event->button()!=Qt::LeftButton) return;
        const QPointF pt=designPoint(event->position());
        const std::array<QColor,8> colors { QColor("#24b8ff"),QColor("#20c5dd"),QColor("#45d877"),QColor("#ffd246"),QColor("#ffae55"),QColor("#f34d70"),QColor("#e359da"),QColor("#9963ec") };
        if (pt.y()>=254&&pt.y()<290&&pt.x()>=29&&pt.x()<245) {const int i=int((pt.x()-34)/26);if(i>=0&&i<8){channel->accent=colors[i];channel->customColor=true;}}
        else if(QRectF(29,351,118,39).contains(pt)) channel->mute=!channel->mute;
        else if(QRectF(165,351,118,39).contains(pt)) channel->solo=!channel->solo;
        else if(QRectF(330,52,80,34).contains(pt)) threeD=false;
        else if(QRectF(421,52,80,34).contains(pt)||QRectF(1134,52,108,35).contains(pt)) threeD=true;
        else if(QRectF(1252,52,110,35).contains(pt))viewMode=0;
        else if(QRectF(521,52,165,34).contains(pt)&&channel->number!=QStringLiteral("MAIN")){
            QMenu menu(this);
            const std::array layouts {Common::AuxChannelLayout::mono,Common::AuxChannelLayout::stereo,Common::AuxChannelLayout::surround51,Common::AuxChannelLayout::surround71};
            for(const auto layout:layouts){auto* action=menu.addAction(Common::auxLayoutName(layout));action->setCheckable(true);action->setChecked(channel->layout==layout);
                connect(action,&QAction::triggered,this,[this,layout]{if(layoutChanged)layoutChanged(layout);});}
            menu.exec(mapToGlobal(event->position().toPoint()));return;
        }
        else if(QRectF(928,86,120,35).contains(pt)){
            QMenu menu(this);
            const std::array<QString,3> names {QStringLiteral("顶视图"),QStringLiteral("前视图"),QStringLiteral("侧视图")};
            for(int i=0;i<3;++i){auto* action=menu.addAction(names[i]);action->setCheckable(true);action->setChecked(i==viewMode);
                connect(action,&QAction::triggered,this,[this,i]{viewMode=i;update();});}
            menu.exec(mapToGlobal(event->position().toPoint()));return;
        }
        else if(QRectF(849,55,42,25).contains(pt)) distribution=!distribution;
        else if(QRectF(918,369,40,40).contains(pt)) headphones=!headphones;
        else if(QRectF(968,369,89,40).contains(pt)||QRectF(1263,477,99,34).contains(pt)) {if(auto* bus=selectedBus()){bus->adjustablePoints=Common::auxLayoutReferencePoints(channel->layout);bus->width=.7;bus->angleDegrees=0;const auto active=Common::auxLayoutChannelLabels(channel->layout).size();for(int i=0;i<active;++i)bus->sends[i].levelDb=-2;}}
        else if(QRectF(1128,477,125,34).contains(pt)) {linked=!linked;if(auto* bus=selectedBus())bus->sendsLinked=linked;}
        else if(QRectF(1000,477,124,34).contains(pt)&&channel->number!=QStringLiteral("MAIN")) {
            QMenu menu(this);
            const std::array layouts {Common::AuxChannelLayout::mono,Common::AuxChannelLayout::stereo,Common::AuxChannelLayout::surround51,Common::AuxChannelLayout::surround71};
            for(const auto layout:layouts){
                auto* action=menu.addAction(Common::auxLayoutName(layout));
                action->setCheckable(true);action->setChecked(channel->layout==layout);
                connect(action,&QAction::triggered,this,[this,layout]{if(layoutChanged)layoutChanged(layout);});
            }
            menu.exec(mapToGlobal(event->position().toPoint()));return;
        }
        else if(pt.x()>=794&&pt.x()<864&&pt.y()>=111&&pt.y()<413){
            if(auto* bus=selectedBus()){
                const int knob=std::clamp(int((pt.y()-111)/109),0,2);
                setKnob(*bus,knob,pt.x());draggedKnob=knob;
            }
        }
        else if(pt.x()>=1003&&pt.x()<1041&&pt.y()>=160&&pt.y()<360){selectedPoint=std::clamp(int((pt.y()-160)/25),0,std::max(0,static_cast<int>(Common::auxLayoutChannelLabels(channel->layout).size())-1));}
        else if(QRectF(385,126,314,286).contains(pt)) {
            if(auto* bus=selectedBus();bus&&!bus->adjustablePoints.empty()){
                qreal nearest=1e9;int nearestIndex=0;
                for(int i=0;i<static_cast<int>(bus->adjustablePoints.size());++i){const auto& point=bus->adjustablePoints[i];const qreal dx=pt.x()-542-point.x()*120,dy=pt.y()-269-point.y()*120;const qreal distance=dx*dx+dy*dy;if(distance<nearest){nearest=distance;nearestIndex=i;}}
                selectedPoint=nearestIndex;draggedPoint=nearestIndex;
                bus->adjustablePoints[nearestIndex]=QPointF(std::clamp((pt.x()-542)/120.0,-1.0,1.0),std::clamp((pt.y()-269)/120.0,-1.0,1.0));bus->linked=false;
            }
        }
        else if(pt.y()>=520&&pt.y()<787&&pt.x()>=328&&pt.x()<1362) {
            auto* bus=selectedBus();
            if(!bus)return;
            const int activeCount=Common::auxLayoutChannelLabels(channel->layout).size();
            const qreal gap=12,sw=(1034.0-gap*7)/8;
            const int i=int((pt.x()-328)/(sw+gap));
            const qreal x=328+i*(sw+gap);
            if(i<0||i>=activeCount||i>=static_cast<int>(bus->sends.size())||pt.x()>x+sw)return;
            if(pt.y()>=744){
                if(pt.x()<x+sw/2)bus->sends[i].mute=!bus->sends[i].mute;
                else bus->sends[i].solo=!bus->sends[i].solo;
            }else if(pt.y()>=550&&pt.y()<=710){
                setSendLevel(*bus,i,12-84*std::clamp((pt.y()-561)/139.0,0.0,1.0));
                draggedFader=i;
            }
        }
        update();if(changed)changed();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if(!channel||!(event->buttons()&Qt::LeftButton))return;
        auto* bus=selectedBus();if(!bus||draggedFader>=static_cast<int>(bus->sends.size()))return;
        if(draggedKnob>=0){setKnob(*bus,draggedKnob,designPoint(event->position()).x());update();if(changed)changed();return;}
        if(draggedPoint>=0&&draggedPoint<static_cast<int>(bus->adjustablePoints.size())){
            const QPointF pt=designPoint(event->position());
            bus->adjustablePoints[draggedPoint]=QPointF(std::clamp((pt.x()-542)/120.0,-1.0,1.0),std::clamp((pt.y()-269)/120.0,-1.0,1.0));update();if(changed)changed();return;
        }
        if(draggedFader<0)return;
        const qreal y=designPoint(event->position()).y();setSendLevel(*bus,draggedFader,12-84*std::clamp((y-561)/139.0,0.0,1.0));update();if(changed)changed();
    }
    void mouseReleaseEvent(QMouseEvent*) override {draggedFader=-1;draggedPoint=-1;draggedKnob=-1;}
private:
    qreal contentScale() const { return std::min(width()/1394.0,height()/813.0); }
    QPointF contentOrigin() const { const auto scale=contentScale();return {(width()-1394*scale)/2,(height()-813*scale)/2}; }
    QPointF designPoint(const QPointF& point) const { return (point-contentOrigin())/contentScale(); }
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
    void setKnob(Common::AuxBusState& bus,int index,qreal x)
    {
        if(index==0)bus.width=std::clamp((x-794)/70.0,0.0,1.0);
        else if(index==1&&!bus.adjustablePoints.empty())
            bus.adjustablePoints[std::clamp(selectedPoint,0,static_cast<int>(bus.adjustablePoints.size())-1)].setY(std::clamp((x-829)/35.0,-1.0,1.0));
        else if(index==2)bus.angleDegrees=std::clamp((x-829)*5.0,-180.0,180.0);
    }
    Common::AuxBusState* selectedBus() const
    {
        return channel ? &channel->selState : nullptr;
    }
    Channel* channel {};
    QLineEdit* name {};
    QLineEdit* comment {};
    QPushButton* colorButton {};
    bool threeD {};
    bool distribution {true};
    bool headphones {};
    bool linked {true};
    int draggedFader {-1};
    int draggedPoint {-1};
    int draggedKnob {-1};
    int selectedPoint {};
    std::uint32_t displayedId {};
    int viewMode {};
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

    void showAuxSettings(const Channel& channel)
    {
        if (auxSettingsWindow.isNull()) {
            auxSettingsWindow = new AuxSettingsWindow(auxState, &owner);
            auxSettingsWindow->setAttribute(Qt::WA_DeleteOnClose);
            auxSettingsWindow->setStateChangedCallback([this] {
                rebuildAuxChannels();
                owner.update();
            });
            auxSettingsWindow->setChainEnabledHandler([this](bool enabled) {
                const auto found = std::find_if(channels.begin(), channels.end(), [this](const Channel& item) {
                    return item.id == auxWindowChannelId;
                });
                if (found != channels.end()) { found->aux = enabled; owner.update(); }
            });
        }
        if (selAuxPage && auxSettingsWindow->parentWidget() == selAuxPage) {
            selAuxPage->layout()->removeWidget(auxSettingsWindow);
            auxSettingsWindow->setParent(&owner, Qt::Window);
        }
        auxWindowChannelId = channel.id;
        const auto sourceKey = channel.auxId == 0
            ? QStringLiteral("input:%1").arg(channel.id)
            : QStringLiteral("aux:%1").arg(channel.auxId);
        auxSettingsWindow->setSource(sourceKey, channel.name, channel.auxId, channel.layout);
        auxSettingsWindow->setChainEnabled(channel.aux);
        if (channel.auxId != 0)
            auxSettingsWindow->selectBus(channel.auxId);
        auxSettingsWindow->show();
        auxSettingsWindow->raise();
        auxSettingsWindow->activateWindow();
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

    void showFxSettings(int index)
    {
        auto* window = fxSettingsFor(index);
        if (selFxPage && window->parentWidget() == selFxPage) {
            selFxPage->layout()->removeWidget(window);
            window->setParent(&owner, Qt::Window);
        }
        window->show();
        window->raise();
        window->activateWindow();
    }

    static QStringList fxChannels(Common::AuxChannelLayout layout)
    {
        if (layout == Common::AuxChannelLayout::mono) return { QStringLiteral("L") };
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
        if (selectedChannelId == channel.id) {
            selectedChannelId = channels.empty() ? 0 : channels.front().id;
            updateSel();
        }
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
                    auxSettingsWindow->setSource(key, found->name, found->auxId, layout);
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
                    auxSettingsWindow->setSource(key,current->name,current->auxId,current->layout);
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
            selStack = new QStackedWidget(selWindow);
            selStack->setObjectName(QStringLiteral("selPages"));
            selMain = new SelMainPage(selStack);
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
                    auxSettingsWindow->setSource(key,found->name,found->auxId,found->layout);
                }
                if(auto fx=fxSettingsWindows.find(found->id);fx!=fxSettingsWindows.end()&&fx->second)
                    fx->second->setSourceName(found->name);
            };
            selMain->layoutChanged = [this](Common::AuxChannelLayout layout) {
                const auto found=std::find_if(channels.cbegin(),channels.cend(),[this](const Channel& item){return item.id==selectedChannelId;});
                if(found==channels.cend()||found->number==QStringLiteral("MAIN"))return;
                setChannelLayout(static_cast<int>(std::distance(channels.cbegin(),found)),layout);
                updateSel();
            };
            selStack->addWidget(selMain);
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
                    for(int j=0;j<3;++j)if(auto* item=header->findChild<QPushButton*>(QStringLiteral("selTab%1").arg(std::array<QString,3>{QStringLiteral("Main"),QStringLiteral("AUX"),QStringLiteral("FX")}[j])))item->setChecked(j==i);
                });
            }
            auto* close = new QPushButton(QStringLiteral("×"),header);
            close->setFixedWidth(48);headerLayout->addWidget(close);
            connect(close,&QPushButton::clicked,selWindow,&QWidget::close);
            layout->addWidget(header);
            layout->addWidget(selStack,1);
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
            auxSettingsWindow->setStateChangedCallback([this]{rebuildAuxChannels();owner.update();});
            auxSettingsWindow->setChainEnabledHandler([this](bool enabled){
                auto it=std::find_if(channels.begin(),channels.end(),[this](const Channel& c){return c.id==selectedChannelId;});
                if(it!=channels.end()){it->aux=enabled;owner.update();}
            });
        }
        const auto key=found->auxId?QStringLiteral("aux:%1").arg(found->auxId):QStringLiteral("input:%1").arg(found->id);
        auxSettingsWindow->setSource(key,found->name,found->auxId,found->layout);
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
        const auto top = card.top() + card.height() * .64;
        return { card.center().x() - 3.0, top, 6.0, card.height() * .23 };
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

        const auto top = card.top() + headerHeight + 2 * heightScale;
        drawAdaptiveText(painter, { card.left(), top, card.width(), 20 * heightScale }, channel.name,
                 label, 16 * heightScale, 10 * heightScale, Qt::AlignCenter, true);
        drawAdaptiveText(painter, { card.left(), top + 26 * heightScale, card.width(), 14 * heightScale }, channel.source,
                 secondary, 12 * heightScale, 8 * heightScale);

        const auto meter = impl_->meterRect(index, heightScale);
        const auto meterMode = impl_->meterModeRect(index, heightScale);
        const auto meterLevel = channel.postFaderMeter
            ? std::clamp(channel.level + faderDbValue(channel.fader), -72.0, 12.0)
            : channel.level;
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

        const auto buttonTop = meter.bottom() + 20 * heightScale;
        const auto buttonHeight = 28 * heightScale;
        const auto side = 6.0;
        const auto halfWidth = (card.width() - side * 3) / 2.0;
        const auto drawButton = [&](const QRectF& button, const QString& text, const QColor& color, bool active) {
            painter.setPen(QPen(active ? color : withAlpha(color, 190), 1.1));
            painter.setBrush(active ? withAlpha(color, 65) : QColor("#16283a"));
            painter.drawRoundedRect(button, 5, 5);
            drawText(painter, button, text, active ? QColor("#ffffff") : label, 12 * heightScale, Qt::AlignCenter, true);
        };
        if (index == impl_->masterIndex()) {
            drawButton({ card.left() + side, buttonTop + buttonHeight, halfWidth, buttonHeight }, "FX", QColor("#9b61ff"), channel.fx);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 2.8, card.width() - side * 2, buttonHeight }, "SEL", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 4.3, card.width() - side * 2, buttonHeight }, "M", QColor("#ff4f55"), channel.mute);
        } else {
            drawButton({ card.left() + side, buttonTop, halfWidth, buttonHeight }, "AUX", QColor("#33b6ff"), channel.aux);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 1.35, halfWidth, buttonHeight }, "FX", QColor("#9b61ff"), channel.fx);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 1.35, halfWidth, buttonHeight }, "⚙", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 2.85, card.width() - side * 2, buttonHeight }, "SEL", QColor("#66809b"), false);
            drawButton({ card.left() + side, buttonTop + buttonHeight * 4.35, halfWidth, buttonHeight }, "M", QColor("#ff4f55"), channel.mute);
            drawButton({ card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 4.35, halfWidth, buttonHeight }, "S", QColor("#f7aa2f"), channel.solo);
        }

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
        auto* fx = menu.addAction(QStringLiteral("打开FX设置"));
        auto* aux = menu.addAction(QStringLiteral("打开AUX设置"));
        aux->setEnabled(index != impl_->masterIndex());
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
        connect(fx, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->showFxSettings(current); });
        });
        connect(aux, &QAction::triggered, this, [this, withChannel] {
            withChannel([this](int current) { impl_->showAuxSettings(impl_->channels[current]); });
        });
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
    const auto headerHeight = 42.0 * heightScale;
    const auto buttonTop = impl_->meterRect(index, heightScale).bottom() + 20 * heightScale;
    const auto buttonHeight = 28 * heightScale;
    const auto side = 6.0;
    const auto halfWidth = (card.width() - side * 3) / 2.0;
    const auto leftButton = [&](qreal row) {
        return QRectF(card.left() + side, buttonTop + buttonHeight * row, halfWidth, buttonHeight);
    };
    if (index == impl_->masterIndex()) {
        const QRectF fxButton(card.left() + side, buttonTop + buttonHeight, halfWidth, buttonHeight);
        const QRectF settingsButton(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight,
                                    halfWidth, buttonHeight);
        if (fxButton.contains(event->position())) {
            channel.fx = !channel.fx;
            if (auto window = impl_->fxSettingsWindows[channel.id]) window->setChainEnabled(channel.fx);
        }
        else if (settingsButton.contains(event->position()))
            impl_->showFxSettings(index);
        else if (QRectF(card.left() + side, buttonTop + buttonHeight * 2.8,
                        card.width() - side * 2, buttonHeight).contains(event->position()))
            impl_->selectChannel(index);
        else if (QRectF(card.left() + side, buttonTop + buttonHeight * 4.3,
                        card.width() - side * 2, buttonHeight).contains(event->position()))
            channel.mute = !channel.mute;
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop,
                      halfWidth, buttonHeight).contains(event->position())) {
        impl_->showAuxSettings(channel);
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 1.35,
                      halfWidth, buttonHeight).contains(event->position())) {
        impl_->showFxSettings(index);
    } else if (leftButton(0).contains(event->position())) {
        channel.aux = !channel.aux;
        if (impl_->auxSettingsWindow && impl_->auxWindowChannelId == channel.id)
            impl_->auxSettingsWindow->setChainEnabled(channel.aux);
    } else if (leftButton(1.35).contains(event->position())) {
        channel.fx = !channel.fx;
        if (auto window = impl_->fxSettingsWindows[channel.id]) window->setChainEnabled(channel.fx);
    } else if (QRectF(card.left() + side, buttonTop + buttonHeight * 2.85,
                      card.width() - side * 2, buttonHeight).contains(event->position())) {
        impl_->selectChannel(index);
    } else if (leftButton(4.35).contains(event->position())) {
        channel.mute = !channel.mute;
    } else if (QRectF(card.left() + side * 2 + halfWidth, buttonTop + buttonHeight * 4.35,
                      halfWidth, buttonHeight).contains(event->position())) {
        channel.solo = !channel.solo;
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
