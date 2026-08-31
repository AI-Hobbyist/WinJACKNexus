#include <WinJACKNexus/UI/AnimatedWidget.hpp>

#include <algorithm>

namespace WinJACKNexus::UI {

AnimatedWidget::AnimatedWidget(QWidget* parent)
    : PaintableWidget(parent)
    , timer_(this)
{
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(1000 / frameRate_);
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
    const auto normalizedRate = std::clamp(framesPerSecond, 1, 240);
    if (frameRate_ == normalizedRate)
        return;
    frameRate_ = normalizedRate;
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