/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/compressor/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
const QColor ink(234,240,255), muted(168,191,228), cyan(33,190,255), amber(255,159,65);
const QColor meterColors[] = {QColor(80,210,255),QColor(255,115,130),QColor(120,230,150),QColor(244,205,96),
                              QColor(185,145,255),QColor(255,165,92),QColor(85,235,215),QColor(235,145,220)};
constexpr const char* channels[] = {"All","L","R","C","LFE","Ls","Rs","Lb","Rb"};
constexpr float limits[][2] = {{-60,0},{1,20},{0,24},{0.1F,200},{1,2000},{-12,24}};
constexpr QPointF centers[] = {{454,143},{569,143},{688,143},{454,276},{569,276},{688,276}};
constexpr QRectF plotRect {72,122,276,236};
constexpr QRectF gainReductionTrack {362,122,9,236};
constexpr float gainReductionRangeDb = 60.0F;
float get(const CompressorParameters& p,int i) {
    switch(i) {case 0:return p.thresholdDb;case 1:return p.ratio;case 2:return p.kneeDb;
    case 3:return p.attackMs;case 4:return p.releaseMs;default:return p.makeupDb;}
}
void put(CompressorParameters& p,int i,float v) {
    switch(i) {case 0:p.thresholdDb=v;break;case 1:p.ratio=v;break;case 2:p.kneeDb=v;break;
    case 3:p.attackMs=v;break;case 4:p.releaseMs=v;break;default:p.makeupDb=v;break;}
}
void box(QPainter& p,QRectF r,qreal radius,QBrush fill,QColor stroke={}) {
    p.setPen(stroke.isValid()?QPen(stroke,1):Qt::NoPen);p.setBrush(fill);p.drawRoundedRect(r,radius,radius);
}
void label(QPainter& p,QRectF r,const QString& text,int size,QColor color=ink,bool bold=false,int align=Qt::AlignCenter) {
    auto font=p.font();font.setPixelSize(size);font.setBold(bold);p.setFont(font);p.setPen(color);p.drawText(r,align,text);
}
void toggle(QPainter& p,QRectF r,bool on) {
    box(p,r,r.height()/2,on?QColor(8,114,206):QColor(28,46,68),on?QColor(24,155,245):QColor(75,108,142));
    p.setBrush(Qt::white);p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(on?r.right()-r.height()/2:r.left()+r.height()/2,r.center().y()),r.height()*0.38,r.height()*0.38);
}
QPointF graph(float x,float y) {return {plotRect.left()+(x+60)*plotRect.width()/60,plotRect.bottom()-(y+60)*plotRect.height()/60};}
}

