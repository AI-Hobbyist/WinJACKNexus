/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/NativeFx/reverb/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMenu>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
using Parameters=ReverbWidget::Parameters;
const QColor background(11,26,42), panel(17,34,53), pale(216,230,252), muted(157,181,219), cyan(42,205,255), violet(194,114,247);
constexpr auto channels = WinJACKNexus::Common::surroundChannelChoices;
constexpr const char* scenes[]={"Room","Hall","Plate","Church","Studio","Spring","Custom"};
constexpr const char* presets[]={"Default","Small Room","Bright Plate","Cathedral","Studio Booth","Vintage Spring","Custom"};
constexpr int presetForScene(int scene) { return scene==0?1:scene==1?0:scene; }
void box(QPainter& p,QRectF r,qreal radius,QColor fill,QColor edge={}) {
    Controls::drawSurface(p,r,fill,edge,radius);
}
void text(QPainter& p,QRectF r,const QString& s,int size,QColor color,bool bold=false,int flags=Qt::AlignCenter) {
    QFont f(QStringLiteral("Microsoft YaHei UI"),size);f.setBold(bold);
    const auto themed = color == pale
        ? Controls::themeTokenColor("native-fx-reverb", "text-primary", color)
        : color == muted
            ? Controls::themeTokenColor("native-fx-reverb", "text-muted", color)
            : color == Qt::white
                ? Controls::themeTokenColor("native-fx-reverb", "text-heading", color) : color;
    Controls::drawText(p,r,s,f,themed,Qt::Alignment(flags));
}
void line(QPainter& p,QPointF a,QPointF b,QColor color,qreal width=1) {p.setPen(QPen(color,width));p.setBrush(Qt::NoBrush);p.drawLine(a,b);}
float value(const Parameters& p,int i) {
    switch(i) {
    case 0:return p.decay;case 1:return p.size;case 2:return p.preDelayMs;case 3:return p.diffusion;
    case 4:return p.earlyDb;case 5:return p.earlyTimeMs;case 6:return p.highFreqHz;case 7:return p.highDampDb;
    case 8:return p.lowFreqHz;case 9:return p.lowDampDb;case 10:return p.wet;case 11:return p.dryDb;
    case 12:return p.width;default:return p.outputDb;
    }
}
void setValue(Parameters& p,int i,float v) {
    switch(i) {
    case 0:p.decay=v;break;case 1:p.size=v;break;case 2:p.preDelayMs=v;break;case 3:p.diffusion=v;break;
    case 4:p.earlyDb=v;break;case 5:p.earlyTimeMs=v;break;case 6:p.highFreqHz=v;break;case 7:p.highDampDb=v;break;
    case 8:p.lowFreqHz=v;break;case 9:p.lowDampDb=v;break;case 10:p.wet=v;break;case 11:p.dryDb=v;break;
    case 12:p.width=v;break;default:p.outputDb=v;break;
    }
}
Parameters presetParameters(int index) {
    static const Parameters choices[]={
        Parameters {},
        {.scene=0,.decay=.7F,.size=28,.preDelayMs=8,.diffusion=65,.earlyDb=-3,.earlyTimeMs=30,.highFreqHz=7000,.highDampDb=-4,.lowFreqHz=180,.lowDampDb=-2,.wet=24},
        {.scene=2,.decay=1.6F,.size=55,.preDelayMs=12,.diffusion=90,.earlyDb=-4,.earlyTimeMs=45,.highFreqHz=9000,.highDampDb=-2,.lowFreqHz=250,.lowDampDb=-5,.wet=32},
        {.scene=3,.decay=5.8F,.size=90,.preDelayMs=38,.diffusion=94,.earlyDb=-9,.earlyTimeMs=112,.highFreqHz=3200,.highDampDb=-9,.lowFreqHz=160,.lowDampDb=-4,.wet=36},
        {.scene=4,.decay=.45F,.size=20,.preDelayMs=4,.diffusion=74,.earlyDb=-2,.earlyTimeMs=22,.highFreqHz=7500,.highDampDb=-5,.lowFreqHz=220,.lowDampDb=-2,.wet=18},
        {.scene=5,.decay=1.9F,.size=35,.preDelayMs=10,.diffusion=48,.earlyDb=-7,.earlyTimeMs=55,.highFreqHz=5400,.highDampDb=-8,.lowFreqHz=270,.lowDampDb=-4,.wet=27},
        {.scene=6,.decay=2.4F,.size=60,.preDelayMs=20,.diffusion=100,.earlyDb=-6,.earlyTimeMs=80,.highFreqHz=4000,.highDampDb=-6,.lowFreqHz=200,.lowDampDb=-3,.wet=30}
    };
    return choices[std::clamp(index,0,6)];
}
bool matchesPreset(const Parameters& parameters,const Parameters& preset) {
    if(parameters.scene!=preset.scene || parameters.enabled!=preset.enabled) return false;
    for(int field=0;field<14;++field) if(value(parameters,field)!=value(preset,field)) return false;
    return true;
}
constexpr float minimum[]={.1F,0,0,0,-60,1,500,-24,20,-24,0,-60,0,-24};
constexpr float maximum[]={15,100,150,100,6,150,16000,0,2000,0,100,6,200,12};
QRect numericReadout(int control) {
    if(control<4) {
        constexpr int x[]={185,280,380,478};
        return {x[control]-47,462,94,22};
    }
    if(control<10) {
        const int row=(control-4)/2;
        const int x=(control%2)?688:596;
        return {x-43,295+row*105,86,19};
    }
    constexpr QRect bottom[]={{141,573,48,29},{253,573,57,29},
                              {449,573,64,29},{681,573,61,29}};
    return bottom[control-10];
}
Controls::NumericControlSpec numericSpec(int control) {
    const Parameters defaults;
    const int decimals=(control==0 || control==4 || control==7 || control==9
                        || control==11 || control==13)?1:0;
    return {minimum[control],maximum[control],value(defaults,control),decimals};
}
QString shown(const Parameters& p,int i) {
    const float v=value(p,i);
    if(i==0)return QStringLiteral("%1 s").arg(v,0,'f',1);
    if(i==1||i==3||i==10||i==12)return QStringLiteral("%1 %").arg(v,0,'f',0);
    if(i==2||i==5)return QStringLiteral("%1 ms").arg(v,0,'f',0);
    if(i==6)return QStringLiteral("%1 kHz").arg(v/1000,0,'f',1);
    if(i==8)return QStringLiteral("%1 Hz").arg(v,0,'f',0);
    return QStringLiteral("%1 dB").arg(v,0,'f',1);
}
void sceneImage(QPainter& p,const QRectF r,const Parameters& v) {
    QLinearGradient backdrop(r.topLeft(),r.bottomLeft());backdrop.setColorAt(0,QColor(13,18,39));backdrop.setColorAt(.65,QColor(35,36,65));backdrop.setColorAt(1,QColor(11,23,38));
    box(p,r,7,QColor(11,22,37),QColor(72,94,123));p.save();p.setClipRect(r.adjusted(2,2,-2,-2));p.fillRect(r.adjusted(2,2,-2,-2),backdrop);
    const float cx=float(r.center().x()),cy=float(r.center().y());
    const float span=52+v.size*.9F,depth=32+v.decay*3.2F;
    const auto sceneLight=Controls::themeTokenColor("native-fx-reverb", "scene-shape-light", QColor(171,162,220));
    const QColor edge(135,139,191,155),face(52,53,83,155),
        light(sceneLight.red(),sceneLight.green(),sceneLight.blue(),150);
    if(v.scene==6){ // Custom: the cylinder floor area follows Size; height follows Decay.
        const float rx=25+v.size*1.05F,ry=10+v.size*.34F;
        const float h=std::min(28+v.decay*4.0F,123-2*ry);
        const float top=r.y()+6+(123-h-2*ry)*.5F+ry;
        p.setPen(QPen(edge,1.3));p.setBrush(face);p.drawRect(QRectF(cx-rx,top,2*rx,h));
        p.setBrush(QColor(87,77,126,190));p.drawEllipse(QRectF(cx-rx,top-ry,2*rx,2*ry));
        p.setBrush(QColor(35,46,76,210));p.drawArc(QRectF(cx-rx,top+h-ry,2*rx,2*ry),180*16,180*16);
    } else if(v.scene==2){ // Plate: a thin rectangular sheet.
        QPolygonF shape{{cx-span,cy+32},{cx+span,cy+32},{cx+span*.8F,cy-28},{cx-span*.8F,cy-28}};
        p.setPen(QPen(light,1.5));p.setBrush(QColor(68,70,111,190));p.drawPolygon(shape);
        for(int n=-2;n<=2;++n)line(p,{cx+n*span*.32F,cy+27},{cx+n*span*.26F,cy-25},edge);
    } else if(v.scene==5){ // Spring: a wire helix inside a box.
        box(p,{cx-span,cy-35,2*span,75},4,QColor(34,41,69,185),edge);
        QPainterPath spring;spring.moveTo(cx-span+8,cy);
        for(int k=0;k<90;++k){const float x=cx-span+8+k*(2*span-16)/89;spring.lineTo(x,cy+std::sin(k*.95F)*15);}
        p.setBrush(Qt::NoBrush);p.setPen(QPen(light,2));p.drawPath(spring);
    } else { // Room, Hall, Church and Studio use distinct box proportions.
        const float tall=v.scene==3?depth+40:v.scene==1?depth+15:v.scene==4?depth-15:depth;
        const float wide=v.scene==1?span+26:v.scene==4?span-25:span;
        QPolygonF floor{{cx-wide,cy+47},{cx+wide,cy+47},{cx+wide*.5F,cy+10},{cx-wide*.5F,cy+10}};
        p.setPen(QPen(edge,1));p.setBrush(QColor(65,57,85,140));p.drawPolygon(floor);
        QPolygonF back{{cx-wide*.5F,cy+10},{cx+wide*.5F,cy+10},{cx+wide*.5F,cy-tall},{cx-wide*.5F,cy-tall}};
        p.setBrush(face);p.drawPolygon(back);
        line(p,{cx-wide,cy+47},{cx-wide*.5F,cy-tall},edge);line(p,{cx+wide,cy+47},{cx+wide*.5F,cy-tall},edge);
        line(p,{cx-wide*.5F,cy-tall},{cx+wide*.5F,cy-tall},light);
        if(v.scene==3){
            QPainterPath arch;arch.moveTo(cx-35,cy+10);arch.lineTo(cx-35,cy-25);arch.arcTo(cx-35,cy-50,70,50,180,-180);arch.lineTo(cx+35,cy+10);
            p.setPen(QPen(light,2));p.setBrush(QColor(105,100,149,100));p.drawPath(arch);
        } else {
            for(int n=-2;n<=2;++n){float x=cx+n*wide*.16F;line(p,{x,cy-tall},{x,cy+10},QColor(137,133,177,80));}
        }
        if(v.scene==4){for(int n=0;n<7;++n)line(p,{cx-wide+n*wide/3,cy+47},{cx-wide*.5F+n*wide/6,cy+10},QColor(122,135,177,75));}
    }
    // A restrained light pool and perspective floor grid recall the reference.
    const auto sceneGlow=Controls::themeTokenColor("native-fx-reverb", "scene-glow", QColor(169,148,242));
    QRadialGradient glow(cx,cy+35,100);
    glow.setColorAt(0,QColor(sceneGlow.red(),sceneGlow.green(),sceneGlow.blue(),70));
    glow.setColorAt(1,QColor(sceneGlow.red(),sceneGlow.green(),sceneGlow.blue(),0));
    p.setPen(Qt::NoPen);p.setBrush(glow);p.drawEllipse(QPointF(cx,cy+35),110,48);
    p.restore();
}
}

