/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Common/ChannelNames.hpp>
#include <WinJACKNexus/NativeFx/parametric_eq/Ui.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/NativeFx/I18n.hpp>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <algorithm>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
using Shape = WinJACKNexus::Common::ParametricEqShape;
constexpr int designWidth = 1536, designHeight = 1136, mainTop = 110;
QPoint widgetPoint(const QWidget& widget, qreal x, qreal y) {
    return {static_cast<int>(std::lround(x*widget.width()/designWidth)),
            static_cast<int>(std::lround((mainTop+y)*widget.height()/designHeight))};
}
constexpr auto channelNames = WinJACKNexus::Common::surroundChannelNames;
QString shapeName(int index) {
    constexpr const char* keys[] = {"filter.lowShelf", "filter.peaking", "filter.highShelf", "filter.lowPass", "filter.highPass"};
    constexpr const char* english[] = {"Low shelf", "Peaking", "High shelf", "Low-pass", "High-pass"};
    const int selected = std::clamp(index, 0, 4);
    return NativeFx::I18n::text(keys[selected], english[selected]);
}

const QColor background("#081c2f"), panel("#10263b"), line("#2b5374"), textColor("#e8f2ff"), muted("#b7d2fa");
const std::array<QColor, 4> bandColors {QColor("#14a9ff"), QColor("#11d7a9"), QColor("#f15bb9"), QColor("#ffcc42")};
float logX(float frequency) { return 101.0F + static_cast<float>(std::log(std::clamp(frequency, 20.0F, 20000.0F) / 20.0F) / std::log(1000.0)) * 1155.0F; }
float xFrequency(float x) { return 20.0F * std::pow(1000.0F, (std::clamp(x, 101.0F, 1256.0F) - 101.0F) / 1155.0F); }
float gainY(float db,int range) { return 306.0F - std::clamp(db, -float(range), float(range)) * (170.0F/range); }
float yGain(float y,int range) { return std::clamp((306.0F - y) * (range/170.0F), -float(range), float(range)); }
void rounded(QPainter& p, QRectF r, QBrush fill, QColor stroke = line, qreal radius = 9) {
    Controls::drawSurface(p,r,fill,stroke,radius);
}
void label(QPainter& p, QRectF r, const QString& value, int size = 17, QColor color = textColor,
           Qt::Alignment align = Qt::AlignCenter, bool bold = false) {
    auto font = p.font(); font.setPixelSize(size); font.setBold(bold);
    const auto themed = color == textColor
        ? Controls::themeTokenColor("native-fx-parametric-eq", "text-primary", color)
        : color == muted
            ? Controls::themeTokenColor("native-fx-parametric-eq", "text-muted", color) : color;
    Controls::drawElidedText(p,r,value,font,themed,align);
}
void switchControl(QPainter& p, QRectF r, bool on, QColor accent = QColor("#087afa"),
                   const char* onRole = "control-toggle-indicator-on") {
    Controls::drawToggle(p, r, on,
        Controls::themeTokenColor("native-fx-parametric-eq", onRole, accent),
        Controls::themeTokenColor("native-fx-parametric-eq", "control-toggle-indicator-off", QColor("#2b4869")));
}
QString frequencyText(float hz) { return hz >= 1000 ? QString::number(hz / 1000.0F, 'f', hz >= 10000 ? 1 : 1) + QStringLiteral(" kHz") : QString::number(hz, 'f', 0) + QStringLiteral(" Hz"); }
QString gainText(float db) { return QString::number(db, 'f', 1).prepend(db > 0 ? "+" : "") + QStringLiteral(" dB"); }
QString presetName(int index) { const QStringList names {NativeFx::I18n::text("preset.default", "Default"), NativeFx::I18n::text("preset.vocalClear", "Vocal Clear"), NativeFx::I18n::text("preset.bassBoost", "Bass Boost"), NativeFx::I18n::text("preset.warmth", "Warmth"), NativeFx::I18n::text("preset.presence", "Presence"), NativeFx::I18n::text("preset.custom", "Custom")}; return names[std::clamp(index, 0, 5)]; }

WinJACKNexus::Common::ParametricEqParameters presetParameters(int index) {
    WinJACKNexus::Common::ParametricEqParameters preset;
    if (index == 1) {
        preset.bands[0].gainDb=4.5F; preset.bands[1].gainDb=-6.0F;
        preset.bands[2].gainDb=5.0F; preset.bands[3].gainDb=3.0F;
    } else if (index == 2) {
        preset.bands[0].gainDb=6; preset.bands[1].gainDb=-2; preset.bands[2].gainDb=1;
    } else if (index == 3) {
        preset.bands[0].gainDb=3; preset.bands[1].gainDb=1.5F;
        preset.bands[2].gainDb=-1; preset.bands[3].gainDb=-2;
    } else if (index == 4) {
        preset.bands[0].gainDb=-1; preset.bands[1].gainDb=-2;
        preset.bands[2].gainDb=4; preset.bands[3].gainDb=2;
    }
    return preset;
}

