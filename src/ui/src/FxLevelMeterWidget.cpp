#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QPainter>
#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::UI {
namespace {
constexpr double minimumDb = -72.0;
constexpr double maximumDb = 12.0;

}

FxLevelMeterWidget::FxLevelMeterWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fxLevelMeter"));
    setMinimumHeight(82);
    animationTimer_.setParent(this);
    bindRefreshTimer(animationTimer_, this);
    peakClock_.start();
    connect(&animationTimer_, &QTimer::timeout, this, [this] {
        const auto elapsedMilliseconds = animationClock_.isValid()
            ? animationClock_.restart() : -1;
        if (elapsedMilliseconds < 0) animationClock_.start();
        const auto elapsedSeconds = elapsedMilliseconds > 0
            ? std::min(elapsedMilliseconds / 1000.0, 0.1)
            : 1.0 / ApplicationSettings::instance().frameRate();
        const auto blend = 1.0 - std::exp(-12.0 * elapsedSeconds);
        const auto advance = [blend](double& displayed, double target) {
            displayed += (target - displayed) * blend;
            if (std::abs(target - displayed) < 0.05) displayed = target;
        };
        advance(displayInputDb_, inputDb_);
        advance(displayOutputDb_, outputDb_);
        const auto now = peakClock_.elapsed();
        const auto holdMilliseconds = static_cast<qint64>(
            ApplicationSettings::instance().peakHoldSeconds() * 1000.0);
        const std::array values {displayInputDb_, displayOutputDb_};
        for (std::size_t row = 0; row < values.size(); ++row) {
            if (values[row] >= peakDb_[row]) {
                peakDb_[row] = values[row];
                peakHoldUntilMs_[row] = now + holdMilliseconds;
            } else if (now >= peakHoldUntilMs_[row]) {
                peakDb_[row] += (values[row] - peakDb_[row]) * blend;
            }
        }
        if (!readoutClock_.isValid() || readoutClock_.elapsed()
            >= ApplicationSettings::instance().peakValueRefreshMilliseconds()) {
            readoutInputDb_ = displayInputDb_;
            readoutOutputDb_ = displayOutputDb_;
            readoutClock_.restart();
        }
        update();
    });
}

void FxLevelMeterWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    animationClock_.start();
    animationTimer_.start();
}

void FxLevelMeterWidget::hideEvent(QHideEvent* event) {
    animationTimer_.stop();
    animationClock_.invalidate();
    QWidget::hideEvent(event);
}

void FxLevelMeterWidget::setLevels(double inputDb, double outputDb) {
    inputDb_ = std::clamp(inputDb, minimumDb, maximumDb);
    outputDb_ = std::clamp(outputDb, minimumDb, maximumDb);
    if (!initialized_) {
        displayInputDb_ = readoutInputDb_ = inputDb_;
        displayOutputDb_ = readoutOutputDb_ = outputDb_;
        peakDb_ = {inputDb_, outputDb_};
        initialized_ = true;
        readoutClock_.start();
        update();
    }
}

QRectF FxLevelMeterWidget::barRect(int row) const noexcept {
    return {34.0, row == 0 ? 7.0 : 34.0, std::max(1.0, width() - 43.0), 17.0};
}

qreal FxLevelMeterWidget::positionForDb(double db) const noexcept {
    const auto bar = barRect(0);
    return bar.left() + (std::clamp(db, minimumDb, maximumDb) - minimumDb)
        / (maximumDb - minimumDb) * bar.width();
}

void FxLevelMeterWidget::paintEvent(QPaintEvent*) {
    const auto themed=[](const char* role,const QColor& fallback){
        return Controls::themeTokenColor(QStringLiteral("sel-fx"),QString::fromLatin1(role),fallback);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), themed("meter-level-surface",QColor("#14283e")));
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.setFont(font);
    for (int row = 0; row < 2; ++row) {
        const QRectF bar = barRect(row);
        painter.setPen(themed("meter-level-label",QColor("#afc7e5")));
        painter.drawText(QRectF(0, bar.top(), 29, bar.height()), Qt::AlignVCenter | Qt::AlignLeft,
                         row == 0 ? QStringLiteral("输入") : QStringLiteral("输出"));
        const double value = row == 0 ? displayInputDb_ : displayOutputDb_;
        Controls::MeterStyle style;
        style.minimumDb = minimumDb;
        style.maximumDb = maximumDb;
        style.zones = {
            {-12.0, themed("meter-level-zone-low-active", QColor("#2bce75")),
                    themed("meter-level-zone-low-inactive", QColor("#163e2e"))},
            {0.0, themed("meter-level-zone-mid-active", QColor("#f4cd42")),
                  themed("meter-level-zone-mid-inactive", QColor("#4a3f1d"))},
            {12.0, themed("meter-level-zone-high-active", QColor("#f35656")),
                   themed("meter-level-zone-high-inactive", QColor("#4c242a"))}
        };
        style.border = themed("meter-level-border", QColor("#426181"));
        style.peakColor = themed("meter-level-peak", QColor("#f7fbff"));
        style.tickColor = themed("meter-level-tick", QColor("#526763"));
        QRectF meterBounds=bar;
        if(row==1) {
            style.ticks={{-72,QStringLiteral("-72")},{-48,QStringLiteral("-48")},
                         {-24,QStringLiteral("-24")},{-12,QStringLiteral("-12")},
                         {0,QStringLiteral("0")},{12,QStringLiteral("+12")}};
            style.tickBandPixels=24;
            style.tickFontPixelSize=9;
            style.labelColor=themed("meter-level-tick-label",QColor("#89a8c9"));
            meterBounds.setHeight(bar.height()+style.tickBandPixels);
        }
        Controls::drawMeter(painter, meterBounds, value,
                            peakDb_[static_cast<std::size_t>(row)], style);
        painter.setPen(themed("meter-level-readout",QColor("#f3f8ff")));
        const double readout = row == 0 ? readoutInputDb_ : readoutOutputDb_;
        painter.drawText(bar.adjusted(4, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
                         readout <= minimumDb + .05 ? QStringLiteral("−∞")
                             : QStringLiteral("%1 dB").arg(QString::number(readout, 'f', 0)));
    }

}

}