CompressorWidget::CompressorWidget(QWidget* parent):QWidget(parent) {
    setFixedSize(770,665);if (!parent) setWindowFlags(Qt::Window);setWindowTitle(QStringLiteral("Compressor 压缩器"));
    meterTimer_.setInterval(33);
    connect(&meterTimer_, &QTimer::timeout, this, &CompressorWidget::pollMeters);
}
void CompressorWidget::setParameters(const std::array<CompressorParameters,8>& values) {parameters_=values;update();}
void CompressorWidget::setChangeHandler(ChangeHandler handler) {changeHandler_=std::move(handler);}
void CompressorWidget::setMeterReader(MeterReader reader) {
    meterReader_=std::move(reader);
    if(meterReader_) {pollMeters();meterTimer_.start();}
    else meterTimer_.stop();
}
void CompressorWidget::setMeters(const std::array<CompressorMeter,8>& meters) {meters_=meters;update();}
void CompressorWidget::pollMeters() {if(meterReader_)setMeters(meterReader_());}
CompressorParameters CompressorWidget::displayed() const noexcept {
    if(selectedChannel_>=0)return parameters_[static_cast<std::size_t>(selectedChannel_)];
    CompressorParameters result {};
    result.thresholdDb=0.0F;result.ratio=0.0F;result.kneeDb=0.0F;
    result.attackMs=0.0F;result.releaseMs=0.0F;result.makeupDb=0.0F;
    result.highPassHz=0.0F;result.lowPassHz=0.0F;
    result.enabled=true;result.rms=true;result.sidechainFilter=true;result.autoMakeup=true;
    for(const auto& parameters:parameters_) {
        result.thresholdDb+=parameters.thresholdDb/8.0F;
        result.ratio+=parameters.ratio/8.0F;
        result.kneeDb+=parameters.kneeDb/8.0F;
        result.attackMs+=parameters.attackMs/8.0F;
        result.releaseMs+=parameters.releaseMs/8.0F;
        result.makeupDb+=parameters.makeupDb/8.0F;
        result.highPassHz+=parameters.highPassHz/8.0F;
        result.lowPassHz+=parameters.lowPassHz/8.0F;
        result.enabled=result.enabled&&parameters.enabled;
        result.rms=result.rms&&parameters.rms;
        result.sidechainFilter=result.sidechainFilter&&parameters.sidechainFilter;
        result.autoMakeup=result.autoMakeup&&parameters.autoMakeup;
    }
    return result;
}
void CompressorWidget::apply(const std::function<void(CompressorParameters&)>& edit) {
    selectedPreset_=-1;
    const int first=selectedChannel_<0?0:selectedChannel_;
    const int last=selectedChannel_<0?8:selectedChannel_+1;
    for(int channel=first;channel<last;++channel) {
        edit(parameters_[static_cast<std::size_t>(channel)]);
        if(changeHandler_)changeHandler_(static_cast<std::size_t>(channel),parameters_[static_cast<std::size_t>(channel)]);
    }
    update();
}
void CompressorWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(),Controls::themeColor(*this,"fxBackground",QColor(13,28,45)));
    box(p,{10,5,748,126},9,QColor(22,40,60),QColor(47,75,103));
    label(p,{25,24,170,28},QStringLiteral("作用声道"),19,ink,true,Qt::AlignVCenter|Qt::AlignLeft);
    for(int i=0;i<9;++i) {
        const QRectF r(26+i*72,62,64,40);const bool chosen=i==0?selectedChannel_<0:selectedChannel_==i-1;
        box(p,r,5,chosen?QColor(12,58,88):QColor(24,43,63),chosen?cyan:QColor(64,91,120));
        label(p,r,QString::fromLatin1(channels[i]),16,chosen?QColor(126,220,255):muted);
    }
    p.save();p.translate(0,145);
    QLinearGradient background(0,0,770,515);background.setColorAt(0,QColor(17,36,55));background.setColorAt(1,QColor(11,29,47));
    box(p,{10,14,748,483},11,background,QColor(33,103,150));
    box(p,{11,15,746,75},10,QColor(23,43,64));
    QLinearGradient icon(21,24,78,82);icon.setColorAt(0,QColor(139,78,31));icon.setColorAt(1,QColor(89,48,21));
    box(p,{21,24,57,58},7,icon,amber);
    p.setPen(QPen(QColor(255,193,118),3));QPainterPath curve;curve.moveTo(35,69);curve.cubicTo(35,49,50,38,66,38);p.drawPath(curve);
    p.setPen(Qt::NoPen);p.setBrush(QColor(255,193,118));p.drawEllipse(QPointF(66,38),2.5,2.5);
    label(p,{97,24,233,34},QStringLiteral("Compressor 压缩器"),21,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{98,59,233,23},QStringLiteral("控制动态范围，使电平更稳定"),15,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    toggle(p,{346,43,34,21},displayed().enabled);
    label(p,{413,39,44,28},QStringLiteral("预设:"),14,muted);
    box(p,{459,37,168,32},5,QColor(19,36,56),QColor(61,94,130));
    label(p,{469,37,137,32},selectedPreset_==0?QStringLiteral("Vocal Smooth"):selectedPreset_==1?QStringLiteral("Punchy Drums"):selectedPreset_==2?QStringLiteral("Gentle Level"):QStringLiteral("Custom"),15,ink,false,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{601,37,23,32},QStringLiteral("⌄"),19,muted);
    for(int i=0;i<2;++i) {const int x=638+i*37;box(p,{qreal(x),37,30,32},5,QColor(27,51,77),QColor(63,101,141));label(p,{qreal(x),37,30,32},i?QStringLiteral("›"):QStringLiteral("‹"),25,muted);}
    box(p,{713,37,30,32},5,QColor(27,51,77),QColor(63,101,141));label(p,{713,37,30,32},QStringLiteral("•••"),13,muted);
    box(p,{21,99,371,316},10,QColor(20,39,60),QColor(45,73,100));
    box(p,{399,99,345,316},10,QColor(20,39,60),QColor(45,73,100));
    const auto s=displayed();
    double inputPower=0.0;
    std::size_t activeChannels=0;
    const int firstChannel=selectedChannel_<0?0:selectedChannel_;
    const int lastChannel=selectedChannel_<0?static_cast<int>(meters_.size()):selectedChannel_+1;
    for(int channel=firstChannel;channel<lastChannel;++channel) {
        const auto& meter=meters_[static_cast<std::size_t>(channel)];
        if(!meter.active)continue;
        inputPower+=std::pow(10.0,static_cast<double>(meter.inputDb)/10.0);
        ++activeChannels;
    }
    const float aggregateInputDb=activeChannels==0?-120.0F:
        static_cast<float>(10.0*std::log10(inputPower/static_cast<double>(activeChannels)));
    const auto transferOutput=[&](const CompressorParameters& parameters,float input) {
        const float delta=input-parameters.thresholdDb;
        float output=input;
        if(parameters.enabled) {
            if(parameters.ratio>1.0F) {
                if(parameters.kneeDb>0.0F && std::abs(delta)<parameters.kneeDb*0.5F) {
                    const float t=delta+parameters.kneeDb*0.5F;
                    output=input+(1.0F/parameters.ratio-1.0F)*t*t/(2.0F*parameters.kneeDb);
                } else if(delta>=parameters.kneeDb*0.5F) {
                    output=parameters.thresholdDb+delta/parameters.ratio;
                }
            }
            output+=parameters.autoMakeup
                ? std::clamp(-parameters.thresholdDb*(1.0F-1.0F/parameters.ratio),-12.0F,24.0F)
                : parameters.makeupDb;
        }
        return output;
    };
    const auto output=[&](float input) {
        if(selectedChannel_>=0)
            return transferOutput(parameters_[static_cast<std::size_t>(selectedChannel_)],input);

        double outputPower=0.0;
        std::size_t channelCount=0;
        for(std::size_t channel=0;channel<parameters_.size();++channel) {
            if(activeChannels>0 && !meters_[channel].active)continue;
            const float outputDb=transferOutput(parameters_[channel],input);
            outputPower+=std::pow(10.0,static_cast<double>(outputDb)/10.0);
            ++channelCount;
        }
        return channelCount==0?input:static_cast<float>(
            10.0*std::log10(outputPower/static_cast<double>(channelCount)));
    };
    p.save();p.setClipRect(plotRect);p.fillRect(plotRect,QColor(13,29,46));
    p.setPen(QPen(QColor(61,82,101,65),0.5));
    for(int i=0;i<=20;++i) {
        const qreal x=plotRect.left()+plotRect.width()*i/20;
        const qreal y=plotRect.top()+plotRect.height()*i/20;
        p.drawLine(QPointF(x,plotRect.top()),QPointF(x,plotRect.bottom()));
        p.drawLine(QPointF(plotRect.left(),y),QPointF(plotRect.right(),y));
    }
    p.setPen(QPen(QColor(66,91,114,150),1));
    for(int i=0;i<=5;++i) {
        const qreal x=plotRect.left()+plotRect.width()*i/5;
        const qreal y=plotRect.top()+plotRect.height()*i/5;
        p.drawLine(QPointF(x,plotRect.top()),QPointF(x,plotRect.bottom()));
        p.drawLine(QPointF(plotRect.left(),y),QPointF(plotRect.right(),y));
    }
    QPainterPath line;
    for(int i=0;i<=120;++i) {
        const float x=-60+i*0.5F;const QPointF q=graph(x,output(x));
        if(i==0)line.moveTo(q);else line.lineTo(q);
    }
    if(activeChannels>0 && aggregateInputDb>-60.0F) {
        const float end=std::clamp(aggregateInputDb,-60.0F,0.0F);
        QPainterPath fill;
        for(int i=0;i<=120;++i) {
            const float x=-60.0F+(end+60.0F)*static_cast<float>(i)/120.0F;
            const QPointF q=graph(x,output(x));
            if(i==0)fill.moveTo(q);else fill.lineTo(q);
        }
        fill.lineTo(graph(end,-60.0F));
        fill.lineTo(graph(-60.0F,-60.0F));
        fill.closeSubpath();
        p.setPen(Qt::NoPen);p.setBrush(QColor(198,111,35,72));p.drawPath(fill);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(amber,1.6));p.drawPath(line);
    const auto point=graph(s.thresholdDb,output(s.thresholdDb));
    p.setPen(QPen(amber,1,Qt::DashLine));p.drawLine(QPointF(point.x(),plotRect.bottom()),point);
    p.setPen(QPen(QColor(255,194,123),2));p.setBrush(amber);p.drawEllipse(point,4.5,4.5);
    p.restore();
    for(int i=0;i<=5;++i) {
        const int db=-60+i*12;
        const qreal x=plotRect.left()+plotRect.width()*i/5;
        const qreal y=plotRect.bottom()-plotRect.height()*i/5;
        label(p,{48.0,y-8,22.0,18.0},QString::number(db),13,muted);
        const qreal tickLabelX=i==0?x:i==5?x-20:x-15;
        const qreal tickLabelWidth=i==0||i==5?20:30;
        const int tickLabelAlign=i==0?Qt::AlignLeft|Qt::AlignVCenter:
                                 i==5?Qt::AlignRight|Qt::AlignVCenter:Qt::AlignCenter;
        label(p,{tickLabelX,plotRect.bottom()+4,tickLabelWidth,21},QString::number(db),13,muted,false,tickLabelAlign);
    }
    float gainReductionDb=0.0F;
    std::size_t gainReductionChannels=0;
    if(selectedChannel_>=0) {
        const auto& meter=meters_[static_cast<std::size_t>(selectedChannel_)];
        if(meter.active)gainReductionDb=std::max(0.0F,meter.gainReductionDb);
    } else {
        for(const auto& meter:meters_) {
            if(!meter.active)continue;
            gainReductionDb+=std::max(0.0F,meter.gainReductionDb);
            ++gainReductionChannels;
        }
        if(gainReductionChannels>0)gainReductionDb/=static_cast<float>(gainReductionChannels);
    }
    gainReductionDb=std::clamp(gainReductionDb,0.0F,gainReductionRangeDb);
    label(p,{357,99,32,20},QStringLiteral("GR"),12,QColor(255,112,122),true);
    p.fillRect(gainReductionTrack,QColor(36,44,57));
    p.setPen(QPen(QColor(88,101,119),1));p.setBrush(Qt::NoBrush);p.drawRect(gainReductionTrack);
    const qreal reductionHeight=gainReductionTrack.height()*gainReductionDb/gainReductionRangeDb;
    if(reductionHeight>0.0)
        p.fillRect(QRectF(gainReductionTrack.left(),gainReductionTrack.top(),gainReductionTrack.width(),reductionHeight),
                   QColor(239,55,72));
    for(int tick=0;tick<=5;++tick) {
        const qreal y=gainReductionTrack.top()+gainReductionTrack.height()*tick/5.0;
        label(p,{373,y-8,18,16},QString::number(-tick*12),10,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    }
    p.save();p.translate(12,308);p.rotate(-90);label(p,{0,0,100,25},QStringLiteral("输出电平 (dB)"),15,muted);p.restore();
    label(p,{157,388,148,25},QStringLiteral("输入电平 (dB)"),15,muted);
    box(p,{234,120,82,48},8,QColor(33,51,72),QColor(88,113,144));
    label(p,{239,122,73,22},QStringLiteral("Threshold"),13,ink);
    label(p,{239,143,73,21},QStringLiteral("%1 dB").arg(s.thresholdDb,0,'f',1),14,ink);
    const QString names[]={QStringLiteral("阈值"),QStringLiteral("比率"),QStringLiteral("拐点 (Soft Knee)"),QStringLiteral("起始时间\n(Attack)"),QStringLiteral("释放时间\n(Release)"),QStringLiteral("输出增益\n(Makeup)")};
    for(int i=0;i<6;++i) {
        const QPointF c=centers[i];const float v=get(s,i);
        const float norm=(v-limits[i][0])/(limits[i][1]-limits[i][0]);
        p.setPen(QPen(QColor(6,16,27),6));p.setBrush(QColor(30,52,73));p.drawEllipse(c,30,30);
        p.setPen(QPen(QColor(15,24,36),2));p.setBrush(Qt::NoBrush);p.drawEllipse(c,33,33);
        p.setPen(QPen(QColor(74,88,101),3));p.drawArc(QRectF(c.x()-31,c.y()-31,62,62),-45*16,270*16);
        p.setPen(QPen(amber,4));p.drawArc(QRectF(c.x()-31,c.y()-31,62,62),(225-270*norm)*16,270*norm*16);
        const double angle=(225-270*norm)*3.141592653589793/180.0;
        p.setPen(QPen(amber,1.7));p.drawLine(c,QPointF(c.x()+20*std::cos(angle),c.y()-20*std::sin(angle)));
        QString valueText=i==1?QStringLiteral("%1:1").arg(v,0,'f',1):i==3||i==4?QStringLiteral("%1 ms").arg(v,0,'f',0):QStringLiteral("%1 dB").arg(v,0,'f',1);
        label(p,{c.x()-51,c.y()+37,102,23},valueText,15,ink);
        label(p,{c.x()-58,c.y()+59,116,43},names[i],14,muted);
    }
    box(p,{22,423,187,67},8,QColor(20,39,60),QColor(47,75,104));
    label(p,{36,427,97,22},QStringLiteral("检测模式:"),14,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    box(p,{36,452,80,29},4,s.rms?QColor(26,48,71):QColor(15,65,101),s.rms?QColor(61,90,123):cyan);
    box(p,{117,452,77,29},4,s.rms?QColor(15,65,101):QColor(26,48,71),s.rms?cyan:QColor(61,90,123));
    label(p,{36,452,80,29},QStringLiteral("峰值"),14,ink);label(p,{117,452,77,29},QStringLiteral("RMS"),14,ink);
    box(p,{219,423,403,67},8,QColor(20,39,60),QColor(47,75,104));
    label(p,{232,440,67,31},QStringLiteral("侧链滤波"),14,muted);toggle(p,{304,445,32,21},s.sidechainFilter);
    label(p,{352,440,32,31},QStringLiteral("高通"),14,muted);
    box(p,{389,440,81,31},5,QColor(17,34,54),QColor(61,91,125));label(p,{392,440,70,31},QStringLiteral("%1 Hz").arg(s.highPassHz,0,'f',0),14,ink);
    label(p,{485,440,32,31},QStringLiteral("低通"),14,muted);
    box(p,{522,440,86,31},5,QColor(17,34,54),QColor(61,91,125));label(p,{524,440,79,31},QStringLiteral("%1 kHz").arg(s.lowPassHz/1000,0,'f',1),14,ink);
    box(p,{629,423,116,67},8,QColor(20,39,60),QColor(47,75,104));
    label(p,{640,440,57,31},QStringLiteral("自动增益"),14,muted);toggle(p,{702,445,32,21},s.autoMakeup);
    p.restore();
}

int CompressorWidget::dialAt(QPointF point) const noexcept {point-=QPointF(0,145);for(int i=0;i<6;++i)if(QLineF(point,centers[i]).length()<35)return i;return -1;}
void CompressorWidget::changeDial(int index,float amount) {const float increments[]={0.5F,0.1F,0.5F,1,5,0.5F};const float v=std::clamp(get(displayed(),index)+amount*increments[index],limits[index][0],limits[index][1]);apply([=](auto& p){put(p,index,v);});}
void CompressorWidget::applyPreset(int index) {
    const int preset=(index%3+3)%3;apply([preset](auto& p){p=CompressorParameters{};
        if(preset==1){p.thresholdDb=-12;p.ratio=4;p.attackMs=3;p.releaseMs=80;}
        if(preset==2){p.thresholdDb=-24;p.ratio=2;p.attackMs=20;p.releaseMs=180;}
    });selectedPreset_=preset;update();
}
void CompressorWidget::mousePressEvent(QMouseEvent* e) {
    const QPointF point=e->position();
    if(point.y()<132)for(int i=0;i<9;++i)if(QRectF(26+i*72,62,64,40).contains(point)){
        selectedChannel_=i==0?-1:i-1;
        update();return;
    }
    if(int dial=dialAt(point);dial>=0){draggedDial_=dial;dragStart_=e->pos();dragParameters_=displayed();return;}
    const QPointF q=point-QPointF(0,145);
    if(QRectF(340,34,48,37).contains(q))apply([](auto& p){p.enabled=!p.enabled;});
    else if(QRectF(36,452,80,29).contains(q))apply([](auto& p){p.rms=false;});
    else if(QRectF(117,452,77,29).contains(q))apply([](auto& p){p.rms=true;});
    else if(QRectF(298,438,43,35).contains(q))apply([](auto& p){p.sidechainFilter=!p.sidechainFilter;});
    else if(QRectF(696,438,43,35).contains(q))apply([](auto& p){p.autoMakeup=!p.autoMakeup;});
    else if(QRectF(459,37,168,32).contains(q)) {
        QMenu menu(this);const QStringList names={QStringLiteral("Vocal Smooth"),QStringLiteral("Punchy Drums"),QStringLiteral("Gentle Level")};
        for(int i=0;i<3;++i){auto* action=menu.addAction(names[i]);connect(action,&QAction::triggered,this,[this,i]{applyPreset(i);});}
        menu.exec(mapToGlobal(QPoint(459,214)));
    } else if(QRectF(638,37,30,32).contains(q))applyPreset(selectedPreset_-1);
    else if(QRectF(675,37,30,32).contains(q))applyPreset(selectedPreset_+1);
    else if(QRectF(713,37,30,32).contains(q)){QMenu menu(this);auto* action=menu.addAction(QStringLiteral("恢复默认值"));connect(action,&QAction::triggered,this,[this]{applyPreset(0);});menu.exec(mapToGlobal(QPoint(713,214)));}
    else if(QRectF(389,440,81,31).contains(q)) {QMenu menu(this);for(float hz:{20,40,60,80,120,160,250,500,1000,2000}){auto* action=menu.addAction(QStringLiteral("%1 Hz").arg(hz));connect(action,&QAction::triggered,this,[this,hz]{apply([hz](auto& p){p.highPassHz=hz;});});}menu.exec(mapToGlobal(QPoint(389,616)));}
    else if(QRectF(522,440,86,31).contains(q)) {QMenu menu(this);for(float hz:{500,1000,2000,4000,8000,12000,16000,20000}){auto* action=menu.addAction(QStringLiteral("%1 kHz").arg(hz/1000));connect(action,&QAction::triggered,this,[this,hz]{apply([hz](auto& p){p.lowPassHz=hz;});});}menu.exec(mapToGlobal(QPoint(522,616)));}
}
void CompressorWidget::mouseMoveEvent(QMouseEvent* e) {if(draggedDial_<0)return;const int i=draggedDial_;const float range=limits[i][1]-limits[i][0];const float delta=static_cast<float>(dragStart_.y()-e->position().y());const float v=std::clamp(get(dragParameters_,i)+delta*range/200,limits[i][0],limits[i][1]);apply([=](auto& p){put(p,i,v);});}
void CompressorWidget::mouseReleaseEvent(QMouseEvent*) {draggedDial_=-1;}
void CompressorWidget::wheelEvent(QWheelEvent* e) {const int dial=dialAt(e->position());if(dial<0)return;changeDial(dial,e->angleDelta().y()/120.0F);e->accept();}
}