bool matchesPreset(const WinJACKNexus::Common::ParametricEqParameters& value,
                   const WinJACKNexus::Common::ParametricEqParameters& preset) {
    for (std::size_t band=0;band<value.bands.size();++band) {
        const auto& a=value.bands[band]; const auto& b=preset.bands[band];
        if (a.frequencyHz!=b.frequencyHz || a.q!=b.q || a.gainDb!=b.gainDb
            || a.shape!=b.shape || a.enabled!=b.enabled) return false;
    }
    return value.highPassHz==preset.highPassHz && value.lowPassHz==preset.lowPassHz
        && value.highPassSlope==preset.highPassSlope && value.lowPassSlope==preset.lowPassSlope
        && value.outputGainDb==preset.outputGainDb
        && value.highPassEnabled==preset.highPassEnabled
        && value.lowPassEnabled==preset.lowPassEnabled && value.enabled==preset.enabled;
}
float knobValue(const WinJACKNexus::Common::ParametricEqParameters& parameters,int index) {
    if(index<12) {
        const auto& band=parameters.bands[static_cast<std::size_t>(index/3)];
        return index%3==0?band.frequencyHz:index%3==1?band.q:band.gainDb;
    }
    return index==12?parameters.highPassHz:index==13?parameters.lowPassHz:parameters.outputGainDb;
}
Controls::NumericControlSpec knobSpec(int index) {
    const WinJACKNexus::Common::ParametricEqParameters defaults;
    const double minimum=index<12?(index%3==0?20.0:index%3==1?.1:-18.0)
        :index==12?20.0:index==13?200.0:-18.0;
    const double maximum=index<12?(index%3==0?20000.0:index%3==1?10.0:18.0)
        :index==12?2000.0:index==13?20000.0:18.0;
    const int decimals=index<12?(index%3==0?0:index%3==1?2:1)
        :index==14?1:0;
    return {minimum,maximum,knobValue(defaults,index),decimals};
}
QRectF knobReadout(int index) {
    if(index<12) {
        const int center=25+374*(index/3)+66+117*(index%3);
        return {qreal(center-54),760,108,26};
    }
    return index==12?QRectF(516,925,59,43)
        :index==13?QRectF(1063,925,75,43):QRectF(1324,925,70,43);
}

void drawShapeIcon(QPainter& painter, const QRectF& bounds, Shape shape, const QColor& color)
{
    const auto point = [&bounds](qreal x, qreal y) {
        return QPointF(bounds.left() + x * bounds.width(), bounds.top() + y * bounds.height());
    };
    QPainterPath curve;
    switch (shape) {
    case Shape::lowShelf:
        curve.moveTo(point(0.04, 0.25));
        curve.lineTo(point(0.25, 0.25));
        curve.cubicTo(point(0.48, 0.25), point(0.52, 0.70), point(0.76, 0.70));
        curve.lineTo(point(0.96, 0.70));
        break;
    case Shape::bell:
        curve.moveTo(point(0.04, 0.72));
        curve.cubicTo(point(0.30, 0.72), point(0.39, 0.20), point(0.50, 0.20));
        curve.cubicTo(point(0.61, 0.20), point(0.70, 0.72), point(0.96, 0.72));
        break;
    case Shape::highShelf:
        curve.moveTo(point(0.04, 0.70));
        curve.lineTo(point(0.24, 0.70));
        curve.cubicTo(point(0.48, 0.70), point(0.52, 0.25), point(0.75, 0.25));
        curve.lineTo(point(0.96, 0.25));
        break;
    case Shape::lowPass:
        curve.moveTo(point(0.04, 0.25));
        curve.lineTo(point(0.38, 0.25));
        curve.cubicTo(point(0.59, 0.25), point(0.64, 0.78), point(0.96, 0.83));
        break;
    case Shape::highPass:
        curve.moveTo(point(0.04, 0.83));
        curve.cubicTo(point(0.36, 0.78), point(0.41, 0.25), point(0.62, 0.25));
        curve.lineTo(point(0.96, 0.25));
        break;
    }
    painter.save();
    QColor baseline = color; baseline.setAlpha(70);
    painter.setPen(QPen(baseline, 1));
    painter.drawLine(point(0.04, 0.71), point(0.96, 0.71));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, bounds.width() < 40 ? 1.7 : 2.2,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curve);
    painter.restore();
}

QIcon shapeMenuIcon(Shape shape)
{
    QPixmap pixmap(40, 26);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    drawShapeIcon(painter, QRectF(3, 3, 34, 20), shape, QColor("#a9d9ff"));
    return QIcon(pixmap);
}

}

