#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/NativeFx/limiter/Ui.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/NativeFx/I18n.hpp>
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
constexpr auto channels = WinJACKNexus::Common::surroundChannelChoices;
void box(QPainter& p,QRectF r,int radius,QColor fill,QColor edge=QColor(53,78,108)){
    Controls::drawSurface(p,r,fill,edge,radius);
}
void label(QPainter& p,QRectF r,QString s,int size,QColor color=ink,bool bold=false,Qt::Alignment align=Qt::AlignCenter){
    auto f=p.font();f.setPixelSize(size);f.setBold(bold);
    const auto themed = color == ink
        ? Controls::themeTokenColor("native-fx-limiter", "text-primary", color)
        : color == muted
            ? Controls::themeTokenColor("native-fx-limiter", "text-muted", color) : color;
    Controls::drawElidedText(p,r,s,f,themed,align);
}
void toggle(QPainter& p,QRectF r,bool on){
    Controls::drawToggle(p,r,on,
        Controls::themeTokenColor("native-fx-limiter", "control-toggle-indicator-on", QColor(8,115,209)),
        Controls::themeTokenColor("native-fx-limiter", "control-toggle-indicator-off", QColor(42,61,81)));
}
void dial(QPainter& p,QPointF c,float fraction,QString number,QString name,QString english,
          const Controls::Palette& colors){
    Controls::drawKnob(p,c,fraction,32,colors);
    label(p,{c.x()-52,c.y()+35,104,22},number,16);label(p,{c.x()-53,c.y()+57,106,20},name,15,muted);
    label(p,{c.x()-54,c.y()+77,108,20},english,14,muted);
}
}
LimiterWidget::LimiterWidget(QWidget* parent):QWidget(parent){
    if (!parent) setWindowFlags(Qt::Window);setFixedSize(768,650);setWindowTitle(NativeFx::I18n::text("module.name", "Limiter"));
    bindRefreshTimer(meterTimer_, this);connect(&meterTimer_,&QTimer::timeout,this,&LimiterWidget::pollMeters);
    NativeFx::I18n::bind(this, [this] {
        setWindowTitle(NativeFx::I18n::text("module.name", "Limiter"));
        setAccessibleDescription(NativeFx::I18n::text("module.description", "Eight-channel native limiter UI."));
        update();
    });
}
void LimiterWidget::setParameters(const std::array<LimiterParameters,8>& values){
    values_=values;
    const LimiterParameters defaults;
    preset_=std::all_of(values.begin(),values.end(),[&defaults](const auto& item){
        return item.thresholdDb==defaults.thresholdDb&&item.ceilingDb==defaults.ceilingDb
            &&item.releaseMs==defaults.releaseMs&&item.lookaheadDb==defaults.lookaheadDb
            &&item.mode==defaults.mode&&item.oversampling==defaults.oversampling
            &&item.autoGain==defaults.autoGain&&item.enabled==defaults.enabled;
    })?0:3;
    if(drag_<0)update();
}
void LimiterWidget::setChangeHandler(ChangeHandler handler){onChange_=std::move(handler);}
void LimiterWidget::setMeterReader(MeterReader reader){
    meterReader_=std::move(reader);
    if(meterReader_){pollMeters();meterTimer_.start();}else if(drag_<0)meterTimer_.stop();
}
void LimiterWidget::setMeters(const std::array<LimiterMeter,8>& meters){
    const auto& settings=ApplicationSettings::instance();
    const double elapsed=meterClock_.isValid()
        ?std::max(meterClock_.restart()/1000.0,1.0/settings.frameRate())
        :1.0/settings.frameRate();
    if(!meterClock_.isValid())meterClock_.start();
    meters_=meters;
    for(std::size_t i=0;i<meters_.size();++i){
        inputBallistics_[i].advance(meters_[i].active?meters_[i].inputDb:-72.0F,
                                    elapsed,settings.peakHoldSeconds());
        outputBallistics_[i].advance(meters_[i].active?meters_[i].outputDb:-72.0F,
                                     elapsed,settings.peakHoldSeconds());
    }
    if(!numericClock_.isValid()||numericClock_.elapsed()>=settings.peakValueRefreshMilliseconds()){
        double inputPower=0.0,outputPower=0.0;
        int count=0;
        for(int i=channel_<0?0:channel_;i<(channel_<0?8:channel_+1);++i){
            const auto& meter=meters_[static_cast<std::size_t>(i)];
            if(!meter.active)continue;
            inputPower+=std::pow(10.0,static_cast<double>(meter.inputDb)/10.0);
            outputPower+=std::pow(10.0,static_cast<double>(meter.outputDb)/10.0);
            ++count;
        }
        numericMeterActive_=count>0;
        numericInputDb_=count?static_cast<float>(10.0*std::log10(inputPower/count)):-120.0F;
        numericOutputDb_=count?static_cast<float>(10.0*std::log10(outputPower/count)):-120.0F;
        numericClock_.restart();
    }
    if(!meterTimer_.isActive())update();
}
void LimiterWidget::pollMeters(){if(meterReader_){setMeters(meterReader_());update();}else if(drag_>=0)update();}
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
    preset_=3;if(drag_<0)update();
}
void LimiterWidget::applyPreset(int index){
    preset_=std::clamp(index,0,3);
    if(preset_==3){update();return;}
    const auto selected=preset_;
    apply([selected](LimiterParameters& s){
        if(selected==0){s=LimiterParameters{};}
        else if(selected==1){s.thresholdDb=-6.0F;s.ceilingDb=-1.0F;s.releaseMs=60.0F;s.mode=2;}
        else{s.thresholdDb=-9.0F;s.ceilingDb=-0.3F;s.releaseMs=35.0F;s.mode=2;}
    });
    preset_=selected;update();
}
int LimiterWidget::dialAt(QPointF p) const noexcept{
    const QPointF centers[]={{591,301},{685,301},{591,444},{685,444}};
    for(int i=0;i<4;++i)if(QLineF(p,centers[i]).length()<40)return i;return -1;
}
namespace {
float dialValue(const LimiterParameters& parameters, int index) noexcept {
    switch(index) {
    case 0: return parameters.thresholdDb;
    case 1: return parameters.ceilingDb;
    case 2: return parameters.releaseMs;
    default: return parameters.lookaheadDb;
    }
}
void setDialValue(LimiterParameters& parameters, int index, float value) noexcept {
    switch(index) {
    case 0: parameters.thresholdDb=value; break;
    case 1: parameters.ceilingDb=value; break;
    case 2: parameters.releaseMs=value; break;
    default: parameters.lookaheadDb=value; break;
    }
}
Controls::NumericControlSpec dialSpec(int index) noexcept {
    constexpr float minima[] {-24.0F,-12.0F,10.0F,0.0F};
    constexpr float maxima[] {6.0F,12.0F,500.0F,12.0F};
    return {minima[index],maxima[index],dialValue(LimiterParameters{},index),index==2?0:1};
}
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
    p.fillRect(rect(),Controls::themeColor(*this,"fxBackground",
        Controls::themeTokenColor("native-fx-limiter", "surface-window", QColor(13,28,45))));
    box(p,{7,8,754,126},10,QColor(21,39,58));
    label(p,{22,22,170,30},NativeFx::I18n::text("control.targetChannels", "Channel"),19,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    for(int i=0;i<9;++i){QRectF r(22+i*80,66,72,40);bool active=i==channel_+1;
        box(p,r,5,active?QColor(10,56,87):QColor(25,43,61),active?cyan:QColor(65,91,119));
        label(p,r,(i == 0 ? NativeFx::I18n::text("control.all", "All") : QString::fromLatin1(channels[i])),15,active?QColor(119,221,255):muted);}
    p.save();p.translate(0,145);
    QLinearGradient bg(0,0,750,500);
    bg.setColorAt(0,Controls::themeTokenColor("native-fx-limiter", "surface-panel-start", QColor(20,36,55)));
    bg.setColorAt(1,Controls::themeTokenColor("native-fx-limiter", "surface-panel-end", QColor(8,29,47)));
    Controls::drawSurface(p,QRectF(3,8,747,493),bg,QColor(43,116,164),11);
    box(p,{15,17,723,61},9,QColor(21,38,58),QColor(37,67,91));
    box(p,{16,25,54,55},7,QColor(100,35,55),pink);
    p.setPen(Qt::NoPen);p.setBrush(QColor(255,140,158));
    for(QRectF r: {QRectF(24,36,9,9),QRectF(36,36,19,9),QRectF(24,48,19,9),QRectF(46,48,15,9),QRectF(24,60,12,8)})p.drawRect(r);
    label(p,{89,23,248,32},NativeFx::I18n::text("module.name", "Limiter"),23,ink,true,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{89,53,248,22},NativeFx::I18n::text("description.limiter", "Prevent overload and limit peak output"),14,muted,false,Qt::AlignLeft|Qt::AlignVCenter);
    toggle(p,{341,40,36,21},shown().enabled);label(p,{394,38,42,28},NativeFx::I18n::text("label.preset", "Preset:"),14,muted);
    box(p,{444,36,160,34},5,QColor(18,36,55));
    const QString presets[]={NativeFx::I18n::text("preset.default", "Default"),NativeFx::I18n::text("preset.broadcast", "Broadcast"),NativeFx::I18n::text("preset.maxLoudness", "Max Loudness"),NativeFx::I18n::text("preset.custom", "Custom")};
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
    label(p,{28,104,79,23},NativeFx::I18n::text("label.inputLevel", "Input level"),14,muted);label(p,{112,104,79,23},NativeFx::I18n::text("label.outputLevel", "Output level"),14,muted);
    label(p,{213,104,45,23},QStringLiteral("GR"),14,muted);
    double smoothedInputPower=0.0,smoothedOutputPower=0.0;
    double peakInputPower=0.0,peakOutputPower=0.0;
    for(int c=firstChannel;c<lastChannel;++c){
        if(!meters_[static_cast<std::size_t>(c)].active)continue;
        const auto& input=inputBallistics_[static_cast<std::size_t>(c)];
        const auto& output=outputBallistics_[static_cast<std::size_t>(c)];
        smoothedInputPower+=std::pow(10.0,input.levelDb()/10.0);
        smoothedOutputPower+=std::pow(10.0,output.levelDb()/10.0);
        peakInputPower+=std::pow(10.0,input.peakDb()/10.0);
        peakOutputPower+=std::pow(10.0,output.peakDb()/10.0);
    }
    const auto aggregateDb=[activeChannels](double power){
        return activeChannels?10.0*std::log10(power/static_cast<double>(activeChannels)):-120.0;
    };
    const auto drawLevelMeter=[&](int x,int tickLabelX,double level,double peak,double numeric,bool active){
        Controls::MeterStyle style;
        style.minimumDb=-60.0;style.maximumDb=12.0;style.vertical=true;
        style.zones={{-12.0,
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-low-active", QColor(30,205,111)),
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-low-inactive", QColor(13,50,42))},
            {0.0,
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-mid-active", QColor(245,224,52)),
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-mid-inactive", QColor(54,50,27))},
            {12.0,
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-high-active", QColor(242,60,76)),
            Controls::themeTokenColor("native-fx-limiter", "meter-level-zone-high-inactive", QColor(57,28,39))}};
        style.border=QColor(65,88,109);
        Controls::drawMeter(p,QRectF(x,151,11,224),active?level:-60.0,
            active?std::optional<double>(peak):std::nullopt,style);
        const QString channelLabel=channel_<0?NativeFx::I18n::text("control.all", "All"):QString::fromLatin1(channels[channel_+1]);
        label(p,{qreal(x-8),377,27,21},channelLabel,12);
        label(p,{qreal(x-12),397,35,20},numericMeterActive_?QStringLiteral("%1").arg(numeric,0,'f',1):QStringLiteral("-inf"),10);
        style.ticks={{12,QStringLiteral("+12")},{6,QStringLiteral("+6")},
                     {0,QStringLiteral("0")},{-6,QStringLiteral("-6")},
                     {-12,QStringLiteral("-12")},{-18,QStringLiteral("-18")},
                     {-24,QStringLiteral("-24")},{-36,QStringLiteral("-36")},
                     {-60,QStringLiteral("-60")}};
        style.ticksLeft=true;
        style.tickBandPixels=x-tickLabelX;
        style.tickFontPixelSize=11;
        style.labelColor=muted;
        Controls::drawMeterScale(p,{qreal(tickLabelX),151,qreal(x-tickLabelX),224},style);
    };
    drawLevelMeter(62,19,aggregateDb(smoothedInputPower),aggregateDb(peakInputPower),
                   numericInputDb_,activeChannels>0);
    drawLevelMeter(146,103,aggregateDb(smoothedOutputPower),aggregateDb(peakOutputPower),
                   numericOutputDb_,activeChannels>0);
    Controls::MeterStyle reductionStyle;
    reductionStyle.minimumDb=-60.0;
    reductionStyle.maximumDb=12.0;
    reductionStyle.fillOriginDb=0.0;
    reductionStyle.vertical=true;
    reductionStyle.showPeak=false;
    reductionStyle.showTickGrid=true;
    reductionStyle.zones={{12.0,
        Controls::themeTokenColor("native-fx-limiter", "meter-reduction-zone-single-active", QColor(225,61,81)),
        Controls::themeTokenColor("native-fx-limiter", "meter-reduction-zone-single-inactive", QColor(12,34,48))}};
    reductionStyle.border=QColor(65,88,109);
    reductionStyle.labelColor=muted;
    reductionStyle.tickColor=QColor(77,103,125);
    reductionStyle.tickFontPixelSize=11;
    reductionStyle.tickBandPixels=31;
    reductionStyle.ticks={{12,QStringLiteral("+12")},{6,QStringLiteral("+6")},
                          {0,QStringLiteral("0")},{-6,QStringLiteral("-6")},
                          {-12,QStringLiteral("-12")},{-18,QStringLiteral("-18")},
                          {-24,QStringLiteral("-24")},{-36,QStringLiteral("-36")},
                          {-60,QStringLiteral("-60")}};
    Controls::drawMeter(p,{230,151,42,224},activeChannels>0?gainReductionDb:0.0,
                        std::nullopt,reductionStyle);
    label(p,{207,397,58,20},activeChannels>0?QStringLiteral("%1 dB").arg(gainReductionDb,0,'f',1):QStringLiteral("0.0 dB"),12);
    box(p,{274,101,267,289},5,
        Controls::themeTokenColor("native-fx-limiter", "chart-background", QColor(12,29,47)),
        QColor(58,90,125));QRectF plot(334,129,191,191);
    Controls::ChartGridStyle grid;
    grid.grid=Controls::themeTokenColor("native-fx-limiter", "chart-grid-major", QColor(39,61,83));
    grid.columns=grid.rows=9;
    Controls::drawChartGrid(p,plot,grid);
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
    }
    Controls::CurveStyle transferStyle;
    transferStyle.stroke=Controls::themeTokenColor("native-fx-limiter", "chart-series-transfer-stroke", pink);
    transferStyle.fill=Controls::themeTokenColor("native-fx-limiter", "chart-series-transfer-fill", QColor(232,75,113,52));
    transferStyle.width=2;
    transferStyle.showFill=activeChannels>0&&energyEnd>-60.0F;
    Controls::drawCurvePath(p,curve,transferStyle,transferStyle.showFill?&fill:nullptr);
    const QPointF knee=point(s.thresholdDb,s.thresholdDb);
    Controls::drawMarker(p,knee,6,pink);
    box(p,{409,114,78,40},6,QColor(25,46,71),QColor(78,111,147));label(p,{410,118,76,16},NativeFx::I18n::text("control.threshold", "Threshold"),12,muted);
    label(p,{410,135,76,17},QStringLiteral("%1 dB").arg(s.thresholdDb,0,'f',1),12,muted);
    const qreal ceilingY=plot.bottom()-(std::clamp(s.ceilingDb,-60.0F,12.0F)+60.0F)*plot.height()/72.0F;
    p.setPen(QPen(QColor(255,194,45),3));p.drawLine(QPointF(plot.right(),plot.top()),QPointF(plot.right(),ceilingY));
    label(p,{371,351,120,22},NativeFx::I18n::text("axis.inputLevel", "Input level (dB)"),14,muted);
    p.save();p.translate(278,289);p.rotate(-90);label(p,{0,0,128,23},NativeFx::I18n::text("axis.outputLevel", "Output level (dB)"),14,muted);p.restore();
    const auto knobColors=Controls::paletteFor(*this,
        {Controls::themeTokenColor("native-fx-limiter", "control-knob-background-base", QColor(13,28,45)),
         QColor(22,41,62), ink,
         Controls::themeTokenColor("native-fx-limiter", "control-knob-indicator-normal", pink),
         Controls::themeTokenColor("native-fx-limiter", "control-knob-track", QColor(58,91,124))});
    dial(p,{591,156},(s.thresholdDb+24)/30,QStringLiteral("%1 dB").arg(s.thresholdDb,0,'f',1),NativeFx::I18n::text("control.threshold", "Threshold"),NativeFx::I18n::text("hint.threshold", "(Threshold)"),knobColors);
    dial(p,{685,156},(s.ceilingDb+12)/24,QStringLiteral("%1 dB").arg(s.ceilingDb,0,'f',1),NativeFx::I18n::text("control.ceiling", "Output ceiling"),NativeFx::I18n::text("hint.ceiling", "(Ceiling)"),knobColors);
    dial(p,{591,299},(s.releaseMs-10)/490,QStringLiteral("%1 ms").arg(s.releaseMs,0,'f',0),NativeFx::I18n::text("control.release", "Release"),NativeFx::I18n::text("hint.release", "(Release)"),knobColors);
    // The legacy field is persisted but is not consumed by LimiterProcessor.
    dial(p,{685,299},s.lookaheadDb/12,QStringLiteral("%1 dB").arg(s.lookaheadDb,0,'f',1),NativeFx::I18n::text("control.lookahead", "Unused"),NativeFx::I18n::text("hint.lookahead", "(Lookahead)"),knobColors);
    box(p,{16,430,723,59},8,QColor(18,38,58));label(p,{26,448,44,23},NativeFx::I18n::text("control.mode", "Mode:"),14,muted);
    const QString modes[]={NativeFx::I18n::text("mode.transparent", "Transparent"),NativeFx::I18n::text("mode.modern", "Modern"),NativeFx::I18n::text("mode.brickwall", "Brickwall")};
    for(int i=0;i<3;++i){box(p,{qreal(71+i*84),443,84,36},5,i==s.mode?QColor(9,62,96):QColor(24,45,67),i==s.mode?cyan:QColor(64,96,130));label(p,{qreal(71+i*84),443,84,36},modes[i],15,i==s.mode?ink:muted);}
    label(p,{351,448,62,23},NativeFx::I18n::text("control.oversampling", "Oversampling:"),14,muted);box(p,{417,443,110,36},5,QColor(20,39,59));
    label(p,{429,443,64,36},QStringLiteral("%1x").arg(s.oversampling),15,ink,false,Qt::AlignLeft|Qt::AlignVCenter);
    label(p,{493,443,30,36},QStringLiteral("⌄"),17,muted);
    label(p,{595,448,67,23},NativeFx::I18n::text("control.autoGain", "Auto gain"),14,muted);toggle(p,{673,450,37,22},s.autoGain);p.restore();
}
void LimiterWidget::mousePressEvent(QMouseEvent* e){
    if(e->button()!=Qt::LeftButton)return;auto q=e->position();
    const QPoint centers[] {{591,301},{685,301},{591,444},{685,444}};
    for(int i=0;i<4;++i){
        const QRect readout(centers[i].x()-52,centers[i].y()+35,104,22);
        if(!readout.contains(e->pos()))continue;
        Controls::editNumericValue(this,readout,dialValue(shown(),i),dialSpec(i),
            [this,i](double target){
                apply([i,target](LimiterParameters& parameters){
                    setDialValue(parameters,i,static_cast<float>(target));
                });
            });
        return;
    }
    if(q.y()>=66&&q.y()<106&&q.x()>=22&&q.x()<734){int i=int((q.x()-22)/80);if(i<9){channel_=i-1;numericClock_.invalidate();update();}return;}
    QPointF v=q-QPointF(0,145);if(v.y()>40&&v.y()<62&&v.x()>340&&v.x()<378){const bool enabled=!shown().enabled;apply([enabled](auto& s){s.enabled=enabled;});return;}
    if(v.y()>36&&v.y()<70&&v.x()>444&&v.x()<604){
        QMenu menu(this);
        const QString names[]={NativeFx::I18n::text("preset.default", "Default"),NativeFx::I18n::text("preset.broadcast", "Broadcast"),NativeFx::I18n::text("preset.maxLoudness", "Max Loudness"),NativeFx::I18n::text("preset.custom", "Custom")};
        for(int i=0;i<4;++i){auto* action=menu.addAction(names[i]);
            constexpr const char* keys[] = {"preset.default", "preset.broadcast", "preset.maxLoudness", "preset.custom"};
            constexpr const char* english[] = {"Default", "Broadcast", "Max Loudness", "Custom"};
            NativeFx::I18n::bindProperty(action, "text", keys[i], english[i]);
            action->setCheckable(true);action->setChecked(preset_==i);
            connect(action,&QAction::triggered,this,[this,i]{applyPreset(i);});}
        menu.exec(mapToGlobal(QPoint(444,215)));return;
    }
    if(v.y()>37&&v.y()<69&&v.x()>616&&v.x()<645){applyPreset((preset_+3)%4);return;}
    if(v.y()>37&&v.y()<69&&v.x()>654&&v.x()<683){applyPreset((preset_+1)%4);return;}
    if(v.y()>37&&v.y()<69&&v.x()>692&&v.x()<723){
        QMenu menu(this);
        auto* resetCurrent=menu.addAction(NativeFx::I18n::text("action.resetCurrent", "Reset current channel"));NativeFx::I18n::bindProperty(resetCurrent, "text", "action.resetCurrent", "Reset current channel");
        auto* resetAll=menu.addAction(NativeFx::I18n::text("action.resetAll", "Reset all channels"));NativeFx::I18n::bindProperty(resetAll, "text", "action.resetAll", "Reset all channels");
        menu.addSeparator();auto* copyAll=menu.addAction(NativeFx::I18n::text("action.copyToAll", "Copy current settings to all channels"));NativeFx::I18n::bindProperty(copyAll, "text", "action.copyToAll", "Copy current settings to all channels");
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
    drag_=dialAt(q);dragPoint_=q.toPoint();if(drag_>=0)meterTimer_.start();
}
void LimiterWidget::mouseDoubleClickEvent(QMouseEvent* e){
    if(e->button()!=Qt::LeftButton)return;
    const int index=dialAt(e->position());
    if(index<0)return;
    drag_=-1;if(!meterReader_)meterTimer_.stop();
    Controls::resetNumericValue(dialSpec(index),[this,index](double target){
        apply([index,target](LimiterParameters& parameters){
            setDialValue(parameters,index,static_cast<float>(target));
        });
    });
}
void LimiterWidget::mouseMoveEvent(QMouseEvent* e){if(drag_>=0){int delta=dragPoint_.y()-e->position().y();if(delta){adjustDial(drag_,float(delta));dragPoint_=e->position().toPoint();}}}
void LimiterWidget::mouseReleaseEvent(QMouseEvent*){drag_=-1;if(!meterReader_)meterTimer_.stop();update();}
void LimiterWidget::wheelEvent(QWheelEvent* e){int i=dialAt(e->position());if(i>=0){adjustDial(i,e->angleDelta().y()/30.f);e->accept();}else QWidget::wheelEvent(e);}
}
