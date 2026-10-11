/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <algorithm>
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/NativeFx/delay/Processor.hpp>
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <array>
#include <functional>

namespace WinJACKNexus::UI
{
using DelayParameters = WinJACKNexus::Common::DelayParameters;
class WJN_FX_API DelayWidget final : public QWidget
{
  public:
    using ChangeHandler = std::function<void(std::size_t, const DelayParameters &)>;
    explicit DelayWidget(QWidget *parent = nullptr);
    void setParameters(const std::array<DelayParameters, 8> &values);
    void setChangeHandler(ChangeHandler handler);
    void setSelectedChannel(int channel)
    {
        channel_ = std::clamp(channel, -1, 7);
        syncControls();
        update();
    }
    int hostPresetIndex() const noexcept
    {
        return preset_;
    }
    void chooseHostPreset(int index)
    {
        presetCombo_->setCurrentIndex(std::clamp(index, 0, 6));
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

  private:
    void syncControls();
    void edit(const std::function<void(DelayParameters &)> &change);
    void adjust(int dial, float steps);
    int dialAt(const QPoint &point) const;
    std::array<DelayParameters, 8> values_{};
    ChangeHandler handler_;
    int channel_ = -1, preset_ = 0, view_ = 0, dragged_ = -1;
    bool playing_ = false;
    QPoint dragStart_;
    DelayParameters dragValue_;
    QTimer dragRefreshTimer_;
    QComboBox *presetCombo_ = nullptr, *noteCombo_ = nullptr, *viewCombo_ = nullptr;
    QComboBox *highPassCombo_ = nullptr, *lowPassCombo_ = nullptr, *mixCombo_ = nullptr;
    QLabel *mixTextLabel_ = nullptr;
};
}