ParametricEqWidget::ParametricEqWidget(QWidget* parent) : QWidget(parent, parent ? Qt::Widget : Qt::Window), spectrumTimer_(this)
{
    if (!parent) setAttribute(Qt::WA_NativeWindow, true);
    setWindowTitle(NativeFx::I18n::text("module.name", "Parametric EQ"));
    setFixedSize(1229, 909); setMouseTracking(true);
    spectrum_.inputDb.fill(-96); spectrum_.outputDb.fill(-96);
    spectrumTimer_.setObjectName(QStringLiteral("nativeFxSpectrumRefreshTimer"));
    bindRefreshTimer(spectrumTimer_, this);
    connect(&spectrumTimer_, &QTimer::timeout, this, &ParametricEqWidget::pollSpectrum);
    NativeFx::I18n::bind(this, [this] { setWindowTitle(NativeFx::I18n::text("module.name", "Parametric EQ")); setAccessibleDescription(NativeFx::I18n::text("module.description", "Eight-channel four-band parametric equalizer.")); update(); });
}
void ParametricEqWidget::setParameters(const std::array<Parameters, 8>& value) { parameters_ = value; refreshPresetSelection(); if(dragBand_<0 && dragKnob_<0) update(); }
void ParametricEqWidget::setChangeHandler(ChangeHandler value) { changeHandler_ = std::move(value); }
void ParametricEqWidget::setSpectrumFrameReader(SpectrumFrameReader value) {
    spectrumFrameReader_ = std::move(value);
    if (spectrumFrameReader_) spectrumTimer_.start(); else if(dragBand_<0 && dragKnob_<0) spectrumTimer_.stop();
}
ParametricEqWidget::Parameters ParametricEqWidget::displayed() const noexcept {
    if (selectedChannel_ >= 0) return parameters_[static_cast<std::size_t>(selectedChannel_)];
    Parameters result = parameters_[0];
    for (std::size_t i=1;i<8;++i) {
        for (std::size_t b=0;b<4;++b) {
            result.bands[b].frequencyHz += parameters_[i].bands[b].frequencyHz;
            result.bands[b].q += parameters_[i].bands[b].q;
            result.bands[b].gainDb += parameters_[i].bands[b].gainDb;
            result.bands[b].enabled &= parameters_[i].bands[b].enabled;
        }
        result.highPassHz += parameters_[i].highPassHz;
        result.lowPassHz += parameters_[i].lowPassHz;
        result.outputGainDb += parameters_[i].outputGainDb;
        result.highPassEnabled &= parameters_[i].highPassEnabled;
        result.lowPassEnabled &= parameters_[i].lowPassEnabled;
        result.enabled &= parameters_[i].enabled;
    }
    for (auto& band : result.bands) { band.frequencyHz /= 8; band.q /= 8; band.gainDb /= 8; }
    result.highPassHz /= 8; result.lowPassHz /= 8; result.outputGainDb /= 8;
    return result;
}
void ParametricEqWidget::apply(const std::function<void(Parameters&)>& edit) {
    const int first = selectedChannel_ < 0 ? 0 : selectedChannel_;
    const int last = selectedChannel_ < 0 ? 8 : selectedChannel_+1;
    for (int c=first;c<last;++c) {
        edit(parameters_[static_cast<std::size_t>(c)]);
        if (changeHandler_) changeHandler_(static_cast<std::size_t>(c), parameters_[static_cast<std::size_t>(c)]);
    }
    refreshPresetSelection(); if(dragBand_<0 && dragKnob_<0) update();
}
void ParametricEqWidget::refreshPresetSelection() {
    const int first=selectedChannel_<0?0:selectedChannel_, last=selectedChannel_<0?8:selectedChannel_+1;
    for (int index=0;index<5;++index) {
        const auto preset=presetParameters(index);
        bool matches=true;
        for (int channel=first;channel<last;++channel)
            matches &= matchesPreset(parameters_[static_cast<std::size_t>(channel)],preset);
        if (matches) {selectedPreset_=index;return;}
    }
    selectedPreset_=5;
}
void ParametricEqWidget::selectPreset(int index) {
    if (index < 0 || index > 4) return;
    const Parameters preset=presetParameters(index);
    const int first=selectedChannel_<0?0:selectedChannel_, last=selectedChannel_<0?8:selectedChannel_+1;
    for (int c=first;c<last;++c) {
        parameters_[static_cast<std::size_t>(c)]=preset;
        if (changeHandler_) changeHandler_(static_cast<std::size_t>(c),preset);
    }
    refreshPresetSelection(); update();
}
void ParametricEqWidget::pollSpectrum() {
    if (!spectrumFrameReader_ || !spectrumFrameReader_(spectrumFrame_)) {
        if(dragBand_>=0 || dragKnob_>=0) update();
        return;
    }
    WinJACKNexus::Common::GraphicEqSpectrumPair next;
    WinJACKNexus::Common::calculateGraphicEqSpectrum(spectrumFrame_, selectedChannel_<0?8:static_cast<std::size_t>(selectedChannel_), next,static_cast<std::size_t>(fftSize_));
    const auto elapsedMilliseconds=spectrumClock_.isValid()?spectrumClock_.restart():-1;
    if(elapsedMilliseconds<0) spectrumClock_.start();
    const auto elapsedSeconds=elapsedMilliseconds>0
        ?elapsedMilliseconds/1000.0:1.0/ApplicationSettings::instance().frameRate();
    const float baseline=smoothing_==0?0.8F:smoothing_==1?0.5F:0.25F;
    const float factor=static_cast<float>(1.0-std::pow(1.0-baseline,elapsedSeconds*60.0));
    for (std::size_t i=0;i<next.inputDb.size();++i) {
        spectrum_.inputDb[i] += (next.inputDb[i]-spectrum_.inputDb[i])*factor;
        spectrum_.outputDb[i] += (next.outputDb[i]-spectrum_.outputDb[i])*factor;
    }
    hasSpectrum_=true; update();
}

