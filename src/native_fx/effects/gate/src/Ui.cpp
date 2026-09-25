/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
const QColor cyan(43, 197, 255), pale(205, 223, 250), muted(151, 181, 224);
constexpr const char* channels[] = { "All", "L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb" };
constexpr const char* labels[] = { "阈值", "比率", "起始时间\n(Attack)", "释放时间\n(Release)", "底噪(地板)\n(Floor)", "滞后\n(Hold)" };
constexpr QPoint dialCenters[] = { {440,144}, {562,144}, {684,144}, {440,292}, {562,292}, {684,292} };
constexpr float minima[] = { -60, 1, 0.1F, 1, -90, 0 };
constexpr float maxima[] = { 0, 20, 200, 2000, 0, 24 };

void box(QPainter& p, QRectF r, qreal radius, QColor fill, QColor edge = {}) {
    p.setPen(edge.isValid() ? QPen(edge, 1) : Qt::NoPen);
    p.setBrush(fill); p.drawRoundedRect(r, radius, radius);
}
void label(QPainter& p, QRectF r, const QString& text, int size, QColor color, bool bold = false) {
    QFont f(QStringLiteral("Microsoft YaHei UI"), size); f.setBold(bold);
    p.setFont(f); p.setPen(color); p.drawText(r, Qt::AlignCenter, text);
}
void toggle(QPainter& p, QRectF r, bool on) {
    Controls::drawToggle(p, r, on, QColor(16,139,236), QColor(45,68,93));
}
void chevron(QPainter& p, QPointF c) {
    p.setPen(QPen(muted,1.5)); p.drawLine(c+QPointF(-4,-2),c+QPointF(0,2));
    p.drawLine(c+QPointF(0,2),c+QPointF(4,-2));
}
float value(const Common::GateParameters& p,int i) {
    switch(i) { case 0:return p.thresholdDb; case 1:return p.ratio; case 2:return p.attackMs;
                case 3:return p.releaseMs; case 4:return p.floorDb; default:return p.holdDb; }
}
void assign(Common::GateParameters& p,int i,float v) {
    v=std::clamp(v,minima[i],maxima[i]);
    switch(i) { case 0:p.thresholdDb=v;break; case 1:p.ratio=v;break; case 2:p.attackMs=v;break;
                case 3:p.releaseMs=v;break; case 4:p.floorDb=v;break; default:p.holdDb=v;break; }
}
QPointF plot(float x,float y) { return {82+(x+60)*280/60,358-(y+60)*246/60}; }
QString formatted(const Common::GateParameters& p,int i) {
    if(i==1) return QStringLiteral("%1:1").arg(value(p,i),0,'f',1);
    if(i==2||i==3) return QStringLiteral("%1 ms").arg(value(p,i),0,'f',i==2?1:0);
    return QStringLiteral("%1 dB").arg(value(p,i),0,'f',1);
}
float normalizedValue(const Common::GateParameters& p,int i) {
    const float v=std::clamp(value(p,i),minima[i],maxima[i]);
    if(i==2) return std::log(v/0.1F)/std::log(200.0F/0.1F);
    if(i==3) return std::log(v/1.0F)/std::log(2000.0F/1.0F);
    return (v-minima[i])/(maxima[i]-minima[i]);
}
float valueAtNormalized(int i,float n) {
    n=std::clamp(n,0.0F,1.0F);
    if(i==2) return 0.1F*std::pow(200.0F/0.1F,n);
    if(i==3) return std::pow(2000.0F,n);
    return minima[i]+n*(maxima[i]-minima[i]);
}
}

GateEffectWidget::GateEffectWidget(QWidget* parent):QWidget(parent) {
    setFixedSize(754,654); setMouseTracking(true); setWindowTitle(QStringLiteral("Gate 门限"));
}
void GateEffectWidget::setParameters(const std::array<Parameters,8>& p) {
    parameters_=p;
    const Parameters defaults;
    selectedPreset_=0;
    for(const auto& item:p) {
        if(item.thresholdDb!=defaults.thresholdDb || item.ratio!=defaults.ratio
           || item.attackMs!=defaults.attackMs || item.releaseMs!=defaults.releaseMs
           || item.floorDb!=defaults.floorDb || item.holdDb!=defaults.holdDb
           || item.highPassHz!=defaults.highPassHz || item.lowPassHz!=defaults.lowPassHz
           || item.sidechainFilter!=defaults.sidechainFilter || item.enabled!=defaults.enabled) {
            selectedPreset_=-1; break;
        }
    }
    update();
}
void GateEffectWidget::setChangeHandler(ChangeHandler handler) { changeHandler_=std::move(handler); }
GateEffectWidget::Parameters GateEffectWidget::displayed() const noexcept {
    return parameters_[selectedChannel_<0?0:static_cast<std::size_t>(selectedChannel_)];
}
void GateEffectWidget::apply(const std::function<void(Parameters&)>& edit) {
    selectedPreset_=-1;
    const int first=selectedChannel_<0?0:selectedChannel_, end=selectedChannel_<0?8:selectedChannel_+1;
    for(int i=first;i<end;++i) { edit(parameters_[i]); if(changeHandler_) changeHandler_(i,parameters_[i]); }
    update();
}

void GateEffectWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Controls::themeColor(*this, "fxBackground", QColor(13,28,45)));
    box(p,{7,8,740,128},11,QColor(21,39,59),QColor(47,75,104));
    label(p,{22,23,110,31},QStringLiteral("作用声道"),18,Qt::white,true);
    for(int i=0;i<9;++i) {
        QRectF r(22+i*80,68,72,40); const bool chosen=i==selectedChannel_+1;
        box(p,r,5,chosen?QColor(13,56,88):QColor(24,43,63),chosen?cyan:QColor(60,86,112));
        label(p,r,QString::fromLatin1(channels[i]),12,chosen?cyan:muted);
    }
    p.save(); p.translate(0,145);
    QLinearGradient outer(0,0,0,509); outer.setColorAt(0,QColor(15,33,52)); outer.setColorAt(1,QColor(15,30,47));
    p.setPen(QPen(QColor(35,111,172),1)); p.setBrush(outer); p.drawRoundedRect(QRectF(8,7,746,495),11,11);
    box(p,{9,8,744,73},10,QColor(23,44,66));
    box(p,{20,14,56,59},7,QColor(13,73,120));
    p.setPen(QPen(cyan,3)); QPainterPath wave; wave.moveTo(25,44); wave.lineTo(33,44);
    wave.lineTo(39,26); wave.lineTo(44,59); wave.lineTo(49,32); wave.lineTo(54,53);
    wave.lineTo(60,39); wave.lineTo(68,44); p.drawPath(wave);
    label(p,{90,12,145,32},QStringLiteral("Gate 门限"),19,Qt::white,true);
    label(p,{90,46,209,22},QStringLiteral("抑制低于阈值的信号，减少底噪"),10,muted);
    box(p,{316,15,72,58},10,QColor(30,52,77)); toggle(p,{334,33,37,20},displayed().enabled);
    label(p,{401,27,42,29},QStringLiteral("预设:"),10,muted);
    box(p,{446,26,170,34},5,QColor(19,36,56),QColor(66,103,144));
    label(p,{452,26,139,34},selectedPreset_==0?QStringLiteral("Vocal Gate"):
          selectedPreset_==1?QStringLiteral("Tight Gate"):
          selectedPreset_==2?QStringLiteral("Gentle Gate"):QStringLiteral("Custom"),11,Qt::white);
    chevron(p,{600,43});
    for(int i=0;i<2;++i) {
        const int x=628+i*38; box(p,{qreal(x),27,30,32},5,QColor(27,51,77),QColor(63,101,141));
        label(p,{qreal(x),27,30,32},i?QStringLiteral("❯"):QStringLiteral("❮"),12,muted);
    }
    box(p,{707,27,31,32},5,QColor(27,51,77),QColor(63,101,141));
    label(p,{707,27,31,32},QStringLiteral("•••"),10,muted);
    box(p,{22,90,350,327},10,QColor(20,39,61),QColor(47,75,104));
    box(p,{382,90,358,327},10,QColor(20,39,61),QColor(47,75,104));
    const auto s=displayed();
    p.save(); p.setClipRect(QRectF(82,112,280,246)); p.fillRect(QRectF(82,112,280,246),QColor(13,31,50));
    p.setPen(QPen(QColor(37,60,82,120),1));
    for(int x=82;x<=362;x+=28) p.drawLine(x,112,x,358);
    for(int y=112;y<=358;y+=49) p.drawLine(82,y,362,y);
    QPainterPath lowerCurve, upperCurve;
    upperCurve.moveTo(plot(s.thresholdDb,s.thresholdDb));
    const float graphFloor=std::clamp(s.floorDb*0.8F,-60.0F,0.0F);
    for(int i=0;i<=120;++i) {
        float input=-60+i*0.5F;
        float output=input>=s.thresholdDb?input:
                     std::max(graphFloor,s.thresholdDb+(input-s.thresholdDb)*(s.ratio/4.0F));
        QPointF q=plot(input,std::clamp(output,-60.0F,0.0F));
        if(input<=s.thresholdDb) {
            if(i==0) lowerCurve.moveTo(q); else lowerCurve.lineTo(q);
        }
        if(input>s.thresholdDb) upperCurve.lineTo(q);
    }
    QPointF marker=plot(s.thresholdDb,s.thresholdDb);
    if(lowerCurve.isEmpty()) lowerCurve.moveTo(marker); else lowerCurve.lineTo(marker);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(149,113,214),1.5)); p.drawPath(lowerCurve);
    p.setPen(QPen(cyan,2)); p.drawPath(upperCurve);
    p.setPen(QPen(cyan,1)); p.drawLine(304,112,362,112); p.drawLine(362,112,362,358);
    p.setPen(QPen(cyan,1,Qt::DashLine)); p.drawLine(marker.x(),112,marker.x(),358);
    p.setPen(QPen(cyan,2)); p.setBrush(Qt::white); p.drawEllipse(marker,6,6); p.restore();
    const qreal tooltipX=std::clamp(marker.x()-45.0,115.0,270.0);
    box(p,{tooltipX,116,91,48},8,QColor(32,54,78),QColor(85,119,154));
    label(p,{tooltipX,118,91,44},QStringLiteral("Threshold\n%1 dB").arg(s.thresholdDb,0,'f',1),10,Qt::white);
    p.setPen(QPen(muted,1));
    for(int t=-60;t<=0;t+=12) {
        int x=int(plot(float(t),-60).x()),y=int(plot(-60,float(t)).y());
        p.drawText(QRectF(x-17,364,34,17),Qt::AlignCenter,QString::number(t));
        p.drawText(QRectF(42,y-9,29,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(t));
    }
    label(p,{160,387,145,24},QStringLiteral("输入电平 (dB)"),12,muted);
    p.save(); p.translate(35,310); p.rotate(-90);
    label(p,{0,0,150,20},QStringLiteral("输出电平 (dB)"),11,muted); p.restore();
    for(int i=0;i<6;++i) {
        QPoint c=dialCenters[i]; float norm=normalizedValue(s,i);
        p.setPen(QPen(QColor(58,91,124),2)); p.setBrush(QColor(18,33,51)); p.drawEllipse(QPointF(c),36,36);
        p.setPen(QPen(QColor(5,14,25),3));
        QRadialGradient knob(QPointF(c.x()-8,c.y()-9),39);
        knob.setColorAt(0,QColor(43,75,104)); knob.setColorAt(1,QColor(22,41,62));
        p.setBrush(knob); p.drawEllipse(QPointF(c),26,26);
        p.setPen(QPen(cyan,4,Qt::SolidLine,Qt::RoundCap)); p.setBrush(Qt::NoBrush);
        constexpr int arcStartAngle=210*16;
        constexpr int arcSweepAtMaximum=-260*16;
        const int arcSweepAngle=static_cast<int>(norm*arcSweepAtMaximum);
        p.drawArc(QRectF(c.x()-32,c.y()-32,64,64),arcStartAngle,arcSweepAngle);
        const float angle=-static_cast<float>(arcStartAngle+arcSweepAngle)/16.0F
                          *3.14159265F/180.0F;
        p.setPen(QPen(cyan,1.5)); p.drawLine(QPointF(c),QPointF(c.x()+std::cos(angle)*20,c.y()+std::sin(angle)*20));
        label(p,{qreal(c.x()-55),qreal(c.y()+36),110,21},formatted(s,i),11,Qt::white);
        label(p,{qreal(c.x()-61),qreal(c.y()+57),122,37},QString::fromUtf8(labels[i]),10,muted);
    }
    box(p,{22,427,430,61},9,QColor(20,39,61),QColor(47,75,104));
    box(p,{463,427,267,61},9,QColor(20,39,61),QColor(47,75,104));
    label(p,{29,442,111,30},QStringLiteral("侧链滤波"),11,muted);
    toggle(p,{167,447,36,20},s.sidechainFilter);
    label(p,{223,442,66,30},QStringLiteral("高通"),10,muted);
    box(p,{291,441,140,35},5,QColor(20,39,60),QColor(62,98,138));
    label(p,{301,441,111,35},QStringLiteral("%1 Hz").arg(s.highPassHz,0,'f',0),11,Qt::white);
    chevron(p,{416,459}); label(p,{473,442,83,30},QStringLiteral("低通"),10,muted);
    box(p,{572,441,137,35},5,QColor(20,39,60),QColor(62,98,138));
    label(p,{583,441,108,35},QStringLiteral("%1 kHz").arg(s.lowPassHz/1000,0,'f',1),11,Qt::white);
    chevron(p,{694,459}); p.restore();
}

