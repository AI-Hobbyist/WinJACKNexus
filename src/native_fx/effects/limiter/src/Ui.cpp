#include <WinJACKNexus/NativeFx/limiter/Ui.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMenu>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
const QColor ink(235,240,255),muted(179,201,238),cyan(16,179,255),pink(255,99,139);
const char* channels[]={"All","L","R","C","LFE","Ls","Rs","Lb","Rb"};
void box(QPainter& p,QRectF r,int radius,QColor fill,QColor edge=QColor(53,78,108)){
    p.setPen(QPen(edge,1));p.setBrush(fill);p.drawRoundedRect(r,radius,radius);
}
void label(QPainter& p,QRectF r,QString s,int size,QColor color=ink,bool bold=false,Qt::Alignment align=Qt::AlignCenter){
    auto f=p.font();f.setPixelSize(size);f.setBold(bold);p.setFont(f);p.setPen(color);p.drawText(r,align,s);
}
void toggle(QPainter& p,QRectF r,bool on){
    box(p,r,11,on?QColor(8,115,209):QColor(42,61,81),on?cyan:QColor(80,105,130));
    p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawEllipse(QPointF(on?r.right()-11:r.left()+11,r.center().y()),8,8);
}
void dial(QPainter& p,QPointF c,float fraction,QString number,QString name,QString english){
    QRectF arc(c.x()-32,c.y()-32,64,64);
    p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor(6,17,29),5));p.drawArc(arc,225*16,-270*16);
    p.setPen(QPen(pink,4,Qt::SolidLine,Qt::RoundCap));p.drawArc(arc,225*16,-qRound(270*std::clamp(fraction,0.f,1.f))*16);
    QRadialGradient grad(c-QPointF(8,11),43);grad.setColorAt(0,QColor(34,58,81));grad.setColorAt(1,QColor(15,31,47));
    p.setPen(QPen(QColor(6,17,30),2));p.setBrush(grad);p.drawEllipse(c,26,26);
    const double angle=(225-270*std::clamp(fraction,0.f,1.f))*3.141592653589793/180;
    p.setPen(QPen(QColor(155,210,255),2));p.drawLine(c+QPointF(14*std::cos(angle),-14*std::sin(angle)),c+QPointF(22*std::cos(angle),-22*std::sin(angle)));
    label(p,{c.x()-52,c.y()+35,104,22},number,16);label(p,{c.x()-53,c.y()+57,106,20},name,15,muted);
    label(p,{c.x()-54,c.y()+77,108,20},english,14,muted);
}
}
LimiterWidget::LimiterWidget(QWidget* parent):QWidget(parent){
    if (!parent) setWindowFlags(Qt::Window);setFixedSize(768,650);setWindowTitle(QStringLiteral("Limiter 限幅器"));
    meterTimer_.setInterval(33);connect(&meterTimer_,&QTimer::timeout,this,&LimiterWidget::pollMeters);
}
void LimiterWidget::setParameters(const std::array<LimiterParameters,8>& values){values_=values;update();}
void LimiterWidget::setChangeHandler(ChangeHandler handler){onChange_=std::move(handler);}
void LimiterWidget::setMeterReader(MeterReader reader){
    meterReader_=std::move(reader);
    if(meterReader_){pollMeters();meterTimer_.start();}else meterTimer_.stop();
}
void LimiterWidget::setMeters(const std::array<LimiterMeter,8>& meters){meters_=meters;update();}
void LimiterWidget::pollMeters(){if(meterReader_)setMeters(meterReader_());}
LimiterParameters LimiterWidget::shown() const noexcept{
    if(channel_>=0)return values_[static_cast<std::size_t>(channel_)];
    LimiterParameters result=values_[0];
    result.thresholdDb=0.0F;
    result.ceilingDb=0.0F;
    result.releaseMs=0.0F;
    result.lookaheadDb=0.0F;
    result.enabled=true;
    result.autoGain=true;
    for(const auto& value:values_){
        result.thresholdDb+=value.thresholdDb/8.0F;
        result.ceilingDb+=value.ceilingDb/8.0F;
        result.releaseMs+=value.releaseMs/8.0F;
        result.lookaheadDb+=value.lookaheadDb/8.0F;
        result.enabled=result.enabled&&value.enabled;
        result.autoGain=result.autoGain&&value.autoGain;
    }
    return result;
}
void LimiterWidget::apply(const std::function<void(LimiterParameters&)>& edit){
    const int first=channel_<0?0:channel_,last=channel_<0?8:channel_+1;
    for(int i=first;i<last;++i){edit(values_[i]);if(onChange_)onChange_(i,values_[i]);}
    preset_=3;update();
}
void LimiterWidget::applyPreset(int index){
    preset_=std::clamp(index,0,3);
    if(preset_==3){update();return;}
    const auto selected=preset_;
    apply([selected](LimiterParameters& s){
        if(selected==0){s.thresholdDb=-3.0F;s.ceilingDb=-1.0F;s.releaseMs=100.0F;s.mode=1;}
        else if(selected==1){s.thresholdDb=-6.0F;s.ceilingDb=-1.0F;s.releaseMs=60.0F;s.mode=2;}
        else{s.thresholdDb=-9.0F;s.ceilingDb=-0.3F;s.releaseMs=35.0F;s.mode=2;}
    });
    preset_=selected;update();
}
int LimiterWidget::dialAt(QPointF p) const noexcept{
    const QPointF centers[]={{591,301},{685,301},{591,444},{685,444}};
    for(int i=0;i<4;++i)if(QLineF(p,centers[i]).length()<40)return i;return -1;
}
void LimiterWidget::adjustDial(int i,float delta){
    const float step[]={.05f,.05f,2.f,.05f};
    const auto current=shown();
    const float base=i==0?current.thresholdDb:i==1?current.ceilingDb:i==2?current.releaseMs:current.lookaheadDb;
    const float value=std::clamp(base+delta*step[i],i==0?-24.f:i==1?-12.f:i==2?10.f:0.f,
        i==1?12.f:i==2?500.f:i==3?12.f:6.f);
    apply([=](LimiterParameters& s){
        float* v=i==0?&s.thresholdDb:i==1?&s.ceilingDb:i==2?&s.releaseMs:&s.lookaheadDb;
        *v=value;
    });
}
void LimiterWidget::paintEvent(QPaintEvent*){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(),Controls::themeColor(*this,"fxBackground",QColor(13,28,45)));
    box(p,{7,8,754,126},10,QColor(21,39,58));
    label(p,{22,22,170,30},QStringLiteral("作用声道"),19,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    for(int i=0;i<9;++i){QRectF r(22+i*80,66,72,40);bool active=i==channel_+1;
        box(p,r,5,active?QColor(10,56,87):QColor(25,43,61),active?cyan:QColor(65,91,119));
        label(p,r,QString::fromLatin1(channels[i]),15,active?QColor(119,221,255):muted);}
    p.save();p.translate(0,145);
    QLinearGradient bg(0,0,750,500);bg.setColorAt(0,QColor(20,36,55));bg.setColorAt(1,QColor(8,29,47));
    p.setBrush(bg);p.setPen(QPen(QColor(43,116,164)));p.drawRoundedRect(QRectF(3,8,747,493),11,11);
    box(p,{15,17,723,61},9,QColor(21,38,58),QColor(37,67,91));
    box(p,{16,25,54,55},7,QColor(100,35,55),pink);
    p.setPen(Qt::NoPen);p.setBrush(QColor(255,140,158));
    for(QRectF r: {QRectF(24,36,9,9),QRectF(36,36,19,9),QRectF(24,48,19,9),QRectF(46,48,15,9),QRectF(24,60,12,8)})p.drawRect(r);
    label(p,{89,23,248,32},QStringLiteral("Limiter 限幅器"),23,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{89,53,248,22},QStringLiteral("防止信号过载，控制最大输出电平"),14,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    toggle(p,{341,40,36,21},shown().enabled);label(p,{394,38,42,28},QStringLiteral("预设:"),14,muted);
    box(p,{444,36,160,34},5,QColor(18,36,55));
    const QString presets[]={QStringLiteral("Vocal Safe"),QStringLiteral("Broadcast"),QStringLiteral("Max Loudness"),QStringLiteral("Custom")};
    label(p,{455,36,125,34},presets[preset_],15,ink,false,Qt::AlignLeft|Qt::AlignVCenter);label(p,{580,36,22,34},QStringLiteral("⌄"),19,muted);
    for(int i=0;i<2;++i){const int x=616+i*38;box(p,{qreal(x),37,29,32},5,QColor(28,49,70));label(p,{qreal(x),37,29,32},i?QStringLiteral("›"):QStringLiteral("‹"),24,muted);}
    box(p,{692,37,31,32},5,QColor(28,49,70));label(p,{692,37,31,32},QStringLiteral("•••"),13,muted);
    box(p,{16,86,723,343},10,QColor(15,34,53),QColor(40,67,94));
    const auto s=shown();
    double inputPower=0.0,outputPower=0.0;std::size_t activeChannels=0;
    const int firstChannel=channel_<0?0:channel_;
    const int lastChannel=channel_<0?static_cast<int>(meters_.size()):channel_+1;
    for(int c=firstChannel;c<lastChannel;++c){
        const auto& meter=meters_[static_cast<std::size_t>(c)];
        if(!meter.active)continue;
        inputPower+=std::pow(10.0,static_cast<double>(meter.inputDb)/10.0);
        outputPower+=std::pow(10.0,static_cast<double>(meter.outputDb)/10.0);++activeChannels;
    }
    const float inputEnergyDb=activeChannels==0?-120.0F:
        static_cast<float>(10.0*std::log10(inputPower/static_cast<double>(activeChannels)));
    const float outputEnergyDb=activeChannels==0?-120.0F:
        static_cast<float>(10.0*std::log10(outputPower/static_cast<double>(activeChannels)));
    float gainReductionDb=0.0F;
    if(channel_>=0){
        const auto& meter=meters_[static_cast<std::size_t>(channel_)];
        if(meter.active)gainReductionDb=meter.gainReductionDb;
    }else if(activeChannels>0){
        for(int c=firstChannel;c<lastChannel;++c)
            if(meters_[static_cast<std::size_t>(c)].active)
                gainReductionDb+=meters_[static_cast<std::size_t>(c)].gainReductionDb/static_cast<float>(activeChannels);
    }
    label(p,{28,104,79,23},QStringLiteral("输入电平"),14,muted);label(p,{112,104,79,23},QStringLiteral("输出电平"),14,muted);
    label(p,{213,104,45,23},QStringLiteral("GR"),14,muted);
    const auto drawLevelMeter=[&](int x,int tickLabelX,float level,bool active){
        const qreal fillTop=151+(12.0F-std::clamp(level,-60.0F,12.0F))*224.0F/72.0F;
        p.fillRect(QRectF(x,151,11,224),QColor(12,34,48));
        QLinearGradient gradient(0,151,0,375);gradient.setColorAt(0,QColor(242,60,76));gradient.setColorAt(.17,QColor(245,224,52));gradient.setColorAt(.4,QColor(91,220,76));gradient.setColorAt(1,QColor(13,98,69));
        if(active&&fillTop<375)p.fillRect(QRectF(x,fillTop,11,375-fillTop),gradient);
        p.setBrush(Qt::NoBrush);p.setPen(QColor(65,88,109));p.drawRect(QRectF(x,151,11,224));
        const QString channelLabel=channel_<0?QStringLiteral("All"):QString::fromLatin1(channels[channel_+1]);
        label(p,{qreal(x-8),377,27,21},channelLabel,12);
        label(p,{qreal(x-12),397,35,20},active?QStringLiteral("%1").arg(level,0,'f',1):QStringLiteral("-inf"),10);
        const QString ticks[]={QStringLiteral("+12"),QStringLiteral("+6"),QStringLiteral("0"),QStringLiteral("-6"),QStringLiteral("-12"),QStringLiteral("-18"),QStringLiteral("-24"),QStringLiteral("-36"),QStringLiteral("-60")};
        const float meterValues[]={12,6,0,-6,-12,-18,-24,-36,-60};
        for(int i=0;i<9;++i){
            const qreal y=151+(12.0F-meterValues[i])*224.0F/72.0F;
            label(p,QRectF(qreal(tickLabelX),y-9,25.0,18.0),ticks[i],11,muted,false,Qt::AlignRight|Qt::AlignVCenter);
        }
    };
    drawLevelMeter(62,19,inputEnergyDb,activeChannels>0);
    drawLevelMeter(146,103,outputEnergyDb,activeChannels>0);
    p.fillRect(QRectF(230,151,11,224),QColor(12,34,48));
    const qreal grZeroY=151+(12.0F-0.0F)*224.0F/72.0F;
    const qreal grBottom=151+(12.0F-std::clamp(gainReductionDb,-60.0F,12.0F))*224.0F/72.0F;
    if(activeChannels>0&&gainReductionDb<0.0F)
        p.fillRect(QRectF(230,grZeroY,11,grBottom-grZeroY),QColor(225,61,81));
    p.setBrush(Qt::NoBrush);p.setPen(QColor(65,88,109));p.drawRect(QRectF(230,151,11,224));
    const QString grTicks[]={QStringLiteral("+12"),QStringLiteral("+6"),QStringLiteral("0"),QStringLiteral("-6"),QStringLiteral("-12"),QStringLiteral("-18"),QStringLiteral("-24"),QStringLiteral("-36"),QStringLiteral("-60")};
    const float grValues[]={12,6,0,-6,-12,-18,-24,-36,-60};
    for(int i=0;i<9;++i){
        const int y=151+qRound((12.0F-grValues[i])*224.0F/72.0F);
        p.setPen(QColor(77,103,125));p.drawLine(225,y,244,y);
        label(p,{244,qreal(y-9),28,18},grTicks[i],11,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    }
    label(p,{207,397,58,20},activeChannels>0?QStringLiteral("%1 dB").arg(gainReductionDb,0,'f',1):QStringLiteral("0.0 dB"),12);
    box(p,{274,101,267,289},5,QColor(12,29,47),QColor(58,90,125));QRectF plot(334,129,191,191);
    p.setPen(QPen(QColor(39,61,83),1));for(int i=0;i<=9;++i)p.drawLine(QPointF(plot.left()+i*plot.width()/9,plot.top()),QPointF(plot.left()+i*plot.width()/9,plot.bottom()));
    for(int i=0;i<=9;++i)p.drawLine(QPointF(plot.left(),plot.top()+i*plot.height()/9),QPointF(plot.right(),plot.top()+i*plot.height()/9));
    const QString axisTicks[]={QStringLiteral("-60"),QStringLiteral("-36"),QStringLiteral("-24"),QStringLiteral("-12"),QStringLiteral("0"),QStringLiteral("+6"),QStringLiteral("+12")};
    const float axisValues[]={-60,-36,-24,-12,0,6,12};
    for(int i=0;i<7;++i){
        const qreal x=plot.left()+(axisValues[i]+60)*plot.width()/72;
        const qreal y=plot.bottom()-(axisValues[i]+60)*plot.height()/72;
        label(p,{x-21,324,42,18},axisTicks[i],11,muted);
        if(i>0)label(p,{302,y-9,28,18},axisTicks[i],11,muted,false,Qt::AlignRight|Qt::AlignVCenter);
    }
    auto point=[&](float x,float y){return QPointF(plot.left()+(x+60)*plot.width()/72,plot.bottom()-(y+60)*plot.height()/72);};
    const auto transfer=[&](const LimiterParameters& parameters,float input){
        if(!parameters.enabled)return input;
        const float ratio=parameters.mode==0?2.0F:parameters.mode==1?4.0F:12.0F;
        const float kneeWidth=parameters.mode==0?6.0F:parameters.mode==1?3.0F:0.0F;
        const float delta=input-parameters.thresholdDb;float output=input;
        if(kneeWidth>0.0F&&delta>-kneeWidth*.5F&&delta<kneeWidth*.5F){
            const float t=delta+kneeWidth*.5F;
            output=input+(1.0F/ratio-1.0F)*t*t/(2.0F*kneeWidth);
        }else if(delta>=kneeWidth*.5F)output=parameters.thresholdDb+delta/ratio;
        return std::min(output,parameters.ceilingDb);
    };
    const auto outputAt=[&](float input){
        if(channel_>=0)return transfer(values_[static_cast<std::size_t>(channel_)],input);
        double power=0.0;std::size_t count=0;
        for(std::size_t c=0;c<values_.size();++c){
            if(activeChannels>0&&!meters_[c].active)continue;
            const float outDb=transfer(values_[c],input);power+=std::pow(10.0,static_cast<double>(outDb)/10.0);++count;
        }
        return count==0?input:static_cast<float>(10.0*std::log10(power/static_cast<double>(count)));
    };
    QPainterPath curve;curve.moveTo(point(-60,outputAt(-60)));
    const float energyEnd=std::clamp(inputEnergyDb,-60.0F,12.0F);
    QPainterPath fill;fill.moveTo(point(-60,outputAt(-60)));
    for(int i=1;i<=144;++i){
        const float input=-60.0F+i*0.5F;const QPointF q=point(input,outputAt(input));curve.lineTo(q);
        if(activeChannels>0&&input<=energyEnd)fill.lineTo(q);
    }
    if(activeChannels>0&&energyEnd>-60.0F){
        const QPointF energyPoint=point(energyEnd,outputAt(energyEnd));
        fill.lineTo(energyPoint.x(),plot.bottom());fill.lineTo(plot.left(),plot.bottom());fill.closeSubpath();
        p.setPen(Qt::NoPen);p.setBrush(QColor(232,75,113,52));p.drawPath(fill);
    }
    p.setBrush(Qt::NoBrush);p.setPen(QPen(pink,2));p.drawPath(curve);
    const QPointF knee=point(s.thresholdDb,s.thresholdDb);p.setBrush(pink);p.setPen(Qt::NoPen);p.drawEllipse(knee,6,6);
    box(p,{409,114,78,40},6,QColor(25,46,71),QColor(78,111,147));label(p,{410,118,76,16},QStringLiteral("Threshold"),12,muted);
    label(p,{410,135,76,17},QStringLiteral("%1 dB").arg(s.thresholdDb,0,'f',1),12,muted);
    const qreal ceilingY=plot.bottom()-(std::clamp(s.ceilingDb,-60.0F,12.0F)+60.0F)*plot.height()/72.0F;
    p.setPen(QPen(QColor(255,194,45),3));p.drawLine(QPointF(plot.right(),plot.top()),QPointF(plot.right(),ceilingY));
    label(p,{371,351,120,22},QStringLiteral("输入电平 (dB)"),14,muted);
    p.save();p.translate(278,289);p.rotate(-90);label(p,{0,0,128,23},QStringLiteral("输出电平 (dB)"),14,muted);p.restore();
    dial(p,{591,156},(s.thresholdDb+24)/30,QStringLiteral("%1 dB").arg(s.thresholdDb,0,'f',1),QStringLiteral("阈值"),QStringLiteral("(Threshold)"));
    dial(p,{685,156},(s.ceilingDb+12)/24,QStringLiteral("%1 dB").arg(s.ceilingDb,0,'f',1),QStringLiteral("释放上限"),QStringLiteral("(Ceiling)"));
    dial(p,{591,299},(s.releaseMs-10)/490,QStringLiteral("%1 ms").arg(s.releaseMs,0,'f',0),QStringLiteral("释放时间"),QStringLiteral("(Release)"));
    dial(p,{685,299},s.lookaheadDb/12,QStringLiteral("%1 dB").arg(s.lookaheadDb,0,'f',1),QStringLiteral("前瞻"),QStringLiteral("(Lookahead)"));
    box(p,{16,430,723,59},8,QColor(18,38,58));label(p,{26,448,44,23},QStringLiteral("模式:"),14,muted);
    const QString modes[]={QStringLiteral("透明"),QStringLiteral("现代"),QStringLiteral("砖墙")};
    for(int i=0;i<3;++i){box(p,{qreal(71+i*84),443,84,36},5,i==s.mode?QColor(9,62,96):QColor(24,45,67),i==s.mode?cyan:QColor(64,96,130));label(p,{qreal(71+i*84),443,84,36},modes[i],15,i==s.mode?ink:muted);}
    label(p,{351,448,62,23},QStringLiteral("过采样:"),14,muted);box(p,{417,443,110,36},5,QColor(20,39,59));
    label(p,{429,443,64,36},QStringLiteral("%1x").arg(s.oversampling),15,ink,false,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{493,443,30,36},QStringLiteral("⌄"),17,muted);
    label(p,{595,448,67,23},QStringLiteral("自动增益"),14,muted);toggle(p,{673,450,37,22},s.autoGain);p.restore();
}
void LimiterWidget::mousePressEvent(QMouseEvent* e){
    if(e->button()!=Qt::LeftButton)return;auto q=e->position();
    if(q.y()>=66&&q.y()<106&&q.x()>=22&&q.x()<734){int i=int((q.x()-22)/80);if(i<9){channel_=i-1;update();}return;}
    QPointF v=q-QPointF(0,145);if(v.y()>40&&v.y()<62&&v.x()>340&&v.x()<378){const bool enabled=!shown().enabled;apply([enabled](auto& s){s.enabled=enabled;});return;}
    if(v.y()>36&&v.y()<70&&v.x()>444&&v.x()<604){
        QMenu menu(this);
        const QString names[]={QStringLiteral("Vocal Safe"),QStringLiteral("Broadcast"),QStringLiteral("Max Loudness"),QStringLiteral("Custom")};
        for(int i=0;i<4;++i){auto* action=menu.addAction(names[i]);action->setCheckable(true);action->setChecked(preset_==i);
            connect(action,&QAction::triggered,this,[this,i]{applyPreset(i);});}
        menu.exec(mapToGlobal(QPoint(444,215)));return;
    }
    if(v.y()>37&&v.y()<69&&v.x()>616&&v.x()<645){applyPreset((preset_+3)%4);return;}
    if(v.y()>37&&v.y()<69&&v.x()>654&&v.x()<683){applyPreset((preset_+1)%4);return;}
    if(v.y()>37&&v.y()<69&&v.x()>692&&v.x()<723){
        QMenu menu(this);
        auto* resetCurrent=menu.addAction(QStringLiteral("重置当前声道"));
        auto* resetAll=menu.addAction(QStringLiteral("重置全部声道"));
        menu.addSeparator();auto* copyAll=menu.addAction(QStringLiteral("将当前设置复制到全部声道"));
        connect(resetCurrent,&QAction::triggered,this,[this]{apply([](auto& s){s=LimiterParameters{};});preset_=0;update();});
        connect(resetAll,&QAction::triggered,this,[this]{
            for(std::size_t i=0;i<values_.size();++i){values_[i]=LimiterParameters{};if(onChange_)onChange_(i,values_[i]);}
            preset_=0;update();
        });
        connect(copyAll,&QAction::triggered,this,[this]{
            const auto source=shown();for(std::size_t i=0;i<values_.size();++i){values_[i]=source;if(onChange_)onChange_(i,values_[i]);}
            preset_=3;update();
        });
        menu.exec(mapToGlobal(QPoint(692,215)));return;
    }
    if(v.y()>443&&v.y()<479){if(v.x()>71&&v.x()<323){int mode=int((v.x()-71)/84);apply([mode](auto& s){s.mode=mode;});return;}
        if(v.x()>417&&v.x()<527){
            QMenu menu(this);
            for(int factor:{2,4,8}){
                auto* action=menu.addAction(QStringLiteral("%1x").arg(factor));
                action->setCheckable(true);action->setChecked(shown().oversampling==factor);
                connect(action,&QAction::triggered,this,[this,factor]{apply([factor](auto& s){s.oversampling=factor;});});
            }
            menu.exec(mapToGlobal(QPoint(417,588)));return;
        }
        if(v.x()>673&&v.x()<711){const bool enabled=!shown().autoGain;apply([enabled](auto& s){s.autoGain=enabled;});return;}}
    drag_=dialAt(q);dragPoint_=q.toPoint();
}
void LimiterWidget::mouseMoveEvent(QMouseEvent* e){if(drag_>=0){int delta=dragPoint_.y()-e->position().y();if(delta){adjustDial(drag_,float(delta));dragPoint_=e->position().toPoint();}}}
void LimiterWidget::mouseReleaseEvent(QMouseEvent*){drag_=-1;}
void LimiterWidget::wheelEvent(QWheelEvent* e){int i=dialAt(e->position());if(i>=0){adjustDial(i,e->angleDelta().y()/30.f);e->accept();}else QWidget::wheelEvent(e);}
}
