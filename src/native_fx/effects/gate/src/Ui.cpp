/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/gate/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
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
    Controls::drawSurface(p,r,fill,edge,radius);
}
void label(QPainter& p, QRectF r, const QString& text, int size, QColor color, bool bold = false) {
    QFont f(QStringLiteral("Microsoft YaHei UI"), size); f.setBold(bold);
    Controls::drawText(p,r,text,f,color);
}
void toggle(QPainter& p, QRectF r, bool on) {
    Controls::drawToggle(p, r, on,
        Controls::themeTokenColor("native-fx-gate", "control-toggle-indicator-on", QColor(16,139,236)),
        Controls::themeTokenColor("native-fx-gate", "control-toggle-indicator-off", QColor(45,68,93)));
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
    dragRefreshTimer_.setParent(this);
    dragRefreshTimer_.setObjectName(QStringLiteral("nativeFxDragRefreshTimer"));
    bindRefreshTimer(dragRefreshTimer_, this);
    connect(&dragRefreshTimer_, &QTimer::timeout, this, [this] { update(); });
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
    if(draggedDial_<0)update();
}
void GateEffectWidget::setChangeHandler(ChangeHandler handler) { changeHandler_=std::move(handler); }
GateEffectWidget::Parameters GateEffectWidget::displayed() const noexcept {
    return parameters_[selectedChannel_<0?0:static_cast<std::size_t>(selectedChannel_)];
}
void GateEffectWidget::apply(const std::function<void(Parameters&)>& edit) {
    selectedPreset_=-1;
    const int first=selectedChannel_<0?0:selectedChannel_, end=selectedChannel_<0?8:selectedChannel_+1;
    for(int i=first;i<end;++i) { edit(parameters_[i]); if(changeHandler_) changeHandler_(i,parameters_[i]); }
    if(draggedDial_<0)update();
}

void GateEffectWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const auto primary = Controls::themeTokenColor("native-fx-gate", "text-primary", Qt::white);
    const auto muted = Controls::themeTokenColor("native-fx-gate", "text-muted", QColor(151,181,224));
    p.fillRect(rect(), Controls::themeColor(*this, "fxBackground", QColor(13,28,45)));
    box(p,{7,8,740,128},11,QColor(21,39,59),QColor(47,75,104));
    label(p,{22,23,110,31},QStringLiteral("作用声道"),18,primary,true);
    for(int i=0;i<9;++i) {
        QRectF r(22+i*80,68,72,40); const bool chosen=i==selectedChannel_+1;
        box(p,r,5,chosen?QColor(13,56,88):QColor(24,43,63),chosen?cyan:QColor(60,86,112));
        label(p,r,QString::fromLatin1(channels[i]),12,chosen?cyan:muted);
    }
    p.save(); p.translate(0,145);
    QLinearGradient outer(0,0,0,509); outer.setColorAt(0,QColor(15,33,52)); outer.setColorAt(1,QColor(15,30,47));
    Controls::drawSurface(p,QRectF(8,7,746,495),outer,QColor(35,111,172),11);
    box(p,{9,8,744,73},10,QColor(23,44,66));
    box(p,{20,14,56,59},7,QColor(13,73,120));
    p.setPen(QPen(cyan,3)); QPainterPath wave; wave.moveTo(25,44); wave.lineTo(33,44);
    wave.lineTo(39,26); wave.lineTo(44,59); wave.lineTo(49,32); wave.lineTo(54,53);
    wave.lineTo(60,39); wave.lineTo(68,44); p.drawPath(wave);
    label(p,{90,12,145,32},QStringLiteral("Gate 门限"),19,primary,true);
    label(p,{90,46,209,22},QStringLiteral("抑制低于阈值的信号，减少底噪"),10,muted);
    box(p,{316,15,72,58},10,QColor(30,52,77)); toggle(p,{334,33,37,20},displayed().enabled);
    label(p,{401,27,42,29},QStringLiteral("预设:"),10,muted);
    box(p,{446,26,170,34},5,QColor(19,36,56),QColor(66,103,144));
    label(p,{452,26,139,34},selectedPreset_==0?QStringLiteral("Default"):
          selectedPreset_==1?QStringLiteral("Tight Gate"):
          selectedPreset_==2?QStringLiteral("Gentle Gate"):QStringLiteral("Custom"),11,primary);
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
    p.save(); p.setClipRect(QRectF(82,112,280,246));
    Controls::ChartGridStyle grid;
    grid.background=Controls::themeTokenColor("native-fx-gate", "chart-background", QColor(13,31,50));
    grid.grid=QColor(37,60,82,120);
    grid.columns=10;
    grid.rows=5;
    Controls::drawChartGrid(p,{82,112,280,246},grid);
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
    Controls::CurveStyle lowerStyle;
    lowerStyle.stroke=Controls::themeTokenColor("native-fx-gate", "chart-series-lower-transfer-stroke", QColor(149,113,214));
    lowerStyle.width=1.5;
    Controls::drawCurvePath(p,lowerCurve,lowerStyle);
    Controls::CurveStyle upperStyle;
    upperStyle.stroke=Controls::themeTokenColor("native-fx-gate", "chart-series-upper-transfer-stroke", cyan);
    upperStyle.width=2;
    Controls::drawCurvePath(p,upperCurve,upperStyle);
    p.setPen(QPen(cyan,1)); p.drawLine(304,112,362,112); p.drawLine(362,112,362,358);
    p.setPen(QPen(cyan,1,Qt::DashLine)); p.drawLine(marker.x(),112,marker.x(),358);
    Controls::drawMarker(p,marker,6,Qt::white,cyan,2); p.restore();
    const qreal tooltipX=std::clamp(marker.x()-45.0,115.0,270.0);
    box(p,{tooltipX,116,91,48},8,QColor(32,54,78),QColor(85,119,154));
    label(p,{tooltipX,118,91,44},QStringLiteral("Threshold\n%1 dB").arg(s.thresholdDb,0,'f',1),10,primary);
    p.setPen(QPen(muted,1));
    for(int t=-60;t<=0;t+=12) {
        int x=int(plot(float(t),-60).x()),y=int(plot(-60,float(t)).y());
        p.drawText(QRectF(x-17,364,34,17),Qt::AlignCenter,QString::number(t));
        p.drawText(QRectF(42,y-9,29,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(t));
    }
    label(p,{160,387,145,24},QStringLiteral("输入电平 (dB)"),12,muted);
    p.save(); p.translate(35,310); p.rotate(-90);
    label(p,{0,0,150,20},QStringLiteral("输出电平 (dB)"),11,muted); p.restore();
    const auto knobColors=Controls::paletteFor(*this,
        {Controls::themeTokenColor("native-fx-gate", "control-knob-background-base", QColor(13,28,45)),
         QColor(22,41,62),primary,
         Controls::themeTokenColor("native-fx-gate", "control-knob-indicator-normal", cyan),
         Controls::themeTokenColor("native-fx-gate", "control-knob-track", QColor(58,91,124))});
    for(int i=0;i<6;++i) {
        QPoint c=dialCenters[i]; float norm=normalizedValue(s,i);
        Controls::drawKnob(p,QPointF(c),norm,36,knobColors);
        label(p,{qreal(c.x()-55),qreal(c.y()+36),110,21},formatted(s,i),11,primary);
        label(p,{qreal(c.x()-61),qreal(c.y()+57),122,37},QString::fromUtf8(labels[i]),10,muted);
    }
    box(p,{22,427,430,61},9,QColor(20,39,61),QColor(47,75,104));
    box(p,{463,427,267,61},9,QColor(20,39,61),QColor(47,75,104));
    label(p,{29,442,111,30},QStringLiteral("侧链滤波"),11,muted);
    toggle(p,{167,447,36,20},s.sidechainFilter);
    label(p,{223,442,66,30},QStringLiteral("高通"),10,muted);
    box(p,{291,441,140,35},5,QColor(20,39,60),QColor(62,98,138));
    label(p,{301,441,111,35},QStringLiteral("%1 Hz").arg(s.highPassHz,0,'f',0),11,primary);
    chevron(p,{416,459}); label(p,{473,442,83,30},QStringLiteral("低通"),10,muted);
    box(p,{572,441,137,35},5,QColor(20,39,60),QColor(62,98,138));
    label(p,{583,441,108,35},QStringLiteral("%1 kHz").arg(s.lowPassHz/1000,0,'f',1),11,primary);
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
    for(int i=0;i<6;++i) {
        const auto center=dialCenters[i];
        const QRect readout(center.x()-55,center.y()+145+36,110,21);
        if(!readout.contains(e->pos())) continue;
        const Parameters defaults;
        const Controls::NumericControlSpec spec {minima[i],maxima[i],value(defaults,i),
                                                  i==3?0:1};
        Controls::editNumericValue(this,readout,value(displayed(),i),spec,
            [this,i](double target) {
                apply([i,target](Parameters& p) { assign(p,i,static_cast<float>(target)); });
            });
        return;
    }
    QPointF q=point-QPointF(0,145);
    if(int dial=dialAt(point);dial>=0) { draggedDial_=dial; dragStart_=e->pos(); dragParameters_=displayed(); dragRefreshTimer_.start(); return; }
    if(QRectF(316,15,72,58).contains(q)) apply([](Parameters& p){p.enabled=!p.enabled;});
    else if(QRectF(167,441,37,35).contains(q)) apply([](Parameters& p){p.sidechainFilter=!p.sidechainFilter;});
    else if(QRectF(446,26,170,34).contains(q)) {
        QMenu menu(this);
        const QStringList names={QStringLiteral("Default"),QStringLiteral("Tight Gate"),
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
void GateEffectWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton) return;
    const int dial=dialAt(event->position());
    if(dial<0) return;
    draggedDial_=-1;dragRefreshTimer_.stop();
    const Parameters defaults;
    Controls::resetNumericValue({minima[dial],maxima[dial],value(defaults,dial),
                                 dial==3?0:1},[this,dial](double target) {
        apply([dial,target](Parameters& p) { assign(p,dial,static_cast<float>(target)); });
    });
}
void GateEffectWidget::mouseMoveEvent(QMouseEvent* e) {
    if(draggedDial_<0)return;
    const float normalized=normalizedValue(dragParameters_,draggedDial_)
        +(dragStart_.y()-e->position().y())/200.0F;
    const float next=valueAtNormalized(draggedDial_,normalized);
    apply([=](Parameters& p){assign(p,draggedDial_,next);});
}
void GateEffectWidget::mouseReleaseEvent(QMouseEvent*) { draggedDial_=-1; dragRefreshTimer_.stop(); update(); }
void GateEffectWidget::wheelEvent(QWheelEvent* e) {
    int dial=dialAt(e->position()); if(dial<0)return;
    adjustDial(dial,e->angleDelta().y()/120.0F); e->accept();
}
}