void ParametricEqWidget::paintEvent(QPaintEvent*) {
    const auto themed = [](const char* role, const QColor& fallback) {
        return Controls::themeTokenColor(QStringLiteral("native-fx-parametric-eq"),
                                         QString::fromLatin1(role), fallback);
    };
    const std::array<QColor, 4> themedBandColors {
        themed("chart-series-band-1-stroke", bandColors[0]),
        themed("chart-series-band-2-stroke", bandColors[1]),
        themed("chart-series-band-3-stroke", bandColors[2]),
        themed("chart-series-band-4-stroke", bandColors[3])};
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.scale(width()/double(designWidth),height()/double(designHeight));
    p.fillRect(QRect(0,0,designWidth,designHeight), Controls::themeColor(*this, "fxBackground",
        themed("surface-window", background)));
    const auto params=displayed();
    const auto mapGain=[this](float db){return gainY(db,displayRange_);};
    const double responseRate=hasSpectrum_?spectrumFrame_.sampleRate:48000.0;
    rounded(p,{0,0,1535,109},QColor("#17283a"),line,9);
    label(p,{16,12,300,33},NativeFx::I18n::text("control.targetChannels", "Channel"),21,textColor,Qt::AlignVCenter|Qt::AlignLeft,true);
    for (int i=-1;i<8;++i) {
        const int x=17+(i+1)*92; QRectF rect(x,58,78,40);
        rounded(p,rect,i==selectedChannel_?QColor("#123c5b"):QColor("#1c3043"),i==selectedChannel_?QColor("#00baff"):QColor("#3b5771"),6);
        label(p,rect,i<0?NativeFx::I18n::text("control.all", "All"):QString::fromLatin1(channelNames[static_cast<std::size_t>(i)]),16,i==selectedChannel_?QColor("#56d5ff"):muted);
    }
    p.translate(0,mainTop);
    QLinearGradient outer(0,0,0,1024); outer.setColorAt(0,QColor("#07314a")); outer.setColorAt(1,QColor("#071b2e"));
    const auto themeAccent = Controls::themeColor(*this, "fxAccent", QColor("#1682d8"));
    rounded(p,{12,13,1512,994},outer,themeAccent,11);
    rounded(p,{13,14,1510,94},QColor("#102c45"),themeAccent,10);
    rounded(p,{26,24,78,77},QColor("#006b78"),QColor("#22d0d2"),11);
    for (int i=0;i<4;++i) { p.setPen(QPen(QColor("#b9faff"),6,Qt::SolidLine,Qt::RoundCap)); p.drawLine(43+i*12,78,43+i*12,74-11*i-(i==3?-10:0)); }
    label(p,{130,24,490,40},NativeFx::I18n::text("module.name", "Parametric EQ"),30,textColor,Qt::AlignVCenter|Qt::AlignLeft,true);
    label(p,{130,69,490,29},NativeFx::I18n::text("description.parametricEq", "Shape the sound with frequency, gain and bandwidth"),20,muted,Qt::AlignVCenter|Qt::AlignLeft);
    switchControl(p,{637,43,57,34},params.enabled,themeAccent,"control-toggle-main-indicator-on");
    label(p,{747,39,69,43},NativeFx::I18n::text("label.preset", "Preset:"),20,muted);
    rounded(p,{819,37,320,48},QColor("#10253b"));
    label(p,{836,37,255,48},presetName(selectedPreset_),22,textColor,Qt::AlignVCenter|Qt::AlignLeft);
    label(p,{1093,37,35,48},QStringLiteral("⌄"),27,muted);
    for (int i=0;i<3;++i) { rounded(p,{1158+i*66.0,37,48,48},QColor("#1a324e")); label(p,{1158+i*66.0,37,48,48},i==0?QStringLiteral("❮"):i==1?QStringLiteral("❯"):QStringLiteral("•••"),22,muted); }

    rounded(p,{24,120,1248,430},themed("chart-background", QColor("#081e31")),QColor("#254b69"));
    QRectF graph(101,145,1155,340);
    p.save(); p.setClipRect(graph);
    const std::array<float,10> ticks {20,50,100,200,500,1000,2000,5000,10000,20000};
    if (showGrid_) {
        Controls::ChartGridStyle grid;
        grid.grid=themed("chart-grid-major", QColor("#25415c"));
        for (float freq:ticks)
            grid.verticalFractions.push_back((logX(freq)-graph.left())/graph.width());
        for (int db=-displayRange_;db<=displayRange_;db+=displayRange_/3)
            grid.horizontalFractions.push_back((mapGain(float(db))-graph.top())/graph.height());
        Controls::drawChartGrid(p,graph,grid);
    }
    if (hasSpectrum_ && showSpectrum_) {
        for (int kind=0;kind<2;++kind) {
            QPainterPath path;
            const auto& values=kind==0?spectrum_.inputDb:spectrum_.outputDb;
            for (std::size_t i=0;i<values.size();++i) {
                const double x=101.0+i*1155.0/(values.size()-1);
                const double y=485.0-std::clamp((values[i]+90.0)/90.0,0.0,1.0)*300.0;
                if (i==0) path.moveTo(x,y); else path.lineTo(x,y);
            }
            Controls::CurveStyle spectrumStyle;
            spectrumStyle.stroke=kind==0
                ? themed("chart-series-input-spectrum-stroke", QColor("#ffc66b"))
                : themed("chart-series-processed-spectrum-stroke", QColor("#45dbf3"));
            spectrumStyle.width=2.4;
            spectrumStyle.penStyle=kind==0?Qt::DashLine:Qt::SolidLine;
            Controls::drawCurvePath(p,path,spectrumStyle);
        }
    }
    if (showResponse_) {
        for (std::size_t b=0;b<4;++b) {
            auto isolated=params; for (std::size_t other=0;other<4;++other) isolated.bands[other].enabled=other==b;
            isolated.highPassEnabled=isolated.lowPassEnabled=false; isolated.outputGainDb=0;
            isolated.bands[b].enabled=params.bands[b].enabled;
            QPainterPath path; path.moveTo(101,mapGain(WinJACKNexus::Common::ParametricEqProcessor::responseDb(isolated,responseRate,20)));
            for (int x=104;x<=1256;x+=4) path.lineTo(x,mapGain(WinJACKNexus::Common::ParametricEqProcessor::responseDb(isolated,responseRate,xFrequency(float(x)))));
            QPainterPath fill=path; fill.lineTo(1256,mapGain(0)); fill.lineTo(101,mapGain(0)); fill.closeSubpath();
            Controls::CurveStyle bandStyle;
            bandStyle.stroke=themedBandColors[b];
            const auto fillRole = QStringLiteral("chart-series-band-%1-fill").arg(b + 1);
            auto fallbackFill = bandColors[b]; fallbackFill.setAlpha(55);
            bandStyle.fill=Controls::themeTokenColor(QStringLiteral("native-fx-parametric-eq"), fillRole, fallbackFill);
            bandStyle.width=2.3;
            bandStyle.showFill=true;
            Controls::drawCurvePath(p,path,bandStyle,&fill);
        }
        QPainterPath total; total.moveTo(101,mapGain(WinJACKNexus::Common::ParametricEqProcessor::responseDb(params,responseRate,20)));
        for (int x=104;x<=1256;x+=4) total.lineTo(x,mapGain(WinJACKNexus::Common::ParametricEqProcessor::responseDb(params,responseRate,xFrequency(float(x)))));
        Controls::CurveStyle totalStyle;
        totalStyle.stroke=themed("chart-series-total-response-stroke", QColor("#e2f2ff"));
        totalStyle.width=1.5;
        Controls::drawCurvePath(p,total,totalStyle);
    }
    p.restore();
    rounded(p,{805,153,440,62},QColor(16,39,59,232),QColor("#416581"),6);
    const auto legendLine=[&p](int x,int y,const QColor& color,Qt::PenStyle style){
        p.setPen(QPen(color,2.5,style,Qt::RoundCap));
        p.drawLine(x,y,x+29,y);
    };
    legendLine(820,172,showSpectrum_?themed("chart-series-input-spectrum-stroke", QColor("#ffc66b")):QColor("#62788a"),Qt::DashLine);
    label(p,{854,159,96,25},NativeFx::I18n::text("label.inputSpectrum", "Input spectrum"),15,muted,Qt::AlignLeft|Qt::AlignVCenter);
    legendLine(966,172,showSpectrum_?themed("chart-series-processed-spectrum-stroke", QColor("#45dbf3")):QColor("#62788a"),Qt::SolidLine);
    label(p,{1000,159,96,25},NativeFx::I18n::text("label.processedSpectrum", "Processed spectrum"),15,muted,Qt::AlignLeft|Qt::AlignVCenter);
    legendLine(1110,172,showResponse_?themed("chart-series-total-response-stroke", QColor("#e2f2ff")):QColor("#62788a"),Qt::SolidLine);
    label(p,{1144,159,94,25},NativeFx::I18n::text("label.combinedResponse", "Combined response"),15,muted,Qt::AlignLeft|Qt::AlignVCenter);
    const std::array<QString,4> legendBands {NativeFx::I18n::text("band.low", "1 Low"),NativeFx::I18n::text("band.lowMid", "2 Low mid"),NativeFx::I18n::text("band.highMid", "3 High mid"),NativeFx::I18n::text("band.high", "4 High")};
    for (std::size_t b=0;b<4;++b) {
        const int x=822+static_cast<int>(b)*103;
        Controls::drawMarker(p,QPointF(x+5,199),5,
            showResponse_?themedBandColors[b]:QColor("#62788a"));
        label(p,{double(x+17),186,86,25},legendBands[b],14,muted,Qt::AlignLeft|Qt::AlignVCenter);
    }
    for (int db=displayRange_;db>=-displayRange_;db-=displayRange_/3) label(p,{55,mapGain(float(db))-12,34,24},db>0?"+"+QString::number(db):QString::number(db),15,muted,Qt::AlignRight|Qt::AlignVCenter);
    p.save(); p.translate(45,306); p.rotate(-90); label(p,{-80,-15,160,25},NativeFx::I18n::text("axis.gain", "Gain (dB)"),17,muted); p.restore();
    for (float freq:std::array<float,10>{20,50,100,200,500,1000,2000,5000,10000,20000}) label(p,{logX(freq)-30,491,60,24},freq>=1000?QString::number(int(freq/1000))+"k":QString::number(int(freq)),15,muted);
    label(p,{680,515,155,25},NativeFx::I18n::text("axis.frequency", "Frequency (Hz)"),17,muted);
    for (std::size_t b=0;b<4;++b) if (showLabels_ && params.bands[b].enabled) {
        const auto x=logX(params.bands[b].frequencyHz), y=mapGain(params.bands[b].gainDb);
        Controls::drawMarker(p,QPointF(x,y),16,themedBandColors[b],QColor("#f0f8ff"),2);
        label(p,{x-15,y-15,30,30},QString::number(b+1),19,Qt::white,Qt::AlignCenter,true);
    }
    rounded(p,{1283,120,228,196},QColor("#122941"));
    label(p,{1298,130,190,28},NativeFx::I18n::text("section.displayOptions", "Display options"),18,textColor,Qt::AlignLeft|Qt::AlignVCenter,true);
    const std::array<QString,4> optionNames {NativeFx::I18n::text("control.spectrumAnalysis", "Spectrum analysis"),NativeFx::I18n::text("control.parameterCurve", "Parameter curve"),NativeFx::I18n::text("control.frequencyLabels", "Frequency labels"),NativeFx::I18n::text("control.gridLines", "Grid lines")};
    const std::array<bool,4> options {showSpectrum_,showResponse_,showLabels_,showGrid_};
    for(int i=0;i<4;++i) { rounded(p,{1298,169+i*34.0,28,28},options[i]?QColor("#087bfc"):QColor("#253f59"),QColor("#33597a"),5); if(options[i]) label(p,{1298,169+i*34.0,28,28},QStringLiteral("✓"),21); label(p,{1337,168+i*34.0,151,29},optionNames[i],19,muted,Qt::AlignLeft|Qt::AlignVCenter); }
    rounded(p,{1283,325,228,224},QColor("#122941"));
    label(p,{1298,334,190,28},NativeFx::I18n::text("section.analysisSettings", "Analysis settings"),18,textColor,Qt::AlignLeft|Qt::AlignVCenter,true);
    const std::array<QString,3> settings {NativeFx::I18n::text("control.fftSize", "FFT size:"),NativeFx::I18n::text("control.smoothing", "Smoothing:"),NativeFx::I18n::text("control.displayRange", "Display range:")};
    const std::array<QString,3> values {QString::number(fftSize_),smoothing_==0?NativeFx::I18n::text("level.low", "Low"):smoothing_==1?NativeFx::I18n::text("level.medium", "Medium"):NativeFx::I18n::text("level.high", "High"),QStringLiteral("±%1 dB").arg(displayRange_)};
    for(int i=0;i<3;++i) { label(p,{1298,367+i*47.0,102,40},settings[i],16,muted,Qt::AlignLeft|Qt::AlignVCenter); rounded(p,{1402,367+i*47.0,97,40},QColor("#10263a")); label(p,{1408,367+i*47.0,89,40},values[i]+QStringLiteral("  ⌄"),16,textColor); }

    const std::array<QString,4> names {NativeFx::I18n::text("band.lowCaption", "Low"),NativeFx::I18n::text("band.lowMidCaption", "Low mid"),NativeFx::I18n::text("band.highMidCaption", "High mid"),NativeFx::I18n::text("band.highCaption", "High")};
    for (int b=0;b<4;++b) {
        const int x=25+374*b; const auto& band=params.bands[static_cast<std::size_t>(b)]; QColor tint=themedBandColors[static_cast<std::size_t>(b)];
        rounded(p,{double(x),560,365,327},QColor("#0e2639"),tint.darker(160));
        QColor header=tint.darker(460); rounded(p,{double(x),560,365,50},header,tint,8);
        Controls::drawMarker(p,QPointF(x+32,586),17,tint,tint.lighter(130));
        label(p,{double(x+17),569,30,33},QString::number(b+1),21,Qt::white,Qt::AlignCenter,true);
        label(p,{double(x+66),567,220,39},names[static_cast<std::size_t>(b)],19,textColor,Qt::AlignLeft|Qt::AlignVCenter,true);
        switchControl(p,{double(x+302),572,46,27},band.enabled);
        rounded(p,{double(x+13),621,70,41},QColor("#102c44"));
        drawShapeIcon(p,{double(x+27),630,42,23},band.shape,muted);
        rounded(p,{double(x+90),621,262,41},themed("surface-panel", QColor("#10263b")));
        label(p,{double(x+104),621,220,41},shapeName(static_cast<int>(band.shape)),17,textColor,Qt::AlignLeft|Qt::AlignVCenter);
        label(p,{double(x+322),621,25,41},QStringLiteral("⌄"),22,muted);
        for (int k=0;k<3;++k) {
            const int cx=x+66+k*117; const float value=k==0?std::log(std::max(20.0F,band.frequencyHz)/20.0F)/std::log(1000.0F):k==1?(band.q-0.1F)/9.9F:(band.gainDb+18)/36;
            const auto indicatorRole = QStringLiteral("control-knob-band-%1-indicator-normal").arg(b + 1);
            const auto colors=Controls::paletteFor(*this,
                {themed("control-knob-background-base", background),QColor("#23415b"),textColor,
                 Controls::themeTokenColor(QStringLiteral("native-fx-parametric-eq"), indicatorRole, tint),
                 themed("control-knob-track", QColor("#314e68"))});
            Controls::drawKnob(p,{double(cx),719},value,34,colors);
            label(p,{double(cx-54),760,108,26},k==0?frequencyText(band.frequencyHz):k==1?QString::number(band.q,'f',1):gainText(band.gainDb),17);
            label(p,{double(cx-58),788,116,23},k==0?NativeFx::I18n::text("control.frequency", "Frequency"):k==1?NativeFx::I18n::text("control.qFactor", "Q factor"):NativeFx::I18n::text("control.gain", "Gain"),16,muted);
        }
        for (int s=0;s<5;++s) {
            QRectF r(x+13+s*70.0,824,70,48); rounded(p,r,s==static_cast<int>(band.shape)?header:QColor("#102437"),s==static_cast<int>(band.shape)?tint:line,6);
            drawShapeIcon(p,r.adjusted(14,11,-14,-11),static_cast<Shape>(s),s==static_cast<int>(band.shape)?tint:muted);
        }
    }
    rounded(p,{25,899,558,95},QColor("#10283d"));
    rounded(p,{591,899,548,95},QColor("#10283d"));
    rounded(p,{1147,899,365,95},QColor("#10283d"));
    label(p,{45,917,136,55},NativeFx::I18n::text("filter.highPassCaption", "High-pass (HPF)"),18,muted); switchControl(p,{183,932,49,30},params.highPassEnabled);
    rounded(p,{248,926,48,42},QColor("#102337")); drawShapeIcon(p,{256,934,32,26},Shape::highPass,muted);
    rounded(p,{307,926,122,42},QColor("#102337")); label(p,{307,926,122,42},QString::number(params.highPassSlope)+QStringLiteral(" dB/Oct ⌄"),16);
    const auto filterColors=Controls::paletteFor(*this,
        {themed("control-knob-background-base", background),QColor("#23415b"),textColor,
         themed("control-knob-filter-indicator-normal", QColor("#35bbff")),
         themed("control-knob-track", QColor("#314e68"))});
    Controls::drawKnob(p,{475,946},std::log(std::max(20.0F,params.highPassHz)/20.0F)/std::log(100.0F),25,filterColors);
    label(p,{516,925,59,43},frequencyText(params.highPassHz),16);
    label(p,{607,917,132,55},NativeFx::I18n::text("filter.lowPassCaption", "Low-pass (LPF)"),18,muted); switchControl(p,{740,932,49,30},params.lowPassEnabled);
    rounded(p,{805,926,48,42},QColor("#102337")); drawShapeIcon(p,{813,934,32,26},Shape::lowPass,muted);
    rounded(p,{864,926,122,42},QColor("#102337")); label(p,{864,926,122,42},QString::number(params.lowPassSlope)+QStringLiteral(" dB/Oct ⌄"),16);
    Controls::drawKnob(p,{1022,946},std::log(std::max(200.0F,params.lowPassHz)/200.0F)/std::log(100.0F),25,filterColors);
    label(p,{1063,925,75,43},frequencyText(params.lowPassHz),16);
    label(p,{1166,917,90,55},NativeFx::I18n::text("control.outputGain", "Output gain:"),18,muted);
    Controls::drawKnob(p,{1286,946},(params.outputGainDb+18)/36,25,filterColors);
    label(p,{1324,925,70,43},gainText(params.outputGainDb),16);
    rounded(p,{1400,922,103,49},QColor("#1e3957"),QColor("#69a5dd")); label(p,{1400,922,103,49},NativeFx::I18n::text("action.reset", "Reset"),18);
}