ReverbWidget::ReverbWidget(QWidget* parent):QWidget(parent) {setFixedSize(770,645);setWindowTitle(QStringLiteral("Reverb 混响"));setMouseTracking(true);dragRefreshTimer_.setParent(this);dragRefreshTimer_.setObjectName(QStringLiteral("nativeFxDragRefreshTimer"));bindRefreshTimer(dragRefreshTimer_,this);connect(&dragRefreshTimer_,&QTimer::timeout,this,[this]{update();});}
ReverbWidget::Parameters ReverbWidget::displayedParameters() const noexcept {
    if(channel_>=0) return values_[static_cast<std::size_t>(channel_)];
    auto displayed=values_[0];
    for(int field=0;field<14;++field) {
        float sum=0;
        for(const auto& channel:values_) sum+=value(channel,field);
        setValue(displayed,field,sum/8.0F);
    }
    displayed.enabled=std::all_of(values_.begin(),values_.end(),
        [](const Parameters& channel){return channel.enabled;});
    return displayed;
}
void ReverbWidget::setParameters(const std::array<Parameters,8>& parameters){values_=parameters;refreshPresetSelection();if(dragged_<0)update();}
void ReverbWidget::setChangeHandler(ChangeHandler handler){handler_=std::move(handler);}
void ReverbWidget::edit(const std::function<void(Parameters&)>& change){
    const auto before=displayedParameters();auto target=before;change(target);
    for(int i=channel_<0?0:channel_;i<(channel_<0?8:channel_+1);++i){
        auto& dst=values_[i];
        if(target.scene!=before.scene)dst.scene=target.scene;
        if(target.enabled!=before.enabled)dst.enabled=target.enabled;
        for(int field=0;field<14;++field)if(value(target,field)!=value(before,field))setValue(dst,field,value(target,field));
        if(handler_)handler_(i,dst);
    }
    refreshPresetSelection();if(dragged_<0)update();
}
void ReverbWidget::refreshPresetSelection() {
    const int first=channel_<0?0:channel_,last=channel_<0?8:channel_+1;
    for(int index=0;index<7;++index) {
        const auto preset=presetParameters(index);
        bool matches=true;
        for(int channel=first;channel<last;++channel)
            matches &= matchesPreset(values_[static_cast<std::size_t>(channel)],preset);
        if(matches) {preset_=index;return;}
    }
    preset_=6;
}
void ReverbWidget::choosePreset(int index){
    index=std::clamp(index,0,6);
    const auto preset=presetParameters(index);
    for(int i=channel_<0?0:channel_;i<(channel_<0?8:channel_+1);++i){values_[i]=preset;if(handler_)handler_(i,values_[i]);}
    refreshPresetSelection();update();
}
int ReverbWidget::controlAt(const QPoint& pos) const {
    const QPoint centers[]={{185,433},{280,433},{380,433},{478,433},{596,276},{688,276},{596,378},{688,378},{596,480},{688,480},{110,585},{224,585},{420,585},{652,585}};
    for(int i=0;i<14;++i)if((pos-centers[i]).manhattanLength()<43)return i;
    return -1;
}
void ReverbWidget::adjust(int control,float steps){
    if(control<0)return;const auto v=displayedParameters();
    float increment=(maximum[control]-minimum[control])/100;
    if(control==0)increment=.1F;if(control==6)increment=100;if(control==8)increment=20;
    const float target=std::clamp(value(v,control)+steps*increment,minimum[control],maximum[control]);
    edit([&](Parameters& p){setValue(p,control,target);});
}
void ReverbWidget::paintEvent(QPaintEvent*){
    const auto themed=[](const char* role,const QColor& fallback){
        return Controls::themeTokenColor(QStringLiteral("native-fx-reverb"),
                                         QString::fromLatin1(role),fallback);
    };
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(),Controls::themeColor(*this,"fxBackground",themed("surface-window",background)));
    const Controls::Palette violetColors=Controls::paletteFor(*this,
        {themed("control-knob-background-base",background),QColor(22,40,59),pale,
         themed("control-knob-violet-indicator-normal",violet),
         themed("control-knob-track",QColor(48,73,97))});
    const Controls::Palette cyanColors=Controls::paletteFor(*this,
        {themed("control-knob-background-base",background),QColor(22,40,59),pale,
         themed("control-knob-cyan-indicator-normal",cyan),
         themed("control-knob-track",QColor(48,73,97))});
    box(p,{1,1,768,135},9,QColor(20,38,57),QColor(45,73,99));
    text(p,{17,11,125,32},QStringLiteral("作用声道"),18,Qt::white,true,Qt::AlignLeft|Qt::AlignVCenter);
    for(int i=0;i<9;++i){const int x=17+i*83;const bool chosen=i==channel_+1;
        box(p,{qreal(x),68,77,40},6,chosen?QColor(13,66,102):QColor(24,43,63),chosen?cyan:QColor(58,84,110));
        text(p,{qreal(x),68,77,40},QString::fromLatin1(channels[i]),15,chosen?cyan:muted);
    }
    p.save();p.translate(0,143);const auto& v=displayedParameters();
    QLinearGradient mainGrad(0,0,770,502);mainGrad.setColorAt(0,QColor(28,32,77));mainGrad.setColorAt(.53,QColor(16,33,55));mainGrad.setColorAt(1,QColor(8,28,43));
    box(p,{8,8,748,484},11,themed("surface-panel",QColor(17,32,52)),QColor(92,75,173));
    Controls::drawSurface(p,QRectF(9,9,746,70),mainGrad,{},9);
    box(p,{20,18,55,55},7,QColor(83,57,166),QColor(125,103,229));
    QPolygonF cube{{47,26},{63,35},{63,56},{47,65},{31,56},{31,35}};p.setPen(QPen(Qt::white,2));p.setBrush(Qt::NoBrush);p.drawPolygon(cube);line(p,{31,35},{47,45},Qt::white,2);line(p,{63,35},{47,45},Qt::white,2);line(p,{47,45},{47,65},Qt::white,2);
    text(p,{94,17,245,33},QStringLiteral("Reverb 混响"),21,Qt::white,true,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,{94,50,265,24},QStringLiteral("模拟真实空间，增强声音的空间感"),13,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    Controls::drawToggle(p,{362,35,37,20},v.enabled,
        themed("control-toggle-indicator-on",QColor(14,126,227)),
        themed("control-toggle-indicator-off",QColor(52,68,83)));
    text(p,{424,28,43,34},QStringLiteral("预设:"),12,muted);
    box(p,{474,29,148,34},6,QColor(18,35,56),QColor(66,91,124));
    text(p,{485,29,111,34},QString::fromLatin1(presets[preset_]),12,pale);
    text(p,{598,29,16,34},QStringLiteral("⌄"),14,muted);
    for(int n=0;n<3;++n){const int x=634+n*38;box(p,{qreal(x),29,31,34},5,QColor(27,47,73),QColor(63,89,124));text(p,{qreal(x),29,31,34},n==2?QStringLiteral("•••"):(n==0?QStringLiteral("‹"):QStringLiteral("›")),18,muted);}
    box(p,{20,87,516,314},8,QColor(13,30,48),QColor(42,67,92));
    text(p,{31,90,90,23},QStringLiteral("混响类型:"),12,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    for(int i=0;i<7;++i){const int y=119+i*39;const bool selected=i==v.scene;
        box(p,{31,qreal(y),91,35},5,selected?QColor(13,67,101):QColor(25,46,68),
            selected?themed("scene-accent-cyan",cyan):QColor(62,91,124));
        text(p,{31,qreal(y),91,35},QString::fromLatin1(scenes[i]),12,pale);
    }
    sceneImage(p,{142,93,383,156},v);
    QLinearGradient captionShade(0,217,0,249);
    captionShade.setColorAt(0,QColor(10,21,36,0));captionShade.setColorAt(1,QColor(10,21,36,230));
    p.fillRect(QRectF(143,217,381,31),captionShade);
    text(p,{145,221,24,28},QStringLiteral("‹"),24,Qt::white);text(p,{496,221,24,28},QStringLiteral("›"),24,Qt::white);
    const QString sceneCaption=v.scene==6
        ?QStringLiteral("Custom · %1 m²").arg(int(3.14159F*std::pow(1+v.size*.12F,2)))
        :QString::fromLatin1(presets[preset_]);
    text(p,{222,222,221,22},sceneCaption,11,Qt::white);
    const QPoint top[]={{185,291},{280,291},{380,291},{478,291}};
    const QString topNames[]={QStringLiteral("混响时间"),QStringLiteral("空间大小"),QStringLiteral("预延迟"),QStringLiteral("扩散")};
    const QString english[]={QStringLiteral("(Decay)"),QStringLiteral("(Size)"),QStringLiteral("(Pre-Delay)"),QStringLiteral("(Diffusion)")};
    for(int i=0;i<4;++i){const auto c=top[i];Controls::drawKnob(p,c,(value(v,i)-minimum[i])/(maximum[i]-minimum[i]),26,violetColors);
        text(p,{qreal(c.x()-47),319,94,22},shown(v,i),12,Qt::white);
        text(p,{qreal(c.x()-47),341,94,21},topNames[i],12,pale);text(p,{qreal(c.x()-49),363,98,19},english[i],10,muted);
    }
    const QString groups[]={QStringLiteral("早期反射"),QStringLiteral("高频阻尼"),QStringLiteral("低频阻尼")};
    const QString labels[]={QStringLiteral("电平"),QStringLiteral("时间"),QStringLiteral("截止频率"),QStringLiteral("衰减"),QStringLiteral("截止频率"),QStringLiteral("衰减")};
    for(int row=0;row<3;++row){const int y=87+row*105;box(p,{546,qreal(y),198,100},8,QColor(19,38,58),QColor(43,70,97));
        text(p,{555,qreal(y+2),87,19},groups[row],12,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
        for(int col=0;col<2;++col){const int i=4+row*2+col,x=col?688:596;Controls::drawKnob(p,{qreal(x),qreal(y+44)},(value(v,i)-minimum[i])/(maximum[i]-minimum[i]),21,row==2?cyanColors:violetColors);
            text(p,{qreal(x-43),qreal(y+65),86,19},shown(v,i),11,Qt::white);
            text(p,{qreal(x-43),qreal(y+82),86,17},labels[row*2+col],10,muted);
        }
    }
    box(p,{20,409,498,73},7,QColor(19,38,58),QColor(43,70,97));
    box(p,{527,409,217,73},7,QColor(19,38,58),QColor(43,70,97));
    const QPoint bottom[]={{110,444},{224,444},{420,444},{652,444}};
    for(int n=0;n<4;++n){const int i=10+n;Controls::drawKnob(p,bottom[n],(value(v,i)-minimum[i])/(maximum[i]-minimum[i]),26,cyanColors);
    }
    text(p,{31,428,39,32},QStringLiteral("混合"),12,muted);
    text(p,{312,428,81,32},QStringLiteral("立体声宽度:"),9,muted);
    text(p,{538,428,78,32},QStringLiteral("输出电平:"),11,muted);
    text(p,{141,430,48,29},shown(v,10),12,pale,false,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,{253,430,57,29},shown(v,11),12,pale,false,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,{449,430,64,29},shown(v,12),12,pale,false,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,{681,430,61,29},shown(v,13),12,pale,false,Qt::AlignLeft|Qt::AlignVCenter);
    text(p,{141,462,48,18},QStringLiteral("Wet"),10,muted);
    text(p,{253,462,57,18},QStringLiteral("Dry"),10,muted);
    p.restore();
}
void ReverbWidget::mousePressEvent(QMouseEvent* e){
    const auto q=e->pos();if(q.y()>=68&&q.y()<108&&q.x()>=17&&q.x()<758){setSelectedChannel(std::clamp((q.x()-17)/83,0,8)-1);return;}
    for(int control=0;control<14;++control) {
        const auto readout=numericReadout(control);
        if(!readout.contains(q))continue;
        Controls::editNumericValue(this,readout,value(displayedParameters(),control),numericSpec(control),
            [this,control](double target){edit([control,target](Parameters& parameters){
                setValue(parameters,control,static_cast<float>(target));
            });});
        return;
    }
    const QPoint m=q-QPoint(0,143);
    if(QRect(362,35,37,20).contains(m)){edit([&](Parameters& v){v.enabled=!v.enabled;});return;}
    if(QRect(474,29,148,34).contains(m)||QRect(710,29,31,34).contains(m)){
        QMenu menu(this);menu.setStyleSheet(QStringLiteral("QMenu{background:#142b43;color:#d8e6fc;border:1px solid #49648c;}QMenu::item{padding:6px 22px;}QMenu::item:selected{background:#14547c;}"));
        for(int i=0;i<7;++i){auto* action=menu.addAction(QString::fromLatin1(presets[i]));connect(action,&QAction::triggered,this,[this,i]{choosePreset(i);});}
        menu.exec(mapToGlobal(QPoint(474,206)));return;
    }
    if(QRect(634,29,69,34).contains(m)){choosePreset((preset_+(m.x()<670?6:1))%7);return;}
    if(m.x()>=31&&m.x()<122&&m.y()>=119&&m.y()<388){const int scene=(m.y()-119)/39;if(scene<7){choosePreset(presetForScene(scene));return;}}
    if(QRect(142,93,383,156).contains(m)&&(m.x()<175||m.x()>492)){choosePreset(presetForScene((displayedParameters().scene+(m.x()<175?6:1))%7));return;}
    dragged_=controlAt(q);dragStart_=q;dragValue_=displayedParameters();if(dragged_>=0)dragRefreshTimer_.start();
}
void ReverbWidget::mouseMoveEvent(QMouseEvent* e){if(dragged_<0||!(e->buttons()&Qt::LeftButton))return;
    const int i=dragged_;const float increment=(maximum[i]-minimum[i])/100;
    const float target=std::clamp(value(dragValue_,i)+(dragStart_.y()-e->pos().y())*increment*.55F,minimum[i],maximum[i]);
    edit([&](Parameters& p){setValue(p,i,target);});
}
void ReverbWidget::mouseReleaseEvent(QMouseEvent*){dragged_=-1;dragRefreshTimer_.stop();update();}
void ReverbWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton)return;
    const int control=controlAt(event->pos());
    if(control<0)return;
    dragged_=-1;dragRefreshTimer_.stop();
    Controls::resetNumericValue(numericSpec(control),[this,control](double target){
        edit([control,target](Parameters& parameters){
            setValue(parameters,control,static_cast<float>(target));
        });
    });
}
void ReverbWidget::wheelEvent(QWheelEvent* e){const int i=controlAt(e->position().toPoint());if(i>=0){adjust(i,e->angleDelta().y()/120.0F);e->accept();}else QWidget::wheelEvent(e);}
}
