/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once

#include <WinJACKNexus/UI/PaintableWidget.hpp>

#include <QTimer>

#include <cstdint>

namespace WinJACKNexus::UI {

class WINJACK_UI_API AnimatedWidget : public PaintableWidget {
    Q_OBJECT
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate)
    Q_PROPERTY(bool animationRunning READ animationRunning)

public:
    explicit AnimatedWidget(QWidget* parent = nullptr);
    ~AnimatedWidget() override;

    int frameRate() const noexcept;
    void setFrameRate(int framesPerSecond);

    void startAnimation();
    void stopAnimation();
    bool animationRunning() const noexcept;
    std::uint64_t frameNumber() const noexcept;

Q_SIGNALS:
    void animationFrameChanged(qulonglong frameNumber);

protected:
    void renderingStateChanged() noexcept override;
    virtual void advanceFrame();

private Q_SLOTS:
    void onAnimationTick();

private:
    void synchronizeTimer();

    QTimer timer_;
    int frameRate_ = 60;
    bool animationRequested_ = false;
    std::uint64_t frameNumber_ = 0;
};

}