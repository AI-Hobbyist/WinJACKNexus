#include <WinJACKNexus/UI/FxLevelMeterWidget.hpp>
#include <WinJACKNexus/UI/Controls.hpp>

#include <QPainter>
#include <algorithm>
#include <array>

namespace WinJACKNexus::UI {
namespace {
constexpr double minimumDb = -60.0;
constexpr double maximumDb = 12.0;

}

FxLevelMeterWidget::FxLevelMeterWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fxLevelMeter"));
    setMinimumHeight(82);
}

void FxLevelMeterWidget::setLevels(double inputDb, double outputDb) {
    inputDb_ = std::clamp(inputDb, minimumDb, maximumDb);
    outputDb_ = std::clamp(outputDb, minimumDb, maximumDb);
    update();
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#14283e"));
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.setFont(font);
    for (int row = 0; row < 2; ++row) {
        const QRectF bar = barRect(row);
        painter.setPen(QColor("#afc7e5"));
        painter.drawText(QRectF(0, bar.top(), 29, bar.height()), Qt::AlignVCenter | Qt::AlignLeft,
                         row == 0 ? QStringLiteral("输入") : QStringLiteral("输出"));
        const double value = row == 0 ? inputDb_ : outputDb_;
        Controls::drawLevelMeter(painter, bar, value);
        painter.setPen(QColor("#f3f8ff"));
        painter.drawText(bar.adjusted(4, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 dB").arg(QString::number(value, 'f', 0)));
    }

    constexpr std::array<int, 5> ticks {-60, -36, -12, 0, 12};
    font.setPixelSize(10);
    painter.setFont(font);
    for (const int tick : ticks) {
        const qreal x = positionForDb(tick);
        painter.setPen(QColor("#89a8c9"));
        painter.drawLine(QPointF(x, 55), QPointF(x, 59));
        const QString text = tick > 0 ? QStringLiteral("+%1").arg(tick) : QString::number(tick);
        const qreal labelLeft = std::clamp(x - 17.0, 0.0, std::max(0.0, width() - 34.0));
        painter.drawText(QRectF(labelLeft, 62, 34, 15), Qt::AlignCenter, text);
    }
}

}
