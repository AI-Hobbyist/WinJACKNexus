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
#include <WinJACKNexus/UI/AnimatedWidget.hpp>
#include <WinJACKNexus/UI/ApplicationSettings.hpp>

#include <algorithm>

namespace WinJACKNexus::UI {

AnimatedWidget::AnimatedWidget(QWidget* parent)
    : PaintableWidget(parent)
    , timer_(this)
{
    auto& settings = ApplicationSettings::instance();
    frameRate_ = settings.frameRate();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(1000 / frameRate_);
    connect(&settings, &ApplicationSettings::frameRateChanged,
            this, &AnimatedWidget::applyFrameRate);
    connect(&timer_, &QTimer::timeout, this, &AnimatedWidget::onAnimationTick);
}

AnimatedWidget::~AnimatedWidget()
{
    timer_.stop();
}

int AnimatedWidget::frameRate() const noexcept
{
    return frameRate_;
}

void AnimatedWidget::setFrameRate(int framesPerSecond)
{
    ApplicationSettings::instance().setFrameRate(framesPerSecond);
}

void AnimatedWidget::applyFrameRate(int framesPerSecond)
{
    if (frameRate_ == framesPerSecond)
        return;
    frameRate_ = framesPerSecond;
    timer_.setInterval(std::max(1, 1000 / frameRate_));
}

void AnimatedWidget::startAnimation()
{
    animationRequested_ = true;
    synchronizeTimer();
}

void AnimatedWidget::stopAnimation()
{
    animationRequested_ = false;
    timer_.stop();
}

bool AnimatedWidget::animationRunning() const noexcept
{
    return timer_.isActive();
}

std::uint64_t AnimatedWidget::frameNumber() const noexcept
{
    return frameNumber_;
}

void AnimatedWidget::renderingStateChanged() noexcept
{
    synchronizeTimer();
}

void AnimatedWidget::advanceFrame()
{
}

void AnimatedWidget::onAnimationTick()
{
    if (!isRenderingAllowed()) {
        synchronizeTimer();
        return;
    }
    ++frameNumber_;
    advanceFrame();
    requestRepaint();
    emit animationFrameChanged(static_cast<qulonglong>(frameNumber_));
}

void AnimatedWidget::synchronizeTimer()
{
    const auto shouldRun = animationRequested_ && isRenderingAllowed();
    if (shouldRun) {
        if (!timer_.isActive())
            timer_.start(std::max(1, 1000 / frameRate_));
    } else {
        timer_.stop();
    }
}

}
