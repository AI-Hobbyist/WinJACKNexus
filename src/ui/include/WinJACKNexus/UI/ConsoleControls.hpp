// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QWidget>
#include <WinJACKNexus/UI/Controls.hpp>
#include <array>
#include <functional>

namespace WinJACKNexus::UI::Controls
{
// Shared console controls own interaction, formatting and theme-role resolution.
class WINJACK_UI_API ConsoleKnob final : public QWidget
{
  public:
    ConsoleKnob(QString scope, NumericControlSpec spec, bool logarithmic = false,
                QWidget *parent = nullptr);
    void setValue(double value);
    double value() const noexcept
    {
        return value_;
    }
    void setUnit(const QString &unit);
    void onChanged(std::function<void(double)> callback);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QString scope_, unit_;
    NumericControlSpec spec_;
    bool logarithmic_;
    double value_ = 0, dragValue_ = 0;
    QPoint dragStart_;
    bool dragging_ = false;
    std::function<void(double)> changed_;
    double normalized(double value) const;
    double fromNormalized(double value) const;
    void apply(double value);
    void edit();
    QRect readout() const;
};

class WINJACK_UI_API ConsoleFader final : public QWidget
{
  public:
    ConsoleFader(QString scope, QString role, int channels, QWidget *parent = nullptr);
    void setValue(double db);
    double value() const noexcept
    {
        return value_;
    }
    int meterChannels() const noexcept
    {
        return channels_;
    }
    void setLevels(const std::array<float, 2> &peaks, double elapsedSeconds, double holdSeconds);
    double displayedLevelDb(int channel = 0) const noexcept;
    void onChanged(std::function<void(double)> callback);
    QRectF track() const;

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QString scope_, role_;
    int channels_;
    double value_ = 0;
    bool dragging_ = false;
    std::array<MeterBallistics, 2> meters_;
    std::function<void(double)> changed_;
    void apply(double value);
    void applyPosition(double y);
    void edit();
};

class WINJACK_UI_API ConsoleSeparator final : public QWidget
{
  public:
    ConsoleSeparator(QString scope, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QString scope_;
};

WINJACK_UI_API void bindConsoleTheme(QWidget *root, const QString &scope);
} // namespace WinJACKNexus::UI::Controls
