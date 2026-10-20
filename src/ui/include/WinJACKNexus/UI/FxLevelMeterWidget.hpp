#pragma once

#include <WinJACKNexus/UI/Export.hpp>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <array>

namespace WinJACKNexus::UI
{

class WINJACK_UI_API FxLevelMeterWidget final : public QWidget
{
  public:
    explicit FxLevelMeterWidget(QWidget *parent = nullptr);
    void setLevels(double inputDb, double outputDb);
    void setSingleChannel(bool enabled, const QString &label = {});
    void setVertical(bool enabled);
    void setSurfaceVisible(bool visible);
    double inputDb() const noexcept
    {
        return inputDb_;
    }
    double outputDb() const noexcept
    {
        return outputDb_;
    }
    double peakDb(int row) const noexcept
    {
        return peakDb_[row == 0 ? 0 : 1];
    }
    QRectF barRect(int row) const noexcept;
    qreal positionForDb(double db) const noexcept;
    QSize sizeHint() const override
    {
        return {260, 86};
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    double inputDb_ = -60.0;
    double outputDb_ = -60.0;
    double displayInputDb_ = -60.0;
    double displayOutputDb_ = -60.0;
    double readoutInputDb_ = -60.0;
    double readoutOutputDb_ = -60.0;
    bool initialized_ = false;
    QTimer animationTimer_;
    QElapsedTimer animationClock_;
    QElapsedTimer readoutClock_;
    QElapsedTimer peakClock_;
    std::array<double, 2> peakDb_{-60.0, -60.0};
    std::array<qint64, 2> peakHoldUntilMs_{};
};

}