QPointF ParametricEqWidget::designPoint(const QPointF& pos) const { return {pos.x()*designWidth/width(),pos.y()*designHeight/height()-mainTop}; }
void ParametricEqWidget::editGraph(const QPointF& pos) {
    if (dragBand_<0 || dragBand_>=4) return;
    const float frequency=xFrequency(float(pos.x())), gain=yGain(float(pos.y()),displayRange_);
    apply([&](Parameters& p){ auto& b=p.bands[static_cast<std::size_t>(dragBand_)]; b.frequencyHz=frequency; b.gainDb=gain; });
}
void ParametricEqWidget::editKnob(const QPointF& pos) {
    if (dragKnob_<0) return;
    const float dy=float(lastMouse_.y()-pos.y()); lastMouse_=pos;
    const auto current=displayed();
    if (dragKnob_<12) {
        const int b=dragKnob_/3,k=dragKnob_%3; const auto old=current.bands[static_cast<std::size_t>(b)];
        if(k==0) { const float target=std::clamp(old.frequencyHz*std::pow(2.0F,dy/60.0F),20.0F,20000.0F); apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].frequencyHz=target;}); }
        else if(k==1) { const float target=std::clamp(old.q+dy/100.0F,0.1F,10.0F); apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].q=target;}); }
        else { const float target=std::clamp(old.gainDb+dy/8.0F,-18.0F,18.0F); apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].gainDb=target;}); }
    } else if(dragKnob_==12) { const float target=std::clamp(current.highPassHz*std::pow(2.0F,dy/60.0F),20.0F,2000.0F); apply([&](Parameters& p){p.highPassHz=target;}); }
    else if(dragKnob_==13) { const float target=std::clamp(current.lowPassHz*std::pow(2.0F,dy/60.0F),200.0F,20000.0F); apply([&](Parameters& p){p.lowPassHz=target;}); }
    else { const float target=std::clamp(current.outputGainDb+dy/8.0F,-18.0F,18.0F); apply([&](Parameters& p){p.outputGainDb=target;}); }
}
void ParametricEqWidget::applyKnobValue(int index,float target) {
    if(index<12) {
        const int band=index/3,field=index%3;
        apply([band,field,target](Parameters& parameters){
            auto& selected=parameters.bands[static_cast<std::size_t>(band)];
            if(field==0)selected.frequencyHz=target;
            else if(field==1)selected.q=target;
            else selected.gainDb=target;
        });
    } else apply([index,target](Parameters& parameters){
        if(index==12)parameters.highPassHz=target;
        else if(index==13)parameters.lowPassHz=target;
        else parameters.outputGainDb=target;
    });
}
void ParametricEqWidget::mousePressEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton) return;
    const auto point=designPoint(event->position()); const float x=float(point.x()),y=float(point.y());
    if(y<0) { if(y>=-52) { for(int i=-1;i<8;++i) if(x>=17+(i+1)*92 && x<95+(i+1)*92) {setSelectedChannel(i); if(hasSpectrum_) pollSpectrum(); return;} } return; }
    const auto params=displayed();
    if(QRectF(637,43,57,34).contains(point)) {const bool target=!params.enabled; apply([&](Parameters& p){p.enabled=target;}); return;}
    if(QRectF(819,37,320,48).contains(point)) {
        QMenu menu(this); for(int i=0;i<5;++i) { auto* action=menu.addAction(presetName(i));action->setData(i);NativeFx::I18n::bind(action, [action,i] { action->setText(presetName(i)); }); connect(action,&QAction::triggered,this,[this,i]{selectPreset(i);}); }
        menu.exec(mapToGlobal(event->pos())); return;
    }
    if(y>=37 && y<=85 && x>=1158 && x<1272) {
        const int next=selectedPreset_==5?(x<1215?4:0):(selectedPreset_+(x<1215?4:1))%5;
        selectPreset(next); return;
    }
    if(y>=37 && y<=85 && x>=1290 && x<1338) { QMenu menu(this); auto* reset=menu.addAction(NativeFx::I18n::text("action.resetDefault", "Reset to default"));NativeFx::I18n::bindProperty(reset, "text", "action.resetDefault", "Reset to default"); connect(reset,&QAction::triggered,this,[this]{selectPreset(0);}); menu.exec(mapToGlobal(event->pos())); return; }
    for(int index=0;index<15;++index) {
        const auto bounds=knobReadout(index);
        if(!bounds.contains(point))continue;
        const QRect editorBounds(widgetPoint(*this,bounds.left(),bounds.top()),
                                 widgetPoint(*this,bounds.right(),bounds.bottom()));
        Controls::editNumericValue(this,editorBounds,knobValue(params,index),knobSpec(index),
            [this,index](double target){applyKnobValue(index,static_cast<float>(target));});
        return;
    }
    if(x>=1298 && x<1500 && y>=169 && y<305) { int row=int((y-169)/34); if(row<4) {if(row==0)showSpectrum_=!showSpectrum_;if(row==1)showResponse_=!showResponse_;if(row==2)showLabels_=!showLabels_;if(row==3)showGrid_=!showGrid_;update();} return;}
    if(x>=1402 && x<1500 && y>=367 && y<502) {
        const int row=int((y-367)/47);
        if(row>=0 && row<3 && QRectF(1402,367+row*47,97,40).contains(point)) {
            QMenu menu(this);
            const std::array<QString,3> fftChoices {QStringLiteral("1024"),QStringLiteral("2048"),QStringLiteral("4096")};
            const std::array<QString,3> smoothingChoices {NativeFx::I18n::text("level.low", "Low"),NativeFx::I18n::text("level.medium", "Medium"),NativeFx::I18n::text("level.high", "High")};
            const std::array<QString,3> rangeChoices {QStringLiteral("±12 dB"),QStringLiteral("±18 dB"),QStringLiteral("±24 dB")};
            for(int option=0;option<3;++option) {
                const auto& choices=row==0?fftChoices:row==1?smoothingChoices:rangeChoices;
                auto* action=menu.addAction(choices[static_cast<std::size_t>(option)]);
                action->setData(option);
                if (row == 1) NativeFx::I18n::bind(action, [action,option] {
                    constexpr const char* keys[] = {"level.low", "level.medium", "level.high"};
                    constexpr const char* english[] = {"Low", "Medium", "High"};
                    action->setText(NativeFx::I18n::text(keys[option], english[option]));
                });
                const int selected=row==0?(fftSize_==1024?0:fftSize_==2048?1:2)
                    :row==1?smoothing_:(displayRange_==12?0:displayRange_==18?1:2);
                action->setCheckable(true);
                action->setChecked(option==selected);
                connect(action,&QAction::triggered,this,[this,row,option] {
                    if(row==0) {
                        fftSize_=std::array<int,3>{1024,2048,4096}[static_cast<std::size_t>(option)];
                        if(hasSpectrum_) {
                            WinJACKNexus::Common::GraphicEqSpectrumPair refreshed;
                            WinJACKNexus::Common::calculateGraphicEqSpectrum(spectrumFrame_,
                                selectedChannel_<0?8:static_cast<std::size_t>(selectedChannel_),
                                refreshed,static_cast<std::size_t>(fftSize_));
                            spectrum_=refreshed;
                        }
                    } else if(row==1) smoothing_=option;
                    else displayRange_=std::array<int,3>{12,18,24}[static_cast<std::size_t>(option)];
                    update();
                });
            }
            menu.exec(mapToGlobal(widgetPoint(*this,1402,367+row*47+43)));
        }
        return;
    }
    if(x>=101 && x<=1256 && y>=145 && y<=485) {
        for(int b=0;b<4;++b) if(std::hypot(x-logX(params.bands[static_cast<std::size_t>(b)].frequencyHz),y-gainY(params.bands[static_cast<std::size_t>(b)].gainDb,displayRange_))<22) {dragBand_=b;spectrumTimer_.start();return;}
    }
    for(int b=0;b<4;++b) {
        const int left=25+374*b; if(x<left || x>left+365 || y<560 || y>887) continue;
        if(y<611 && x>left+294) { const bool target=!params.bands[static_cast<std::size_t>(b)].enabled; apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].enabled=target;}); return; }
        if(y>=621 && y<=662) {
            QMenu menu(this);
            for(int s=0;s<5;++s) {auto* action=menu.addAction(shapeMenuIcon(static_cast<Shape>(s)),shapeName(s));action->setData(s);NativeFx::I18n::bind(action, [action,s] { action->setText(shapeName(s)); }); connect(action,&QAction::triggered,this,[this,b,s]{apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].shape=static_cast<Shape>(s);});});} menu.exec(mapToGlobal(widgetPoint(*this,left+90,667))); return;
        }
        if(y>=824 && y<872) {const int s=std::clamp(int((x-left-13)/70),0,4); apply([&](Parameters& p){p.bands[static_cast<std::size_t>(b)].shape=static_cast<Shape>(s);}); return;}
        if(y>=673 && y<758) {for(int k=0;k<3;++k) if(std::abs(x-(left+66+k*117))<45) {dragKnob_=b*3+k;lastMouse_=point;spectrumTimer_.start();return;}}
    }
    if(y>=922 && y<=976) {
        if(x>=183 && x<232) {const bool target=!params.highPassEnabled;apply([&](Parameters& p){p.highPassEnabled=target;});return;}
        if(x>=740 && x<789) {const bool target=!params.lowPassEnabled;apply([&](Parameters& p){p.lowPassEnabled=target;});return;}
        if((x>=307 && x<429) || (x>=864 && x<986)) {
            const bool highPass=x<429;
            QMenu menu(this);
            for(const int slope:std::array<int,3>{12,24,48}) {
                auto* action=menu.addAction(QStringLiteral("%1 dB/Oct").arg(slope));
                connect(action,&QAction::triggered,this,[this,highPass,slope] {
                    apply([highPass,slope](Parameters& p) {
                        if(highPass)p.highPassSlope=slope;
                        else p.lowPassSlope=slope;
                    });
                });
            }
            const auto anchor=widgetPoint(*this,highPass?307:864,926);
            menu.exec(mapToGlobal(QPoint(anchor.x(),anchor.y()-menu.sizeHint().height()-6)));
            return;
        }
        if(x>=1400 && x<1503) {selectPreset(0);return;}
        if(std::abs(x-475)<37) dragKnob_=12; else if(std::abs(x-1022)<37)dragKnob_=13; else if(std::abs(x-1286)<37)dragKnob_=14;
        lastMouse_=point;
        if(dragKnob_>=0) spectrumTimer_.start();
    }
}
void ParametricEqWidget::mouseMoveEvent(QMouseEvent* event) { const auto point=designPoint(event->position()); if(dragBand_>=0)editGraph(point); if(dragKnob_>=0)editKnob(point); }
void ParametricEqWidget::mouseReleaseEvent(QMouseEvent*) {dragBand_=-1;dragKnob_=-1;if(!spectrumFrameReader_)spectrumTimer_.stop();update();}
void ParametricEqWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton)return;
    const auto point=designPoint(event->position()); const float x=float(point.x()),y=float(point.y());
    int index=-1;
    if(y>=673 && y<758) {
        for(int band=0;band<4&&index<0;++band)
            for(int field=0;field<3;++field)
                if(std::abs(x-(25+374*band+66+117*field))<45)
                    {index=band*3+field;break;}
    } else if(y>=922 && y<=976) {
        if(std::abs(x-475)<37)index=12;
        else if(std::abs(x-1022)<37)index=13;
        else if(std::abs(x-1286)<37)index=14;
    }
    if(index<0)return;
    dragKnob_=-1;
    if(!spectrumFrameReader_)spectrumTimer_.stop();
    Controls::resetNumericValue(knobSpec(index),[this,index](double target){
        applyKnobValue(index,static_cast<float>(target));
    });
}
}