int GateEffectWidget::dialAt(const QPointF& point) const noexcept {
    QPointF q=point-QPointF(0,145);
    for(int i=0;i<6;++i) if(QLineF(q,QPointF(dialCenters[i])).length()<37) return i;
    return -1;
}
void GateEffectWidget::adjustDial(int index,float steps) {
    constexpr float increments[]={0.5F,0.1F,1,5,0.5F,0.1F};
    float next=std::clamp(value(displayed(),index)+steps*increments[index],minima[index],maxima[index]);
    apply([=](Parameters& p){assign(p,index,next);});
}
void GateEffectWidget::applyPreset(int index) {
    const int preset=(index%3+3)%3;
    apply([preset](Parameters& p){ p=Parameters{};
        if(preset==1){p.thresholdDb=-24;p.ratio=8;p.attackMs=3;p.releaseMs=90;}
        if(preset==2){p.thresholdDb=-42;p.ratio=2;p.attackMs=20;p.releaseMs=250;}
    });
    selectedPreset_=preset;
    update();
}
void GateEffectWidget::mousePressEvent(QMouseEvent* e) {
    QPointF point=e->position();
    if(point.y()<145) for(int i=0;i<9;++i) if(QRectF(22+i*80,68,72,40).contains(point)) {
        selectedChannel_=i-1; update(); return;
    }
    QPointF q=point-QPointF(0,145);
    if(int dial=dialAt(point);dial>=0) { draggedDial_=dial; dragStart_=e->pos(); dragParameters_=displayed(); return; }
    if(QRectF(316,15,72,58).contains(q)) apply([](Parameters& p){p.enabled=!p.enabled;});
    else if(QRectF(167,441,37,35).contains(q)) apply([](Parameters& p){p.sidechainFilter=!p.sidechainFilter;});
    else if(QRectF(446,26,170,34).contains(q)) {
        QMenu menu(this);
        const QStringList names={QStringLiteral("Vocal Gate"),QStringLiteral("Tight Gate"),
                                 QStringLiteral("Gentle Gate")};
        for(int i=0;i<3;++i) {
            auto* action=menu.addAction(names[i]);
            connect(action,&QAction::triggered,this,[this,i]{applyPreset(i);});
        }
        menu.exec(mapToGlobal(QPoint(446,205)));
    }
    else if(QRectF(628,27,30,32).contains(q)) applyPreset(selectedPreset_-1);
    else if(QRectF(666,27,30,32).contains(q)) applyPreset(selectedPreset_+1);
    else if(QRectF(707,27,31,32).contains(q)) {
        QMenu menu(this);
        auto* reset=menu.addAction(QStringLiteral("恢复默认值"));
        connect(reset,&QAction::triggered,this,[this]{applyPreset(0);});
        menu.exec(mapToGlobal(QPoint(707,205)));
    }
    else if(QRectF(291,441,140,35).contains(q)) {
        constexpr float list[]={20,40,60,80,120,160,250,500,1000,2000};
        QMenu menu(this);
        for(float frequency:list) {
            auto* action=menu.addAction(QStringLiteral("%1 Hz").arg(frequency,0,'f',0));
            connect(action,&QAction::triggered,this,[this,frequency] {
                apply([frequency](Parameters& p){p.highPassHz=frequency;});
            });
        }
        menu.exec(mapToGlobal(QPoint(291,621)));
    } else if(QRectF(572,441,137,35).contains(q)) {
        constexpr float list[]={500,1000,2000,4000,8000,12000,16000,20000};
        QMenu menu(this);
        for(float frequency:list) {
            auto* action=menu.addAction(QStringLiteral("%1 kHz").arg(frequency/1000,0,'f',1));
            connect(action,&QAction::triggered,this,[this,frequency] {
                apply([frequency](Parameters& p){p.lowPassHz=frequency;});
            });
        }
        menu.exec(mapToGlobal(QPoint(572,621)));
    }
}
void GateEffectWidget::mouseMoveEvent(QMouseEvent* e) {
    if(draggedDial_<0)return;
    const float normalized=normalizedValue(dragParameters_,draggedDial_)
        +(dragStart_.y()-e->position().y())/200.0F;
    const float next=valueAtNormalized(draggedDial_,normalized);
    apply([=](Parameters& p){assign(p,draggedDial_,next);});
}
void GateEffectWidget::mouseReleaseEvent(QMouseEvent*) { draggedDial_=-1; }
void GateEffectWidget::wheelEvent(QWheelEvent* e) {
    int dial=dialAt(e->position()); if(dial<0)return;
    adjustDial(dial,e->angleDelta().y()/120.0F); e->accept();
}
}
